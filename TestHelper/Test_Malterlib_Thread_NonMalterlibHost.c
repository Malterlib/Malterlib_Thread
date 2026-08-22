// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <pthread.h>
#endif

typedef uint32_t (*FTest)(void);
typedef void (*FLibraryFunc)(void);

struct CExistingThreadState
{
	FTest m_fTest;
	uint32_t m_Result;
#ifdef _WIN32
	HANDLE m_pReady;
	HANDLE m_pRun;
#else
	pthread_mutex_t m_Mutex;
	pthread_cond_t m_Condition;
	int m_bReady;
	int m_bRun;
#endif
};

#ifdef _WIN32
static DWORD WINAPI fg_ExistingThread(void *_pState)
{
	struct CExistingThreadState *pState = (struct CExistingThreadState *)_pState;
	SetEvent(pState->m_pReady);
	WaitForSingleObject(pState->m_pRun, INFINITE);
	if (pState->m_fTest)
		pState->m_Result = pState->m_fTest();
	return 0;
}
#else
static void *fg_ExistingThread(void *_pState)
{
	struct CExistingThreadState *pState = (struct CExistingThreadState *)_pState;
	pthread_mutex_lock(&pState->m_Mutex);
	pState->m_bReady = 1;
	pthread_cond_signal(&pState->m_Condition);
	while (!pState->m_bRun)
		pthread_cond_wait(&pState->m_Condition, &pState->m_Mutex);
	pthread_mutex_unlock(&pState->m_Mutex);

	if (pState->m_fTest)
		pState->m_Result = pState->m_fTest();
	return NULL;
}
#endif

int main(int _nArguments, char **_pArguments)
{
	if (_nArguments != 2)
		return 1;

	struct CExistingThreadState State = {0};
	State.m_Result = UINT32_MAX;

#ifdef _WIN32
	State.m_pReady = CreateEventA(NULL, TRUE, FALSE, NULL);
	State.m_pRun = CreateEventA(NULL, TRUE, FALSE, NULL);
	if (!State.m_pReady || !State.m_pRun)
		return 5;
	HANDLE pThread = CreateThread(NULL, 0, &fg_ExistingThread, &State, 0, NULL);
	if (!pThread)
		return 5;
	WaitForSingleObject(State.m_pReady, INFINITE);

	HMODULE pLibrary = LoadLibraryA(_pArguments[1]);
	// Match NSys::fg_LoadLibrary: a non-Malterlib host must call the module's pre-load hook so the
	// matching pre-unload hook below can stop Malterlib's threads before FreeLibrary takes the loader lock.
	if (pLibrary)
	{
		FLibraryFunc fLoadExternal = (FLibraryFunc)GetProcAddress(pLibrary, "IdsLoadLibraryExternal");
		if (fLoadExternal)
			fLoadExternal();
	}
	FTest fTest = pLibrary ? (FTest)GetProcAddress(pLibrary, "fg_TestSetAnotherThreadLocal") : NULL;
#else
	pthread_mutex_init(&State.m_Mutex, NULL);
	pthread_cond_init(&State.m_Condition, NULL);
	pthread_t Thread;
	if (pthread_create(&Thread, NULL, &fg_ExistingThread, &State))
		return 5;
	pthread_mutex_lock(&State.m_Mutex);
	while (!State.m_bReady)
		pthread_cond_wait(&State.m_Condition, &State.m_Mutex);
	pthread_mutex_unlock(&State.m_Mutex);

	void *pLibrary = dlopen(_pArguments[1], RTLD_NOW | RTLD_LOCAL);
	FTest fTest = pLibrary ? (FTest)dlsym(pLibrary, "fg_TestSetAnotherThreadLocal") : NULL;
#endif
	State.m_fTest = fTest;

	uint32_t Result = fTest ? fTest() : UINT32_MAX;

#ifdef _WIN32
	SetEvent(State.m_pRun);
	WaitForSingleObject(pThread, INFINITE);
	CloseHandle(pThread);
	CloseHandle(State.m_pRun);
	CloseHandle(State.m_pReady);
#else
	pthread_mutex_lock(&State.m_Mutex);
	State.m_bRun = 1;
	pthread_cond_signal(&State.m_Condition);
	pthread_mutex_unlock(&State.m_Mutex);
	pthread_join(Thread, NULL);
	pthread_cond_destroy(&State.m_Condition);
	pthread_mutex_destroy(&State.m_Mutex);
#endif

	if (!pLibrary)
		return 2;
	if (!fTest)
		return 3;
	Result |= State.m_Result;

#ifdef _WIN32
	// Match NSys::fg_FreeLibrary: on Windows the non-Malterlib host must call the module's pre-unload hook
	// before FreeLibrary so Malterlib's threads are stopped before the loader lock is taken. Unloading
	// without it deadlocks in the concurrency-manager shutdown that runs under the loader lock during
	// DLL_PROCESS_DETACH. dlclose on POSIX runs the destructors without the loader lock, so it needs nothing.
	{
		FLibraryFunc fFreeExternal = (FLibraryFunc)GetProcAddress(pLibrary, "IdsFreeLibraryExternal");
		if (fFreeExternal)
			fFreeExternal();
	}
	if (!FreeLibrary(pLibrary))
		return 4;
#else
	if (dlclose(pLibrary))
		return 4;
#endif

	return (int)Result;
}
