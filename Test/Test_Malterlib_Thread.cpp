// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Test/Performance>

#if 1
#if defined(DPlatformFamily_Windows)
#define DEnableWin32ThreadTest
#endif

#if defined(DEnableWin32ThreadTest)
#include <windows.h>
#if defined(DEnableWin32ConCrt)
#include <concrt.h>
#endif
typedef CRITICAL_SECTION CWindowsCriticalSection;
DWORD winFlsAlloc(PFLS_CALLBACK_FUNCTION lpCallback);
BOOL winFlsFree(DWORD dwFlsIndex);
PVOID winFlsGetValue(DWORD dwFlsIndex);
BOOL winFlsSetValue(DWORD dwFlsIndex, PVOID lpFlsData);
#endif

#include <thread>
#include <mutex>

using namespace NMib::NTime;
using namespace NMib::NThread;

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
		NMib::NTraits::TCAlign<NMib::NThread::CMutualManyRead, ECacheLineSize>::CType m_IncLock;
		NMib::NTraits::TCAlign<NMib::NThread::CMutual, ECacheLineSize>::CType m_IncLockMutual;
		NMib::NTraits::TCAlign<NMib::NThread::CMutual, ECacheLineSize>::CType m_IncDoneLock;
		bool m_bDummy1;
		aint m_ChangingValue;
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

			NMib::NThread::CEventAutoResetReportable m_Event;
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
				m_EventWantQuit.f_ReportTo(&m_Event);
				while (1)
				{
					m_Event.f_Wait();
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

			NMib::NThread::CEventAutoResetReportable m_Event;
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

			mint m_nReads;
			NMib::NAtomic::TCAtomic<smint> m_bStop;

			aint f_Main()
			{
				m_EventWantQuit.f_ReportTo(&m_Event);
				bint bWantStop = false;
				while (1)
				{
					m_Event.f_Wait();
					if (f_GetState() == NMib::NThread::EThreadState_EventWantQuit)
						break;
					m_nReads = 0;
//					DMibTestSuite("ReadThread")
					{
						bint bInvalidValue = false;
						while (1)
						{
							if (m_bStop.f_Exchange(0))
							{
								bWantStop = true;
								break;
							}

							{
								m_pTest->m_IncLock.f_LockRead();

								if (m_pTest->m_ChangingValue != 1112)
								{
									if (!bInvalidValue)
									{
										bInvalidValue = true;
										DMibTest(DMibExpr(!bInvalidValue));
									}
								}
#if DMibEnableSafeCheck > 0 && 0
								DMibFastCheck(!m_pTest->m_IncLock.f_IsLocked());
								for (mint i = 0; i < 1000; ++i)
									NMib::fg_Volatile(m_pTest->m_pIncValue) = nullptr;
								NMib::fg_Volatile(m_pTest->m_pIncValue) = &m_pTest->m_IncValue;
								DMibFastCheck(!m_pTest->m_IncLock.f_IsLocked());
#endif
								++m_nReads;

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
				NMib::NSys::fg_Thread_SetPriority(NMib::NSys::fg_Thread_GetCurrent(), NMib::EThreadPriority_Normal);
			}
			catch (NMib::NException::CException const &)
			{
			}
#endif
			NMib::NThread::CMutual Lock;
			NMib::NTime::CCyclesMin Timer;

			{
				CIncThread IncThreads[EIncThreads];
				CReadThread ReadThreads[EReadThreads];
				for (mint i = 0; i < EIncThreads; ++i)
					IncThreads[i].f_Start(this);
				for (mint i = 0; i < EReadThreads; ++i)
					ReadThreads[i].f_Start(this);

				NMib::NSys::fg_Thread_Sleep(1.0);

				Timer.f_Reset();
				mint nReads = 0;
				m_ChangingValue = 1112;
				m_nTests = 250000 / EIncThreads;
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
							for (mint i = 0; i < EReadThreads; ++i)
							{
								ReadThreads[i].m_Event.f_Signal();
							}
							for (mint i = 0; i < EIncThreads; ++i)
								IncThreads[i].m_Event.f_Signal();

							while (1)
							{
								m_IncEvent.f_Wait();
								DMibLock(m_IncDoneLock);
								if (m_IncDone == (EIncThreads))
									break;
							}
							for (mint i = 0; i < EReadThreads; ++i)
							{
								nReads += NMib::fg_Volatile(ReadThreads[i].m_nReads);
								ReadThreads[i].m_bStop.f_Exchange(1);
								ReadThreads[i].m_Event.f_Signal();
							}
							Timer.f_Stop();
							while (1)
							{
								bint bAllStopped = true;
								for (mint i = 0; i < EReadThreads; ++i)
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

				NMib::NTime::CCyclesMin TimerRead = Timer;
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
				NMib::NSys::fg_Thread_SetPriority(NMib::NSys::fg_Thread_GetCurrent(), NMib::EThreadPriority_Highest);
			}
			catch (NMib::NException::CException const &)
			{
			}
#endif
		}
	};

#ifdef DPlatformFamily_Windows
		__declspec(thread) mint g_ThreadLocal = 0;
		__declspec(thread) mint g_ThreadLocalArray[16] = {0};
#else
		__thread mint __attribute__((tls_model("local-exec"))) g_ThreadLocal = 0;
		__thread mint __attribute__((tls_model("local-exec"))) g_ThreadLocalArray[16] = {0};
#endif
	mint g_LocalArrayIndex = 11;
#	if defined(DEnableWin32ThreadTest)
		DWORD g_TlsLocal;
		LPVOID (WINAPI *pTlsGetValue)(DWORD dwTlsIndex);
		DWORD g_FlsLocal;
		//LPVOID (WINAPI *pFlsGetValue)(DWORD dwFlsIndex);
#	endif

	TCThreadLocal<mint, NMib::NMem::CAllocator_Heap, EThreadLocalFlag_AlwaysCreated> g_ThreadLocalMalterlib;
	TCThreadLocal<mint, NMib::NMem::CAllocator_Heap, EThreadLocalFlag(uint32(EThreadLocalFlag_AlwaysCreated) | uint32(EThreadLocalFlag_FastThreadLocal))> g_ThreadLocalMalterlibFast;
	mint g_ThreadLocalFastIndex;
	mint g_ThreadLocalIndex;

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
				mint *pTls = (mint *)pTlsGetValue(g_TlsLocal);
				++(*pTls);
			}
			static void fs_IncFls()
			{
				mint *pTls = (mint *)winFlsGetValue(g_FlsLocal);
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


		static mint fs_CurrentThreadNative()
		{
#ifdef DPlatformFamily_Windows
			return GetCurrentThreadId();
#else
			return (mint)pthread_self();
#endif
		}

		static mint fs_CurrentThreadMalterlib()
		{
//			return __readgsdword(0x48);
//000000013F93B004  mov         rax,qword ptr gs:[30h]  
//000000013F93B00D  mov         eax,dword ptr [rax+48h]  

			return NMib::NSys::fg_Thread_GetCurrentUID();
		}

		static void fs_IncMalterlibStorageFast()
		{
			mint *pTls = (mint *)NMib::NSys::fg_Thread_GetLocalFast(g_ThreadLocalFastIndex);
			++(*pTls);
		}

		static void fs_IncMalterlibStorage()
		{
			mint *pTls = (mint *)NMib::NSys::fg_Thread_GetLocal(g_ThreadLocalIndex);
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
					g_FlsLocal = winFlsAlloc(nullptr);
					TlsSetValue(g_TlsLocal, DMibNew mint);
					winFlsSetValue(g_FlsLocal, DMibNew mint);
					*((mint *)TlsGetValue(g_TlsLocal)) = 0;
					*((mint *)winFlsGetValue(g_FlsLocal)) = 0;
#				endif
				
				g_ThreadLocalFastIndex = NMib::NSys::fg_Thread_AllocLocalFast();
				NMib::NSys::fg_Thread_SetLocalFast(g_ThreadLocalFastIndex, DMibNew mint);
				*((mint *)NMib::NSys::fg_Thread_GetLocalFast(g_ThreadLocalFastIndex)) = 0;

				g_ThreadLocalIndex = NMib::NSys::fg_Thread_AllocLocal();
				NMib::NSys::fg_Thread_SetLocal(g_ThreadLocalIndex, DMibNew mint);
				*((mint *)NMib::NSys::fg_Thread_GetLocal(g_ThreadLocalIndex)) = 0;

				*g_ThreadLocalMalterlib = 0;
				*g_ThreadLocalMalterlibFast = 0;
				const static mint nTests = 33;
				const static mint nLoops = 100000;

				g_LocalArrayIndex = NMib::NStr::CStr((NMib::NStr::CStr::CFormat("{}") << (12))).f_ToInt();

				CCyclesMin NativeTime;
				CCyclesMin NativeArrayTime;
#				if defined(DEnableWin32ThreadTest)
					CCyclesMin TlsTime;
					CCyclesMin FlsTime;
#				endif

				CCyclesMin MalterlibTime;
				CCyclesMin MalterlibFastTime;
				CCyclesMin MalterlibStorageFastTime;
				CCyclesMin MalterlibStorageTime;

				auto Native = [&] () 
				{
					NativeTime.f_Start();
					[]() inline_never
						{
							for (mint i = 0; i < nLoops; ++i)
							{
								CThread_Tests::fs_IncNative();
#ifndef DCompiler_MSVC
								asm("");
#endif
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
							for (mint i = 0; i < nLoops; ++i)
							{
								CThread_Tests::fs_IncNativeArray();
#ifndef DCompiler_MSVC
								asm("");
#endif
							}
						}()
					;
					NativeArrayTime.f_Stop();
				};
#				if defined(DEnableWin32ThreadTest)
					auto Tls = [&] () 
					{
						TlsTime.f_Start();
						for (mint i = 0; i < nLoops; ++i)
						{
							CThread_Tests::fs_IncTls();
#ifndef DCompiler_MSVC
							asm("");
#endif
						}
						TlsTime.f_Stop();
					};
					auto Fls = [&] () 
					{
						FlsTime.f_Start();
						for (mint i = 0; i < nLoops; ++i)
						{
							CThread_Tests::fs_IncFls();
#ifndef DCompiler_MSVC
							asm("");
#endif
						}
						FlsTime.f_Stop();
					};
#				endif

				auto Malterlib = [&] () 
				{
					MalterlibTime.f_Start();
					[]() inline_never
						{
							for (mint i = 0; i < nLoops; ++i)
							{
								CThread_Tests::fs_IncMalterlib();
#ifndef DCompiler_MSVC
								asm("");
#endif
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
							for (mint i = 0; i < nLoops; ++i)
							{
								CThread_Tests::fs_IncMalterlibFast();
#ifndef DCompiler_MSVC
								asm("");
#endif
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
							for (mint i = 0; i < nLoops; ++i)
							{
								CThread_Tests::fs_IncMalterlibStorageFast();
#ifndef DCompiler_MSVC
								asm("");
#endif
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
							for (mint i = 0; i < nLoops; ++i)
							{
								CThread_Tests::fs_IncMalterlibStorage();
#ifndef DCompiler_MSVC
								asm("");
#endif
							}
						}()
					;
					MalterlibStorageTime.f_Stop();
				};

				for (mint i = 0; i < nTests; ++i)
				{
					fs_CallFunctor(Malterlib);
					fs_CallFunctor2(Malterlib);
				}
				for (mint i = 0; i < nTests; ++i)
				{
					fs_CallFunctor(MalterlibFast);
					fs_CallFunctor2(MalterlibFast);
				}
				for (mint i = 0; i < nTests; ++i)
				{
					fs_CallFunctor(MalterlibStorageFast);
					fs_CallFunctor2(MalterlibStorageFast);
				}
				for (mint i = 0; i < nTests; ++i)
				{
					fs_CallFunctor(MalterlibStorage);
					fs_CallFunctor2(MalterlibStorage);
				}
				for (mint i = 0; i < nTests; ++i)
				{
					fs_CallFunctor(Native);
					fs_CallFunctor2(Native);
				}
				for (mint i = 0; i < nTests; ++i)
				{
					fs_CallFunctor(NativeArray);
					fs_CallFunctor2(NativeArray);
				}
#				if defined(DEnableWin32ThreadTest)
					for (mint i = 0; i < nTests; ++i)
					{
						fs_CallFunctor(Tls);
						fs_CallFunctor2(Tls);
					}
					for (mint i = 0; i < nTests; ++i)
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
					DMibTest(DMibExpr(g_ThreadLocal) == DMibExpr(*((mint *)TlsGetValue(g_TlsLocal))));
					DMibTest(DMibExpr(g_ThreadLocal) == DMibExpr(*((mint *)winFlsGetValue(g_FlsLocal))));
					DMibTest(DMibExpr(g_ThreadLocal) == DMibExpr(*((mint *)NMib::NSys::fg_Thread_GetLocalFast(g_ThreadLocalFastIndex))));
					DMibTest(DMibExpr(g_ThreadLocal) == DMibExpr(*((mint *)NMib::NSys::fg_Thread_GetLocal(g_ThreadLocalIndex))));
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
					delete ((mint *)TlsGetValue(g_TlsLocal));
					TlsFree(g_TlsLocal);
					delete ((mint *)winFlsGetValue(g_FlsLocal));
					winFlsFree(g_FlsLocal);
#				endif

				delete ((mint *)NMib::NSys::fg_Thread_GetLocalFast(g_ThreadLocalFastIndex));
				NMib::NSys::fg_Thread_SetLocalFast(g_ThreadLocalFastIndex, nullptr);
				NMib::NSys::fg_Thread_FreeLocalFast(g_ThreadLocalFastIndex);
				delete ((mint *)NMib::NSys::fg_Thread_GetLocal(g_ThreadLocalIndex));
				NMib::NSys::fg_Thread_SetLocal(g_ThreadLocalIndex, nullptr);
				NMib::NSys::fg_Thread_FreeLocal(g_ThreadLocalIndex);
			};

			DMibTestSuite("Current Thread Performance")
			{
				const static mint nTests = 100;
				const static mint nLoops = 1000000;
				CCyclesMin NativeTime;
				CCyclesMin MalterlibTime;

				volatile mint ThreadResultMalterlib = 0;
				volatile mint ThreadResultNative = 0;
				
				for (mint i = 0; i < nTests; ++i)
				{
					NativeTime.f_Start();
					ThreadResultNative = []() inline_never
						{
							mint Results = 0;
							for (mint i = 0; i < nLoops; ++i)
							{
								Results += fs_CurrentThreadNative();
#ifndef DCompiler_MSVC
								asm("");
#endif

							}
							return Results;
						}()
					;
					NativeTime.f_Stop();
				}

				for (mint i = 0; i < nTests; ++i)
				{
					MalterlibTime.f_Start();
					ThreadResultMalterlib = []() inline_never
						{
							mint Results = 0;
							for (mint i = 0; i < nLoops; ++i)
							{
								Results += fs_CurrentThreadMalterlib();
#ifndef DCompiler_MSVC
								asm("");
#endif

							}
							return Results;
						}()
					;
					MalterlibTime.f_Stop();
				}

				NativeTime /= nLoops;
				MalterlibTime /= nLoops;
				
				mint ThreadResultMalterlib1 = ThreadResultMalterlib;
				mint ThreadResultNative1 = ThreadResultNative;
				DMibTest(DMibExpr(ThreadResultMalterlib1) == DMibExpr(ThreadResultNative1));
				if (NMib::NTest::fg_GroupActive("Performance"))
					DMibTest(DMibExpr(NativeTime) / DMibExpr(MalterlibTime) >= DMibExpr(1.0));

			};
			DMibTestSuite("Lock Performance")
			{
				const static mint nTests = 101;
				const static mint nLoops = 100000;

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
						for (mint i = 0; i < nTests; ++i)
						{
							Measure.f_Start();
							[&]() inline_never
								{
									for (mint i = 0; i < nLoops; ++i)
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
						for (mint i = 0; i < nTests; ++i)
						{
							EnterCriticalSection((CRITICAL_SECTION *)&Crit);
							Measure.f_Start();
							[&]() inline_never
								{
									for (mint i = 0; i < nLoops; ++i)
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
							for (mint i = 0; i < nTests; ++i)
							{
								Measure.f_Start();
								[&]() inline_never
									{
										for (mint i = 0; i < nLoops; ++i)
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
							for (mint i = 0; i < nTests; ++i)
							{
								Measure.f_Start();
								[&]() inline_never
									{
										for (mint i = 0; i < nLoops; ++i)
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
						for (mint i = 0; i < nTests; ++i)
						{
							Measure.f_Start();
							[]() inline_never
								{
									NMib::NThread::CMutual Lock;
									for (mint i = 0; i < nLoops; ++i)
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
						for (mint i = 0; i < nTests; ++i)
						{
							NMib::NThread::CMutual Lock;
							DMibLock(Lock);
							Measure.f_Start();
							[&]() inline_never
								{
									for (mint i = 0; i < nLoops; ++i)
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
						for (mint i = 0; i < nTests; ++i)
						{
							NMib::NThread::CMutualSpin Lock;
							Measure.f_Start();
							[&]() inline_never
								{
									for (mint i = 0; i < nLoops; ++i)
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
						for (mint i = 0; i < nTests; ++i)
						{
							NMib::NThread::CMutualSpin Lock;
							DMibLock(Lock);
							Measure.f_Start();
							[&]() inline_never
								{
									for (mint i = 0; i < nLoops; ++i)
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
						for (mint i = 0; i < nTests; ++i)
						{
							std::recursive_mutex Lock;
							Measure.f_Start();
							[&]() inline_never
								{
									for (mint i = 0; i < nLoops; ++i)
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
						for (mint i = 0; i < nTests; ++i)
						{
							std::recursive_mutex Lock;
							Lock.lock();
							Measure.f_Start();
							[&]() inline_never
								{
									for (mint i = 0; i < nLoops; ++i)
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
					for (mint i = 0; i < nTests; ++i)
					{
						NMib::NThread::CMutualSimple Lock;
						Measure.f_Start();
						[&]() inline_never
							{
								for (mint i = 0; i < nLoops; ++i)
									DMibLock(Lock);
							}()
						;
						Measure.f_Stop(nLoops);
					}
					PerfTestSimpleMutual.f_Add(Measure);
				}
				{
					CTestPerformanceMeasure Measure("CSpinLock");
					for (mint i = 0; i < nTests; ++i)
					{
						NMib::NThread::CSpinLock Lock;
						Measure.f_Start();
						[&]() inline_never
							{
								for (mint i = 0; i < nLoops; ++i)
									DMibLock(Lock);
							}()
						;
						Measure.f_Stop(nLoops);
					}
					PerfTestSimpleMutual.f_Add(Measure);
				}
				{
					CTestPerformanceMeasure Measure("mutex");
					for (mint i = 0; i < nTests; ++i)
					{
						std::mutex Lock;
						Measure.f_Start();
						[&]() inline_never
							{
								for (mint i = 0; i < nLoops; ++i)
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
					for (mint i = 0; i < nTests; ++i)
					{
						NMib::NThread::CMutualManyRead Lock;
						{
							DMibLock(Lock);
						}
						Measure.f_Start();
						[&]() inline_never
							{
								for (mint i = 0; i < nLoops; ++i)
									DMibLock(Lock);
							}()
						;
						Measure.f_Stop(nLoops);
					}
					PerfTestReadWriteWrite.f_Add(Measure);
				}
				{
					CTestPerformanceMeasure Measure("CMutualManyRead");
					for (mint i = 0; i < nTests; ++i)
					{
						NMib::NThread::CMutualManyRead Lock;
						Measure.f_Start();
						[&]() inline_never
							{
								for (mint i = 0; i < nLoops; ++i)
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

#if DMibConfig_Tests_Enable
			DMibTestCategory("Thread local")
			{
				DMibTestSuite("Always created")
				{
					for (mint i = 0; i < 10; ++i)
					{
						NMib::NAggregate::TCAggregate<TCThreadLocal<CTemp35, NMib::NMem::CAllocator_Heap, EThreadLocalFlag_AlwaysCreated>> ThreadLocal = {DAggregateInit};
						NMib::NThread::CEvent Event;
						NMib::NThread::CEvent EventDone;
						NMib::NPtr::TCUniquePointer<CThreadObject> pThread = CThreadObject::fs_StartThread
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
					for (mint i = 0; i < 10; ++i)
					{
						NMib::NAggregate::TCAggregate<TCThreadLocal<CTemp, NMib::NMem::CAllocator_Heap, EThreadLocalFlag_Inherit>> ThreadLocal = {DAggregateInit};
						
						NMib::NThread::CEvent Event;
						NMib::NThread::CEvent EventDone;
						(*ThreadLocal)->m_Value = 36;

						DMibTest(DMibExpr((*ThreadLocal)->m_Value) == DMibExpr(36)) (ETestFlag_Aggregated);
						NMib::NPtr::TCUniquePointer<CThreadObject> pThread = CThreadObject::fs_StartThread
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
					for (mint i = 0; i < 10; ++i)
					{
						NMib::NAggregate::TCAggregate<TCThreadLocal<CTemp, NMib::NMem::CAllocator_Heap, EThreadLocalFlag(uint32(EThreadLocalFlag_Inherit) | uint32(EThreadLocalFlag_AlwaysCreated))>> ThreadLocal = {DAggregateInit};
						NMib::NThread::CEvent Event;
						NMib::NThread::CEvent EventDone;
						NMib::NPtr::TCUniquePointer<CThreadObject> pThread0 = CThreadObject::fs_StartThread
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
						NMib::NPtr::TCUniquePointer<CThreadObject> pThread1 = CThreadObject::fs_StartThread
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

typedef CRITICAL_SECTION CWindowsCriticalSection;

#endif



class CTestThread : public CMalterlibTest
{
public:

	static CTestThread* ms_pThis;

	class CThread : public NMib::NThread::CThread
	{
	public:
		NMib::NThread::CEventAutoResetReportable m_Event;

		NMib::NStr::CStr f_GetThreadName()
		{
			return "MalterlibCertifier_TestThread";
		}

		CThread()
		{
			m_EventWantQuit.f_ReportTo(&m_Event);
		}
		~CThread()
		{
			f_Stop();
		}

		aint f_Main()
		{
			while (1)
			{
				m_Event.f_Wait();
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

		NMib::NThread::CEventAutoResetReportable m_Event;
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
			m_EventWantQuit.f_ReportTo(&m_Event);
			while (1)
			{
				m_Event.f_Wait();
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

		NMib::NThread::CEventAutoResetReportable m_Event;
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

		mint m_nReads;
		bint m_bStop;

		aint f_Main()
		{
			m_EventWantQuit.f_ReportTo(&m_Event);
			while (1)
			{
				m_Event.f_Wait();
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
						for (mint i = 0; i < 1000; ++i)
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

	class CReportableThread : public NMib::NThread::CThread
	{
	public:
		NMib::NStr::CStr f_GetThreadName()
		{
			return "MalterlibCertifier_ReadThread";
		}

		NMib::NThread::CEventAutoResetReportable m_Event;
		NMib::NThread::CEventAutoResetReportable m_EventTest;
		CTestThread *m_pTest;

		CReportableThread()
		{
			m_pTest = nullptr;
			m_bStop = false;
			m_nOperations = 0;
		}

		void f_Start(CTestThread *_pTest)
		{
			m_pTest = _pTest;
			NMib::NThread::CThread::f_Start();
		}

		void f_Stop()
		{
			m_bStop = true;
			NMib::NThread::CThread::f_Stop();

		}

		~CReportableThread()
		{
			f_Stop();

		}

		mint m_nOperations;

		bint m_bStop;

		aint f_Main()
		{
			m_EventWantQuit.f_ReportTo(&m_Event);
			while (1)
			{
				NMib::NMisc::CRandom31 Random;
				Random.f_SetSeed(NMib::fg_GetSys()->f_GetTimerValue());
				m_Event.f_Wait();
				if (f_GetState() == NMib::NThread::EThreadState_EventWantQuit)
					break;
				while (1)
				{
					if (NMib::fg_Volatile(m_bStop))
						break;

					{
						int32 iThread = Random.f_Get() % 16;

						int32 WhatToDo = Random.f_Get() % 7;
						switch (WhatToDo)
						{
						case 0:
							m_pTest->m_ReportableThreads[iThread].m_EventTest.f_ReportTo(&m_EventTest);
							break;
						case 1:
							m_EventTest.f_ReportTo(&m_pTest->m_ReportableThreads[iThread].m_EventTest);
							break;
						case 2:
							m_EventTest.f_ClearReportFrom();
							break;
						case 3:
							m_EventTest.f_ClearReportTo();
							break;
						case 4:
							m_pTest->m_ReportableThreads[iThread].m_EventTest.f_ClearReportFrom();
							break;
						case 5:
							m_pTest->m_ReportableThreads[iThread].m_EventTest.f_ClearReportTo();
							break;
						case 6:
							m_pTest->m_ReportableThreads[iThread].m_EventTest.f_Signal();
							break;
						case 7:
							m_EventTest.f_Signal();
							break;
						}
						++m_nOperations;

					}
				}
				m_bStop = false;
				m_pTest->m_IncEvent.f_Signal();
			}

			return 0;
		}
	};

	CReportableThread m_ReportableThreads[16];

	NAtomic::TCAtomic<smint> m_ThreadLocalInstances;
	class CThreadLocal
	{
	public:

		CTestThread *m_pThread;
		mint m_TestInherit;

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

	NMib::NThread::TCThreadLocal<CThreadLocal, NMib::NMem::CAllocator_Heap, true> m_ThreadLocal;

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
			for (mint i = 0; i < 10; ++i)
			{
				Threads[i].f_Init(this);
				Threads[i].f_Start();
			}
			for (mint i = 0; i < 10; ++i)
			{
				Threads[i].f_Stop();
			}
		}

		
		DMibCheck(m_ThreadLocalInstances.f_Get() == 1);

		{
			NMib::NTime::CTimerMin Timer;

			for (mint i = 0; i < 16; ++i)
				m_ReportableThreads[i].f_Start(this);
			Timer.f_Start();
			for (mint i = 0; i < 16; ++i)
				m_ReportableThreads[i].m_Event.f_Signal();

			NMib::NSys::fg_Thread_Sleep(1.0);

			mint nOper = 0;
			for (mint i = 0; i < 16; ++i)
			{
				m_ReportableThreads[i].f_Stop();
				nOper += m_ReportableThreads[i].m_nOperations;
			}
			Timer.f_Stop();

			DMibConOut("Performance for reportable events registration = {0} operations per second\r\n", ((aint)((fp64(nOper) / Timer.f_GetTime()).f_Get())));
		}

		return "";

		NMib::NThread::CMutual Lock;
		NMib::NTime::CTimerMin Timer;

		{
			enum
			{
				EIncThreads = 4,
				EReadThreads = 7,
			};
			static CIncThread IncThreads[EIncThreads];
			static CReadThread ReadThreads[EReadThreads];
			for (mint i = 0; i < EIncThreads; ++i)
				IncThreads[i].f_Start(this);
			for (mint i = 0; i < EReadThreads; ++i)
				ReadThreads[i].f_Start(this);

			NMib::NSys::fg_Thread_Sleep(1.0);

			Timer.f_Reset();
			mint nReads = 0;
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
						DMibScopeTimerMin(Timer);
						for (mint i = 0; i < EReadThreads; ++i)
						{
							ReadThreads[i].m_Event.f_Signal();
						}
						for (mint i = 0; i < EIncThreads; ++i)
							IncThreads[i].m_Event.f_Signal();

						while (1)
						{
							m_IncEvent.f_Wait();
							DMibLock(m_IncDoneLock);
							if (m_IncDone == (EIncThreads))
								break;
						}
						for (mint i = 0; i < EReadThreads; ++i)
						{
							ReadThreads[i].m_bStop = true;
							nReads += ReadThreads[i].m_nReads;
						}
						while (1)
						{
							bint bAllStopped = true;
							for (mint i = 0; i < EReadThreads; ++i)
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
			DMibConOut("Performance for CMutualManyRead Lock+Unlock from {} threads = {} locks per second IncValue = {}\r\n", int32(EIncThreads) << fp64(m_IncValue) / Timer.f_GetTime() << m_IncValue);
			DMibConOut("Performance for CMutualManyRead LockRead+UnlockRead from {} threads = {} locks per second nReads = {}\r\n", int32(EReadThreads) << fp64(nReads) / Timer.f_GetTime() << nReads);

		}


		
		/*

		{
			Timer.f_Reset();
			NMib::NThread::CMutualManyRead Lock;
			for (aint j = 0; j < NumTests; ++j)
			{
				{
					DMibScopeTimerMin(Timer);
					
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
					DMibScopeTimerMin(Timer);
					
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
		Thread.m_Event.f_Signal();
		NMib::NSys::fg_Thread_Sleep(0.5);
		Thread.m_Event.f_Signal();
		NMib::NSys::fg_Thread_Sleep(0.5);
		Thread.m_Event.f_Signal();
		NMib::NSys::fg_Thread_Sleep(0.5);
		Thread.m_Event.f_Signal();
		NMib::NSys::fg_Thread_Sleep(0.5);
		Thread.m_Event.f_Signal();
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
