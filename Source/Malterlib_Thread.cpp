// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>

#if DMibConfig_RefCountDebugging
namespace NMib::NStorage
{
	CRefCountDebugReference::CRefCountDebugReference() = default;

	CRefCountDebugReference::~CRefCountDebugReference()
	{
		DMibFastCheck(!m_pCallstack);
	}

	CRefCountDebugReference::CRefCountDebugReference(CRefCountDebugReference &&_Other)
	{
		m_pCallstack = _Other.m_pCallstack;
		_Other.m_pCallstack = nullptr;
	}

	CRefCountDebugReference &CRefCountDebugReference::operator = (CRefCountDebugReference &&_Other)
	{
		m_pCallstack = _Other.m_pCallstack;
		_Other.m_pCallstack = nullptr;

		return *this;
	}
}
#endif

#if DMibConfig_RefCountDebugging && DMibConfig_RefCountLeakDebugging
namespace NMib::NStorage
{
	constinit umint CRefCountDebug::ms_Magic = NMisc::CRandomShiftRNG(123456789 + DMibPLine, 123456789 + DMibPLine, 123456789 + DMibPLine).f_GetValue<umint>();

	template <>
	constinit umint TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, smint>::ms_Magic = NMisc::CRandomShiftRNG
		(
			123456789 + DMibPLine
			, 123456789 + DMibPLine
			, 123456789 + DMibPLine
		)
		.f_GetValue<umint>()
	;

	template <>
	constinit umint TCIntrusiveRefCount<ESharedPointerOption_None, smint>::ms_Magic = NMisc::CRandomShiftRNG(123456789 + DMibPLine, 123456789 + DMibPLine, 123456789 + DMibPLine)
		.f_GetValue<umint>()
	;

	template <>
	constinit umint TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, int32>::ms_Magic = NMisc::CRandomShiftRNG
		(
			123456789 + DMibPLine
			, 123456789 + DMibPLine
			, 123456789 + DMibPLine
		)
		.f_GetValue<umint>()
	;

	template <>
	constinit umint TCIntrusiveRefCount<ESharedPointerOption_None, int32>::ms_Magic = NMisc::CRandomShiftRNG(123456789 + DMibPLine, 123456789 + DMibPLine, 123456789 + DMibPLine)
		.f_GetValue<umint>()
	;

	CRefCountDebug::CRefCountDebug() = default;
	CRefCountDebug::~CRefCountDebug()
	{
		m_Magic = 0;
	}
}
#endif

namespace NMib::NThread
{
	CDisableLazyCheckoutReturnScope::CDisableLazyCheckoutReturnScope()
	{
		NMib::NSys::fg_Mem_DisableLazyReturnCheckout();
	}

	CDisableLazyCheckoutReturnScope::~CDisableLazyCheckoutReturnScope()
	{
		NMib::NSys::fg_Mem_EnableLazyReturnCheckout();
	}

	CSemaphoreAggregate::CSemaphoreAggregate()
	{
	}

	void CSemaphoreAggregate::f_PrepareFork()
	{
	}

	void CSemaphoreAggregate::f_ForkedChild()
	{
		// The lock word is plain memory; waiters do not exist in the child
		m_Data.f_Store(m_Data.f_Load(NAtomic::gc_MemoryOrder_Relaxed) & mcp_ValueMask, NAtomic::gc_MemoryOrder_Relaxed);
	}

	void CSemaphoreAggregate::f_ForkedParent()
	{
	}

	CEventAutoResetAggregate::CEventAutoResetAggregate()
	{
	}

	CEventAutoReset::CEventAutoReset()
	{
		f_Construct();
	}

	CEventAutoReset::~CEventAutoReset()
	{
		f_Destruct();
	}

	CEventAggregate::CEventAggregate()
	{
	}

	void CEventAggregate::f_PrepareFork()
	{
	}

	void CEventAggregate::f_ForkedChild()
	{
		// Waiters do not exist in the child
		m_State.f_Store(m_State.f_Load(NAtomic::gc_MemoryOrder_Relaxed) & ~mcp_FlagWaiters, NAtomic::gc_MemoryOrder_Relaxed);
	}

	void CEventAggregate::f_ForkedParent()
	{
	}

	CEvent::CEvent()
	{
		f_Construct();
	}

	CEvent::~CEvent()
	{
		f_Destruct();
	}

	CSemaphore::CSemaphore(aint _Initial, aint _Max)
	{
		f_Construct(_Initial, _Max);
	}

	CSemaphore::~CSemaphore()
	{
		f_Destruct();
	}

	bool CNoLock::f_OwnsLock()
	{
		return true;
	}

	inline_always_lto void CLowLevelContendedLockAggregate::f_Lock()
	{
		if (!CLowLevelLockAggregate::f_TryLock())
		{
			++m_Contention;
			CLowLevelLockAggregate::f_Lock();
			--m_Contention;
		}
	}

	inline_always_lto void CLowLevelContendedLockAggregate::f_LockNoSanitize()
	{
		if (!CLowLevelLockAggregate::f_TryLockNoSanitize())
		{
			++m_Contention;
			CLowLevelLockAggregate::f_LockNoSanitize();
			--m_Contention;
		}
	}

	inline_always_lto bool CLowLevelContendedLockAggregate::f_Contended() const
	{
		return m_Contention.f_Load(NAtomic::gc_MemoryOrder_Relaxed) > 0;
	}

	void CLowLevelContendedLockAggregate::f_ForkedChildUnlocked()
	{
		CLowLevelLockAggregate::f_ForkedChildUnlocked();
		m_Contention = 0;
	}

	void CLowLevelContendedLockAggregate::f_ForkedChildLocked()
	{
		CLowLevelLockAggregate::f_ForkedChildLocked();
		m_Contention = 0;
	}

	CLowLevelLock::CLowLevelLock()
	{
		f_Construct();
	}

	CLowLevelLock::~CLowLevelLock()
	{
		f_Destruct();
	}

	CLowLevelContendedLock::CLowLevelContendedLock()
	{
		f_Construct();
	}

	CLowLevelContendedLock::~CLowLevelContendedLock()
	{
		f_Destruct();
	}

	void CLowLevelRecursiveLockAggregate::f_Lock()
	{
		umint CurrentThread = NSys::fg_Thread_GetCurrentUID();

		if (m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == CurrentThread)
		{
			++m_nRecurse;
			return;
		}

		CLowLevelLockAggregate::f_Lock();

		m_ThreadID.f_Store(NSys::fg_Thread_GetCurrentUID(), NAtomic::gc_MemoryOrder_Relaxed);
		m_nRecurse = 1;
	}

	void CLowLevelRecursiveLockAggregate::f_LockNoSanitize()
	{
		umint CurrentThread = NSys::fg_Thread_GetCurrentUID();

		if (m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == CurrentThread)
		{
			++m_nRecurse;
			return;
		}

		CLowLevelLockAggregate::f_LockNoSanitize();

		m_ThreadID.f_Store(NSys::fg_Thread_GetCurrentUID(), NAtomic::gc_MemoryOrder_Relaxed);
		m_nRecurse = 1;
	}

	void CLowLevelRecursiveLockAggregate::f_ForkedChildUnlocked()
	{
		CLowLevelLockAggregate::f_ForkedChildUnlocked();

		m_ThreadID.f_Store(0, NAtomic::gc_MemoryOrder_Relaxed);
		m_nRecurse = 0;
	}

	void CLowLevelRecursiveLockAggregate::f_ForkedChildLocked()
	{
		CLowLevelLockAggregate::f_ForkedChildLocked();

		m_ThreadID.f_Store(NSys::fg_Thread_GetCurrentUID(), NAtomic::gc_MemoryOrder_Relaxed);
		m_nRecurse = 1;
	}

	void CLowLevelRecursiveLockAggregate::f_Unlock()
	{
		DMibFastCheck(m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == NSys::fg_Thread_GetCurrentUID());

		if ((--m_nRecurse) == 0) [[likely]]
		{
			m_ThreadID.f_Store(0, NAtomic::gc_MemoryOrder_Relaxed);
			CLowLevelLockAggregate::f_Unlock();
		}
	}

	void CLowLevelRecursiveLockAggregate::f_UnlockNoSanitize()
	{
		DMibFastCheck(m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == NSys::fg_Thread_GetCurrentUID());

		if ((--m_nRecurse) == 0) [[likely]]
		{
			m_ThreadID.f_Store(0, NAtomic::gc_MemoryOrder_Relaxed);
			CLowLevelLockAggregate::f_UnlockNoSanitize();
		}
	}

	CLowLevelRecursiveLock::CLowLevelRecursiveLock()
	{
		f_Construct();
	}

	CLowLevelRecursiveLock::~CLowLevelRecursiveLock()
	{
		f_Destruct();
	}

	CScopeUnlockRead::~CScopeUnlockRead()
	{
		mp_fLockFunc(mp_pLock);
	}

	/***************************************************************************************************\
	|¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯|
	| CThread																							|
	|___________________________________________________________________________________________________|
	\***************************************************************************************************/

	CThread::CThread()
	{
		mp_ParentThreadID = 0;
		mp_ReturnValue = 0;
		mp_pThread = nullptr;
		mp_ThreadID = 0;
		mp_pThreadDestroyContext = nullptr;
		mp_bAutoDestroy = false;
		mp_bLockHeld = false;
		mp_ThreadQuitEvent.f_SetSignaled();
	}

	void CThread::fp_Cleanup()
	{
		if (mp_pThreadDestroyContext)
		{
			NSys::fg_Thread_BlockUntilExit(mp_pThreadDestroyContext);
			NSys::fg_Thread_EndDestroy(mp_pThreadDestroyContext);
			mp_pThreadDestroyContext = nullptr;
		}
	}

	bool CThread::f_DestroyThread()
	{
		fg_DeleteObject(NMemory::CDefaultAllocator(), this);
		return true;
	}

	CThread::~CThread()
	{
		DMibLockTyped(CMutual, mp_Lock);
		if (mp_StateAtomic.f_Load() > 1)
		{
			DMibUnlockTyped(CMutual, mp_Lock);
			f_Stop();
		}
		fp_Cleanup();

		if (mp_bLockHeld)
			mp_Lock.f_Unlock();
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
		//fg_GetSys()->f_ThreadLocalCreateThread(NSys::fg_Thread_GetCurrentUID(), pThread->mp_ParentThreadID);
//#endif

		aint Return = pThread->f_Main();

#ifndef DMibPSupportThreadDestroyNotification
		fg_GetSys()->f_ThreadLocalFreeThread();
#endif

		pThread->mp_Lock.f_Lock();

		pThread->mp_ReturnValue = Return;
		pThread->mp_StateAtomic.f_Exchange(EThreadState_Stopped);
		pSystemThread = pThread->mp_pThread;
		pThread->mp_pThread = nullptr;
		pThread->mp_ThreadID = 0;

		if (pThread->mp_bAutoDestroy)
		{
			pThread->mp_bAutoDestroy = false;
			void *pThreadDestroyContext = pThread->mp_pThreadDestroyContext;
			pThread->mp_pThreadDestroyContext = nullptr;

			NSys::fg_Thread_Destroy(pSystemThread);

			if (pThreadDestroyContext)
			{
				NSys::fg_Thread_WillNotBlockUntilExit(pThreadDestroyContext);
				NSys::fg_Thread_EndDestroy(pThreadDestroyContext);
			}

			pThread->mp_bLockHeld = true;
			if (!pThread->f_DestroyThread())
			{
				pThread->mp_bLockHeld = false;
				pThread->mp_Lock.f_Unlock();
			}
		}
		else
		{
			pThread->mp_ThreadQuitEvent.f_SetSignaled();
			NSys::fg_Thread_Destroy(pSystemThread);
			pThread->mp_Lock.f_Unlock();
		}

		return Return;
	}

	/*¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯*\
	|	Function:			Get the state of the thread								|
	|																				|
	|	Returns:			The state of the thread @See(EThreadState)				|
	|																				|
	|	Comments:			Longer_description_not_mandatory						|
	\*_____________________________________________________________________________*/
	EThreadState CThread::f_GetState() const
	{
		return (EThreadState)mp_StateAtomic.f_Load();
	}

	void *CThread::f_GetThread() const
	{
		return mp_pThread;
	}

	umint CThread::f_GetThreadID() const
	{
		return mp_ThreadID;
	}

	bool CThread::f_CallingFromThread()
	{
		return mp_ThreadID == NSys::fg_Thread_GetCurrentUID();
	}

	void CThread::f_Start(EExecutionPriority _Prio, umint _StackSize, umint _Affinity, EThreadCreateFlag _Flags)
	{
		// Make sure that no thread is already running
		{
			DMibLockTyped(CMutual, mp_Lock);
			if (mp_StateAtomic.f_Load() > EThreadState_Stopped)
				DMibError("You cannot start a thread that is already running");
			fp_Cleanup();
			mp_ThreadQuitEvent.f_ResetSignaled();
			mp_ReturnValue = 0;
			mp_ParentThreadID = NSys::fg_Thread_GetCurrentUID();
			mp_bAutoDestroy = (_Flags & EThreadCreateFlag_AutoDestroy) != EThreadCreateFlag_None;
			NStr::CStr ThreadName;
			auto *pName = f_GetThreadNameRaw();
			if (!pName)
			{
				ThreadName = f_GetThreadName();
				pName = ThreadName;
			}

			mp_pThread = NSys::fg_Thread_Create(fsp_ThreadMain, this, _Prio, _StackSize, false, pName, _Affinity, mp_ThreadID);
			mp_pThreadDestroyContext = NSys::fg_Thread_BeginDestroy(mp_pThread);
			mp_StateAtomic.f_Exchange(EThreadState_Running);
		}
	}

	ch8 const *CThread::f_GetThreadNameRaw()
	{
		return nullptr;
	}

	umint CThread::f_Stop(bool _bBlock)
	{
		{
			mp_Lock.f_Lock();

			auto Unlock = fg_OnScopeExit
				(
					[this]
					{
						mp_Lock.f_Unlock();
					}
				)
			;

			switch (mp_StateAtomic.f_Load())
			{
			case EThreadState_None: return 0;
			case EThreadState_Stopped:
				{
					DMibUnlockTyped(CMutual, mp_Lock);
					if (mp_pThreadDestroyContext)
					{
						// This is to guard against thread termination
						NSys::fg_Thread_BlockUntilExit(mp_pThreadDestroyContext);
						NSys::fg_Thread_EndDestroy(mp_pThreadDestroyContext);
						mp_pThreadDestroyContext = nullptr;
					}
				}
				return f_GetReturnValue();
			case EThreadState_Running:
				{
					mp_StateAtomic.f_Exchange(EThreadState_EventWantQuit);
					m_EventWantQuit.f_Signal();
				}
				break;
			}

			if (_bBlock)
			{
				bool bAutoDestroy = mp_bAutoDestroy;
				mp_bAutoDestroy = false;
				auto pDestroyContext = mp_pThreadDestroyContext;
				mp_pThreadDestroyContext = nullptr;

				{
					DMibUnlockTyped(CMutual, mp_Lock);
					mp_ThreadQuitEvent.f_Wait();
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
					mp_bLockHeld = true;
					if (!f_DestroyThread())
					{
						mp_bLockHeld = false;
						mp_Lock.f_Unlock();
					}
				}

				return ReturnValue;
			}
		}

		return 0;
	}

	/*¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯*\
	|	Function:			Returns the return value for a stopped thread			|
	|																				|
	|	Returns:			The return value of the stopped thread					|
	|																				|
	|	Comments:			If the thread isn't in the stopped state this function	|
	|						will throw an exception									|
	\*_____________________________________________________________________________*/
	aint CThread::f_GetReturnValue()
	{
		DMibLockTyped(CMutual, mp_Lock);
		if (mp_StateAtomic.f_Load() == EThreadState_Stopped)
			return mp_ReturnValue;

		DMibError("You are trying to get a return value from a thread that isn't stopped (or has never run)");
	}

	void CThread::f_Suspend()
	{
		DMibLockTyped(CMutual, mp_Lock);
		DMibSafeCheck(mp_pThread, "Must be started");
		NSys::fg_Thread_Suspend(mp_pThread);
	}

	void CThread::f_Resume()
	{
		DMibLockTyped(CMutual, mp_Lock);
		DMibSafeCheck(mp_pThread, "Must be started");
		NSys::fg_Thread_Resume(mp_pThread);
	}

	void CThread::f_SetPriority(EExecutionPriority _Prio)
	{
		DMibLockTyped(CMutual, mp_Lock);
		DMibSafeCheck(mp_pThread, "Must be started");
		NSys::fg_Thread_SetPriority(mp_pThread, _Prio);
	}

	bool CThread::f_TrySetPriority(EExecutionPriority _Prio)
	{
		DMibLockTyped(CMutual, mp_Lock);
		DMibSafeCheck(mp_pThread, "Must be started");
		return NSys::fg_Thread_TrySetPriority(mp_pThread, _Prio);
	}

	void CThread::f_PrepareFork()
	{
		mp_Lock.f_Lock();
		mp_Lock.f_PrepareFork();
		m_EventWantQuit.f_PrepareFork();
		mp_ThreadQuitEvent.f_PrepareFork();
		DMibFastCheck(!mp_bLockHeld);
	}

	void CThread::f_ForkedChild()
	{
		DMibFastCheck(!mp_bLockHeld);
		mp_pThreadDestroyContext = nullptr;
		mp_StateAtomic.f_Exchange(EThreadState_None);
		mp_ThreadQuitEvent.f_ForkedChild();
		m_EventWantQuit.f_ForkedChild();
		mp_Lock.f_ForkedChild();
		mp_Lock.f_Unlock();
	}

	void CThread::f_ForkedParent()
	{
		DMibFastCheck(!mp_bLockHeld);
		mp_ThreadQuitEvent.f_ForkedParent();
		m_EventWantQuit.f_ForkedParent();
		mp_Lock.f_ForkedParent();
		mp_Lock.f_Unlock();
	}

	CThreadSpinWaiter::CThreadSpinWaiter(umint _nYieldsBeforeSleep)
		: m_nYieldsBeforeSleep(_nYieldsBeforeSleep)
	{
	}

	inline_never void CThreadSpinWaiter::f_WaitSlow()
	{
		if (m_nWaits < m_nYieldsBeforeSleep)
		{
			NSys::fg_Thread_Yield();
			return;
		}

		NSys::fg_Thread_SmallestSleep();
	}
}

namespace NMib::NStorage
{
	template <>
	TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, smint>::~TCIntrusiveRefCount()
	{
#if defined(DMibContractConfigure_CheckEnabled) || DMibConfig_RefCountDebugging
		smint RefCount = f_Get();
#endif
		DMibCheck(RefCount == 0 || RefCount == -1)(RefCount);
		DIfRefCountDebugging(if (RefCount == 0) m_Debug.f_Destruct());
	}

	template <>
	TCIntrusiveRefCount<ESharedPointerOption_None, smint>::~TCIntrusiveRefCount()
	{
#if defined(DMibContractConfigure_CheckEnabled) || DMibConfig_RefCountDebugging
		int32 RefCount = f_Get();
#endif
		DMibCheck(RefCount == 0 || RefCount == -1)(RefCount);
		DIfRefCountDebugging(if (RefCount == 0) m_Debug.f_Destruct());
	}

	template <>
	TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, int32>::~TCIntrusiveRefCount()
	{
#if defined(DMibContractConfigure_CheckEnabled) || DMibConfig_RefCountDebugging
		int32 RefCount = f_Get();
#endif
		DMibCheck(RefCount == 0 || RefCount == -1)(RefCount);
		DIfRefCountDebugging(if (RefCount == 0) m_Debug.f_Destruct());
	}

	template <>
	TCIntrusiveRefCount<ESharedPointerOption_None, int32>::~TCIntrusiveRefCount()
	{
#if defined(DMibContractConfigure_CheckEnabled) || DMibConfig_RefCountDebugging
		int32 RefCount = f_Get();
#endif
		DMibCheck(RefCount == 0 || RefCount == -1)(RefCount);
		DIfRefCountDebugging(if (RefCount == 0) m_Debug.f_Destruct());
	}

#ifdef DCompiler_MSVC_Workaround
	namespace NPrivate
	{
		CSharedPointerCounterVirtualBase::~CSharedPointerCounterVirtualBase()
		{
		}
	}
#endif
}
