// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <pthread.h>
#endif

#ifdef __APPLE__
#include <pthread/introspection.h>

static void fg_HostIntrospectionHook(unsigned int _Event, pthread_t _pThread, void *_pAddress, size_t _Size)
{
}

// Installing a hook and putting the displaced one straight back reads out what is installed without
// changing it
static pthread_introspection_hook_t fg_CurrentIntrospectionHook(void)
{
	pthread_introspection_hook_t fCurrent = pthread_introspection_hook_install(&fg_HostIntrospectionHook);
	(void)pthread_introspection_hook_install(fCurrent);

	return fCurrent;
}

static void *fg_AfterUnloadThread(void *_pUnused)
{
	return NULL;
}
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
	if (_nArguments < 2)
		return 1;

#ifdef __APPLE__
	int bExpectHook = 0;
#endif
	int bExitLoaded = 0;
	for (int iArgument = 2; iArgument < _nArguments; ++iArgument)
	{
		if (strcmp(_pArguments[iArgument], "--expect-hook") == 0)
		{
#ifdef __APPLE__
			bExpectHook = 1;
#endif
		}
		else if (strcmp(_pArguments[iArgument], "--exit-loaded") == 0)
			bExitLoaded = 1;
		else
			return 1;
	}

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

#ifdef __APPLE__
	// In a host that is not a Malterlib executable the library chains the process global pthread
	// introspection hook itself, with a hook that lives in its own image. Whether it was built to
	// is something the launcher knows and this host cannot
	if (bExpectHook && !fg_CurrentIntrospectionHook())
		return 8;
#endif

	// A host may also exit with the library still loaded, which runs its destructors from exit
	if (bExitLoaded)
		return (int)Result;

#ifdef _WIN32
	// Match NSys::fg_FreeLibrary: on Windows the non-Malterlib host must call the module's pre-unload hook
	// before FreeLibrary so Malterlib's threads are stopped before the loader lock is taken. Unloading
	// without it deadlocks in the concurrency-manager shutdown that runs under the loader lock during
	// DLL_PROCESS_DETACH. dlclose needs no such hook, although on macOS dyld does hold its loader lock
	// across the destructors it runs.
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

#ifdef __APPLE__
	// dladdr fails for an address no loaded image covers, so a hook that no longer resolves is one
	// pthread calls into unmapped memory on the next thread create
	{
		pthread_introspection_hook_t fRemaining = fg_CurrentIntrospectionHook();
		Dl_info Info;
		memset(&Info, 0, sizeof(Info));
		if (fRemaining && !dladdr((void *)fRemaining, &Info))
			return 6;

		pthread_t HookThread;
		if (pthread_create(&HookThread, NULL, &fg_AfterUnloadThread, NULL))
			return 7;
		pthread_join(HookThread, NULL);
	}
#endif

	return (int)Result;
}
