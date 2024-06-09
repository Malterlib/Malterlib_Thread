// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>

#if DMibConfig_RefCountDebugging && DMibConfig_RefCountLeakDebugging
namespace NMib::NStorage
{
	constinit mint CRefCountDebug::ms_Magic = NMisc::CRandomShiftRNG(123456789 + DMibPLine, 123456789 + DMibPLine, 123456789 + DMibPLine).f_GetValue<mint>();
	constinit mint TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer>::ms_Magic
		= NMisc::CRandomShiftRNG(123456789 + DMibPLine, 123456789 + DMibPLine, 123456789 + DMibPLine).f_GetValue<mint>()
	;
	constinit mint TCIntrusiveRefCount<ESharedPointerOption_None>::ms_Magic = NMisc::CRandomShiftRNG(123456789 + DMibPLine, 123456789 + DMibPLine, 123456789 + DMibPLine).f_GetValue<mint>();

	CRefCountDebug::CRefCountDebug() = default;
	CRefCountDebug::~CRefCountDebug()
	{
		m_Magic = 0;
	}
}
#endif

namespace NMib::NThread
{
	/***************************************************************************************************\
	|¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯|
	| CThread																							|
	|___________________________________________________________________________________________________|
	\***************************************************************************************************/

	CThread::CThread()
	{
		m_ParentThreadID = 0;
		m_ReturnValue = 0;
		m_pThread = nullptr;
		m_ThreadID = 0;
		m_pThreadDestroyContext = nullptr;
		m_bAutoDestroy = false;
		m_bLockHeld = false;
		m_ThreadQuitEvent.f_SetSignaled();
	}

	void CThread::fp_Cleanup()
	{
		if (m_pThreadDestroyContext)
		{
			NSys::fg_Thread_BlockUntilExit(m_pThreadDestroyContext);
			NSys::fg_Thread_EndDestroy(m_pThreadDestroyContext);
			m_pThreadDestroyContext = nullptr;
		}
	}

	bool CThread::f_DestroyThread()
	{
		fg_DeleteObject(NMemory::CDefaultAllocator(), this);
		return true;
	}

	CThread::~CThread()
	{
		DMibLockTyped(CMutual, m_Lock);
		if (m_StateAtomic.f_Load() > 1)
		{
			DMibUnlockTyped(CMutual, m_Lock);
			f_Stop();
		}
		fp_Cleanup();

		if (m_bLockHeld)
			m_Lock.f_Unlock();
	}

	aint CThread::fsp_ThreadMain(void *_pContext)
	{
		CThread *pThread = ((CThread *)_pContext);
#if DMibConfig_MemoryManager_Stats_EnableCategories
		NStr::CStr ThreadName;
		auto *pName = pThread->f_GetThreadNameRaw();
		if (!pName)
		{
			ThreadName = pThread->f_GetThreadName();
			pName = ThreadName;
		}
		auto pCategory = DMibMemoryDefineDynamicCategory(pName);
		DMibMemoryDynamicCategory(pCategory);
#endif

		void *pSystemThread;

		// This should be called by the system implementation
	//#ifndef DMibPSupportThreadCreateNotification
		//fg_GetSys()->f_ThreadLocalCreateThread(NSys::fg_Thread_GetCurrentUID(), pThread->m_ParentThreadID);
	//#endif

		aint Return = pThread->f_Main();

	#ifndef DMibPSupportThreadDestroyNotification
		fg_GetSys()->f_ThreadLocalFreeThread();
	#endif

		pThread->m_Lock.f_Lock();

		pThread->m_ReturnValue = Return;
		pThread->m_StateAtomic.f_Exchange(EThreadState_Stopped);
		pSystemThread = pThread->m_pThread;
		pThread->m_pThread = nullptr;
		pThread->m_ThreadID = 0;

		if (pThread->m_bAutoDestroy)
		{
			pThread->m_bAutoDestroy = false;
			void *pThreadDestroyContext = pThread->m_pThreadDestroyContext;
			pThread->m_pThreadDestroyContext = nullptr;

			NSys::fg_Thread_Destroy(pSystemThread);

			if (pThreadDestroyContext)
			{
				NSys::fg_Thread_WillNotBlockUntilExit(pThreadDestroyContext);
				NSys::fg_Thread_EndDestroy(pThreadDestroyContext);
			}

			pThread->m_bLockHeld = true;
			if (!pThread->f_DestroyThread())
			{
				pThread->m_bLockHeld = false;
				pThread->m_Lock.f_Unlock();
			}
		}
		else
		{
			pThread->m_ThreadQuitEvent.f_SetSignaled();
			NSys::fg_Thread_Destroy(pSystemThread);
			pThread->m_Lock.f_Unlock();
		}

		return Return;
	}

	void CThread::f_Start(EExecutionPriority _Prio, mint _StackSize, mint _Affinity, bool _bAutoDestroy)
	{
		// Make sure that no thread is already running
		{
			DMibLockTyped(CMutual, m_Lock);
			if (m_StateAtomic.f_Load() > EThreadState_Stopped)
			{
				DMibError("You cannot start a thread that is already running");
			}
			fp_Cleanup();
			m_ThreadQuitEvent.f_ResetSignaled();
			m_ReturnValue = 0;
			m_ParentThreadID = NSys::fg_Thread_GetCurrentUID();
			m_bAutoDestroy = _bAutoDestroy;
			NStr::CStr ThreadName;
			auto *pName = f_GetThreadNameRaw();
			if (!pName)
			{
				ThreadName = f_GetThreadName();
				pName = ThreadName;
			}

			m_pThread = NSys::fg_Thread_Create(fsp_ThreadMain, this, _Prio, _StackSize, false, pName, _Affinity, m_ThreadID);
			m_pThreadDestroyContext = NSys::fg_Thread_BeginDestroy(m_pThread);
			m_StateAtomic.f_Exchange(EThreadState_Running);
		}

	}

	ch8 const *CThread::f_GetThreadNameRaw()
	{
		return nullptr;
	}


	mint CThread::f_Stop(bool _bBlock)
	{

		{
			m_Lock.f_Lock();

			auto Unlock
				= fg_OnScopeExit
				(
					[this]
					{
						m_Lock.f_Unlock();
					}
				)
			;


			switch (m_StateAtomic.f_Load())
			{
			case EThreadState_None:
				return 0;
			case EThreadState_Stopped:
				{
					DMibUnlockTyped(CMutual, m_Lock);
					if (m_pThreadDestroyContext)
					{
						// This is to guard against thread termination
						NSys::fg_Thread_BlockUntilExit(m_pThreadDestroyContext);
						NSys::fg_Thread_EndDestroy(m_pThreadDestroyContext);
						m_pThreadDestroyContext = nullptr;
					}
				}
				return f_GetReturnValue();
			case EThreadState_Running:
				{
					m_StateAtomic.f_Exchange(EThreadState_EventWantQuit);
					m_EventWantQuit.f_Signal();
				}
				break;
			}

			if (_bBlock)
			{
				bool bAutoDestroy = m_bAutoDestroy;
				m_bAutoDestroy = false;
				auto pDestroyContext = m_pThreadDestroyContext;
				m_pThreadDestroyContext = nullptr;

				{
					DMibUnlockTyped(CMutual, m_Lock);
					m_ThreadQuitEvent.f_Wait();
					if (pDestroyContext)
					{
						// This is to guard against thread termination
						NSys::fg_Thread_BlockUntilExit(pDestroyContext);
						NSys::fg_Thread_EndDestroy(pDestroyContext);
					}
				}

				auto ReturnValue = f_GetReturnValue();

				if (bAutoDestroy)
				{
					Unlock.f_Clear();
					m_bLockHeld = true;
					if (!f_DestroyThread())
					{
						m_bLockHeld = false;
						m_Lock.f_Unlock();
					}
				}

				return ReturnValue;
			}
		}

		return 0;

	}

	void CThread::f_Suspend()
	{
		DMibLockTyped(CMutual, m_Lock);
		DMibSafeCheck(m_pThread, "Must be started");
		NSys::fg_Thread_Suspend(m_pThread);
	}

	void CThread::f_Resume()
	{
		DMibLockTyped(CMutual, m_Lock);
		DMibSafeCheck(m_pThread, "Must be started");
		NSys::fg_Thread_Resume(m_pThread);
	}

	void CThread::f_SetPriority(EExecutionPriority _Prio)
	{
		DMibLockTyped(CMutual, m_Lock);
		DMibSafeCheck(m_pThread, "Must be started");
		NSys::fg_Thread_SetPriority(m_pThread, _Prio);
	}

	void CThread::f_PrepareFork()
	{
		m_Lock.f_Lock();
		m_Lock.f_PrepareFork();
		m_EventWantQuit.f_PrepareFork();
		m_ThreadQuitEvent.f_PrepareFork();
		DMibFastCheck(!m_bLockHeld);
	}

	void CThread::f_ForkedChild()
	{
		DMibFastCheck(!m_bLockHeld);
		m_pThreadDestroyContext = nullptr;
		m_StateAtomic.f_Exchange(EThreadState_None);
		m_ThreadQuitEvent.f_ForkedChild();
		m_EventWantQuit.f_ForkedChild();
		m_Lock.f_ForkedChild();
		m_Lock.f_Unlock();
	}

	void CThread::f_ForkedParent()
	{
		DMibFastCheck(!m_bLockHeld);
		m_ThreadQuitEvent.f_ForkedParent();
		m_EventWantQuit.f_ForkedParent();
		m_Lock.f_ForkedParent();
		m_Lock.f_Unlock();
	}

	inline_never void CThreadSpinWaiter::f_WaitSlow()
	{
		if (m_nWaits < 140)
		{
			NSys::fg_Thread_Yield();
			return;
		}

		NSys::fg_Thread_SmallestSleep();
	}
}

namespace NMib::NStorage
{
	TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer>::~TCIntrusiveRefCount()
	{
#if defined(DMibContractConfigure_CheckEnabled) || DMibConfig_RefCountDebugging
		smint RefCount = f_Get();
#endif
		DMibCheck(RefCount == 0 || RefCount == -1)(RefCount);
		DMibRefCountDebuggingOnly(if (RefCount == 0) m_Debug.f_Destruct());
	}

	TCIntrusiveRefCount<ESharedPointerOption_None>::~TCIntrusiveRefCount()
	{
#if defined(DMibContractConfigure_CheckEnabled) || DMibConfig_RefCountDebugging
		smint RefCount = f_Get();
#endif
		DMibCheck(RefCount == 0 || RefCount == -1)(RefCount);
		DMibRefCountDebuggingOnly(if (RefCount == 0) m_Debug.f_Destruct());
	}
}
