// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Test/Performance>
#include <Mib/Time/PerfTimeMeasure>
#include <Mib/File/File>
#include <Mib/Process/ProcessLaunch>

#if 1
#	if defined(DPlatformFamily_Windows)
#		define DEnableWin32ThreadTest
#	endif

#	if defined(DEnableWin32ThreadTest)
#		include <windows.h>
#		include <Fibersapi.h>
#		if defined(DEnableWin32ConCrt)
#			include <concrt.h>
#		endif
using CWindowsCriticalSection = CRITICAL_SECTION;
#	endif

#	include <thread>
#	include <mutex>

#	if defined(DPlatformFamily_macOS) || defined(DPlatformFamily_Linux)
#		include <sys/wait.h>
#		include <unistd.h>
#	endif

#	ifdef DPlatformFamily_Linux
#		include <Mib/Core/PlatformSpecific/LinuxProcFS>
#		include <fcntl.h>
#		include <sched.h>
#		include <signal.h>
#		include <sys/resource.h>
#	endif

using namespace NMib::NTime;
using namespace NMib::NThread;

#	ifndef DCompiler_MSVC
#		define DLimitOptimizations asm("")
#	else
#		define DLimitOptimizations
#	endif

constexpr aint gc_CacheLineSize = 64;

namespace
{
	struct CReadWriteContention
	{
		struct CIncThread : public NMib::NThread::CThread
		{
			CIncThread();
			~CIncThread();

			NMib::NStr::CStr f_GetThreadName();

			using NMib::NThread::CThread::f_Start;

			void f_Start(CReadWriteContention *_pTest);

			aint f_Main();

			CReadWriteContention *m_pTest;
		};

		struct CReadThread : public NMib::NThread::CThread
		{
			CReadThread();
			~CReadThread();

			NMib::NStr::CStr f_GetThreadName();

			using NMib::NThread::CThread::f_Start;

			void f_Start(CReadWriteContention *_pTest);

			aint f_Main();

			CReadWriteContention *m_pTest;

			NMib::NAtomic::TCAtomic<umint> m_nReads;
			NMib::NAtomic::TCAtomic<smint> m_bStop;
		};

		CReadWriteContention();

		void f_DoTest();

		static constexpr aint mc_IncThreads = 4;
		static constexpr aint mc_ReadThreads = 4;

		aint m_IncValue;
		aint *m_pIncValue;
		NMib::NThread::CEventAutoReset m_IncEvent;

		bool m_bDummy0;
		alignas(gc_CacheLineSize) NMib::NThread::CMutualManyRead m_IncLock;
		alignas(gc_CacheLineSize) NMib::NThread::CMutual m_IncLockMutual;
		alignas(gc_CacheLineSize) NMib::NThread::CMutual m_IncDoneLock;
		bool m_bDummy1;
		NMib::NAtomic::TCAtomic<aint> m_ChangingValue;
		aint m_IncDone;
		aint m_nTests;
	};

	CReadWriteContention::CIncThread::CIncThread()
	{
		m_pTest = nullptr;
	}

	CReadWriteContention::CIncThread::~CIncThread()
	{
		f_Stop();
	}

	NMib::NStr::CStr CReadWriteContention::CIncThread::f_GetThreadName()
	{
		return "MalterlibCertifier_IncThread";
	}

	void CReadWriteContention::CIncThread::f_Start(CReadWriteContention *_pTest)
	{
		m_pTest = _pTest;
		NMib::NThread::CThread::f_Start();
	}

	aint CReadWriteContention::CIncThread::f_Main()
	{
		while (1)
		{
			m_EventWantQuit.f_Wait();
			if (f_GetState() == NMib::NThread::EThreadState_EventWantQuit)
				break;

			int nTest = m_pTest->m_nTests;
			while (nTest)
			{
				{
					m_pTest->m_IncLock.f_Lock();
					m_pTest->m_ChangingValue = 5544;
#	if DMibEnableSafeCheck > 0 && 0
					++(*m_pTest->m_pIncValue);
#	else
					++m_pTest->m_IncValue;
#	endif
					m_pTest->m_ChangingValue = 1112;
					m_pTest->m_IncLock.f_Unlock();
				}
				--nTest;
			}
			{
				DMibLock(m_pTest->m_IncDoneLock);
				++m_pTest->m_IncDone;
			}
			m_pTest->m_IncEvent.f_Signal();
		}

		return 0;
	}

	CReadWriteContention::CReadThread::CReadThread()
	{
		m_pTest = nullptr;
		m_nReads = 0;
	}

	CReadWriteContention::CReadThread::~CReadThread()
	{
		f_Stop();
	}

	NMib::NStr::CStr CReadWriteContention::CReadThread::f_GetThreadName()
	{
		return "MalterlibCertifier_ReadThread";
	}

	void CReadWriteContention::CReadThread::f_Start(CReadWriteContention *_pTest)
	{
		m_pTest = _pTest;
		NMib::NThread::CThread::f_Start();
	}

	aint CReadWriteContention::CReadThread::f_Main()
	{
		bool bWantStop = false;
		while (1)
		{
			m_EventWantQuit.f_Wait();
			if (f_GetState() == NMib::NThread::EThreadState_EventWantQuit)
				break;
			m_nReads = 0;
			// DMibTestSuite("ReadThread")
			{
				bool bInvalidValue = false;
				while (1)
				{
					if (m_bStop.f_Exchange(0))
					{
						bWantStop = true;
						break;
					}

					{
						m_pTest->m_IncLock.f_LockRead();

						if (m_pTest->m_ChangingValue.f_Load() != 1112)
						{
							if (!bInvalidValue)
							{
								bInvalidValue = true;
								DMibTest(DMibExpr(!bInvalidValue));
							}
						}
#	if DMibEnableSafeCheck > 0 && 0
						DMibFastCheck(!m_pTest->m_IncLock.f_IsLocked());
						for (umint i = 0; i < 1000; ++i)
							NMib::fg_Volatile(m_pTest->m_pIncValue) = nullptr;
						NMib::fg_Volatile(m_pTest->m_pIncValue) = &m_pTest->m_IncValue;
						DMibFastCheck(!m_pTest->m_IncLock.f_IsLocked());
#	endif
						m_nReads.f_FetchAdd(1, NMib::NAtomic::gc_MemoryOrder_Relaxed);

						m_pTest->m_IncLock.f_UnlockRead();
					}
				}
			};
			if (bWantStop)
				break;
			m_pTest->m_IncEvent.f_Signal();
		}
		return 0;
	}

	CReadWriteContention::CReadWriteContention()
	{
	}

	void CReadWriteContention::f_DoTest()
	{
#	if !defined(DMibDebug)
#		if 0
		try
		{
			NMib::NSys::fg_Process_SetPriority(NMib::EExecutionPriority_Normal);
		}
		catch (NMib::NException::CException const &_Exception)
		{
			// You are not allowed to do this on unix unless you are runnig as root
		}
#		endif
		try
		{
			NMib::NSys::fg_Thread_SetPriority(NMib::NSys::fg_Thread_GetCurrent(), NMib::EExecutionPriority_Normal);
		}
		catch (NMib::NException::CException const &)
		{
		}
#	endif
		NMib::NThread::CMutual Lock;
		NMib::NTime::CPrefCyclesTimeMeasureMin Timer;

		{
			CIncThread IncThreads[mc_IncThreads];
			CReadThread ReadThreads[mc_ReadThreads];
			for (umint iIncThread = 0; iIncThread < mc_IncThreads; ++iIncThread)
				IncThreads[iIncThread].f_Start(this);
			for (umint iReadThread = 0; iReadThread < mc_ReadThreads; ++iReadThread)
				ReadThreads[iReadThread].f_Start(this);

			NMib::NSys::fg_Thread_Sleep(1.0);

			Timer.f_Reset();
			umint nReads = 0;
			m_ChangingValue = 1112;
			m_nTests = 25000 / mc_IncThreads;
			{
				{
					m_IncLock.f_Destruct();
					m_IncLock.f_Construct();
					m_IncLockMutual.f_Destruct();
					m_IncLockMutual.f_Construct();

					m_IncValue = 0;
					m_pIncValue = &m_IncValue;
					m_IncDone = 0;
					{
						Timer.f_Start();
						for (umint iReadThread = 0; iReadThread < mc_ReadThreads; ++iReadThread)
							ReadThreads[iReadThread].m_EventWantQuit.f_Signal();
						for (umint iIncThread = 0; iIncThread < mc_IncThreads; ++iIncThread)
							IncThreads[iIncThread].m_EventWantQuit.f_Signal();

						while (1)
						{
							m_IncEvent.f_Wait();
							DMibLock(m_IncDoneLock);
							if (m_IncDone == (mc_IncThreads))
								break;
						}
						for (umint iReadThread = 0; iReadThread < mc_ReadThreads; ++iReadThread)
						{
							nReads += ReadThreads[iReadThread].m_nReads.f_Load(NMib::NAtomic::gc_MemoryOrder_Relaxed);
							ReadThreads[iReadThread].m_bStop.f_Exchange(1);
							ReadThreads[iReadThread].m_EventWantQuit.f_Signal();
						}
						Timer.f_Stop();
						while (1)
						{
							bool bAllStopped = true;
							for (umint iReadThread = 0; iReadThread < mc_ReadThreads; ++iReadThread)
							{
								if (ReadThreads[iReadThread].m_bStop.f_Load())
									bAllStopped = false;
							}
							if (bAllStopped)
								break;
							NMib::NSys::fg_Thread_Sleep(0.01f);
						}
					}
				}
			}

			DMibTest(DMibExpr(m_IncValue) == DMibExpr(m_nTests*mc_IncThreads));

			NMib::NTime::CPrefCyclesTimeMeasureMin TimerRead = Timer;
			TimerRead /= nReads;
			Timer /= m_IncValue;

			if (NMib::NTest::fg_GroupActive("Performance"))
				DMibTest(DMibExpr(Timer) / DMibExpr(TimerRead) >= DMibExpr(1.0));
		}

#	if !defined(DMibDebug)
#		if 0
		try
		{
			NMib::NSys::fg_Process_SetPriority(NMib::EExecutionPriority_High);
		}
		catch (NMib::NException::CException const &_Exception)
		{
			// You are not allowed to do this on unix unless you are runnig as root
		}
#		endif
		try
		{
			NMib::NSys::fg_Thread_SetPriority(NMib::NSys::fg_Thread_GetCurrent(), NMib::EExecutionPriority_Highest);
		}
		catch (NMib::NException::CException const &)
		{
		}
#	endif
	}

#	ifdef DPlatformFamily_Windows
	__declspec(thread) umint g_ThreadLocal = 0;
	__declspec(thread) umint g_ThreadLocalArray[16] = {0};
#	else
	__thread umint __attribute__((tls_model("local-exec"))) g_ThreadLocal = 0;
	__thread umint __attribute__((tls_model("local-exec"))) g_ThreadLocalArray[16] = {0};
#	endif
	umint g_LocalArrayIndex = 11;
#	if defined(DEnableWin32ThreadTest)
	DWORD g_TlsLocal;
	LPVOID (WINAPI *g_fTlsGetValue)(DWORD _TlsIndex);
	DWORD g_FlsLocal;
	//LPVOID (WINAPI *pFlsGetValue)(DWORD dwFlsIndex);
#	endif

	TCThreadLocal<umint, NMib::NMemory::CAllocator_Heap, EThreadLocalFlag_AlwaysCreated> g_ThreadLocalMalterlib;
	TCThreadLocal<umint, NMib::NMemory::CAllocator_Heap, EThreadLocalFlag(uint32(EThreadLocalFlag_AlwaysCreated) | uint32(EThreadLocalFlag_FastThreadLocal))> g_ThreadLocalMalterlibFast;
	umint g_ThreadLocalFastIndex;
	umint g_ThreadLocalIndex;

	struct CThread_Tests : public NMib::NTest::CTest
	{
		struct CTemp
		{
			CTemp();

			int32 m_Value;
		};

		struct CTemp35
		{
			CTemp35();

			int32 m_Value;
		};

		static void fs_IncNative();
		static void fs_IncNativeArray();
#	if defined(DEnableWin32ThreadTest)
		static void fs_IncTls();
		static void fs_IncFls();
#	endif
		static void fs_IncMalterlib();
		static void fs_IncMalterlibFast();
		static umint fs_CurrentThreadNative();
		static umint fs_CurrentThreadMalterlib();
		static void fs_IncMalterlibStorageFast();
		static void fs_IncMalterlibStorage();

		template <typename tf_CFunctor>
		static void fs_CallFunctor(tf_CFunctor _Functor);

		template <typename tf_CFunctor>
		static void fs_CallFunctor2(tf_CFunctor _Functor);

		void f_DoTests();
	};

	CThread_Tests::CTemp::CTemp()
	{
		m_Value = 0;
	}

	CThread_Tests::CTemp35::CTemp35()
	{
		m_Value = 35;
	}

	void CThread_Tests::fs_IncNative()
	{
		++g_ThreadLocal;
	}

	void CThread_Tests::fs_IncNativeArray()
	{
		++g_ThreadLocalArray[g_LocalArrayIndex];
	}

#	if defined(DEnableWin32ThreadTest)
	void CThread_Tests::fs_IncTls()
	{
		umint *pTls = (umint *)g_fTlsGetValue(g_TlsLocal);
		++(*pTls);
	}

	void CThread_Tests::fs_IncFls()
	{
		umint *pTls = (umint *)FlsGetValue(g_FlsLocal);
		++(*pTls);
	}
#	endif

	void CThread_Tests::fs_IncMalterlib()
	{
		++(*g_ThreadLocalMalterlib);
	}

	void CThread_Tests::fs_IncMalterlibFast()
	{
		++(*g_ThreadLocalMalterlibFast);
	}

	umint CThread_Tests::fs_CurrentThreadNative()
	{
#	ifdef DPlatformFamily_Windows
		return GetCurrentThreadId();
#	else
		return (umint)pthread_self();
#	endif
	}

	umint CThread_Tests::fs_CurrentThreadMalterlib()
	{
		//			return __readgsdword(0x48);
		//000000013F93B004  mov         rax,qword ptr gs:[30h]
		//000000013F93B00D  mov         eax,dword ptr [rax+48h]

		return NMib::NSys::fg_Thread_GetCurrentUID();
	}

	void CThread_Tests::fs_IncMalterlibStorageFast()
	{
		umint *pTls = (umint *)NMib::NSys::fg_Thread_GetLocalFast(g_ThreadLocalFastIndex);
		++(*pTls);
	}

	void CThread_Tests::fs_IncMalterlibStorage()
	{
		umint *pTls = (umint *)NMib::NSys::fg_Thread_GetLocal(g_ThreadLocalIndex);
		++(*pTls);
	}

	template <typename tf_CFunctor>
	void CThread_Tests::fs_CallFunctor(tf_CFunctor _Functor)
	{
		_Functor();
	}

	template <typename tf_CFunctor>
	void CThread_Tests::fs_CallFunctor2(tf_CFunctor _Functor)
	{
		_Functor();
		_Functor();
	}

	void CThread_Tests::f_DoTests()
	{
#	if defined(DEnableWin32ThreadTest)
		HMODULE pKernel32 = GetModuleHandle(str_utf16("kernelbase.dll"));
		if (!pKernel32)
			pKernel32 = GetModuleHandle(str_utf16("kernel32.dll"));
		(FARPROC &)g_fTlsGetValue = GetProcAddress(pKernel32, "TlsGetValue");
		//(FARPROC &)pFlsGetValue = GetProcAddress(pKernel32, "FlsGetValue");
#	endif

		DMibTestSuite("TLS Performance")
		{
			g_ThreadLocal = 0;
#	if defined(DEnableWin32ThreadTest)
			g_TlsLocal = TlsAlloc();
			g_FlsLocal = FlsAlloc(nullptr);
			TlsSetValue(g_TlsLocal, DMibNew umint);
			FlsSetValue(g_FlsLocal, DMibNew umint);
			*((umint *)TlsGetValue(g_TlsLocal)) = 0;
			*((umint *)FlsGetValue(g_FlsLocal)) = 0;
#	endif

			g_ThreadLocalFastIndex = NMib::NSys::fg_Thread_AllocLocalFast();
			NMib::NSys::fg_Thread_SetLocalFast(g_ThreadLocalFastIndex, DMibNew umint);
			*((umint *)NMib::NSys::fg_Thread_GetLocalFast(g_ThreadLocalFastIndex)) = 0;

			g_ThreadLocalIndex = NMib::NSys::fg_Thread_AllocLocal();
			NMib::NSys::fg_Thread_SetLocal(g_ThreadLocalIndex, DMibNew umint);
			*((umint *)NMib::NSys::fg_Thread_GetLocal(g_ThreadLocalIndex)) = 0;

			*g_ThreadLocalMalterlib = 0;
			*g_ThreadLocalMalterlibFast = 0;
			static constexpr umint c_nTests = 33;
			static constexpr umint c_nLoops = 100000;

			g_LocalArrayIndex = NMib::NStr::CStr((NMib::NStr::CStr::CFormat("{}") << (12))).f_ToInt();

			CPrefCyclesTimeMeasureMin NativeTime;
			CPrefCyclesTimeMeasureMin NativeArrayTime;
#	if defined(DEnableWin32ThreadTest)
			CPrefCyclesTimeMeasureMin TlsTime;
			CPrefCyclesTimeMeasureMin FlsTime;
#	endif

			CPrefCyclesTimeMeasureMin MalterlibTime;
			CPrefCyclesTimeMeasureMin MalterlibFastTime;
			CPrefCyclesTimeMeasureMin MalterlibStorageFastTime;
			CPrefCyclesTimeMeasureMin MalterlibStorageTime;

			auto fNative = [&]()
				{
					NativeTime.f_Start();
					[]() inline_never
					{
						for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
						{
							CThread_Tests::fs_IncNative();
							DLimitOptimizations;
						}
					}();
					NativeTime.f_Stop();
				}
			;
			auto fNativeArray = [&]()
				{
					NativeArrayTime.f_Start();
					[]() inline_never
					{
						for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
						{
							CThread_Tests::fs_IncNativeArray();
							DLimitOptimizations;
						}
					}();
					NativeArrayTime.f_Stop();
				}
			;
#	if defined(DEnableWin32ThreadTest)
			auto fTls = [&]()
				{
					TlsTime.f_Start();
					for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
					{
						CThread_Tests::fs_IncTls();
						DLimitOptimizations;
					}
					TlsTime.f_Stop();
				}
			;
			auto fFls = [&]()
				{
					FlsTime.f_Start();
					for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
					{
						CThread_Tests::fs_IncFls();
						DLimitOptimizations;
					}
					FlsTime.f_Stop();
				}
			;
#	endif

			auto fMalterlib = [&]()
				{
					MalterlibTime.f_Start();
					[]() inline_never
					{
						for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
						{
							CThread_Tests::fs_IncMalterlib();
							DLimitOptimizations;
						}
					}();
					MalterlibTime.f_Stop();
				}
			;

			auto fMalterlibFast = [&]()
				{
					MalterlibFastTime.f_Start();
					[]() inline_never
					{
						for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
						{
							CThread_Tests::fs_IncMalterlibFast();
							DLimitOptimizations;
						}
					}();
					MalterlibFastTime.f_Stop();
				}
			;

			auto fMalterlibStorageFast = [&]()
				{
					MalterlibStorageFastTime.f_Start();
					[]() inline_never
					{
						for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
						{
							CThread_Tests::fs_IncMalterlibStorageFast();
							DLimitOptimizations;
						}
					}();
					MalterlibStorageFastTime.f_Stop();
				}
			;

			auto fMalterlibStorage = [&]()
				{
					MalterlibStorageTime.f_Start();
					[]() inline_never
					{
						for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
						{
							CThread_Tests::fs_IncMalterlibStorage();
							DLimitOptimizations;
						}
					}();
					MalterlibStorageTime.f_Stop();
				}
			;

			for (umint iTest = 0; iTest < c_nTests; ++iTest)
			{
				fs_CallFunctor(fMalterlib);
				fs_CallFunctor2(fMalterlib);
			}
			for (umint iTest = 0; iTest < c_nTests; ++iTest)
			{
				fs_CallFunctor(fMalterlibFast);
				fs_CallFunctor2(fMalterlibFast);
			}
			for (umint iTest = 0; iTest < c_nTests; ++iTest)
			{
				fs_CallFunctor(fMalterlibStorageFast);
				fs_CallFunctor2(fMalterlibStorageFast);
			}
			for (umint iTest = 0; iTest < c_nTests; ++iTest)
			{
				fs_CallFunctor(fMalterlibStorage);
				fs_CallFunctor2(fMalterlibStorage);
			}
			for (umint iTest = 0; iTest < c_nTests; ++iTest)
			{
				fs_CallFunctor(fNative);
				fs_CallFunctor2(fNative);
			}
			for (umint iTest = 0; iTest < c_nTests; ++iTest)
			{
				fs_CallFunctor(fNativeArray);
				fs_CallFunctor2(fNativeArray);
			}
#	if defined(DEnableWin32ThreadTest)
			for (umint iTest = 0; iTest < c_nTests; ++iTest)
			{
				fs_CallFunctor(fTls);
				fs_CallFunctor2(fTls);
			}
			for (umint iTest = 0; iTest < c_nTests; ++iTest)
			{
				fs_CallFunctor(fFls);
				fs_CallFunctor2(fFls);
			}
#	endif

			NativeTime /= c_nLoops;
			NativeArrayTime /= c_nLoops;
#	if defined(DEnableWin32ThreadTest)
			TlsTime /= c_nLoops;
			FlsTime /= c_nLoops;
#	endif

			MalterlibTime /= c_nLoops;
			MalterlibFastTime /= c_nLoops;
			MalterlibStorageFastTime /= c_nLoops;
			MalterlibStorageTime /= c_nLoops;

			DMibTest(DMibExpr(g_ThreadLocal) == DMibExpr(*g_ThreadLocalMalterlib));
			DMibTest(DMibExpr(g_ThreadLocal) == DMibExpr(*g_ThreadLocalMalterlibFast));
			DMibTest(DMibExpr(g_ThreadLocal) == DMibExpr(g_ThreadLocalArray[g_LocalArrayIndex]));
#	if defined(DEnableWin32ThreadTest)
			DMibTest(DMibExpr(g_ThreadLocal) == DMibExpr(*((umint *)TlsGetValue(g_TlsLocal))));
			DMibTest(DMibExpr(g_ThreadLocal) == DMibExpr(*((umint *)FlsGetValue(g_FlsLocal))));
			DMibTest(DMibExpr(g_ThreadLocal) == DMibExpr(*((umint *)NMib::NSys::fg_Thread_GetLocalFast(g_ThreadLocalFastIndex))));
			DMibTest(DMibExpr(g_ThreadLocal) == DMibExpr(*((umint *)NMib::NSys::fg_Thread_GetLocal(g_ThreadLocalIndex))));
#	else
			DMibTest(DMibExpr(*g_ThreadLocalMalterlib) == DMibExpr(*g_ThreadLocalMalterlibFast));
#	endif

			if (NMib::NTest::fg_GroupActive("Performance"))
			{
				DMibTest(DMibExpr(MalterlibStorageFastTime) / DMibExpr(MalterlibFastTime) >= DMibExpr(0.75));
				DMibTest(DMibExpr(MalterlibStorageTime) / DMibExpr(MalterlibTime) >= DMibExpr(0.75));
				DMibTest(DMibExpr(NativeTime) / DMibExpr(MalterlibFastTime) >= DMibExpr(0.75));
				DMibTest(DMibExpr(NativeArrayTime) / DMibExpr(MalterlibFastTime) >= DMibExpr(0.75));
#	if defined(DEnableWin32ThreadTest)
				DMibTest(DMibExpr(TlsTime) / DMibExpr(MalterlibTime) >= DMibExpr(0.75));
				DMibTest(DMibExpr(FlsTime) / DMibExpr(MalterlibTime) >= DMibExpr(0.75));
#	endif
			}

#	if defined(DEnableWin32ThreadTest)
			delete ((umint *)TlsGetValue(g_TlsLocal));
			TlsFree(g_TlsLocal);
			delete ((umint *)FlsGetValue(g_FlsLocal));
			FlsFree(g_FlsLocal);
#	endif

			delete ((umint *)NMib::NSys::fg_Thread_GetLocalFast(g_ThreadLocalFastIndex));
			NMib::NSys::fg_Thread_SetLocalFast(g_ThreadLocalFastIndex, nullptr);
			NMib::NSys::fg_Thread_FreeLocalFast(g_ThreadLocalFastIndex);
			delete ((umint *)NMib::NSys::fg_Thread_GetLocal(g_ThreadLocalIndex));
			NMib::NSys::fg_Thread_SetLocal(g_ThreadLocalIndex, nullptr);
			NMib::NSys::fg_Thread_FreeLocal(g_ThreadLocalIndex);
		};

		DMibTestSuite("Current Thread Performance")
		{
			static constexpr umint c_nTests = 100;
			static constexpr umint c_nLoops = 1000000;
			CPrefCyclesTimeMeasureMin NativeTime;
			CPrefCyclesTimeMeasureMin MalterlibTime;

			umint volatile ThreadResultMalterlib = 0;
			umint volatile ThreadResultNative = 0;

			for (umint iTest = 0; iTest < c_nTests; ++iTest)
			{
				NativeTime.f_Start();
				ThreadResultNative = []() inline_never
					{
						umint Results = 0;
						for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
						{
							Results += fs_CurrentThreadNative();
							DLimitOptimizations;
						}
						return Results;
					}
					()
				;
				NativeTime.f_Stop();
			}

			for (umint iTest = 0; iTest < c_nTests; ++iTest)
			{
				MalterlibTime.f_Start();
				ThreadResultMalterlib = []() inline_never
					{
						umint Results = 0;
						for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
						{
							Results += fs_CurrentThreadMalterlib();
							DLimitOptimizations;
						}
						return Results;
					}
					()
				;
				MalterlibTime.f_Stop();
			}

			NativeTime /= c_nLoops;
			MalterlibTime /= c_nLoops;

			umint ThreadResultMalterlib1 = ThreadResultMalterlib;
			umint ThreadResultNative1 = ThreadResultNative;
			DMibTest(DMibExpr(ThreadResultMalterlib1) == DMibExpr(ThreadResultNative1));
			if (NMib::NTest::fg_GroupActive("Performance"))
				DMibTest(DMibExpr(NativeTime) / DMibExpr(MalterlibTime) >= DMibExpr(1.0));
		};
#	if !defined(DPlatformFamily_Linux) || defined(DMibConfig_LinuxPThreadMonitoring)
		DMibTestSuite("Enumerate other threads")
		{
			NMib::NAtomic::TCAtomic<umint> ThreadID{0};
			CEvent ThreadReady;
			CEvent FinishThread;
			auto pThread = CThreadObject::fs_StartThread
				(
					[&](CThreadObject *_pThread) -> aint
					{
						ThreadID.f_Store(NMib::NSys::fg_Thread_GetCurrentUID(), NMib::NAtomic::gc_MemoryOrder_Release);
						ThreadReady.f_SetSignaled();
						FinishThread.f_Wait();
						return 0;
					}
					, "Enumerated thread"
				)
			;

			ThreadReady.f_Wait();
			umint ExpectedThreadID = ThreadID.f_Load(NMib::NAtomic::gc_MemoryOrder_Acquire);
			bool bFoundThread = false;
			NMib::NSys::fg_Thread_EnumOtherThreadsInProcess
				(
					[&](umint _ThreadID)
					{
						if (_ThreadID == ExpectedThreadID)
							bFoundThread = true;
					}
				)
			;

			FinishThread.f_SetSignaled();
			pThread->f_Stop();
			pThread.f_Clear();

			DMibTest(DMibExpr(bFoundThread) == DMibExpr(true));
		};
#	endif
#	ifdef DPlatformFamily_Linux
		DMibTestSuite("LinuxPriority")
		{
			struct CSchedule
			{
				int m_Policy = -1;
				int m_Nice = 100;
			};

			auto fGetSchedule = []
				{
					CSchedule Schedule;
					Schedule.m_Policy = sched_getscheduler(0) & ~0x40000000; // SCHED_RESET_ON_FORK
					Schedule.m_Nice = getpriority(PRIO_PROCESS, 0);

					return Schedule;
				}
			;

			// Starts a thread at _Priority, which in turn starts one at _ChildPriority
			auto fRun = [&](NMib::EExecutionPriority _Priority, NMib::EExecutionPriority _ChildPriority, CSchedule &o_Schedule, CSchedule &o_ChildSchedule)
				{
					auto pThread = CThreadObject::fs_StartThread
						(
							[&](CThreadObject *_pThread) -> aint
							{
								o_Schedule = fGetSchedule();

								auto pChildThread = CThreadObject::fs_StartThread
									(
										[&](CThreadObject *_pChildThread) -> aint
										{
											o_ChildSchedule = fGetSchedule();
											return 0;
										}
										, "Priority child"
										, _ChildPriority
									)
								;
								pChildThread->f_Stop();

								return 0;
							}
							, "Priority parent"
							, _Priority
						)
					;
					pThread->f_Stop();
				}
			;

			// A process that was started lowered cannot reach the priorities above its starting point
			int BaseNice = fGetSchedule().m_Nice;

			auto fCheck = [&](NMib::NStr::CStr const &_Case, NMib::EExecutionPriority _Priority, int _Policy, int _Nice)
				{
					DMibTestPath(_Case);

					CSchedule Schedule;
					CSchedule ChildSchedule;
					fRun(_Priority, NMib::EExecutionPriority_Normal, Schedule, ChildSchedule);

					DMibExpect(Schedule.m_Policy, ==, _Policy);
					if (_Policy == SCHED_OTHER)
						DMibExpect(Schedule.m_Nice, ==, NMib::fg_Max(_Nice, BaseNice));

					DMibExpect(ChildSchedule.m_Policy, ==, SCHED_OTHER);
					DMibExpect(ChildSchedule.m_Nice, ==, NMib::fg_Max(0, BaseNice));
				}
			;

			fCheck("Lowest", NMib::EExecutionPriority_Lowest, SCHED_IDLE, 0);
			fCheck("Low", NMib::EExecutionPriority_Low, SCHED_OTHER, 10);
			fCheck("BelowNormal", NMib::EExecutionPriority_BelowNormal, SCHED_OTHER, 5);
			fCheck("Normal", NMib::EExecutionPriority_Normal, SCHED_OTHER, 0);

			rlimit NiceLimit = {};
			bool bGranted = !getrlimit(RLIMIT_NICE, &NiceLimit) && 20 - (int)NMib::fg_Min(NiceLimit.rlim_cur, rlim_t(40)) <= BaseNice;

			// Empty for a thread that exited after the tasks were listed
			auto fReadProcFile = [](NMib::NStr::CStr const &_Path) -> NMib::NStr::CStr
				{
					NMib::NContainer::TCVector<ch8> Data;
					if (!NMib::NPlatform::fg_ReadProcFS(NMib::NStr::CFStr256(_Path), Data))
						return {};

					return NMib::NStr::CStr(Data.f_GetArray(), Data.f_GetLen());
				}
			;

			// Returns why the child failed. A child that hangs is described before it is killed: what each of its threads
			// is blocked in, and for a futex the owner the lock word names, which no longer exists when the lock was held
			// by another thread of the parent at the time of the fork
			auto fWaitForChild = [&](pid_t _ProcessID) -> NMib::NStr::CStr
				{
					int Status = 0;
					pid_t Waited = 0;
					for (umint iPoll = 0; iPoll < 3000 && Waited == 0; ++iPoll)
					{
						Waited = waitpid(_ProcessID, &Status, WNOHANG);
						if (Waited == 0)
							NMib::NSys::fg_Thread_Sleep(0.01f);
					}

					if (Waited < 0)
						return NMib::NStr::CStr::CFormat("waitpid: {}\n") << strerror(errno);

					if (Waited == _ProcessID)
					{
						if (WIFSIGNALED(Status))
							return NMib::NStr::CStr::CFormat("child terminated by signal {}\n") << WTERMSIG(Status);

						if (!WIFEXITED(Status) || WEXITSTATUS(Status) != 0)
							return NMib::NStr::CStr::CFormat("child exited with status {}\n") << Status;

						return {};
					}

					NMib::NStr::CStr Description = NMib::NStr::CStr::CFormat("child {} hangs\n") << (int)_ProcessID;
					try
					{
						NMib::NStr::CStr ProcessDirectory = NMib::NStr::CStr::CFormat("/proc/{}") << (int)_ProcessID;
						NMib::NStr::CStr Maps = fReadProcFile(ProcessDirectory + "/maps");
						Description += NMib::NStr::CStr::CFormat("   maps: {}\n") << Maps.f_Left(NMib::fg_Max(Maps.f_Find("\n"), aint(0)));

						int MemoryFile = open(ProcessDirectory + "/mem", O_RDONLY);
						for (auto &TaskDirectory : NMib::NFile::CFile::fs_FindFiles(ProcessDirectory + "/task/*", NMib::NFile::EFileAttrib_Directory))
						{
							NMib::NStr::CStr SystemCall = fReadProcFile(TaskDirectory + "/syscall").f_Trim();
							Description
								+= NMib::NStr::CStr::CFormat("   {} {}: wchan {} syscall {}\n")
								<< NMib::NFile::CFile::fs_GetFile(TaskDirectory)
								<< fReadProcFile(TaskDirectory + "/comm").f_Trim()
								<< fReadProcFile(TaskDirectory + "/wchan").f_Trim()
								<< SystemCall
							;

							uint64 Number = 0;
							uint64 Address = 0;
							uint32 Word = 0;
							aint nParsed = 0;
							(NMib::NStr::CStr::CParse("{} 0x{nfh}") >> Number >> Address).f_Parse(SystemCall, nParsed);
							bool bFutex = nParsed == 2 && (Number == 98 || Number == 202 || Number == 240 || Number == 422); // futex on arm64, x64 and x86
							if (bFutex && MemoryFile >= 0 && pread(MemoryFile, &Word, sizeof(Word), (off_t)Address) == (ssize_t)sizeof(Word))
								Description += NMib::NStr::CStr::CFormat("      futex word {nfh} owner {}\n") << Word << (Word & 0x3FFFFFFF);
						}

						if (MemoryFile >= 0)
							close(MemoryFile);
					}
					catch (NMib::NException::CException const &_Exception)
					{
						Description += NMib::NStr::CStr::CFormat("   {}\n") << _Exception.f_GetErrorStr();
					}

					kill(_ProcessID, SIGKILL);
					waitpid(_ProcessID, &Status, 0);

					return Description;
				}
			;

			DMibTestCategory("Fork")
			{
				// Neither the spawn helper nor a spawn server exists in the child, so the thread is created directly
				pid_t ProcessID = fork();
				DMibAssertTrue(ProcessID >= 0);
				if (!ProcessID)
				{
					alarm(60);

					CSchedule Schedule;
					CSchedule ChildSchedule;
					fRun(NMib::EExecutionPriority_Low, NMib::EExecutionPriority_Normal, Schedule, ChildSchedule);

					bool bSuccess
						= Schedule.m_Policy == SCHED_OTHER
						&& Schedule.m_Nice == NMib::fg_Max(10, BaseNice)
						&& ChildSchedule.m_Policy == SCHED_OTHER
						&& (!bGranted || ChildSchedule.m_Nice == NMib::fg_Max(0, BaseNice))
					;

					_exit(bSuccess ? 0 : 1);
				}

				DMibExpect(fWaitForChild(ProcessID), ==, "");

				fCheck("ParentAfterFork", NMib::EExecutionPriority_Low, SCHED_OTHER, 10);
			};

			DMibTestCategory("ForkDuringHandoff")
			{
				// The fork hooks take the lock that thread creation hands off under
				NMib::NAtomic::TCAtomic<bool> bStop{false};
				NMib::NAtomic::TCAtomic<umint> nHandoffs{0};
				auto pHandoffThread = CThreadObject::fs_StartThread
					(
						[&](CThreadObject *_pThread) -> aint
						{
							while (!bStop.f_Load())
							{
								CSchedule Schedule;
								CSchedule ChildSchedule;
								fRun(NMib::EExecutionPriority_Low, NMib::EExecutionPriority_Normal, Schedule, ChildSchedule);
								++nHandoffs;
							}

							return 0;
						}
						, "Handoff loop"
					)
				;

				NMib::NStr::CStr Failures;
				for (umint iFork = 0; iFork < 20; ++iFork)
				{
					pid_t ProcessID = fork();
					if (ProcessID < 0)
					{
						Failures += NMib::NStr::CStr::CFormat("fork: {}\n") << strerror(errno);
						continue;
					}

					if (!ProcessID)
					{
						alarm(60);

						CSchedule Schedule;
						CSchedule ChildSchedule;
						fRun(NMib::EExecutionPriority_Low, NMib::EExecutionPriority_Normal, Schedule, ChildSchedule);

						_exit(ChildSchedule.m_Policy == SCHED_OTHER ? 0 : 1);
					}

					Failures += fWaitForChild(ProcessID);
				}

				bStop = true;
				pHandoffThread->f_Stop();

				DMibExpect(Failures, ==, "");
				DMibExpect(nHandoffs.f_Load(), >, umint(0));
			};

			// With the grant every thread corrects itself, so creation must never have been handed off
			if (bGranted)
			{
				bool bFoundSpawnHelper = false;
				bool bReadThreadName = false;
				for (auto &TaskDirectory : NMib::NFile::CFile::fs_FindFiles("/proc/self/task/*", NMib::NFile::EFileAttrib_Directory))
				{
					NMib::NStr::CStr ThreadName = fReadProcFile(TaskDirectory + "/comm").f_Trim();
					if (ThreadName)
						bReadThreadName = true;

					if (ThreadName == "Thread spawner")
						bFoundSpawnHelper = true;
				}

				DMibExpectTrue(bReadThreadName);
				DMibExpectFalse(bFoundSpawnHelper);
			}
		};
#	endif
		DMibTestSuite("Lock Performance")
		{
			static constexpr umint c_nTests = 101;
			static constexpr umint c_nLoops = 100000;

			CTestPerformance PerfTestMutual(0.75, false);
			CTestPerformance PerfTestMutualRecursive(0.75, false);

			CTestPerformance PerfTestSimpleMutual(0.75, false);

			CTestPerformance PerfTestReadWriteRead(0.75, false);
			CTestPerformance PerfTestReadWriteWrite(0.75, false);

#	if defined(DEnableWin32ThreadTest)
			{
				CWindowsCriticalSection Crit;
				InitializeCriticalSection((CRITICAL_SECTION *)&Crit);

				{
					CTestPerformanceMeasure Measure("WinCriticalSection");
					for (umint iTest = 0; iTest < c_nTests; ++iTest)
					{
						Measure.f_Start();
						[&]() inline_never
						{
							for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
							{
								EnterCriticalSection((CRITICAL_SECTION *)&Crit);
								LeaveCriticalSection((CRITICAL_SECTION *)&Crit);
							}
						}();
						Measure.f_Stop(c_nLoops);
					}

					PerfTestMutual.f_AddReference(Measure);
				}
				{
					CTestPerformanceMeasure Measure("WinCriticalSectionRecursive");
					for (umint iTest = 0; iTest < c_nTests; ++iTest)
					{
						EnterCriticalSection((CRITICAL_SECTION *)&Crit);
						Measure.f_Start();
						[&]() inline_never
						{
							for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
							{
								EnterCriticalSection((CRITICAL_SECTION *)&Crit);
								LeaveCriticalSection((CRITICAL_SECTION *)&Crit);
							}
						}();
						Measure.f_Stop(c_nLoops);
						LeaveCriticalSection((CRITICAL_SECTION *)&Crit);
					}
					PerfTestMutualRecursive.f_AddReference(Measure);
				}
				DeleteCriticalSection((CRITICAL_SECTION *)&Crit);
			}

#		if defined(DEnableWin32ConCrt)
			{
				{
					Concurrency::reader_writer_lock Lock;
					{
						Lock.lock();
						Lock.unlock();
					}
					{
						CTestPerformanceMeasure Measure("Win32ConCrtReadWrite");
						for (umint iTest = 0; iTest < c_nTests; ++iTest)
						{
							Measure.f_Start();
							[&]() inline_never
							{
								for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
								{
									Lock.lock();
									Lock.unlock();
								}
							}();
							Measure.f_Stop(c_nLoops);
						}
						PerfTestReadWriteWrite.f_AddReference(Measure);
					}
				}
				{
					Concurrency::reader_writer_lock Lock;
					{
						CTestPerformanceMeasure Measure("Win32ConCrtReadWrite");
						for (umint iTest = 0; iTest < c_nTests; ++iTest)
						{
							Measure.f_Start();
							[&]() inline_never
							{
								for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
								{
									Lock.lock_read();
									Lock.unlock();
								}
							}();
							Measure.f_Stop(c_nLoops);
						}
						PerfTestReadWriteRead.f_AddReference(Measure);
					}
				}
			}
#		endif

#	endif
			{
				{
					CTestPerformanceMeasure Measure("CMutual");
					for (umint iTest = 0; iTest < c_nTests; ++iTest)
					{
						Measure.f_Start();
						[]() inline_never
						{
							NMib::NThread::CMutual Lock;
							for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
								DMibLock(Lock);
						}();
						Measure.f_Stop(c_nLoops);
					}
					PerfTestMutual.f_Add(Measure);
					PerfTestSimpleMutual.f_Add(Measure);
				}
				{
					CTestPerformanceMeasure Measure("CMutual");
					for (umint iTest = 0; iTest < c_nTests; ++iTest)
					{
						NMib::NThread::CMutual Lock;
						DMibLock(Lock);
						Measure.f_Start();
						[&]() inline_never
						{
							for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
								DMibLock(Lock);
						}();
						Measure.f_Stop(c_nLoops);
					}
					PerfTestMutualRecursive.f_Add(Measure);
				}
			}
			{
				{
					CTestPerformanceMeasure Measure("CMutualSpin");
					for (umint iTest = 0; iTest < c_nTests; ++iTest)
					{
						NMib::NThread::CMutualSpin Lock;
						Measure.f_Start();
						[&]() inline_never
						{
							for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
								DMibLock(Lock);
						}();
						Measure.f_Stop(c_nLoops);
					}
					PerfTestMutual.f_Add(Measure);
					PerfTestSimpleMutual.f_Add(Measure);
				}
				{
					CTestPerformanceMeasure Measure("CMutualSpin");
					for (umint iTest = 0; iTest < c_nTests; ++iTest)
					{
						NMib::NThread::CMutualSpin Lock;
						DMibLock(Lock);
						Measure.f_Start();
						[&]() inline_never
						{
							for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
								DMibLock(Lock);
						}();
						Measure.f_Stop(c_nLoops);
					}
					PerfTestMutualRecursive.f_Add(Measure);
				}
			}
			{
				{
					CTestPerformanceMeasure Measure("recursive_mutex");
					for (umint iTest = 0; iTest < c_nTests; ++iTest)
					{
						std::recursive_mutex Lock;
						Measure.f_Start();
						[&]() inline_never
						{
							for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
							{
								Lock.lock();
								Lock.unlock();
							}
						}();
						Measure.f_Stop(c_nLoops);
					}
					PerfTestMutual.f_AddReference(Measure);
				}
				{
					CTestPerformanceMeasure Measure("recursive_mutex");
					for (umint iTest = 0; iTest < c_nTests; ++iTest)
					{
						std::recursive_mutex Lock;
						Lock.lock();
						Measure.f_Start();
						[&]() inline_never
						{
							for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
							{
								Lock.lock();
								Lock.unlock();
							}
						}();
						Lock.unlock();
						Measure.f_Stop(c_nLoops);
					}
					PerfTestMutualRecursive.f_AddReference(Measure);
				}
			}
			{
				CTestPerformanceMeasure Measure("CMutualSimple");
				for (umint iTest = 0; iTest < c_nTests; ++iTest)
				{
					NMib::NThread::CMutualSimple Lock;
					Measure.f_Start();
					[&]() inline_never
					{
						for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
							DMibLock(Lock);
					}();
					Measure.f_Stop(c_nLoops);
				}
				PerfTestSimpleMutual.f_Add(Measure);
			}
			{
				CTestPerformanceMeasure Measure("CLowLevelLock");
				for (umint iTest = 0; iTest < c_nTests; ++iTest)
				{
					NMib::NThread::CLowLevelLock Lock;
					Measure.f_Start();
					[&]() inline_never
					{
						for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
							DMibLock(Lock);
					}();
					Measure.f_Stop(c_nLoops);
				}
				PerfTestSimpleMutual.f_Add(Measure);
			}
			{
				CTestPerformanceMeasure Measure("mutex");
				for (umint iTest = 0; iTest < c_nTests; ++iTest)
				{
					std::mutex Lock;
					Measure.f_Start();
					[&]() inline_never
					{
						for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
						{
							Lock.lock();
							Lock.unlock();
						}
					}();
					Measure.f_Stop(c_nLoops);
				}
				PerfTestSimpleMutual.f_AddReference(Measure);
			}
			{
				CTestPerformanceMeasure Measure("CMutualManyRead");
				for (umint iTest = 0; iTest < c_nTests; ++iTest)
				{
					NMib::NThread::CMutualManyRead Lock;
					{
						DMibLock(Lock);
					}
					Measure.f_Start();
					[&]() inline_never
					{
						for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
							DMibLock(Lock);
					}();
					Measure.f_Stop(c_nLoops);
				}
				PerfTestReadWriteWrite.f_Add(Measure);
			}
			{
				CTestPerformanceMeasure Measure("CMutualManyRead");
				for (umint iTest = 0; iTest < c_nTests; ++iTest)
				{
					NMib::NThread::CMutualManyRead Lock;
					Measure.f_Start();
					[&]() inline_never
					{
						for (umint iLoop = 0; iLoop < c_nLoops; ++iLoop)
							DMibLockRead(Lock);
					}();
					Measure.f_Stop(c_nLoops);
				}
				PerfTestReadWriteRead.f_Add(Measure);
			}

			if (NMib::NTest::fg_GroupActive("Performance"))
			{
				DMibTest(DMibExpr(PerfTestMutual));
				DMibTest(DMibExpr(PerfTestSimpleMutual));
				DMibTest(DMibExpr(PerfTestMutualRecursive));
				DMibTest(DMibExpr(PerfTestReadWriteWrite));
				DMibTest(DMibExpr(PerfTestReadWriteRead));
			}
		};

		DMibTestSuite("Read write contention")
		{
			CReadWriteContention Contention;
			Contention.f_DoTest();
		};

		DMibTestSuite("Event set reset")
		{
			// A waiter parked at signal time must complete even when a reset
			// lands before it is scheduled
			for (umint iIteration = 0; iIteration < 100; ++iIteration)
			{
				NMib::NThread::CEvent Event;
				NMib::NAtomic::TCAtomic<uint32> nDone{0};

				NMib::NStorage::TCUniquePointer<CThreadObject> pWaiter = CThreadObject::fs_StartThread
					(
						[&](CThreadObject *_pThread) -> aint
						{
							Event.f_Wait();
							nDone.f_FetchAdd(1);
							return 0;
						}
						, "Event set reset waiter"
					)
				;

				// Wait until the waiter has registered on the event (the waiters
				// flag guarantees it captured the pre-signal generation) so the
				// set/reset pair deterministically races with a parked waiter
				while (!Event.f_HasWaiters())
					NMib::NSys::fg_Thread_Yield();

				Event.f_SetSignaled();
				Event.f_ResetSignaled();

				pWaiter->f_Stop();
				pWaiter.f_Clear();

				DMibTest(DMibExpr(nDone.f_Load()) == DMibExpr(1u))(ETestFlag_Aggregated);
			}
		};

		DMibTestSuite("Futex")
		{
			NMib::NAtomic::TCAtomic<uint32> Word{0};
			static_assert(sizeof(Word) == sizeof(uint32));
			uint32 volatile *pWord = (uint32 volatile *)&Word;

			// A wait with a mismatched expected value must return immediately
			NMib::NSys::fg_Futex_Wait(pWord, 1);

			// Wakes with no waiters are no-ops
			NMib::NSys::fg_Futex_WakeOne(pWord);
			NMib::NSys::fg_Futex_WakeAll(pWord);

			// A wait with zero timeout reports a timeout immediately
			DMibTest(DMibExpr(NMib::NSys::fg_Futex_WaitTimeout(pWord, 0, 0.0)) == DMibExpr(true));

			{
				// A timed wait on a matching value times out (loop tolerates spurious wakeups)
				bool bTimedOut = false;
				for (umint iAttempt = 0; iAttempt < 100; ++iAttempt)
				{
					if (NMib::NSys::fg_Futex_WaitTimeout(pWord, 0, 0.02))
					{
						bTimedOut = true;
						break;
					}
				}

				DMibTest(DMibExpr(bTimedOut) == DMibExpr(true));
			}

			{
				// Cross-thread wake-one releases a parked waiter
				Word.f_Store(0);

				NMib::NStorage::TCUniquePointer<CThreadObject> pWaker = CThreadObject::fs_StartThread
					(
						[&](CThreadObject *_pThread) -> aint
						{
							NMib::NSys::fg_Thread_Sleep(0.02f);
							Word.f_Store(1);
							NMib::NSys::fg_Futex_WakeOne(pWord);
							return 0;
						}
						, "Futex test waker"
					)
				;

				while (Word.f_Load() == 0)
					NMib::NSys::fg_Futex_Wait(pWord, 0);

				pWaker->f_Stop();
				pWaker.f_Clear();
			}

			{
				// Wake-all releases every parked waiter
				Word.f_Store(0);

				NMib::NAtomic::TCAtomic<uint32> nWoken{0};

				constexpr aint c_nWaiters = 4;

				NMib::NStorage::TCUniquePointer<CThreadObject> Waiters[c_nWaiters];
				for (umint iWaiter = 0; iWaiter < c_nWaiters; ++iWaiter)
				{
					Waiters[iWaiter] = CThreadObject::fs_StartThread
						(
							[&](CThreadObject *_pThread) -> aint
							{
								while (Word.f_Load() == 0)
									NMib::NSys::fg_Futex_Wait(pWord, 0);
								nWoken.f_FetchAdd(1);
								return 0;
							}
							, "Futex test waiter"
						)
					;
				}

				NMib::NSys::fg_Thread_Sleep(0.05f);
				Word.f_Store(1);
				NMib::NSys::fg_Futex_WakeAll(pWord);

				for (umint iWaiter = 0; iWaiter < c_nWaiters; ++iWaiter)
				{
					Waiters[iWaiter]->f_Stop();
					Waiters[iWaiter].f_Clear();
				}

				DMibTest(DMibExpr(nWoken.f_Load()) == DMibExpr((uint32)c_nWaiters));
			}

			{
				// A timed wait that is woken before the timeout must not report a timeout
				Word.f_Store(0);

				NMib::NStorage::TCUniquePointer<CThreadObject> pWaker = CThreadObject::fs_StartThread
					(
						[&](CThreadObject *_pThread) -> aint
						{
							NMib::NSys::fg_Thread_Sleep(0.02f);
							Word.f_Store(1);
							NMib::NSys::fg_Futex_WakeAll(pWord);
							return 0;
						}
						, "Futex test timed waker"
					)
				;

				bool bTimedOut = false;
				while (Word.f_Load() == 0)
				{
					if (NMib::NSys::fg_Futex_WaitTimeout(pWord, 0, 10.0))
					{
						bTimedOut = true;
						break;
					}
				}

				pWaker->f_Stop();
				pWaker.f_Clear();

				DMibTest(DMibExpr(bTimedOut) == DMibExpr(false));
			}
		};

#	if DMibConfig_Tests_Enable
		DMibTestCategory("Thread local")
		{
			DMibTestSuite("Set another thread's local storage from dynamic library")
			{
				NMib::NStr::CStr DllPath = NMib::NStr::CStr("Test_Malterlib_Helper_Thread") + NMib::NFile::CFile::fs_GetDllExtension();
				DllPath = NMib::NFile::CFile::fs_AppendPath(NMib::NFile::CFile::fs_GetProgramDirectory(), DllPath);

				uint32 (calling_convention_c *fTestFunction)() = nullptr;
				NMib::NThread::CEvent ExistingThreadReady;
				NMib::NThread::CEvent RunExistingThreadTest;
				NMib::NAtomic::TCAtomic<uint32> ExistingThreadResult{NMib::TCLimitsInt<uint32>::mc_Max};
				auto pExistingThread = NMib::NThread::CThreadObject::fs_StartThread
					(
						[&](NMib::NThread::CThreadObject *_pThread) -> aint
						{
							ExistingThreadReady.f_SetSignaled();
							RunExistingThreadTest.f_Wait();
							if (fTestFunction)
								ExistingThreadResult = fTestFunction();
							return 0;
						}
						, "Use dynamically loaded Malterlib from existing thread"
					)
				;
				auto StopExistingThread = NMib::g_OnScopeExit / [&]
					{
						RunExistingThreadTest.f_SetSignaled();
						if (pExistingThread)
						{
							pExistingThread->f_Stop();
							pExistingThread.f_Clear();
						}
					}
				;
				ExistingThreadReady.f_Wait();

				void *pDll = NMib::NSys::fg_LoadLibrary(DllPath);
				DMibTest(DMibExpr(pDll))(ETest_FailAndStop);

				(void * &)fTestFunction = NMib::NSys::fg_GetLibrarySymbol(pDll, "fg_TestSetAnotherThreadLocal");
				DMibTest(DMibExpr(fTestFunction))(ETest_FailAndStop);
				DMibTest(DMibExpr(fTestFunction()) == DMibExpr(uint32(0)));

				RunExistingThreadTest.f_SetSignaled();
				pExistingThread->f_Stop();
				pExistingThread.f_Clear();
				DMibTest(DMibExpr(ExistingThreadResult.f_Load()) == DMibExpr(uint32(0)));

				// A build that assumes a Malterlib host refuses to load into anything else, except on Windows where
				// the library needs nothing from its host
#		if defined(DPlatformFamily_Windows) || !defined(DMibAssumeMalterlibHost)
				for (bool bExitLoaded : {false, true})
				{
					DMibTestPath(bExitLoaded ? "Exit with the library loaded" : "Unload");

					NMib::NStr::CStr ProgramDirectory = NMib::NFile::CFile::fs_GetProgramDirectory();
					NMib::NStr::CStr LauncherPath = NMib::NFile::CFile::fs_AppendPath
						(
							ProgramDirectory
							, NMib::NStr::CStr("Test_Malterlib_Helper_Thread_NonMalterlibHost") + NMib::NFile::CFile::mc_ExecutableExtension
						)
					;
					NMib::NContainer::TCVector<NMib::NStr::CStr> LauncherParameters;
					LauncherParameters.f_Insert(DllPath);
#			if defined(DPlatformFamily_macOS) && defined(DMibConfig_PThreadIntrospection)
					LauncherParameters.f_Insert("--expect-hook");
#			endif
					if (bExitLoaded)
						LauncherParameters.f_Insert("--exit-loaded");
					NMib::NAtomic::TCAtomic<uint32> ExitCode{NMib::TCLimitsInt<uint32>::mc_Max};
					NMib::NAtomic::TCAtomic<bool> bLaunchFailed{false};
					NMib::NProcess::CProcessLaunchParams LaunchParams = NMib::NProcess::CProcessLaunchParams::fs_LaunchExecutable
						(
							LauncherPath
							, LauncherParameters
							, ProgramDirectory
							, [&](NMib::NProcess::CProcessLaunchStateChangeVariant const &_State, fp64 _TimeSinceStart)
							{
								switch (_State.f_GetTypeID())
								{
								case NMib::NProcess::EProcessLaunchState_LaunchFailed: bLaunchFailed = true; break;
								case NMib::NProcess::EProcessLaunchState_Exited: ExitCode = _State.f_Get<NMib::NProcess::EProcessLaunchState_Exited>(); break;
								default: break;
								}
							}
						)
					;
					LaunchParams.m_bShowLaunched = false;
					{
						NMib::NProcess::CProcessLaunch Launcher(LaunchParams, NMib::NProcess::EProcessLaunchCloseFlag_BlockOnExit);
					}

					DMibTest(DMibExpr(bLaunchFailed.f_Load()) == DMibExpr(false));
					DMibTest(DMibExpr(ExitCode.f_Load()) == DMibExpr(uint32(0)));
				}
#		endif

#		if defined(DPlatformFamily_macOS) || defined(DPlatformFamily_Linux)
				pid_t ProcessID = fork();
				DMibTest(DMibExpr(ProcessID >= 0))(ETest_FailAndStop);
				if (!ProcessID)
					_exit((int)fTestFunction());

				int Status = 0;
				DMibTest(DMibExpr(waitpid(ProcessID, &Status, 0)) == DMibExpr(ProcessID));
				DMibTest(DMibExpr(WIFEXITED(Status)) == DMibExpr(true));
				DMibTest(DMibExpr(WEXITSTATUS(Status)) == DMibExpr(0));
#		endif

				NMib::NSys::fg_FreeLibrary(pDll);
			};

			DMibTestSuite("Set another thread's local storage")
			{
				NMib::NContainer::TCVector<umint> ThreadLocals;
				umint iThreadLocal;
				do
				{
					iThreadLocal = NMib::NSys::fg_Thread_AllocLocal();
					ThreadLocals.f_InsertLast(iThreadLocal);
				}
				while (iThreadLocal < 32 || iThreadLocal % 32);

				NMib::NAtomic::TCAtomic<umint> ThreadID{0};
				NMib::NAtomic::TCAtomic<void *> pThreadValue{nullptr};
				umint Value = 0x12345678;
				CEvent ThreadReady;
				CEvent ReadValue;
				auto pThread = CThreadObject::fs_StartThread
					(
						[&](CThreadObject *_pThread) -> aint
						{
							ThreadID.f_Store(NMib::NSys::fg_Thread_GetCurrentUID(), NMib::NAtomic::gc_MemoryOrder_Release);
							ThreadReady.f_SetSignaled();
							ReadValue.f_Wait();
							pThreadValue.f_Store(NMib::NSys::fg_Thread_GetLocal(iThreadLocal), NMib::NAtomic::gc_MemoryOrder_Release);
							NMib::NSys::fg_Thread_SetLocal(iThreadLocal, nullptr);
							return 0;
						}
						, "Set another thread's local storage"
					)
				;

				ThreadReady.f_Wait();
				umint TargetThreadID = ThreadID.f_Load(NMib::NAtomic::gc_MemoryOrder_Acquire);
				NMib::NSys::fg_Thread_SetLocal(TargetThreadID, iThreadLocal, &Value);
				DMibTest(DMibExpr(NMib::NSys::fg_Thread_GetLocal(TargetThreadID, iThreadLocal)) == DMibExpr((void *)&Value));

				ReadValue.f_SetSignaled();
				pThread->f_Stop();
				pThread.f_Clear();

				DMibTest(DMibExpr(pThreadValue.f_Load(NMib::NAtomic::gc_MemoryOrder_Acquire)) == DMibExpr((void *)&Value));

				for (umint iLocal = ThreadLocals.f_GetLen(); iLocal-- > 0;)
					NMib::NSys::fg_Thread_FreeLocal(ThreadLocals[iLocal]);
			};

			DMibTestSuite("Always created")
			{
				for (umint iIteration = 0; iIteration < 10; ++iIteration)
				{
					NMib::NStorage::TCAggregate<TCThreadLocal<CTemp35, NMib::NMemory::CAllocator_Heap, EThreadLocalFlag_AlwaysCreated>> ThreadLocal = {DAggregateInit};
					NMib::NThread::CEvent Event;
					NMib::NThread::CEvent EventDone;
					NMib::NStorage::TCUniquePointer<CThreadObject> pThread = CThreadObject::fs_StartThread
						(
							[&](CThreadObject *_pThread) -> aint
							{
								_pThread->m_EventWantQuit.f_Wait();
								int32 ValueInThread = (*ThreadLocal)->m_Value;
								DMibTest(DMibExpr(ValueInThread) == DMibExpr(35))(ETestFlag_Aggregated);
								EventDone.f_SetSignaled();
								Event.f_Wait();
								return 0;
							}
							, "Test thread local thread"
						)
					;
					if (iIteration > 5)
						NMib::NSys::fg_Thread_Sleep(0.050f);
					(*ThreadLocal)->m_Value = 36;
					pThread->m_EventWantQuit.f_Signal();
					EventDone.f_Wait();
					DMibTest(DMibExpr((*ThreadLocal)->m_Value) == DMibExpr(36))(ETestFlag_Aggregated);
					if (iIteration & 1)
					{
						ThreadLocal.f_Destruct();
						Event.f_SetSignaled();
						pThread->f_Stop();
						pThread.f_Clear();
					}
					else
					{
						Event.f_SetSignaled();
						pThread->f_Stop();
						pThread.f_Clear();
						ThreadLocal.f_Destruct();
					}
				};
			};

			DMibTestSuite("Inherited")
			{
				for (umint iIteration = 0; iIteration < 10; ++iIteration)
				{
					NMib::NStorage::TCAggregate<TCThreadLocal<CTemp, NMib::NMemory::CAllocator_Heap, EThreadLocalFlag_Inherit>> ThreadLocal = {DAggregateInit};

					NMib::NThread::CEvent Event;
					NMib::NThread::CEvent EventDone;
					(*ThreadLocal)->m_Value = 36;

					DMibTest(DMibExpr((*ThreadLocal)->m_Value) == DMibExpr(36))(ETestFlag_Aggregated);
					NMib::NStorage::TCUniquePointer<CThreadObject> pThread = CThreadObject::fs_StartThread
						(
							[&](CThreadObject *_pThread) -> aint
							{
								_pThread->m_EventWantQuit.f_Wait();
								int32 ValueInThread = (*ThreadLocal)->m_Value;
								DMibTest(DMibExpr(ValueInThread) == DMibExpr(36))(ETestFlag_Aggregated);
								EventDone.f_SetSignaled();
								Event.f_Wait();
								return 0;
							}
							, "Test thread local thread"
						)
					;
					if (iIteration > 5)
						NMib::NSys::fg_Thread_Sleep(0.050f);
					pThread->m_EventWantQuit.f_Signal();
					EventDone.f_Wait();
					if (iIteration & 1)
					{
						ThreadLocal.f_Destruct();
						Event.f_SetSignaled();
						pThread->f_Stop();
						pThread.f_Clear();
					}
					else
					{
						Event.f_SetSignaled();
						pThread->f_Stop();
						pThread.f_Clear();
						ThreadLocal.f_Destruct();
					}
				}
			};

			DMibTestSuite("Inherited and always create")
			{
				for (umint iIteration = 0; iIteration < 10; ++iIteration)
				{
					NMib::NStorage::TCAggregate
						<
							TCThreadLocal<CTemp, NMib::NMemory::CAllocator_Heap, EThreadLocalFlag(uint32(EThreadLocalFlag_Inherit) | uint32(EThreadLocalFlag_AlwaysCreated))>
						>
						ThreadLocal = {DAggregateInit}
					;
					NMib::NThread::CEvent Event;
					NMib::NThread::CEvent EventDone;
					NMib::NStorage::TCUniquePointer<CThreadObject> pThread0 = CThreadObject::fs_StartThread
						(
							[&](CThreadObject *_pThread) -> aint
							{
								_pThread->m_EventWantQuit.f_Wait();
								int32 ValueInThread0 = (*ThreadLocal)->m_Value;
								DMibTest(DMibExpr(ValueInThread0) == DMibExpr(36) || DMibExpr(ValueInThread0) == DMibExpr(0))(ETestFlag_Aggregated);
								return 0;
							}
							, "Test thread local thread 0"
						)
					;
					if (iIteration > 5)
						NMib::NSys::fg_Thread_Sleep(0.050f);
					(*ThreadLocal)->m_Value = 36;
					DMibTest(DMibExpr((*ThreadLocal)->m_Value) == DMibExpr(36))(ETestFlag_Aggregated);
					NMib::NStorage::TCUniquePointer<CThreadObject> pThread1 = CThreadObject::fs_StartThread
						(
							[&](CThreadObject *_pThread) -> aint
							{
								_pThread->m_EventWantQuit.f_Wait();
								int32 ValueInThread1 = (*ThreadLocal)->m_Value;
								DMibTest(DMibExpr(ValueInThread1) == DMibExpr(36))(ETestFlag_Aggregated);
								EventDone.f_SetSignaled();
								Event.f_Wait();
								return 0;
							}
							, "Test thread local thread 1"
						)
					;

					pThread0->m_EventWantQuit.f_Signal();
					pThread0->f_Stop();
					pThread1->m_EventWantQuit.f_Signal();
					EventDone.f_Wait();
					if (iIteration & 1)
					{
						ThreadLocal.f_Destruct();
						Event.f_SetSignaled();
						pThread1->f_Stop();
						pThread0.f_Clear();
						pThread1.f_Clear();
					}
					else
					{
						Event.f_SetSignaled();
						pThread1->f_Stop();
						pThread0.f_Clear();
						pThread1.f_Clear();
						ThreadLocal.f_Destruct();
					}
				}
			};
		};
#	endif
	}

	DMibTestRegister(CThread_Tests, Malterlib::Thread);
}

#else

#	if defined(DPlatformFamily_Windows)
#		define DEnableWin32ThreadTest
#	endif

#	if defined(DEnableWin32ThreadTest)
#		include <windows.h>
/*
struct CDebugInfo
{
private:
	uint16   Type;
	uint16   CreatorBackTraceIndex;
	struct CWindowsCriticalSection *CriticalSection;
	LIST_ENTRY ProcessLocksList;
	uint32 EntryCount;
	uint32 ContentionCount;
	uint32 Spare[ 2 ];
};

#define RTL_CRITSECT_TYPE 0 // NOLINT(malterlib-macro-naming)
#define RTL_RESOURCE_TYPE 1 // NOLINT(malterlib-macro-naming)

struct CWindowsCriticalSection
{
private:
	CDebugInfo * DebugInfo;

	//
	//  The following three fields control entering and exiting the critical
	//  section for the resource
	//

	LONG LockCount;
	LONG RecursionCount;
	HANDLE OwningThread;        // from the thread's ClientId->UniqueThread
	HANDLE LockSemaphore;
	ULONG_PTR SpinCount;        // force size on 64-bit systems when packed
};*/

using CWindowsCriticalSection = CRITICAL_SECTION;

#	endif

struct CTestThread : public CMalterlibTest
{
	static CTestThread *ms_pThis;

	struct CThread : public NMib::NThread::CThread
	{
		NMib::NStr::CStr f_GetThreadName()
		{
			return "MalterlibCertifier_TestThread";
		}

		CThread()
		{
		}

		~CThread()
		{
			f_Stop();
		}

		aint f_Main()
		{
			while (1)
			{
				m_EventWantQuit.f_Wait();
				if (f_GetState() == NMib::NThread::EThreadState_EventWantQuit)
				{
					DMibTrace("Got thread quit event\r\n", 0);
					return 0;
				}

				DMibTrace("Got an event\r\n", 0);
			}

			return 0;
		}
	};

	bool f_AutomaticTest()
	{
		return true;
	}

	static aint m_IncValue;
	static aint *m_pIncValue;
	static NMib::NThread::CMutualManyRead m_IncLock;
	static NMib::NThread::CMutual m_IncLockMutual;
	static NMib::NThread::CEventAutoReset m_IncEvent;

	static NMib::NThread::CMutual m_IncDoneLock;
	static aint m_IncDone;

	struct CIncThread : public NMib::NThread::CThread
	{
		NMib::NStr::CStr f_GetThreadName()
		{
			return "MalterlibCertifier_IncThread";
		}

		CTestThread *m_pTest;

		CIncThread()
		{
			m_pTest = nullptr;
		}

		void f_Start(CTestThread *_pTest)
		{
			m_pTest = _pTest;
			NMib::NThread::CThread::f_Start();
		}

		~CIncThread()
		{
			f_Stop();
		}

		aint f_Main()
		{
			while (1)
			{
				m_EventWantQuit.f_Wait();
				if (f_GetState() == NMib::NThread::EThreadState_EventWantQuit)
					break;

				int nTest = 100000;
				while (nTest)
				{
					{
						m_pTest->m_IncLock.f_Lock();
#	if DMibEnableSafeCheck > 0 && 0
						++(*m_pTest->m_pIncValue);
#	else
						++m_pTest->m_IncValue;
#	endif
						m_pTest->m_IncLock.f_Unlock();
					}
					--nTest;
				}
				{
					DMibLock(m_pTest->m_IncDoneLock);
					++m_pTest->m_IncDone;
				}
				m_pTest->m_IncEvent.f_Signal();
			}

			return 0;
		}
	};

	struct CReadThread : public NMib::NThread::CThread
	{
		NMib::NStr::CStr f_GetThreadName()
		{
			return "MalterlibCertifier_ReadThread";
		}

		CTestThread *m_pTest;

		CReadThread()
		{
			m_pTest = nullptr;
			m_nReads = 0;
			m_bStop = false;
		}

		void f_Start(CTestThread *_pTest)
		{
			m_pTest = _pTest;
			NMib::NThread::CThread::f_Start();
		}

		~CReadThread()
		{
			f_Stop();
		}

		umint m_nReads;
		bool m_bStop;

		aint f_Main()
		{
			while (1)
			{
				m_EventWantQuit.f_Wait();
				if (f_GetState() == NMib::NThread::EThreadState_EventWantQuit)
					break;
				m_nReads = 0;
				while (1)
				{
					if (NMib::fg_Volatile(m_bStop))
						break;

					{
						m_pTest->m_IncLock.f_LockRead();

#	if DMibEnableSafeCheck > 0 && 0
						DMibFastCheck(!m_pTest->m_IncLock.f_IsLocked());
						for (umint i = 0; i < 1000; ++i)
							NMib::fg_Volatile(m_pTest->m_pIncValue) = nullptr;
						NMib::fg_Volatile(m_pTest->m_pIncValue) = &m_pTest->m_IncValue;
						DMibFastCheck(!m_pTest->m_IncLock.f_IsLocked());
#	endif
						++m_nReads;

						m_pTest->m_IncLock.f_UnlockRead();
					}
				}
				m_bStop = false;
				m_pTest->m_IncEvent.f_Signal();
			}

			return 0;
		}
	};

	NAtomic::TCAtomic<smint> m_ThreadLocalInstances;
	struct CThreadLocal
	{
		CTestThread *m_pThread;
		umint m_TestInherit;

		CThreadLocal()
		{
			m_pThread = nullptr;
			m_TestInherit = 0;
		}

		CThreadLocal(CThreadLocal const &_Other)
		{
			m_pThread = nullptr;
			m_TestInherit = 0;
			DMibRequire(_Other.m_TestInherit == 4545);
		}

		~CThreadLocal()
		{
			DMibRequire(m_pThread);
			m_pThread->m_ThreadLocalInstances.f_Decrease();
		}

		void f_Init(CTestThread *_pThread)
		{
			m_pThread = _pThread;
			m_pThread->m_ThreadLocalInstances.f_Increase();
		}
	};

	NMib::NThread::TCThreadLocal<CThreadLocal, NMib::NMemory::CAllocator_Heap, true> m_ThreadLocal;

	struct CThreadLocalTest : public NMib::NThread::CThread
	{
		CTestThread *m_pThread;
		void f_Init(CTestThread *_pThread)
		{
			m_pThread = _pThread;
		}

		NMib::NStr::CStr f_GetThreadName()
		{
			return "MalterlibCertifier_CThreadLocalTest";
		}

		aint f_Main()
		{
			m_pThread->m_ThreadLocal->f_Init(m_pThread);

			return 0;
		}
	};

	NMib::NStr::CStr Certify(CTestInterface &_Interface)
	{
		ms_pThis = this;
#	ifdef DMibDebug
		aint const Tests = 1000000;
		static aint const NumTests = 1;
#	else
		aint const Tests = 1000000;
		static aint const NumTests = 2;
#	endif
		m_ThreadLocal->f_Init(this);
		m_ThreadLocal->m_TestInherit = 4545;

		{
			CThreadLocalTest Threads[10];
			for (umint i = 0; i < 10; ++i)
			{
				Threads[i].f_Init(this);
				Threads[i].f_Start();
			}
			for (umint i = 0; i < 10; ++i)
				Threads[i].f_Stop();
		}

		DMibCheck(m_ThreadLocalInstances.f_Get() == 1);

		return "";

		NMib::NThread::CMutual Lock;
		NMib::NTime::CPerfTimeMeasureMin Timer;

		{
			enum
			{
				mc_IncThreads    = 4
				, mc_ReadThreads = 7
			};

			static CIncThread IncThreads[mc_IncThreads];
			static CReadThread ReadThreads[mc_ReadThreads];
			for (umint i = 0; i < mc_IncThreads; ++i)
				IncThreads[i].f_Start(this);
			for (umint i = 0; i < mc_ReadThreads; ++i)
				ReadThreads[i].f_Start(this);

			NMib::NSys::fg_Thread_Sleep(1.0);

			Timer.f_Reset();
			umint nReads = 0;
			{
				{
					m_IncLock.f_Destruct();
					m_IncLock.f_Construct();
					m_IncLockMutual.f_Destruct();
					m_IncLockMutual.f_Construct();

					m_IncValue = 0;
					m_pIncValue = &m_IncValue;
					m_IncDone = 0;
					{
						DMibScopePerfTimeMeasureMin(Timer);
						for (umint i = 0; i < mc_ReadThreads; ++i)
							ReadThreads[i].m_EventWantQuit.f_Signal();
						for (umint i = 0; i < mc_IncThreads; ++i)
							IncThreads[i].m_EventWantQuit.f_Signal();

						while (1)
						{
							m_IncEvent.f_Wait();
							DMibLock(m_IncDoneLock);
							if (m_IncDone == (mc_IncThreads))
								break;
						}
						for (umint i = 0; i < mc_ReadThreads; ++i)
						{
							ReadThreads[i].m_bStop = true;
							nReads += ReadThreads[i].m_nReads;
						}
						while (1)
						{
							bool bAllStopped = true;
							for (umint i = 0; i < mc_ReadThreads; ++i)
							{
								if (NMib::fg_Volatile(ReadThreads[i].m_bStop))
									bAllStopped = false;
							}
							if (bAllStopped)
								break;
						}
					}
				}
			}
			DMibConOut
				(
					"Performance for CMutualManyRead Lock+Unlock from {} threads = {} locks per second IncValue = {}\r\n"
					, int32(mc_IncThreads)
					, fp64(m_IncValue) / Timer.f_GetTime()
					, m_IncValue
				)
			;
			DMibConOut
				(
					"Performance for CMutualManyRead LockRead+UnlockRead from {} threads = {} locks per second nReads = {}\r\n"
					, int32(mc_ReadThreads)
					, fp64(nReads) / Timer.f_GetTime()
					, nReads
				)
			;
		}

		/*

		{
			Timer.f_Reset();
			NMib::NThread::CMutualManyRead Lock;
			for (aint j = 0; j < NumTests; ++j)
			{
				{
					DMibScopePerfTimeMeasureMin(Timer);

					for (aint i = 0; i < Tests; ++i)
					{
						Lock.f_Lock();
						Lock.f_Unlock();
					}
				}
			}
			DMibTrace("Performance for CMutualManyRead Lock+Unlock = {0} locks per second\n", ((aint)((fp64(Tests) / Timer.f_GetTime()).f_Get())));
		}

		{
			Timer.f_Reset();
			NMib::NThread::CMutualManyRead Lock;
			for (aint j = 0; j < NumTests; ++j)
			{
				{
					DMibScopePerfTimeMeasureMin(Timer);

					for (aint i = 0; i < Tests; ++i)
					{
						Lock.LockRead();
						Lock.UnlockRead();
					}
				}
			}
			DMibTrace("Performance for CMutualManyRead LockRead+UnlockRead = {0} locks per second\n", ((aint)((fp64(Tests) / Timer.f_GetTime()).f_Get())));
		}
		*/

		CThread Thread;
		Thread.f_Start();
		NMib::NSys::fg_Thread_Sleep(0.5);
		Thread.m_EventWantQuit.f_Signal();
		NMib::NSys::fg_Thread_Sleep(0.5);
		Thread.m_EventWantQuit.f_Signal();
		NMib::NSys::fg_Thread_Sleep(0.5);
		Thread.m_EventWantQuit.f_Signal();
		NMib::NSys::fg_Thread_Sleep(0.5);
		Thread.m_EventWantQuit.f_Signal();
		NMib::NSys::fg_Thread_Sleep(0.5);
		Thread.m_EventWantQuit.f_Signal();
		NMib::NSys::fg_Thread_Sleep(0.5);

		Thread.f_Stop();

		return "";
	}
};

CTestThread *CTestThread::ms_pThis = nullptr;

aint CTestThread::m_IncValue;
aint *CTestThread::m_pIncValue = nullptr;
NMib::NThread::CMutualManyRead CTestThread::m_IncLock;
NMib::NThread::CMutual CTestThread::m_IncLockMutual;
NMib::NThread::CEventAutoReset CTestThread::m_IncEvent;

NMib::NThread::CMutual CTestThread::m_IncDoneLock;
aint CTestThread::m_IncDone;

DMibRuntimeClass(CMalterlibTest, CTestThread);
#endif
