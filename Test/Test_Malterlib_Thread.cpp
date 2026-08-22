// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Test/Performance>
#include <Mib/Time/PerfTimeMeasure>
#include <Mib/File/File>
#include <Mib/Process/ProcessLaunch>

#if 1
#if defined(DPlatformFamily_Windows)
#define DEnableWin32ThreadTest
#endif

#if defined(DEnableWin32ThreadTest)
#include <windows.h>
#include <Fibersapi.h>
#if defined(DEnableWin32ConCrt)
#include <concrt.h>
#endif
using CWindowsCriticalSection = CRITICAL_SECTION;
#endif

#include <thread>
#include <mutex>

#if defined(DPlatformFamily_macOS) || defined(DPlatformFamily_Linux)
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace NMib::NTime;
using namespace NMib::NThread;

#ifndef DCompiler_MSVC
#define DLimitOptimizations asm("")
#else
#define DLimitOptimizations
#endif

enum
{
	ECacheLineSize = 64

};
namespace
{

	class CReadWriteContention
	{
	public:
		aint m_IncValue;
		aint *m_pIncValue;
		NMib::NThread::CEventAutoReset m_IncEvent;

		bool m_bDummy0;
		alignas(ECacheLineSize) NMib::NThread::CMutualManyRead m_IncLock;
		alignas(ECacheLineSize) NMib::NThread::CMutual m_IncLockMutual;
		alignas(ECacheLineSize) NMib::NThread::CMutual m_IncDoneLock;
		bool m_bDummy1;
		NMib::NAtomic::TCAtomic<aint> m_ChangingValue;
		aint m_IncDone;
		aint m_nTests;

		CReadWriteContention()
		{
		}

		class CIncThread : public NMib::NThread::CThread
		{
		public:
			NMib::NStr::CStr f_GetThreadName()
			{
				return "MalterlibCertifier_IncThread";
			}

			CReadWriteContention *m_pTest;

			CIncThread()
			{
				m_pTest = nullptr;
			}


			using NMib::NThread::CThread::f_Start;

			void f_Start(CReadWriteContention *_pTest)
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

					int nTest = m_pTest->m_nTests;
					while (nTest)
					{
						{
							m_pTest->m_IncLock.f_Lock();
							m_pTest->m_ChangingValue = 5544;
#if DMibEnableSafeCheck > 0 && 0
							++(*m_pTest->m_pIncValue);
#else
							++m_pTest->m_IncValue;
#endif
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
		};

		class CReadThread : public NMib::NThread::CThread
		{
		public:
			NMib::NStr::CStr f_GetThreadName()
			{
				return "MalterlibCertifier_ReadThread";
			}

			CReadWriteContention *m_pTest;

			CReadThread()
			{
				m_pTest = nullptr;
				m_nReads = 0;
			}

			using NMib::NThread::CThread::f_Start;

			void f_Start(CReadWriteContention *_pTest)
			{
				m_pTest = _pTest;
				NMib::NThread::CThread::f_Start();
			}

			~CReadThread()
			{
				f_Stop();

			}

			NMib::NAtomic::TCAtomic<umint> m_nReads;
			NMib::NAtomic::TCAtomic<smint> m_bStop;

			aint f_Main()
			{
				bool bWantStop = false;
				while (1)
				{
					m_EventWantQuit.f_Wait();
					if (f_GetState() == NMib::NThread::EThreadState_EventWantQuit)
						break;
					m_nReads = 0;
//					DMibTestSuite("ReadThread")
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
#if DMibEnableSafeCheck > 0 && 0
								DMibFastCheck(!m_pTest->m_IncLock.f_IsLocked());
								for (umint i = 0; i < 1000; ++i)
									NMib::fg_Volatile(m_pTest->m_pIncValue) = nullptr;
								NMib::fg_Volatile(m_pTest->m_pIncValue) = &m_pTest->m_IncValue;
								DMibFastCheck(!m_pTest->m_IncLock.f_IsLocked());
#endif
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
		};

		enum
		{
			EIncThreads = 4,
			EReadThreads = 4,
		};
		void f_DoTest()
		{
#if !defined(DMibDebug)
#if 0
			try
			{
				NMib::NSys::fg_Process_SetPriority(NMib::EExecutionPriority_Normal);
			}
			catch (NMib::NException::CException const &_Exception)
			{
				// You are not allowed to do this on unix unless you are runnig as root
			}
#endif
			try
			{
				NMib::NSys::fg_Thread_SetPriority(NMib::NSys::fg_Thread_GetCurrent(), NMib::EExecutionPriority_Normal);
			}
			catch (NMib::NException::CException const &)
			{
			}
#endif
			NMib::NThread::CMutual Lock;
			NMib::NTime::CPrefCyclesTimeMeasureMin Timer;

			{
				CIncThread IncThreads[EIncThreads];
				CReadThread ReadThreads[EReadThreads];
				for (umint i = 0; i < EIncThreads; ++i)
					IncThreads[i].f_Start(this);
				for (umint i = 0; i < EReadThreads; ++i)
					ReadThreads[i].f_Start(this);

				NMib::NSys::fg_Thread_Sleep(1.0);

				Timer.f_Reset();
				umint nReads = 0;
				m_ChangingValue = 1112;
				m_nTests = 25000 / EIncThreads;
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
							for (umint i = 0; i < EReadThreads; ++i)
							{
								ReadThreads[i].m_EventWantQuit.f_Signal();
							}
							for (umint i = 0; i < EIncThreads; ++i)
								IncThreads[i].m_EventWantQuit.f_Signal();

							while (1)
							{
								m_IncEvent.f_Wait();
								DMibLock(m_IncDoneLock);
								if (m_IncDone == (EIncThreads))
									break;
							}
							for (umint i = 0; i < EReadThreads; ++i)
							{
								nReads += ReadThreads[i].m_nReads.f_Load(NMib::NAtomic::gc_MemoryOrder_Relaxed);
								ReadThreads[i].m_bStop.f_Exchange(1);
								ReadThreads[i].m_EventWantQuit.f_Signal();
							}
							Timer.f_Stop();
							while (1)
							{
								bool bAllStopped = true;
								for (umint i = 0; i < EReadThreads; ++i)
								{
									if (ReadThreads[i].m_bStop.f_Load())
										bAllStopped = false;
								}
								if (bAllStopped)
									break;
								NMib::NSys::fg_Thread_Sleep(0.01f);
							}
						}
					}
				}

				DMibTest(DMibExpr(m_IncValue) == DMibExpr(m_nTests*EIncThreads));

				NMib::NTime::CPrefCyclesTimeMeasureMin TimerRead = Timer;
				TimerRead /= nReads;
				Timer /= m_IncValue;

				if (NMib::NTest::fg_GroupActive("Performance"))
					DMibTest(DMibExpr(Timer) / DMibExpr(TimerRead) >= DMibExpr(1.0));
			}

#if !defined(DMibDebug)
#if 0
			try
			{
				NMib::NSys::fg_Process_SetPriority(NMib::EExecutionPriority_High);
			}
			catch (NMib::NException::CException const &_Exception)
			{
				// You are not allowed to do this on unix unless you are runnig as root
			}
#endif
			try
			{
				NMib::NSys::fg_Thread_SetPriority(NMib::NSys::fg_Thread_GetCurrent(), NMib::EExecutionPriority_Highest);
			}
			catch (NMib::NException::CException const &)
			{
			}
#endif
		}
	};

#ifdef DPlatformFamily_Windows
		__declspec(thread) umint g_ThreadLocal = 0;
		__declspec(thread) umint g_ThreadLocalArray[16] = {0};
#else
		__thread umint __attribute__((tls_model("local-exec"))) g_ThreadLocal = 0;
		__thread umint __attribute__((tls_model("local-exec"))) g_ThreadLocalArray[16] = {0};
#endif
	umint g_LocalArrayIndex = 11;
#	if defined(DEnableWin32ThreadTest)
		DWORD g_TlsLocal;
		LPVOID (WINAPI *pTlsGetValue)(DWORD dwTlsIndex);
		DWORD g_FlsLocal;
		//LPVOID (WINAPI *pFlsGetValue)(DWORD dwFlsIndex);
#	endif

	TCThreadLocal<umint, NMib::NMemory::CAllocator_Heap, EThreadLocalFlag_AlwaysCreated> g_ThreadLocalMalterlib;
	TCThreadLocal<umint, NMib::NMemory::CAllocator_Heap, EThreadLocalFlag(uint32(EThreadLocalFlag_AlwaysCreated) | uint32(EThreadLocalFlag_FastThreadLocal))> g_ThreadLocalMalterlibFast;
	umint g_ThreadLocalFastIndex;
	umint g_ThreadLocalIndex;

	class CThread_Tests : public NMib::NTest::CTest
	{
	public:

		static void fs_IncNative()
		{
			++g_ThreadLocal;
		}
		static void fs_IncNativeArray()
		{
			++g_ThreadLocalArray[g_LocalArrayIndex];
		}

#		if defined(DEnableWin32ThreadTest)
			static void fs_IncTls()
			{
				umint *pTls = (umint *)pTlsGetValue(g_TlsLocal);
				++(*pTls);
			}
			static void fs_IncFls()
			{
				umint *pTls = (umint *)FlsGetValue(g_FlsLocal);
				++(*pTls);
			}
#		endif

		static void fs_IncMalterlib()
		{
			++(*g_ThreadLocalMalterlib);
		}

		static void fs_IncMalterlibFast()
		{
			++(*g_ThreadLocalMalterlibFast);
		}


		static umint fs_CurrentThreadNative()
		{
#ifdef DPlatformFamily_Windows
			return GetCurrentThreadId();
#else
			return (umint)pthread_self();
#endif
		}

		static umint fs_CurrentThreadMalterlib()
		{
//			return __readgsdword(0x48);
//000000013F93B004  mov         rax,qword ptr gs:[30h]
//000000013F93B00D  mov         eax,dword ptr [rax+48h]

			return NMib::NSys::fg_Thread_GetCurrentUID();
		}

		static void fs_IncMalterlibStorageFast()
		{
			umint *pTls = (umint *)NMib::NSys::fg_Thread_GetLocalFast(g_ThreadLocalFastIndex);
			++(*pTls);
		}

		static void fs_IncMalterlibStorage()
		{
			umint *pTls = (umint *)NMib::NSys::fg_Thread_GetLocal(g_ThreadLocalIndex);
			++(*pTls);
		}

		template <typename t_CFunctor>
		static void fs_CallFunctor(t_CFunctor _Functor)
		{
			_Functor();
		}

		template <typename t_CFunctor>
		static void fs_CallFunctor2(t_CFunctor _Functor)
		{
			_Functor();
			_Functor();
		}

		class CTemp
		{
		public:
			CTemp()
			{
				m_Value = 0;
			}
			int32 m_Value;
		};

		class CTemp35
		{
		public:
			CTemp35()
			{
				m_Value = 35;
			}
			int32 m_Value;
		};

		void f_DoTests()
		{

#			if defined(DEnableWin32ThreadTest)
				HMODULE Kernel32 = GetModuleHandle(str_utf16("kernelbase.dll"));
				if (!Kernel32)
					Kernel32 = GetModuleHandle(str_utf16("kernel32.dll"));
				(FARPROC &)pTlsGetValue = GetProcAddress(Kernel32, "TlsGetValue");
				//(FARPROC &)pFlsGetValue = GetProcAddress(Kernel32, "FlsGetValue");
#			endif

			DMibTestSuite("TLS Performance")
			{
				g_ThreadLocal = 0;
#				if defined(DEnableWin32ThreadTest)
					g_TlsLocal = TlsAlloc();
					g_FlsLocal = FlsAlloc(nullptr);
					TlsSetValue(g_TlsLocal, DMibNew umint);
					FlsSetValue(g_FlsLocal, DMibNew umint);
					*((umint *)TlsGetValue(g_TlsLocal)) = 0;
					*((umint *)FlsGetValue(g_FlsLocal)) = 0;
#				endif

				g_ThreadLocalFastIndex = NMib::NSys::fg_Thread_AllocLocalFast();
				NMib::NSys::fg_Thread_SetLocalFast(g_ThreadLocalFastIndex, DMibNew umint);
				*((umint *)NMib::NSys::fg_Thread_GetLocalFast(g_ThreadLocalFastIndex)) = 0;

				g_ThreadLocalIndex = NMib::NSys::fg_Thread_AllocLocal();
				NMib::NSys::fg_Thread_SetLocal(g_ThreadLocalIndex, DMibNew umint);
				*((umint *)NMib::NSys::fg_Thread_GetLocal(g_ThreadLocalIndex)) = 0;

				*g_ThreadLocalMalterlib = 0;
				*g_ThreadLocalMalterlibFast = 0;
				const static umint nTests = 33;
				const static umint nLoops = 100000;

				g_LocalArrayIndex = NMib::NStr::CStr((NMib::NStr::CStr::CFormat("{}") << (12))).f_ToInt();

				CPrefCyclesTimeMeasureMin NativeTime;
				CPrefCyclesTimeMeasureMin NativeArrayTime;
#				if defined(DEnableWin32ThreadTest)
					CPrefCyclesTimeMeasureMin TlsTime;
					CPrefCyclesTimeMeasureMin FlsTime;
#				endif

				CPrefCyclesTimeMeasureMin MalterlibTime;
				CPrefCyclesTimeMeasureMin MalterlibFastTime;
				CPrefCyclesTimeMeasureMin MalterlibStorageFastTime;
				CPrefCyclesTimeMeasureMin MalterlibStorageTime;

				auto Native = [&] ()
				{
					NativeTime.f_Start();
					[]() inline_never
						{
							for (umint i = 0; i < nLoops; ++i)
							{
								CThread_Tests::fs_IncNative();
								DLimitOptimizations;
							}
						}()
					;
					NativeTime.f_Stop();
				};
				auto NativeArray = [&] ()
				{
					NativeArrayTime.f_Start();
					[]() inline_never
						{
							for (umint i = 0; i < nLoops; ++i)
							{
								CThread_Tests::fs_IncNativeArray();
								DLimitOptimizations;
							}
						}()
					;
					NativeArrayTime.f_Stop();
				};
#				if defined(DEnableWin32ThreadTest)
					auto Tls = [&] ()
					{
						TlsTime.f_Start();
						for (umint i = 0; i < nLoops; ++i)
						{
							CThread_Tests::fs_IncTls();
							DLimitOptimizations;
						}
						TlsTime.f_Stop();
					};
					auto Fls = [&] ()
					{
						FlsTime.f_Start();
						for (umint i = 0; i < nLoops; ++i)
						{
							CThread_Tests::fs_IncFls();
							DLimitOptimizations;
						}
						FlsTime.f_Stop();
					};
#				endif

				auto Malterlib = [&] ()
				{
					MalterlibTime.f_Start();
					[]() inline_never
						{
							for (umint i = 0; i < nLoops; ++i)
							{
								CThread_Tests::fs_IncMalterlib();
								DLimitOptimizations;
							}
						}()
					;
					MalterlibTime.f_Stop();
				};

				auto MalterlibFast = [&] ()
				{
					MalterlibFastTime.f_Start();
					[]() inline_never
						{
							for (umint i = 0; i < nLoops; ++i)
							{
								CThread_Tests::fs_IncMalterlibFast();
								DLimitOptimizations;
							}
						}()
					;
					MalterlibFastTime.f_Stop();
				};

				auto MalterlibStorageFast = [&] ()
				{
					MalterlibStorageFastTime.f_Start();
					[]() inline_never
						{
							for (umint i = 0; i < nLoops; ++i)
							{
								CThread_Tests::fs_IncMalterlibStorageFast();
								DLimitOptimizations;
							}
						}()
					;
					MalterlibStorageFastTime.f_Stop();
				};

				auto MalterlibStorage = [&] ()
				{
					MalterlibStorageTime.f_Start();
					[]() inline_never
						{
							for (umint i = 0; i < nLoops; ++i)
							{
								CThread_Tests::fs_IncMalterlibStorage();
								DLimitOptimizations;
							}
						}()
					;
					MalterlibStorageTime.f_Stop();
				};

				for (umint i = 0; i < nTests; ++i)
				{
					fs_CallFunctor(Malterlib);
					fs_CallFunctor2(Malterlib);
				}
				for (umint i = 0; i < nTests; ++i)
				{
					fs_CallFunctor(MalterlibFast);
					fs_CallFunctor2(MalterlibFast);
				}
				for (umint i = 0; i < nTests; ++i)
				{
					fs_CallFunctor(MalterlibStorageFast);
					fs_CallFunctor2(MalterlibStorageFast);
				}
				for (umint i = 0; i < nTests; ++i)
				{
					fs_CallFunctor(MalterlibStorage);
					fs_CallFunctor2(MalterlibStorage);
				}
				for (umint i = 0; i < nTests; ++i)
				{
					fs_CallFunctor(Native);
					fs_CallFunctor2(Native);
				}
				for (umint i = 0; i < nTests; ++i)
				{
					fs_CallFunctor(NativeArray);
					fs_CallFunctor2(NativeArray);
				}
#				if defined(DEnableWin32ThreadTest)
					for (umint i = 0; i < nTests; ++i)
					{
						fs_CallFunctor(Tls);
						fs_CallFunctor2(Tls);
					}
					for (umint i = 0; i < nTests; ++i)
					{
						fs_CallFunctor(Fls);
						fs_CallFunctor2(Fls);
					}
#				endif


					NativeTime /= nLoops;
					NativeArrayTime /= nLoops;
#				if defined(DEnableWin32ThreadTest)
					TlsTime /= nLoops;
					FlsTime /= nLoops;
#				endif

				MalterlibTime /= nLoops;
				MalterlibFastTime /= nLoops;
				MalterlibStorageFastTime /= nLoops;
				MalterlibStorageTime /= nLoops;

				DMibTest(DMibExpr(g_ThreadLocal) == DMibExpr(*g_ThreadLocalMalterlib));
				DMibTest(DMibExpr(g_ThreadLocal) == DMibExpr(*g_ThreadLocalMalterlibFast));
				DMibTest(DMibExpr(g_ThreadLocal) == DMibExpr(g_ThreadLocalArray[g_LocalArrayIndex]));
#				if defined(DEnableWin32ThreadTest)
					DMibTest(DMibExpr(g_ThreadLocal) == DMibExpr(*((umint *)TlsGetValue(g_TlsLocal))));
					DMibTest(DMibExpr(g_ThreadLocal) == DMibExpr(*((umint *)FlsGetValue(g_FlsLocal))));
					DMibTest(DMibExpr(g_ThreadLocal) == DMibExpr(*((umint *)NMib::NSys::fg_Thread_GetLocalFast(g_ThreadLocalFastIndex))));
					DMibTest(DMibExpr(g_ThreadLocal) == DMibExpr(*((umint *)NMib::NSys::fg_Thread_GetLocal(g_ThreadLocalIndex))));
#				else
					DMibTest(DMibExpr(*g_ThreadLocalMalterlib) == DMibExpr(*g_ThreadLocalMalterlibFast));
#				endif

				if (NMib::NTest::fg_GroupActive("Performance"))
				{
					DMibTest(DMibExpr(MalterlibStorageFastTime) / DMibExpr(MalterlibFastTime) >= DMibExpr(0.75));
					DMibTest(DMibExpr(MalterlibStorageTime) / DMibExpr(MalterlibTime) >= DMibExpr(0.75));
					DMibTest(DMibExpr(NativeTime) / DMibExpr(MalterlibFastTime) >= DMibExpr(0.75));
					DMibTest(DMibExpr(NativeArrayTime) / DMibExpr(MalterlibFastTime) >= DMibExpr(0.75));
#					if defined(DEnableWin32ThreadTest)
						DMibTest(DMibExpr(TlsTime) / DMibExpr(MalterlibTime) >= DMibExpr(0.75));
						DMibTest(DMibExpr(FlsTime) / DMibExpr(MalterlibTime) >= DMibExpr(0.75));
#					endif
				}

#				if defined(DEnableWin32ThreadTest)
					delete ((umint *)TlsGetValue(g_TlsLocal));
					TlsFree(g_TlsLocal);
					delete ((umint *)FlsGetValue(g_FlsLocal));
					FlsFree(g_FlsLocal);
#				endif

				delete ((umint *)NMib::NSys::fg_Thread_GetLocalFast(g_ThreadLocalFastIndex));
				NMib::NSys::fg_Thread_SetLocalFast(g_ThreadLocalFastIndex, nullptr);
				NMib::NSys::fg_Thread_FreeLocalFast(g_ThreadLocalFastIndex);
				delete ((umint *)NMib::NSys::fg_Thread_GetLocal(g_ThreadLocalIndex));
				NMib::NSys::fg_Thread_SetLocal(g_ThreadLocalIndex, nullptr);
				NMib::NSys::fg_Thread_FreeLocal(g_ThreadLocalIndex);
			};

			DMibTestSuite("Current Thread Performance")
			{
				const static umint nTests = 100;
				const static umint nLoops = 1000000;
				CPrefCyclesTimeMeasureMin NativeTime;
				CPrefCyclesTimeMeasureMin MalterlibTime;

				volatile umint ThreadResultMalterlib = 0;
				volatile umint ThreadResultNative = 0;

				for (umint i = 0; i < nTests; ++i)
				{
					NativeTime.f_Start();
					ThreadResultNative = []() inline_never
						{
							umint Results = 0;
							for (umint i = 0; i < nLoops; ++i)
							{
								Results += fs_CurrentThreadNative();
								DLimitOptimizations;
							}
							return Results;
						}()
					;
					NativeTime.f_Stop();
				}

				for (umint i = 0; i < nTests; ++i)
				{
					MalterlibTime.f_Start();
					ThreadResultMalterlib = []() inline_never
						{
							umint Results = 0;
							for (umint i = 0; i < nLoops; ++i)
							{
								Results += fs_CurrentThreadMalterlib();
								DLimitOptimizations;
							}
							return Results;
						}()
					;
					MalterlibTime.f_Stop();
				}

				NativeTime /= nLoops;
				MalterlibTime /= nLoops;

				umint ThreadResultMalterlib1 = ThreadResultMalterlib;
				umint ThreadResultNative1 = ThreadResultNative;
				DMibTest(DMibExpr(ThreadResultMalterlib1) == DMibExpr(ThreadResultNative1));
				if (NMib::NTest::fg_GroupActive("Performance"))
					DMibTest(DMibExpr(NativeTime) / DMibExpr(MalterlibTime) >= DMibExpr(1.0));

			};
		#if !defined(DPlatformFamily_Linux) || defined(DMibConfig_LinuxPThreadMonitoring)
			DMibTestSuite("Enumerate other threads")
			{
				NMib::NAtomic::TCAtomic<umint> ThreadID{0};
				CEvent ThreadReady;
				CEvent FinishThread;
				auto pThread = CThreadObject::fs_StartThread
					(
						[&] (CThreadObject *) -> aint
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
		#endif
			DMibTestSuite("Lock Performance")
			{
				const static umint nTests = 101;
				const static umint nLoops = 100000;

				CTestPerformance PerfTestMutual(0.75, false);
				CTestPerformance PerfTestMutualRecursive(0.75, false);

				CTestPerformance PerfTestSimpleMutual(0.75, false);

				CTestPerformance PerfTestReadWriteRead(0.75, false);
				CTestPerformance PerfTestReadWriteWrite(0.75, false);

#				if defined(DEnableWin32ThreadTest)
				{
					CWindowsCriticalSection Crit;
					InitializeCriticalSection((CRITICAL_SECTION *)&Crit);

					{
						CTestPerformanceMeasure Measure("WinCriticalSection");
						for (umint i = 0; i < nTests; ++i)
						{
							Measure.f_Start();
							[&]() inline_never
								{
									for (umint i = 0; i < nLoops; ++i)
									{
										EnterCriticalSection((CRITICAL_SECTION *)&Crit);
										LeaveCriticalSection((CRITICAL_SECTION *)&Crit);
									}
								}()
							;
							Measure.f_Stop(nLoops);
						}

						PerfTestMutual.f_AddReference(Measure);
					}
					{
						CTestPerformanceMeasure Measure("WinCriticalSectionRecursive");
						for (umint i = 0; i < nTests; ++i)
						{
							EnterCriticalSection((CRITICAL_SECTION *)&Crit);
							Measure.f_Start();
							[&]() inline_never
								{
									for (umint i = 0; i < nLoops; ++i)
									{
										EnterCriticalSection((CRITICAL_SECTION *)&Crit);
										LeaveCriticalSection((CRITICAL_SECTION *)&Crit);
									}
								}()
							;
							Measure.f_Stop(nLoops);
							LeaveCriticalSection((CRITICAL_SECTION *)&Crit);
						}
						PerfTestMutualRecursive.f_AddReference(Measure);
					}
					DeleteCriticalSection((CRITICAL_SECTION *)&Crit);
				}

#				if defined(DEnableWin32ConCrt)
				{
					{
						Concurrency::reader_writer_lock Lock;
						{
							Lock.lock();
							Lock.unlock();
						}
						{
							CTestPerformanceMeasure Measure("Win32ConCrtReadWrite");
							for (umint i = 0; i < nTests; ++i)
							{
								Measure.f_Start();
								[&]() inline_never
									{
										for (umint i = 0; i < nLoops; ++i)
										{
											Lock.lock();
											Lock.unlock();
										}
									}()
								;
								Measure.f_Stop(nLoops);
							}
							PerfTestReadWriteWrite.f_AddReference(Measure);
						}
					}
					{
						Concurrency::reader_writer_lock Lock;
						{
							CTestPerformanceMeasure Measure("Win32ConCrtReadWrite");
							for (umint i = 0; i < nTests; ++i)
							{
								Measure.f_Start();
								[&]() inline_never
									{
										for (umint i = 0; i < nLoops; ++i)
										{
											Lock.lock_read();
											Lock.unlock();
										}
									}()
								;
								Measure.f_Stop(nLoops);
							}
							PerfTestReadWriteRead.f_AddReference(Measure);
						}
					}
				}
#				endif

#				endif
				{
					{
						CTestPerformanceMeasure Measure("CMutual");
						for (umint i = 0; i < nTests; ++i)
						{
							Measure.f_Start();
							[]() inline_never
								{
									NMib::NThread::CMutual Lock;
									for (umint i = 0; i < nLoops; ++i)
										DMibLock(Lock);
								}()
							;
							Measure.f_Stop(nLoops);
						}
						PerfTestMutual.f_Add(Measure);
						PerfTestSimpleMutual.f_Add(Measure);
					}
					{
						CTestPerformanceMeasure Measure("CMutual");
						for (umint i = 0; i < nTests; ++i)
						{
							NMib::NThread::CMutual Lock;
							DMibLock(Lock);
							Measure.f_Start();
							[&]() inline_never
								{
									for (umint i = 0; i < nLoops; ++i)
										DMibLock(Lock);
								}()
							;
							Measure.f_Stop(nLoops);
						}
						PerfTestMutualRecursive.f_Add(Measure);
					}
				}
				{
					{
						CTestPerformanceMeasure Measure("CMutualSpin");
						for (umint i = 0; i < nTests; ++i)
						{
							NMib::NThread::CMutualSpin Lock;
							Measure.f_Start();
							[&]() inline_never
								{
									for (umint i = 0; i < nLoops; ++i)
										DMibLock(Lock);
								}()
							;
							Measure.f_Stop(nLoops);
						}
						PerfTestMutual.f_Add(Measure);
						PerfTestSimpleMutual.f_Add(Measure);
					}
					{
						CTestPerformanceMeasure Measure("CMutualSpin");
						for (umint i = 0; i < nTests; ++i)
						{
							NMib::NThread::CMutualSpin Lock;
							DMibLock(Lock);
							Measure.f_Start();
							[&]() inline_never
								{
									for (umint i = 0; i < nLoops; ++i)
										DMibLock(Lock);
								}()
							;
							Measure.f_Stop(nLoops);
						}
						PerfTestMutualRecursive.f_Add(Measure);
					}
				}
				{
					{
						CTestPerformanceMeasure Measure("recursive_mutex");
						for (umint i = 0; i < nTests; ++i)
						{
							std::recursive_mutex Lock;
							Measure.f_Start();
							[&]() inline_never
								{
									for (umint i = 0; i < nLoops; ++i)
									{
										Lock.lock();
										Lock.unlock();
									}
								}()
							;
							Measure.f_Stop(nLoops);
						}
						PerfTestMutual.f_AddReference(Measure);
					}
					{
						CTestPerformanceMeasure Measure("recursive_mutex");
						for (umint i = 0; i < nTests; ++i)
						{
							std::recursive_mutex Lock;
							Lock.lock();
							Measure.f_Start();
							[&]() inline_never
								{
									for (umint i = 0; i < nLoops; ++i)
									{
										Lock.lock();
										Lock.unlock();
									}
								}()
							;
							Lock.unlock();
							Measure.f_Stop(nLoops);
						}
						PerfTestMutualRecursive.f_AddReference(Measure);
					}
				}
				{
					CTestPerformanceMeasure Measure("CMutualSimple");
					for (umint i = 0; i < nTests; ++i)
					{
						NMib::NThread::CMutualSimple Lock;
						Measure.f_Start();
						[&]() inline_never
							{
								for (umint i = 0; i < nLoops; ++i)
									DMibLock(Lock);
							}()
						;
						Measure.f_Stop(nLoops);
					}
					PerfTestSimpleMutual.f_Add(Measure);
				}
				{
					CTestPerformanceMeasure Measure("CLowLevelLock");
					for (umint i = 0; i < nTests; ++i)
					{
						NMib::NThread::CLowLevelLock Lock;
						Measure.f_Start();
						[&]() inline_never
							{
								for (umint i = 0; i < nLoops; ++i)
									DMibLock(Lock);
							}()
						;
						Measure.f_Stop(nLoops);
					}
					PerfTestSimpleMutual.f_Add(Measure);
				}
				{
					CTestPerformanceMeasure Measure("mutex");
					for (umint i = 0; i < nTests; ++i)
					{
						std::mutex Lock;
						Measure.f_Start();
						[&]() inline_never
							{
								for (umint i = 0; i < nLoops; ++i)
								{
									Lock.lock();
									Lock.unlock();
								}
							}()
						;
						Measure.f_Stop(nLoops);
					}
					PerfTestSimpleMutual.f_AddReference(Measure);
				}
				{
					CTestPerformanceMeasure Measure("CMutualManyRead");
					for (umint i = 0; i < nTests; ++i)
					{
						NMib::NThread::CMutualManyRead Lock;
						{
							DMibLock(Lock);
						}
						Measure.f_Start();
						[&]() inline_never
							{
								for (umint i = 0; i < nLoops; ++i)
									DMibLock(Lock);
							}()
						;
						Measure.f_Stop(nLoops);
					}
					PerfTestReadWriteWrite.f_Add(Measure);
				}
				{
					CTestPerformanceMeasure Measure("CMutualManyRead");
					for (umint i = 0; i < nTests; ++i)
					{
						NMib::NThread::CMutualManyRead Lock;
						Measure.f_Start();
						[&]() inline_never
							{
								for (umint i = 0; i < nLoops; ++i)
									DMibLockRead(Lock);
							}()
						;
						Measure.f_Stop(nLoops);
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
				for (umint i = 0; i < 100; ++i)
				{
					NMib::NThread::CEvent Event;
					NMib::NAtomic::TCAtomic<uint32> nDone{0};

					NMib::NStorage::TCUniquePointer<CThreadObject> pWaiter = CThreadObject::fs_StartThread
						(
							[&] (CThreadObject *_pThread) -> aint
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
					while (!(Event.m_State.f_Load(NMib::NAtomic::gc_MemoryOrder_Relaxed) & NMib::NThread::CEventAggregate::mcp_FlagWaiters))
						NMib::NSys::fg_Thread_Yield();

					Event.f_SetSignaled();
					Event.f_ResetSignaled();

					pWaiter->f_Stop();
					pWaiter.f_Clear();

					DMibTest(DMibExpr(nDone.f_Load()) == DMibExpr(1u)) (ETestFlag_Aggregated);
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
					for (umint i = 0; i < 100; ++i)
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
							[&] (CThreadObject *_pThread) -> aint
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

					enum { EnWaiters = 4 };
					NMib::NStorage::TCUniquePointer<CThreadObject> pWaiters[EnWaiters];
					for (umint i = 0; i < EnWaiters; ++i)
					{
						pWaiters[i] = CThreadObject::fs_StartThread
							(
								[&] (CThreadObject *_pThread) -> aint
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

					for (umint i = 0; i < EnWaiters; ++i)
					{
						pWaiters[i]->f_Stop();
						pWaiters[i].f_Clear();
					}

					DMibTest(DMibExpr(nWoken.f_Load()) == DMibExpr((uint32)EnWaiters));
				}

				{
					// A timed wait that is woken before the timeout must not report a timeout
					Word.f_Store(0);

					NMib::NStorage::TCUniquePointer<CThreadObject> pWaker = CThreadObject::fs_StartThread
						(
							[&] (CThreadObject *_pThread) -> aint
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

#if DMibConfig_Tests_Enable
			DMibTestCategory("Thread local")
			{
				DMibTestSuite("Set another thread's local storage from dynamic library")
				{
					NMib::NStr::CStr DllPath = NMib::NStr::CStr("Test_Malterlib_Helper_Thread") + NMib::NFile::CFile::fs_GetDllExtension();
					DllPath = NMib::NFile::CFile::fs_AppendPath(NMib::NFile::CFile::fs_GetProgramDirectory(), DllPath);

					uint32 (calling_convention_c *pTestFunction)() = nullptr;
					NMib::NThread::CEvent ExistingThreadReady;
					NMib::NThread::CEvent RunExistingThreadTest;
					NMib::NAtomic::TCAtomic<uint32> ExistingThreadResult{NMib::TCLimitsInt<uint32>::mc_Max};
					auto pExistingThread = NMib::NThread::CThreadObject::fs_StartThread
						(
							[&](NMib::NThread::CThreadObject *) -> aint
							{
								ExistingThreadReady.f_SetSignaled();
								RunExistingThreadTest.f_Wait();
								if (pTestFunction)
									ExistingThreadResult = pTestFunction();
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

					(void * &)pTestFunction = NMib::NSys::fg_GetLibrarySymbol(pDll, "fg_TestSetAnotherThreadLocal");
					DMibTest(DMibExpr(pTestFunction))(ETest_FailAndStop);
					DMibTest(DMibExpr(pTestFunction()) == DMibExpr(uint32(0)));

					RunExistingThreadTest.f_SetSignaled();
					pExistingThread->f_Stop();
					pExistingThread.f_Clear();
					DMibTest(DMibExpr(ExistingThreadResult.f_Load()) == DMibExpr(uint32(0)));

				#if defined(DPlatformFamily_Windows) || defined(DPlatformFamily_macOS) || !defined(DMibAssumeMalterlibHost)
					{
						NMib::NStr::CStr ProgramDirectory = NMib::NFile::CFile::fs_GetProgramDirectory();
						NMib::NStr::CStr LauncherPath = NMib::NFile::CFile::fs_AppendPath
							(
								ProgramDirectory
								, NMib::NStr::CStr("Test_Malterlib_Helper_Thread_NonMalterlibHost") + NMib::NFile::CFile::mc_ExecutableExtension
							)
						;
						NMib::NContainer::TCVector<NMib::NStr::CStr> LauncherParameters;
						LauncherParameters.f_Insert(DllPath);
						NMib::NAtomic::TCAtomic<uint32> ExitCode{NMib::TCLimitsInt<uint32>::mc_Max};
						NMib::NAtomic::TCAtomic<bool> bLaunchFailed{false};
						NMib::NProcess::CProcessLaunchParams LaunchParams = NMib::NProcess::CProcessLaunchParams::fs_LaunchExecutable
							(
								LauncherPath
								, LauncherParameters
								, ProgramDirectory
								, [&](NMib::NProcess::CProcessLaunchStateChangeVariant const &_State, fp64)
								{
									switch (_State.f_GetTypeID())
									{
									case NMib::NProcess::EProcessLaunchState_LaunchFailed:
										bLaunchFailed = true;
										break;
									case NMib::NProcess::EProcessLaunchState_Exited:
										ExitCode = _State.f_Get<NMib::NProcess::EProcessLaunchState_Exited>();
										break;
									default:
										break;
									}
								}
							)
						;
						LaunchParams.m_bShowLaunched = false;
						{
							NMib::NProcess::CProcessLaunch Launcher
								(
									LaunchParams
									, NMib::NProcess::EProcessLaunchCloseFlag_BlockOnExit
								)
							;
						}

						DMibTest(DMibExpr(bLaunchFailed.f_Load()) == DMibExpr(false));
						DMibTest(DMibExpr(ExitCode.f_Load()) == DMibExpr(uint32(0)));
					}
				#endif

				#if defined(DPlatformFamily_macOS) || defined(DPlatformFamily_Linux)
					pid_t ProcessID = fork();
					DMibTest(DMibExpr(ProcessID >= 0))(ETest_FailAndStop);
					if (!ProcessID)
						_exit((int)pTestFunction());

					int Status = 0;
					DMibTest(DMibExpr(waitpid(ProcessID, &Status, 0)) == DMibExpr(ProcessID));
					DMibTest(DMibExpr(WIFEXITED(Status)) == DMibExpr(true));
					DMibTest(DMibExpr(WEXITSTATUS(Status)) == DMibExpr(0));
				#endif

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
							[&] (CThreadObject *) -> aint
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
					for (umint i = 0; i < 10; ++i)
					{
						NMib::NStorage::TCAggregate<TCThreadLocal<CTemp35, NMib::NMemory::CAllocator_Heap, EThreadLocalFlag_AlwaysCreated>> ThreadLocal = {DAggregateInit};
						NMib::NThread::CEvent Event;
						NMib::NThread::CEvent EventDone;
						NMib::NStorage::TCUniquePointer<CThreadObject> pThread = CThreadObject::fs_StartThread
							(
								[&] (CThreadObject *_pThread) -> aint
								{
									_pThread->m_EventWantQuit.f_Wait();
									int32 ValueInThread = (*ThreadLocal)->m_Value;
									DMibTest(DMibExpr(ValueInThread) == DMibExpr(35)) (ETestFlag_Aggregated);
									EventDone.f_SetSignaled();
									Event.f_Wait();
									return 0;
								}
								, "Test thread local thread"
							)
						;
						if (i > 5)
							NMib::NSys::fg_Thread_Sleep(0.050f);
						(*ThreadLocal)->m_Value = 36;
						pThread->m_EventWantQuit.f_Signal();
						EventDone.f_Wait();
						DMibTest(DMibExpr((*ThreadLocal)->m_Value) == DMibExpr(36)) (ETestFlag_Aggregated);
						if (i & 1)
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
					for (umint i = 0; i < 10; ++i)
					{
						NMib::NStorage::TCAggregate<TCThreadLocal<CTemp, NMib::NMemory::CAllocator_Heap, EThreadLocalFlag_Inherit>> ThreadLocal = {DAggregateInit};

						NMib::NThread::CEvent Event;
						NMib::NThread::CEvent EventDone;
						(*ThreadLocal)->m_Value = 36;

						DMibTest(DMibExpr((*ThreadLocal)->m_Value) == DMibExpr(36)) (ETestFlag_Aggregated);
						NMib::NStorage::TCUniquePointer<CThreadObject> pThread = CThreadObject::fs_StartThread
							(
								[&] (CThreadObject *_pThread) -> aint
								{
									_pThread->m_EventWantQuit.f_Wait();
									int32 ValueInThread = (*ThreadLocal)->m_Value;
									DMibTest(DMibExpr(ValueInThread) == DMibExpr(36)) (ETestFlag_Aggregated);
									EventDone.f_SetSignaled();
									Event.f_Wait();
									return 0;
								}
								, "Test thread local thread"
							)
						;
						if (i > 5)
							NMib::NSys::fg_Thread_Sleep(0.050f);
						pThread->m_EventWantQuit.f_Signal();
						EventDone.f_Wait();
						if (i & 1)
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
					for (umint i = 0; i < 10; ++i)
					{
						NMib::NStorage::TCAggregate
							<
								TCThreadLocal<CTemp, NMib::NMemory::CAllocator_Heap, EThreadLocalFlag(uint32(EThreadLocalFlag_Inherit) | uint32(EThreadLocalFlag_AlwaysCreated))>
							> ThreadLocal = {DAggregateInit}
						;
						NMib::NThread::CEvent Event;
						NMib::NThread::CEvent EventDone;
						NMib::NStorage::TCUniquePointer<CThreadObject> pThread0 = CThreadObject::fs_StartThread
							(
								[&] (CThreadObject *_pThread) -> aint
								{
									_pThread->m_EventWantQuit.f_Wait();
									int32 ValueInThread0 = (*ThreadLocal)->m_Value;
									DMibTest(DMibExpr(ValueInThread0) == DMibExpr(36) || DMibExpr(ValueInThread0) == DMibExpr(0)) (ETestFlag_Aggregated);
									return 0;
								}
								, "Test thread local thread 0"
							)
						;
						if (i > 5)
							NMib::NSys::fg_Thread_Sleep(0.050f);
						(*ThreadLocal)->m_Value = 36;
						DMibTest(DMibExpr((*ThreadLocal)->m_Value) == DMibExpr(36)) (ETestFlag_Aggregated);
						NMib::NStorage::TCUniquePointer<CThreadObject> pThread1 = CThreadObject::fs_StartThread
							(
								[&] (CThreadObject *_pThread) -> aint
								{
									_pThread->m_EventWantQuit.f_Wait();
									int32 ValueInThread1 = (*ThreadLocal)->m_Value;
									DMibTest(DMibExpr(ValueInThread1) == DMibExpr(36)) (ETestFlag_Aggregated);
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
						if (i & 1)
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
#endif

		}
	};

	DMibTestRegister(CThread_Tests, Malterlib::Thread);
}


#else


#if defined(DPlatformFamily_Windows)
#define DEnableWin32ThreadTest
#endif

#if defined(DEnableWin32ThreadTest)
#include <windows.h>
/*
class CDebugInfo
{
    uint16   Type;
    uint16   CreatorBackTraceIndex;
    class CWindowsCriticalSection *CriticalSection;
    LIST_ENTRY ProcessLocksList;
    uint32 EntryCount;
    uint32 ContentionCount;
    uint32 Spare[ 2 ];
};

#define RTL_CRITSECT_TYPE 0
#define RTL_RESOURCE_TYPE 1

class CWindowsCriticalSection
{
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

#endif



class CTestThread : public CMalterlibTest
{
public:

	static CTestThread* ms_pThis;

	class CThread : public NMib::NThread::CThread
	{
	public:
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

	class CIncThread : public NMib::NThread::CThread
	{
	public:
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
#if DMibEnableSafeCheck > 0 && 0
						++(*m_pTest->m_pIncValue);
#else
						++m_pTest->m_IncValue;
#endif
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

	class CReadThread : public NMib::NThread::CThread
	{
	public:
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

#if DMibEnableSafeCheck > 0 && 0
						DMibFastCheck(!m_pTest->m_IncLock.f_IsLocked());
						for (umint i = 0; i < 1000; ++i)
							NMib::fg_Volatile(m_pTest->m_pIncValue) = nullptr;
						NMib::fg_Volatile(m_pTest->m_pIncValue) = &m_pTest->m_IncValue;
						DMibFastCheck(!m_pTest->m_IncLock.f_IsLocked());
#endif
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
	class CThreadLocal
	{
	public:

		CTestThread *m_pThread;
		umint m_TestInherit;

		CThreadLocal()
		{
			m_pThread = nullptr;
			m_TestInherit = 0;
		}

		CThreadLocal(const CThreadLocal &_Other)
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

	class CThreadLocalTest : public NMib::NThread::CThread
	{
	public:
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
#ifdef DMibDebug
		const aint Tests = 1000000;
		static const aint NumTests = 1;
#else
		const aint Tests = 1000000;
		static const aint NumTests = 2;
#endif
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
			{
				Threads[i].f_Stop();
			}
		}


		DMibCheck(m_ThreadLocalInstances.f_Get() == 1);

		return "";

		NMib::NThread::CMutual Lock;
		NMib::NTime::CPerfTimeMeasureMin Timer;

		{
			enum
			{
				EIncThreads = 4,
				EReadThreads = 7,
			};
			static CIncThread IncThreads[EIncThreads];
			static CReadThread ReadThreads[EReadThreads];
			for (umint i = 0; i < EIncThreads; ++i)
				IncThreads[i].f_Start(this);
			for (umint i = 0; i < EReadThreads; ++i)
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
						for (umint i = 0; i < EReadThreads; ++i)
						{
							ReadThreads[i].m_EventWantQuit.f_Signal();
						}
						for (umint i = 0; i < EIncThreads; ++i)
							IncThreads[i].m_EventWantQuit.f_Signal();

						while (1)
						{
							m_IncEvent.f_Wait();
							DMibLock(m_IncDoneLock);
							if (m_IncDone == (EIncThreads))
								break;
						}
						for (umint i = 0; i < EReadThreads; ++i)
						{
							ReadThreads[i].m_bStop = true;
							nReads += ReadThreads[i].m_nReads;
						}
						while (1)
						{
							bool bAllStopped = true;
							for (umint i = 0; i < EReadThreads; ++i)
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
			DMibConOut("Performance for CMutualManyRead Lock+Unlock from {} threads = {} locks per second IncValue = {}\r\n", int32(EIncThreads), fp64(m_IncValue) / Timer.f_GetTime(), m_IncValue);
			DMibConOut("Performance for CMutualManyRead LockRead+UnlockRead from {} threads = {} locks per second nReads = {}\r\n", int32(EReadThreads), fp64(nReads) / Timer.f_GetTime(), nReads);

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

CTestThread* CTestThread::ms_pThis = nullptr;

aint CTestThread::m_IncValue;
aint *CTestThread::m_pIncValue = nullptr;
NMib::NThread::CMutualManyRead CTestThread::m_IncLock;
NMib::NThread::CMutual CTestThread::m_IncLockMutual;
NMib::NThread::CEventAutoReset CTestThread::m_IncEvent;

NMib::NThread::CMutual CTestThread::m_IncDoneLock;
aint CTestThread::m_IncDone;

DMibRuntimeClass(CMalterlibTest, CTestThread);
#endif
