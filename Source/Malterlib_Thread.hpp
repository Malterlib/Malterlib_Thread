// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

namespace NMib::NThread
{
	constexpr CSemaphoreAggregate::CSemaphoreAggregate(EAggregateInitialization _Init)
		: CSemaphoreAggregate{_Init, 0}
	{
	}

	constexpr CSemaphoreAggregate::CSemaphoreAggregate(EAggregateInitialization _Init, uint32 _Maximum)
		: m_Data{0}
		, m_Maximum{_Maximum}
	{
	}

	// Waiters futex-wait on the count half of m_Data
	inline uint32 volatile *CSemaphoreAggregate::fp_GetFutexWord()
	{
#ifdef DMibPLittleEndian
		return (uint32 volatile *)&m_Data;
#else
		return (uint32 volatile *)&m_Data + 1;
#endif
	}

	inline void CSemaphoreAggregate::f_Construct(aint _Initial, aint _Max)
	{
		m_Data.f_Store((uint64)(uint32)fg_Min<umint>((umint)_Initial, TCLimitsInt<uint32>::mc_Max), NAtomic::gc_MemoryOrder_Relaxed);
		m_Maximum = (uint32)fg_Min<umint>((umint)_Max, TCLimitsInt<uint32>::mc_Max);
	}

	inline void CSemaphoreAggregate::f_Destruct()
	{
	}

	inline void CSemaphoreAggregate::f_Signal(int _nToSignal)
	{
		uint64 Old = m_Data.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
		uint32 Delta;
		while (true)
		{
			umint Target = fg_Min((umint)(uint32)Old + (umint)_nToSignal, (umint)m_Maximum);
			Delta = (uint32)Target - (uint32)Old;
			if (Delta == 0)
				return;

			// Release publishes the tokens; the old value carries the waiter
			// count atomically with the hand-over, and nothing but futex wakes
			// may happen after this succeeds (see class comment)
			if (m_Data.f_CompareExchangeWeak(Old, (Old & ~mcp_ValueMask) | (uint64)(uint32)Target, NAtomic::gc_MemoryOrder_Release, NAtomic::gc_MemoryOrder_Relaxed))
				break;
		}

		uint32 nWaiters = (uint32)(Old >> 32);
		if (nWaiters != 0) [[unlikely]]
			fp_WakeSlow(Delta, nWaiters);
	}

	inline void CSemaphoreAggregate::f_Signal()
	{
		f_Signal(1);
	}

	inline bool CSemaphoreAggregate::f_TryWait()
	{
		uint64 Old = m_Data.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
		while ((uint32)Old > 0)
		{
			if (m_Data.f_CompareExchangeWeak(Old, Old - 1, NAtomic::gc_MemoryOrder_Acquire, NAtomic::gc_MemoryOrder_Relaxed))
				return true;
		}
		return false;
	}

	inline void CSemaphoreAggregate::f_Wait()
	{
		if (f_TryWait()) [[likely]]
			return;

		fp_WaitSlow();
	}

	// Returns true if the wait timed out
	inline bool CSemaphoreAggregate::f_WaitTimeout(fp64 _Timeout)
	{
		if (f_TryWait())
			return false;

		return fp_WaitTimeoutSlow(_Timeout);
	}

	inline uint32 volatile *CSemaphoreAggregate::f_GetExternalFutexWord()
	{
		return fp_GetFutexWord();
	}

	inline void CSemaphoreAggregate::f_ExternalWaiterRegister()
	{
		m_Data.f_FetchAdd(mcp_WaiterOne, NAtomic::gc_MemoryOrder_Relaxed);
	}

	inline void CSemaphoreAggregate::f_ExternalWaiterUnregister()
	{
		m_Data.f_FetchSub(mcp_WaiterOne, NAtomic::gc_MemoryOrder_Relaxed);
	}

	constexpr CEventAutoResetAggregate::CEventAutoResetAggregate(EAggregateInitialization _Init)
		: CSemaphoreAggregate{_Init, 1}
	{
	}

	inline void CEventAutoResetAggregate::f_Construct()
	{
		CSemaphoreAggregate::f_Construct(0, 1);
	}

	constexpr CEventAggregate::CEventAggregate(EAggregateInitialization _Init)
		: m_State{0}
	{
	}

	inline void CEventAggregate::f_Construct(bool _bInitialSignal)
	{
		m_State.f_Store(_bInitialSignal ? mcp_FlagSignaled : 0u, NAtomic::gc_MemoryOrder_Relaxed);
	}

	inline void CEventAggregate::f_Destruct()
	{
	}

	inline void CEventAggregate::f_SetSignaled()
	{
		uint32 Old = m_State.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
		while (true)
		{
			if (Old & mcp_FlagSignaled)
				return;

			// Advance the generation, set the signaled flag and take the waiters
			// flag in one operation; the old value carries the waiters flag and
			// nothing but futex wakes may happen after this succeeds (see class
			// comment). The waiter bits sit below mcp_GenerationOne, so the
			// generation add cannot carry into them.
			if
			(
				m_State.f_CompareExchangeWeak(Old, ((Old + mcp_GenerationOne) & ~mcp_FlagWaiters) | mcp_FlagSignaled, NAtomic::gc_MemoryOrder_AcquireRelease, NAtomic::gc_MemoryOrder_Relaxed)
			)
			{
				break;
			}
		}

		if (Old & mcp_FlagWaiters) [[unlikely]]
			fp_WakeAllSlow();
	}

	inline void CEventAggregate::f_ResetSignaled()
	{
		// Keep the waiters flag and the generation so waiters parked at an
		// earlier signal still complete
		m_State.f_FetchAnd(~mcp_FlagSignaled, NAtomic::gc_MemoryOrder_AcquireRelease);
	}

	inline void CEventAggregate::f_Wait()
	{
		if (f_TryWait()) [[likely]]
			return;

		fp_WaitSlow();
	}

	// Returns true if the wait timed out
	inline bool CEventAggregate::f_WaitTimeout(fp64 _Timeout)
	{
		if (f_TryWait())
			return false;

		return fp_WaitTimeoutSlow(_Timeout);
	}

	inline bool CEventAggregate::f_TryWait()
	{
		return (m_State.f_Load(NAtomic::gc_MemoryOrder_Acquire) & mcp_FlagSignaled) != 0;
	}

	inline bool CEventAggregate::f_HasWaiters() const
	{
		return (m_State.f_Load(NAtomic::gc_MemoryOrder_Relaxed) & mcp_FlagWaiters) != 0;
	}

	inline void CNoLock::f_Construct()
	{
	}

	inline void CNoLock::f_Destruct()
	{
	}

	inline void CNoLock::f_Lock()
	{
	}

	inline void CNoLock::f_Unlock()
	{
	}

	template <typename t_CLock>
	inline TCScopeLock<t_CLock>::TCScopeLock(t_CLock &_Lock)
		: mp_Lock(_Lock)
	{
		_Lock.f_Lock();
	}

	template <typename t_CLock>
	inline TCScopeLock<t_CLock>::~TCScopeLock()
	{
		mp_Lock.f_Unlock();
	}

	template <typename t_CLock>
	inline TCScopeUnlock<t_CLock>::TCScopeUnlock(t_CLock &_Lock)
		: mp_Lock(_Lock)
	{
		_Lock.f_Unlock();
	}

	template <typename t_CLock>
	inline TCScopeUnlock<t_CLock>::~TCScopeUnlock()
	{
		mp_Lock.f_Lock();
	}

	template <typename t_CLock>
	inline TCScopeLockRead<t_CLock>::TCScopeLockRead(t_CLock &_Lock)
		: mp_Lock(_Lock)
	{
		_Lock.f_LockRead();
	}

	template <typename t_CLock>
	inline TCScopeLockRead<t_CLock>::~TCScopeLockRead()
	{
		mp_Lock.f_UnlockRead();
	}

	template <typename t_CLock>
	inline TCScopeLockReadLocked<t_CLock>::TCScopeLockReadLocked(t_CLock &_Lock)
		: mp_Lock(_Lock)
	{
		if (_Lock.f_OwnsLock())
			mp_bWasLocked = true;
		else
		{
			mp_bWasLocked = false;
			_Lock.f_LockRead();
		}
	}

	template <typename t_CLock>
	inline TCScopeLockReadLocked<t_CLock>::~TCScopeLockReadLocked()
	{
		if (!mp_bWasLocked)
			mp_Lock.f_UnlockRead();
	}

	template <typename t_CLock>
	inline TCScopeUnlockRead<t_CLock>::TCScopeUnlockRead(t_CLock &_Lock)
		: mp_Lock(_Lock)
	{
		_Lock.f_UnlockRead();
	}

	template <typename t_CLock>
	inline TCScopeUnlockRead<t_CLock>::~TCScopeUnlockRead()
	{
		mp_Lock.f_LockRead();
	}

	template <typename t_CLock>
	inline TCMovableScopeLock<t_CLock>::TCMovableScopeLock(t_CLock &_Lock)
		: mp_pLock(&_Lock)
	{
		_Lock.f_Lock();
	}

	template <typename t_CLock>
	inline TCMovableScopeLock<t_CLock>::TCMovableScopeLock(t_CLock &_Lock, bool _bTryLock)
		: mp_pLock(nullptr)
	{
		if (_bTryLock)
		{
			if (_Lock.f_TryLock())
				mp_pLock = &_Lock;
		}
		else
		{
			mp_pLock = &_Lock;
			_Lock.f_Lock();
		}
	}

	template <typename t_CLock>
	inline TCMovableScopeLock<t_CLock>::~TCMovableScopeLock()
	{
		if (mp_pLock)
			mp_pLock->f_Unlock();
	}

	template <typename t_CLock>
	inline TCMovableScopeLock<t_CLock>::TCMovableScopeLock(TCMovableScopeLock &&_Other)
		: mp_pLock(_Other.mp_pLock)
	{
		_Other.mp_pLock = nullptr;
	}

	template <typename t_CLock>
	inline TCMovableScopeLock<t_CLock> &TCMovableScopeLock<t_CLock>::operator = (TCMovableScopeLock &&_Other)
	{
		mp_pLock = _Other.mp_pLock;
		_Other.mp_pLock = nullptr;
		return *this;
	}

	template <typename t_CLock>
	inline bool TCMovableScopeLock<t_CLock>::f_IsLocked() const
	{
		return mp_pLock != nullptr;
	}

	template <typename t_CLock>
	inline TCMovableScopeUnlock<t_CLock>::TCMovableScopeUnlock(t_CLock &_Lock)
		: mp_pLock(&_Lock)
	{
		_Lock.f_Unlock();
	}

	template <typename t_CLock>
	inline TCMovableScopeUnlock<t_CLock>::~TCMovableScopeUnlock()
	{
		if (mp_pLock)
			mp_pLock->f_Lock();
	}

	template <typename t_CLock>
	inline TCMovableScopeUnlock<t_CLock>::TCMovableScopeUnlock(TCMovableScopeUnlock &&_Other)
		: mp_pLock(_Other.mp_pLock)
	{
		_Other.mp_pLock = nullptr;
	}

	template <typename t_CLock>
	inline TCMovableScopeUnlock<t_CLock> &TCMovableScopeUnlock<t_CLock>::operator = (TCMovableScopeUnlock &&_Other)
	{
		mp_pLock = _Other.mp_pLock;
		_Other.mp_pLock = nullptr;
		return *this;
	}

	template <typename t_CLock>
	inline TCMovableScopeLockRead<t_CLock>::TCMovableScopeLockRead(t_CLock &_Lock)
		: mp_pLock(&_Lock)
	{
		_Lock.f_LockRead();
	}

	template <typename t_CLock>
	inline TCMovableScopeLockRead<t_CLock>::~TCMovableScopeLockRead()
	{
		if (mp_pLock)
			mp_pLock->f_UnlockRead();
	}

	template <typename t_CLock>
	inline TCMovableScopeLockRead<t_CLock>::TCMovableScopeLockRead(TCMovableScopeLockRead &&_Other)
		: mp_pLock(_Other.mp_pLock)
	{
		_Other.mp_pLock = nullptr;
	}

	template <typename t_CLock>
	inline TCMovableScopeLockRead<t_CLock> &TCMovableScopeLockRead<t_CLock>::operator = (TCMovableScopeLockRead &&_Other)
	{
		mp_pLock = _Other.mp_pLock;
		_Other.mp_pLock = nullptr;
		return *this;
	}

	template <typename t_CLock>
	inline TCMovableScopeUnlockRead<t_CLock>::TCMovableScopeUnlockRead(t_CLock &_Lock)
		: mp_pLock(&_Lock)
	{
		_Lock.f_UnlockRead();
	}

	template <typename t_CLock>
	inline TCMovableScopeUnlockRead<t_CLock>::~TCMovableScopeUnlockRead()
	{
		if (mp_pLock)
			mp_pLock->f_LockRead();
	}

	template <typename t_CLock>
	inline TCMovableScopeUnlockRead<t_CLock>::TCMovableScopeUnlockRead(TCMovableScopeUnlockRead &&_Other)
		: mp_pLock(_Other.mp_pLock)
	{
		_Other.mp_pLock = nullptr;
	}

	template <typename t_CLock>
	inline TCMovableScopeUnlockRead<t_CLock> &TCMovableScopeUnlockRead<t_CLock>::operator = (TCMovableScopeUnlockRead &&_Other)
	{
		mp_pLock = _Other.mp_pLock;
		_Other.mp_pLock = nullptr;
		return *this;
	}

	constexpr CLowLevelLockAggregate::CLowLevelLockAggregate(EAggregateInitialization _Init)
		: m_Lock{0}
#if DMibEnableSafeCheck > 0
		, m_ThreadID{0}
		, m_AlternateThreadID{0}
		, m_nForked{0}
#endif
	{
	}

	constexpr CLowLevelLockAggregate::CLowLevelLockAggregate()
		: m_Lock{0}
#if DMibEnableSafeCheck > 0
		, m_ThreadID{0}
		, m_AlternateThreadID{0}
		, m_nForked{0}
#endif
	{
	}

	template <typename t_CEvent>
	inline inline_never void TCMutualSimpleAggregate<t_CEvent>::fp_WaitForIt()
	{
		CDisableLazyCheckoutReturnScope DisableLazy;
		m_Event.f_Wait();
	}

	template <typename t_CEvent>
	constexpr TCMutualSimpleAggregate<t_CEvent>::TCMutualSimpleAggregate(EAggregateInitialization _Init)
		: m_nLocked{0}
		, m_Event{_Init}
	{
	}

	template <typename t_CEvent>
	inline TCMutualSimpleAggregate<t_CEvent>::TCMutualSimpleAggregate()
	{
	}

	template <typename t_CEvent>
	inline void TCMutualSimpleAggregate<t_CEvent>::f_Construct()
	{
		DMibSanitizerAnnotate_MutexCreate(this, __tsan_mutex_not_static);

		m_Event.f_Construct();
		m_nLocked.f_Store(0, NAtomic::gc_MemoryOrder_Relaxed);
	}

	template <typename t_CEvent>
	inline void TCMutualSimpleAggregate<t_CEvent>::f_Destruct()
	{
		DMibSanitizerAnnotate_MutexDestroy(this, 0);
		m_Event.f_Destruct();
	}

	template <typename t_CEvent>
	inline void TCMutualSimpleAggregate<t_CEvent>::f_Lock()
	{
		DMibSanitizerAnnotate_MutexPreLock(this, 0);
		if (m_nLocked.f_FetchAdd(1, NAtomic::gc_MemoryOrder_Acquire) > 0)
			fp_WaitForIt();
		DMibSanitizerAnnotate_MutexPostLock(this, 0, 1);
	}

	template <typename t_CEvent>
	inline void TCMutualSimpleAggregate<t_CEvent>::f_Unlock()
	{
		DMibSanitizerAnnotate_MutexPreUnlock(this, 0);
		if (m_nLocked.f_FetchSub(1, NAtomic::gc_MemoryOrder_Release) > 1)
		{
			// Someone is waiting
			m_Event.f_Signal();
		}
		DMibSanitizerAnnotate_MutexPostUnlock(this, 0);
	}

	template <typename t_CEvent>
	inline void TCMutualSimpleAggregate<t_CEvent>::f_LockRead()
	{
		f_Lock();
	}

	template <typename t_CEvent>
	inline void TCMutualSimpleAggregate<t_CEvent>::f_UnlockRead()
	{
		f_Unlock();
	}

	template <typename t_CEvent>
	inline TCMutualSimple<t_CEvent>::TCMutualSimple()
	{
		TCMutualSimpleAggregate<t_CEvent>::f_Construct();
	}

	template <typename t_CEvent>
	inline TCMutualSimple<t_CEvent>::~TCMutualSimple()
	{
		TCMutualSimpleAggregate<t_CEvent>::f_Destruct();
	}

	inline_always void CThreadSpinWaiter::f_Wait()
	{
		if (++m_nWaits < 100)
		{
			yield_cpu;
			return;
		}

		f_WaitSlow();
	}

	template <typename t_CEvent, bool t_bAllowRecursive>
	inline void TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_PrepareFork()
	{
		DMibFastCheck(m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == NSys::fg_Thread_GetCurrentUID()); // Must already be locked
		DMibFastCheck(m_nRecurse == 1); // Must already be locked

		m_Event.f_PrepareFork();
	}

	template <typename t_CEvent, bool t_bAllowRecursive>
	inline void TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_ForkedChild()
	{
		m_Event.f_ForkedChild();

		// Clear out any other threads that might have been waiting for the lock
		//m_nRecurse = 1; // This should be safe to keep
		m_ThreadID.f_Store(NSys::fg_Thread_GetCurrentUID(), NAtomic::gc_MemoryOrder_Relaxed);
		m_nLocked.f_FetchAnd(~mcp_AtomicMask);
		m_nLocked.f_FetchAdd(1);
	}

	template <typename t_CEvent, bool t_bAllowRecursive>
	inline void TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_ForkedParent()
	{
		m_Event.f_ForkedParent();
	}

	template <typename t_CEvent, bool t_bAllowRecursive>
	inline inline_never void TCMutualAggregate<t_CEvent, t_bAllowRecursive>::fp_WaitForIt()
	{
		CDisableLazyCheckoutReturnScope DisableLazy;
		m_Event.f_Wait();
	}

	template <typename t_CEvent, bool t_bAllowRecursive>
	inline inline_never void TCMutualAggregate<t_CEvent, t_bAllowRecursive>::fp_SignalIt()
	{
		// Someone is waiting
		m_Event.f_Signal();
	}

	template <typename t_CEvent, bool t_bAllowRecursive>
	constexpr TCMutualAggregate<t_CEvent, t_bAllowRecursive>::TCMutualAggregate(EAggregateInitialization _Init)
		: m_nLocked{0}
		, m_ThreadID{0}
		, m_nRecurse{0}
		, m_Event{_Init}
#if DMibEnableSafeCheck > 0
		, m_AlternateThreadID(0)
#endif
	{
	}

	template <typename t_CEvent, bool t_bAllowRecursive>
	inline TCMutualAggregate<t_CEvent, t_bAllowRecursive>::TCMutualAggregate()
	{
	}

	template <typename t_CEvent, bool t_bAllowRecursive>
	inline void TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_Construct()
	{
		// m_Event.f_Construct();
		// m_nLocked.f_Construct(umint(2) << (EAtomicBits-2));
		DMibSanitizerAnnotate_MutexCreate(this, __tsan_mutex_write_reentrant | __tsan_mutex_not_static);

		m_Event.f_Construct();
		m_nLocked.f_Store(0, NAtomic::gc_MemoryOrder_Relaxed);
		m_nRecurse = 0;
		m_ThreadID = 0;
#if DMibEnableSafeCheck > 0
		m_AlternateThreadID = 0;
#endif
	}

	template <typename t_CEvent, bool t_bAllowRecursive>
	inline void TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_Destruct()
	{
		DMibSanitizerAnnotate_MutexDestroy(this, 0);

		DMibFastCheck((m_nLocked.f_Load() & mcp_AtomicMask) == 0);
		DMibFastCheck(m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == 0);
		m_Event.f_Destruct();
	}

	template <typename t_CEvent, bool t_bAllowRecursive>
	inline inline_never bool TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_TryLock()
	{
		DMibSanitizerAnnotate_MutexPreLock(this, __tsan_mutex_write_reentrant | __tsan_mutex_try_lock);

		umint CurrentThread = NSys::fg_Thread_GetCurrentUID();

		if (m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == CurrentThread)
		{
			++m_nRecurse;
			DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant | __tsan_mutex_try_lock, 1);
			return true;
		}

		umint Original = m_nLocked.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
		if (Original & mcp_AtomicMask)
		{
			DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant | __tsan_mutex_try_lock | __tsan_mutex_try_lock_failed, 1);
			return false;
		}

		if (m_nLocked.f_CompareExchangeStrong(Original, Original + 1, NAtomic::gc_MemoryOrder_Acquire, NAtomic::gc_MemoryOrder_Relaxed))
		{
			m_ThreadID.f_Store(CurrentThread, NAtomic::gc_MemoryOrder_Relaxed);
			m_nRecurse = 1;
			DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant | __tsan_mutex_try_lock, 1);

			return true;
		}

		DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant | __tsan_mutex_try_lock | __tsan_mutex_try_lock_failed, 1);
		return false;
	}

	template <typename t_CEvent, bool t_bAllowRecursive>
	inline bool TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_IsLocked()
	{
		return m_ThreadID.f_Load() != 0;
	}

	template <typename t_CEvent, bool t_bAllowRecursive>
	inline inline_never bool TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_TryLock(aint _SpinCount)
	{
		DMibSanitizerAnnotate_MutexPreLock(this, __tsan_mutex_write_reentrant | __tsan_mutex_try_lock);

		umint CurrentThread = NSys::fg_Thread_GetCurrentUID();

		if (m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == CurrentThread)
		{
			DMibFastCheck(t_bAllowRecursive);
			++m_nRecurse;

			DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant | __tsan_mutex_try_lock, 1);
			return true;
		}

		while (_SpinCount--)
		{
			umint Original = m_nLocked.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
			if (Original & mcp_AtomicMask)
				continue;
			if (m_nLocked.f_CompareExchangeWeak(Original, Original + 1, NAtomic::gc_MemoryOrder_Acquire, NAtomic::gc_MemoryOrder_Relaxed))
			{
				m_ThreadID.f_Store(CurrentThread, NAtomic::gc_MemoryOrder_Relaxed);
#if DMibEnableSafeCheck > 0
				m_AlternateThreadID = NSys::fg_Thread_GetCurrentUIDAlternate();
#endif
				m_nRecurse = 1;

				DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant | __tsan_mutex_try_lock, 1);
				return true;
			}
		}

		DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant | __tsan_mutex_try_lock | __tsan_mutex_try_lock_failed, 1);
		return false;
	}

	template <typename t_CEvent, bool t_bAllowRecursive>
	inline inline_never void TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_SetLockForThread(umint _ThreadID)
	{
		umint CurrentThread = _ThreadID;

		if (m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == CurrentThread)
		{
			DMibFastCheck(t_bAllowRecursive);
			++m_nRecurse;
			return;
		}

		// Try to take the lock
		umint nLockedValue = m_nLocked.f_FetchAdd(1);
		umint nLocked = nLockedValue & mcp_AtomicMask;

		if (nLocked > 0)
			fp_WaitForIt();

		m_ThreadID.f_Store(CurrentThread, NAtomic::gc_MemoryOrder_Relaxed);
#if DMibEnableSafeCheck > 0
		m_AlternateThreadID = NSys::fg_Thread_GetCurrentUIDAlternate();
#endif
		m_nRecurse = 1;
	}

	template <typename t_CEvent, bool t_bAllowRecursive>
	inline inline_never void TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_Lock()
	{
		DMibSanitizerAnnotate_MutexPreLock(this, __tsan_mutex_write_reentrant);

		umint CurrentThread = NSys::fg_Thread_GetCurrentUID();

		if (m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == CurrentThread)
		{
			DMibFastCheck(t_bAllowRecursive);
			++m_nRecurse;
			DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant, 1);
			return;
		}

		// Try to take the lock
		umint nLockedValue = m_nLocked.f_FetchAdd(1, NAtomic::gc_MemoryOrder_Acquire);
		umint nLocked = nLockedValue & mcp_AtomicMask;

		if (nLocked > 0) [[unlikely]]
			fp_WaitForIt();

		m_ThreadID.f_Store(CurrentThread, NAtomic::gc_MemoryOrder_Relaxed);
#if DMibEnableSafeCheck > 0
		m_AlternateThreadID = NSys::fg_Thread_GetCurrentUIDAlternate();
#endif

		m_nRecurse = 1;

		DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant, 1);
	}

	template <typename t_CEvent, bool t_bAllowRecursive>
	inline inline_never void TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_Lock(aint _SpinCount)
	{
		DMibSanitizerAnnotate_MutexPreLock(this, __tsan_mutex_write_reentrant);

		umint CurrentThread = NSys::fg_Thread_GetCurrentUID();

		if (m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == CurrentThread)
		{
			DMibFastCheck(t_bAllowRecursive);
			++m_nRecurse;

			DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant, 1);
			return;
		}

		while (_SpinCount--) [[likely]]
		{
			umint Original = m_nLocked.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
			if (Original & mcp_AtomicMask)
			{
				yield_cpu;
				continue;
			}

			if (m_nLocked.f_CompareExchangeWeak(Original, Original + 1, NAtomic::gc_MemoryOrder_Acquire, NAtomic::gc_MemoryOrder_Relaxed)) [[likely]]
			{
				m_ThreadID.f_Store(CurrentThread, NAtomic::gc_MemoryOrder_Relaxed);
#if DMibEnableSafeCheck > 0
				m_AlternateThreadID = NSys::fg_Thread_GetCurrentUIDAlternate();
#endif
				m_nRecurse = 1;

				DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant, 1);
				return;
			}
			yield_cpu;
		}

		// Try to take the lock
		umint nLockedValue = m_nLocked.f_FetchAdd(1, NAtomic::gc_MemoryOrder_Acquire);
		umint nLocked = nLockedValue & mcp_AtomicMask;

		if (nLocked > 0)
			fp_WaitForIt();

		m_ThreadID.f_Store(CurrentThread, NAtomic::gc_MemoryOrder_Relaxed);
#if DMibEnableSafeCheck > 0
		m_AlternateThreadID = NSys::fg_Thread_GetCurrentUIDAlternate();
#endif
		m_nRecurse = 1;

		DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant, 1);
	}

	template <typename t_CEvent, bool t_bAllowRecursive>
	inline bool TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_OwnsLock() const
	{
		umint CurrentThread = NSys::fg_Thread_GetCurrentUID();
		return m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == CurrentThread;
	}

	template <typename t_CEvent, bool t_bAllowRecursive>
	inline bool TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_Contended() const
	{
		DMibFastCheck(f_OwnsLock());
		return (m_nLocked.f_Load(NAtomic::gc_MemoryOrder_Relaxed) & mcp_AtomicMask) > 1;
	}

	template <typename t_CEvent, bool t_bAllowRecursive>
	inline inline_never void TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_Unlock()
	{
		DMibSanitizerAnnotate_MutexPreUnlock(this, 0);

		DMibFastCheck(m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == NSys::fg_Thread_GetCurrentUID());

		if ((--m_nRecurse) == 0) [[likely]]
		{
			m_ThreadID.f_Store(0, NAtomic::gc_MemoryOrder_Relaxed);
			umint nLockedValue = m_nLocked.f_FetchSub(1, NAtomic::gc_MemoryOrder_AcquireRelease); // Acquire for m_Event
			if ((nLockedValue & mcp_AtomicMask) > 1)
				fp_SignalIt();
		}

		DMibSanitizerAnnotate_MutexPostUnlock(this, 0);
	}

	template <typename t_CEvent, bool t_bAllowRecursive>
	inline void TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_LockRead()
	{
		f_Lock();
	}

	template <typename t_CEvent, bool t_bAllowRecursive>
	inline void TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_UnlockRead()
	{
		f_Unlock();
	}

	template <typename t_CEvent, umint t_nSpins, bool t_bAllowRecursive>
	constexpr TCMutualSpinAggregate<t_CEvent, t_nSpins, t_bAllowRecursive>::TCMutualSpinAggregate(EAggregateInitialization _Init)
		: TCMutualAggregate<t_CEvent, t_bAllowRecursive>{_Init}
	{
	}

	template <typename t_CEvent, umint t_nSpins, bool t_bAllowRecursive>
	inline TCMutualSpinAggregate<t_CEvent, t_nSpins, t_bAllowRecursive>::TCMutualSpinAggregate()
	{
	}

	template <typename t_CEvent, umint t_nSpins, bool t_bAllowRecursive>
	inline bool TCMutualSpinAggregate<t_CEvent, t_nSpins, t_bAllowRecursive>::f_TryLock()
	{
		return TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_TryLock(t_nSpins);
	}

	template <typename t_CEvent, umint t_nSpins, bool t_bAllowRecursive>
	inline void TCMutualSpinAggregate<t_CEvent, t_nSpins, t_bAllowRecursive>::f_Lock()
	{
		return TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_Lock(t_nSpins);
	}

	template <typename t_CEvent, umint t_nSpins, bool t_bAllowRecursive>
	inline void TCMutualSpinAggregate<t_CEvent, t_nSpins, t_bAllowRecursive>::f_LockRead()
	{
		f_Lock();
	}

	template <typename t_CEvent, bool t_bAllowRecursive>
	inline TCMutual<t_CEvent, t_bAllowRecursive>::TCMutual()
	{
		TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_Construct();
	}

	template <typename t_CEvent, bool t_bAllowRecursive>
	inline TCMutual<t_CEvent, t_bAllowRecursive>::~TCMutual()
	{
		TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_Destruct();
	}

	template <typename t_CEvent, umint t_nSpins, bool t_bAllowRecursive>
	inline TCMutualSpin<t_CEvent, t_nSpins, t_bAllowRecursive>::TCMutualSpin()
	{
		TCMutualSpinAggregate<t_CEvent, t_nSpins, t_bAllowRecursive>::f_Construct();
	}

	template <typename t_CEvent, umint t_nSpins, bool t_bAllowRecursive>
	inline TCMutualSpin<t_CEvent, t_nSpins, t_bAllowRecursive>::~TCMutualSpin()
	{
		TCMutualSpinAggregate<t_CEvent, t_nSpins, t_bAllowRecursive>::f_Destruct();
	}

	template <typename t_CEventAutoreset, typename t_CBase>
	constexpr TCMutualManyReadAggregate<t_CEventAutoreset, t_CBase>::TCMutualManyReadAggregate(EAggregateInitialization _Init)
		: t_CBase{_Init}
	{
	}

	template <typename t_CEventAutoreset, typename t_CBase>
	inline TCMutualManyReadAggregate<t_CEventAutoreset, t_CBase>::TCMutualManyReadAggregate()
	{
	}

	template <typename t_CEventAutoreset, typename t_CBase>
	inline void TCMutualManyReadAggregate<t_CEventAutoreset, t_CBase>::f_Construct()
	{
		t_CBase::f_Construct();
		m_nReading.f_Store(0, NAtomic::gc_MemoryOrder_Relaxed);
		m_WriteOkEvent.f_Construct();
#if DMibEnableSafeCheck > 0
		m_nReadingDebugCheck.f_Store(0, NAtomic::gc_MemoryOrder_Relaxed);
#endif
	}

	template <typename t_CEventAutoreset, typename t_CBase>
	inline void TCMutualManyReadAggregate<t_CEventAutoreset, t_CBase>::f_Destruct()
	{
		DMibFastCheck((m_nReading.f_Load() & mc_nReadingMask) == 0);
		m_WriteOkEvent.f_Destruct();
		t_CBase::f_Destruct();
	}

	template <typename t_CEventAutoreset, typename t_CBase>
	inline void TCMutualManyReadAggregate<t_CEventAutoreset, t_CBase>::f_PrepareFork()
	{
		t_CBase::f_PrepareFork();
		m_WriteOkEvent.f_PrepareFork();
	}

	template <typename t_CEventAutoreset, typename t_CBase>
	inline void TCMutualManyReadAggregate<t_CEventAutoreset, t_CBase>::f_ForkedChild()
	{
		m_WriteOkEvent.f_ForkedChild();
		m_nReading.f_Store(mc_FlagReadingNotAllowed);
#if DMibEnableSafeCheck > 0
		m_nReadingDebugCheck.f_Store(0, NAtomic::gc_MemoryOrder_Relaxed);
#endif

		t_CBase::f_ForkedChild();
	}

	template <typename t_CEventAutoreset, typename t_CBase>
	inline void TCMutualManyReadAggregate<t_CEventAutoreset, t_CBase>::f_ForkedParent()
	{
		m_WriteOkEvent.f_ForkedParent();
		t_CBase::f_ForkedParent();
	}

	template <typename t_CEventAutoreset, typename t_CBase>
	inline inline_never void TCMutualManyReadAggregate<t_CEventAutoreset, t_CBase>::f_LockRead()
	{
		umint CurrentThread = NMib::NSys::fg_Thread_GetCurrentUID();

		if (t_CBase::m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == CurrentThread)
		{
			DMibSanitizerAnnotate_MutexPreLock(this, __tsan_mutex_write_reentrant);
			++t_CBase::m_nRecurse;
			DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant, 1);
			return;
		}

		DMibSanitizerAnnotate_MutexPreLock(this, __tsan_mutex_write_reentrant | __tsan_mutex_read_lock);

	RestartLock:
		uint32 nReading = m_nReading.f_FetchAdd(1, NAtomic::gc_MemoryOrder_Acquire);
		if (nReading & mc_FlagReadingNotAllowed)
		{
			f_UnlockReadInternal();
			{
				CDisableLazyCheckoutReturnScope DisableLazy;

				uint32 Current = m_nReading.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
				while (Current & mc_FlagReadingNotAllowed)
				{
					if (!(Current & mc_FlagReadersWaiting))
					{
						if (!m_nReading.f_CompareExchangeWeak(Current, Current | mc_FlagReadersWaiting, NAtomic::gc_MemoryOrder_Relaxed, NAtomic::gc_MemoryOrder_Relaxed))
							continue;

						Current |= mc_FlagReadersWaiting;
					}

					NSys::fg_Futex_Wait((uint32 volatile *)&m_nReading, Current);
					Current = m_nReading.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
				}
			}
			goto RestartLock;
		}
#if DMibEnableSafeCheck > 0
		m_nReadingDebugCheck.f_FetchAdd(1, NAtomic::gc_MemoryOrder_Relaxed);
#endif

		DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant | __tsan_mutex_read_lock, 1);
	}

	template <typename t_CEventAutoreset, typename t_CBase>
	inline inline_never void TCMutualManyReadAggregate<t_CEventAutoreset, t_CBase>::f_UnlockReadInternal()
	{
		uint32 nReading = m_nReading.f_FetchSub(1, NAtomic::gc_MemoryOrder_AcquireRelease);
		if ((nReading & (mc_FlagReadingNotAllowed | mc_nReadingMask)) == (mc_FlagReadingNotAllowed | 1))
			m_WriteOkEvent.f_Signal();
	}

	template <typename t_CEventAutoreset, typename t_CBase>
	inline inline_never void TCMutualManyReadAggregate<t_CEventAutoreset, t_CBase>::f_UnlockRead()
	{
		DMibSanitizerAnnotate_MutexPreUnlock(this, __tsan_mutex_read_lock);
		umint CurrentThread = NMib::NSys::fg_Thread_GetCurrentUID();

		if (t_CBase::m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == CurrentThread)
		{
			f_Unlock();
			return;
		}

#if DMibEnableSafeCheck > 0
		m_nReadingDebugCheck.f_FetchSub(1, NAtomic::gc_MemoryOrder_Relaxed);
#endif
		f_UnlockReadInternal();
		DMibSanitizerAnnotate_MutexPostUnlock(this, __tsan_mutex_read_lock);
	}

	template <typename t_CEventAutoreset, typename t_CBase>
	inline inline_never void TCMutualManyReadAggregate<t_CEventAutoreset, t_CBase>::f_Lock()
	{
		DMibSanitizerAnnotate_MutexPreLock(this, __tsan_mutex_write_reentrant);

		umint CurrentThread = NMib::NSys::fg_Thread_GetCurrentUID();

		if (t_CBase::m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == CurrentThread)
		{
			++t_CBase::m_nRecurse;
			DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant, 1);
			return;
		}

		// Try to take the lock
		umint nLockedValue = t_CBase::m_nLocked.f_FetchAdd(1, NAtomic::gc_MemoryOrder_Acquire);
		umint nLocked = nLockedValue & mcp_AtomicMask;

		if (nLocked > 0)
			t_CBase::fp_WaitForIt();

		uint32 nReading = m_nReading.f_FetchOr(mc_FlagReadingNotAllowed, NAtomic::gc_MemoryOrder_Acquire);
		if ((nReading & mc_nReadingMask) > 0)
		{
			while (1)
			{
				uint32 nReading = m_nReading.f_FetchOr(mc_FlagReadingNotAllowed, NAtomic::gc_MemoryOrder_Acquire);

				if ((nReading & mc_nReadingMask) > 0)
				{
					CDisableLazyCheckoutReturnScope DisableLazy;
					m_WriteOkEvent.f_Wait();
				}
				else
					break;
			}
		}
		t_CBase::m_ThreadID.f_Store(CurrentThread, NAtomic::gc_MemoryOrder_Relaxed);
#if DMibEnableSafeCheck > 0
		t_CBase::m_AlternateThreadID = NSys::fg_Thread_GetCurrentUIDAlternate();
#endif
		t_CBase::m_nRecurse = 1;
		DMibFastCheck(m_nReadingDebugCheck.f_Load() == 0);

		DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant, 1);
	}

	template <typename t_CEventAutoreset, typename t_CBase>
	inline inline_never bool TCMutualManyReadAggregate<t_CEventAutoreset, t_CBase>::f_TryLock()
	{
		DMibSanitizerAnnotate_MutexPreLock(this, __tsan_mutex_write_reentrant | __tsan_mutex_try_lock);

		umint CurrentThread = NMib::NSys::fg_Thread_GetCurrentUID();

		if (t_CBase::m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == CurrentThread)
		{
			++t_CBase::m_nRecurse;
			DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant | __tsan_mutex_try_lock, 1);
			return true;
		}

		umint Original = t_CBase::m_nLocked.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
		if (Original & mcp_AtomicMask)
		{
			DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant | __tsan_mutex_try_lock_failed, 1);
			return false;
		}

		if (t_CBase::m_nLocked.f_CompareExchangeStrong(Original, Original + 1, NAtomic::gc_MemoryOrder_Acquire, NAtomic::gc_MemoryOrder_Relaxed))
		{
			t_CBase::m_ThreadID.f_Store(CurrentThread, NAtomic::gc_MemoryOrder_Relaxed);
#if DMibEnableSafeCheck > 0
			t_CBase::m_AlternateThreadID = NSys::fg_Thread_GetCurrentUIDAlternate();
#endif
			t_CBase::m_nRecurse = 1;

			DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant | __tsan_mutex_try_lock, 1);
		}
		else
		{
			DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant | __tsan_mutex_try_lock_failed, 1);
			return false;
		}

		uint32 ReadingOriginal = m_nReading.f_Load(NAtomic::gc_MemoryOrder_Acquire);

		if ((ReadingOriginal & mc_nReadingMask) > 0)
		{
			t_CBase::f_Unlock();
			return false;
		}

		if (!m_nReading.f_CompareExchangeStrong(ReadingOriginal, ReadingOriginal | mc_FlagReadingNotAllowed, NAtomic::gc_MemoryOrder_Acquire))
		{
			t_CBase::f_Unlock();
			return false;
		}
		DMibFastCheck(m_nReadingDebugCheck.f_Load() == 0);
		return true;
	}

	template <typename t_CEventAutoreset, typename t_CBase>
	inline inline_never void TCMutualManyReadAggregate<t_CEventAutoreset, t_CBase>::f_Unlock()
	{
		DMibSanitizerAnnotate_MutexPreUnlock(this, 0);

		DMibFastCheck(t_CBase::m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == NMib::NSys::fg_Thread_GetCurrentUID());

		if ((--t_CBase::m_nRecurse) == 0)
		{
			t_CBase::m_ThreadID.f_Store(0, NAtomic::gc_MemoryOrder_Relaxed);
			uint32 OldReading = m_nReading.f_FetchAnd(~(mc_FlagReadingNotAllowed | mc_FlagReadersWaiting), NAtomic::gc_MemoryOrder_AcquireRelease);
			DMibFastCheck((OldReading & mc_FlagReadingNotAllowed));
			if (OldReading & mc_FlagReadersWaiting)
				NSys::fg_Futex_WakeAll((uint32 volatile *)&m_nReading);

			umint nLockedValue = t_CBase::m_nLocked.f_FetchSub(1, NAtomic::gc_MemoryOrder_AcquireRelease); // Acquire for m_Event
			if ((nLockedValue & mcp_AtomicMask) > 1)
				t_CBase::fp_SignalIt();
		}

		DMibSanitizerAnnotate_MutexPostUnlock(this, 0);
	}

	template <typename t_CEventAutoreset, typename t_CBase>
	inline TCMutualManyRead<t_CEventAutoreset, t_CBase>::TCMutualManyRead()
	{
		TCMutualManyReadAggregate<t_CEventAutoreset, t_CBase>::f_Construct();
	}

	template <typename t_CEventAutoreset, typename t_CBase>
	inline TCMutualManyRead<t_CEventAutoreset, t_CBase>::~TCMutualManyRead()
	{
		TCMutualManyReadAggregate<t_CEventAutoreset, t_CBase>::f_Destruct();
	}

	template <typename t_CLock>
	inline void CScopeUnlock::TCLocker<t_CLock>::fs_Locker(void *_pLock)
	{
		((t_CLock *)_pLock)->f_Lock();
	}

	template <typename tf_CLock>
	inline CScopeUnlock::CScopeUnlock(tf_CLock &_Lock)
	{
		_Lock.f_Unlock();
		mp_pLock = &_Lock;
		mp_fLockFunc = TCLocker<tf_CLock>::fs_Locker;
	}

	inline CScopeUnlock::~CScopeUnlock()
	{
		mp_fLockFunc(mp_pLock);
	}

	template <typename t_CLock>
	inline void CScopeLockRead::TCLocker<t_CLock>::fs_Locker(void *_pLock)
	{
		((t_CLock *)_pLock)->f_UnlockRead();
	}

	template <typename tf_CLock>
	inline CScopeLockRead::CScopeLockRead(tf_CLock &_Lock)
	{
		_Lock.f_LockRead();
		mp_pLock = &_Lock;
		mp_fUnlockFunc = TCLocker<tf_CLock>::fs_Locker;
	}

	inline CScopeLockRead::~CScopeLockRead()
	{
		mp_fUnlockFunc(mp_pLock);
	}

	template <typename t_CLock>
	inline void CScopeUnlockRead::TCLocker<t_CLock>::fs_Locker(void *_pLock)
	{
		((t_CLock *)_pLock)->f_LockRead();
	}

	template <typename tf_CLock>
	inline CScopeUnlockRead::CScopeUnlockRead(tf_CLock &_Lock)
	{
		_Lock.f_UnlockRead();
		mp_pLock = &_Lock;
		mp_fLockFunc = TCLocker<tf_CLock>::fs_Locker;
	}

	template <typename tf_CData>
	tf_CData fg_Thread_GetDataLocked(tf_CData const &_Data, CMutual &_Lock)
	{
		DMibLock(_Lock);
		return _Data;
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	TCThreadLocal<t_CData, t_CAllocator, t_Flags>::TCThreadLocal()
	{
		if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
			m_Flags |= EThreadLocalInterfaceFlag_UseFastStorage;
		if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
			m_Flags |= EThreadLocalInterfaceFlag_AlwaysCreated;
		if constexpr ((mc_Flags & EThreadLocalFlag_Inherit) != 0)
			m_Flags |= EThreadLocalInterfaceFlag_Inherit;

		m_Storage = (umint)fg_GetSys()->f_ThreadLocalAlloc(*this, m_ThreadLocalLocal);
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	void TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_Destroy()
	{
		if (m_Storage)
		{
			fg_GetSys()->f_ThreadLocalFree(*this, (void *)m_Storage);
			m_Storage = 0;
		}
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	TCThreadLocal<t_CData, t_CAllocator, t_Flags>::~TCThreadLocal()
	{
		f_Destroy();
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	bool TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_IsValid()
	{
		if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return NSys::fg_Thread_GetLocalAlwaysSetFast(m_ThreadLocalLocal) != nullptr;
			else
				return NSys::fg_Thread_GetLocalFast(m_ThreadLocalLocal) != nullptr;
		}
		else
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return NSys::fg_Thread_GetLocalAlwaysSet(m_ThreadLocalLocal) != nullptr;
			else
				return NSys::fg_Thread_GetLocal(m_ThreadLocalLocal) != nullptr;
		}
		//t_CData *pData = ((t_CData *)fg_GetSys()->f_ThreadLocalGet((void *)m_Storage));
		//return pData != 0;
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	inline_never t_CData *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::fp_GetNew()
	{
		t_CData *pData = ((t_CData *)fg_GetSys()->f_ThreadLocalGet((void *)m_Storage));
		if (pData)
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
				NSys::fg_Thread_SetLocalFast(m_ThreadLocalLocal, pData);
			else
				NSys::fg_Thread_SetLocal(m_ThreadLocalLocal, pData);
			return pData;
		}
		pData = (t_CData *)f_CreateData(nullptr, false, NSys::fg_Thread_GetCurrentUID());
		fg_GetSys()->f_ThreadLocalSet((void *)m_Storage, pData);
		return pData;
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	inline_small t_CData *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_Get()
	{
		t_CData *pData;
		if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
			{
				pData = (t_CData *)NSys::fg_Thread_GetLocalAlwaysSetFast(m_ThreadLocalLocal);
				DMibFastCheck(pData);
			}
			else
				pData = (t_CData *)NSys::fg_Thread_GetLocalFast(m_ThreadLocalLocal);
		}
		else
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
			{
				pData = (t_CData *)NSys::fg_Thread_GetLocalAlwaysSet(m_ThreadLocalLocal);
				DMibFastCheck(pData);
			}
			else
				pData = (t_CData *)NSys::fg_Thread_GetLocal(m_ThreadLocalLocal);
		}
		if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
			return pData;
		else
		{
			if (pData) [[likely]]
			{
				//DMibFastCheck(((t_CData *)fg_GetSys()->f_ThreadLocalGet((void *)m_Storage)) == pData);
				return pData;
			}
			return fp_GetNew();
		}
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	inline_small t_CData *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_TryGet()
	{
		if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSetFast(m_ThreadLocalLocal);
			else
				return (t_CData *)NSys::fg_Thread_GetLocalFast(m_ThreadLocalLocal);
		}
		else
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSet(m_ThreadLocalLocal);
			else
				return (t_CData *)NSys::fg_Thread_GetLocal(m_ThreadLocalLocal);
		}
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	inline_small t_CData *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_GetForThread(umint _ThreadID)
	{
		auto pData = f_TryGetForThread(_ThreadID);
		DMibFastCheck(pData);
		return pData;
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	inline_small t_CData *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_TryGetForThread(umint _ThreadID)
	{
		if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSetFast(_ThreadID, m_ThreadLocalLocal);
			else
				return (t_CData *)NSys::fg_Thread_GetLocalFast(_ThreadID, m_ThreadLocalLocal);
		}
		else
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSet(_ThreadID, m_ThreadLocalLocal);
			else
				return (t_CData *)NSys::fg_Thread_GetLocal(_ThreadID, m_ThreadLocalLocal);
		}
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	inline_small void TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_ReinitForThread()
	{
		return fg_GetSys()->f_ThreadLocalReinitForThread((void *)m_Storage);
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	inline_small void TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_DestroyForThread()
	{
		return fg_GetSys()->f_ThreadLocalDestroyForThread((void *)m_Storage);
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	inline_small TCThreadLocal<t_CData, t_CAllocator, t_Flags>::operator t_CData * ()
	{
		return f_Get();
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	inline_small t_CData *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::operator -> ()
	{
		return f_Get();
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	inline_small t_CData &TCThreadLocal<t_CData, t_CAllocator, t_Flags>::operator & ()
	{
		return *f_Get();
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	void TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_DeleteItem(void *_pItem)
	{
		t_CData *pData = (t_CData *)_pItem;
		fg_DeleteObjectDefiniteType(t_CAllocator(), pData, fg_Max(umint(DMibPMemoryCacheLineSize), alignof(t_CData)));
	}

	namespace NPrivate
	{
		struct CCreateHelperDo
		{
			template <typename tf_CType>
			static void *fs_CreateHelper(void *_pDest, void *_pSource)
			{
				tf_CType const *pSource = (tf_CType const *)_pSource;
				tf_CType *pData = new(_pDest) tf_CType(*pSource);
				return pData;
			}

			template <typename tf_CType>
			static void *fs_CreateHelperMove(void *_pDest, void *_pSource)
			{
				tf_CType *pSource = (tf_CType *)_pSource;
				tf_CType *pData = new(_pDest) tf_CType(fg_Move(*pSource));
				return pData;
			}
		};

		struct CCreateHelperDoNot
		{
			template <typename tf_CType>
			static void *fs_CreateHelper(void *_pDest, void *_pSource)
			{
				return nullptr;
			}

			template <typename tf_CType>
			static void *fs_CreateHelperMove(void *_pDest, void *_pSource)
			{
				return nullptr;
			}
		};
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	auto TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_AllocData() -> CSafeAlloc
	{
		constexpr bool c_bInherit = (t_Flags & int(EThreadLocalFlag_Inherit)) != 0;

		if constexpr (c_bInherit)
			return CSafeAlloc(this, {t_CAllocator::f_AllocAligned(sizeof(t_CData), fg_Max(umint(DMibPMemoryCacheLineSize), alignof(t_CData))), sizeof(t_CData)});

		return CSafeAlloc(nullptr, {nullptr, 0});
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	void TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_FreeData(CSafeAllocMemory const &_Alloc)
	{
		return t_CAllocator::f_Free(_Alloc.m_pMemory, sizeof(t_CData));
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	void *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_CreateDataCopy(void *_pSource, void *_pMemory)
	{
		constexpr bool c_bInherit = (t_Flags & int(EThreadLocalFlag_Inherit)) != 0;

		if constexpr (c_bInherit)
		{
			t_CData const *pSource = (t_CData const *)_pSource;
			t_CData *pData = new(_pMemory) t_CData(*pSource);
			return pData;
		}

		return nullptr;
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	void *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_CreateDataMove(void *_pSource, void *_pMemory)
	{
		constexpr bool c_bInherit = (t_Flags & int(EThreadLocalFlag_Inherit)) != 0;

		if constexpr (c_bInherit)
		{
			t_CData *pSource = (t_CData *)_pSource;
			t_CData *pData = new(_pMemory) t_CData(fg_Move(*pSource));
			return pData;
		}

		return nullptr;
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	void *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_CreateData(void *_pMemory, bool _bInitial, umint _ThreadID)
	{
		if ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0 || !_bInitial)
		{
			if (!_pMemory)
				_pMemory = t_CAllocator::f_AllocAligned(sizeof(t_CData), fg_Max(umint(DMibPMemoryCacheLineSize), alignof(t_CData)));
			if constexpr
			(
				requires
				{
					t_CData::mc_bThreadLocalConstructForThread;
				}
			)
			{
				static_assert(t_CData::mc_bThreadLocalConstructForThread);
				return new(_pMemory) t_CData(CThreadLocalConstructionContext{_ThreadID});
			}
			else
				return new(_pMemory) t_CData();
		}
		return nullptr;
	}

#if DMibEnableSafeCheck > 0
	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	ch8 const *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_GetName()
	{
		return fg_GetTypeName<t_CData>();
	}
#endif

	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template <typename t_CData, EThreadLocalFlag t_Flags>
	TCThreadLocalDynamic<t_CData, t_Flags>::TCThreadLocalDynamic
		(
			NFunction::TCFunctionNoAlloc<CSafeAllocMemory ()> const &_fAlloc
			, NFunction::TCFunctionNoAlloc<void (CSafeAllocMemory const &_Alloc)> const &_fFree
			, NFunction::TCFunctionNoAlloc<t_CData *(t_CData *_pParent, void *_pMemory, bool _bMove)> const &_fConstruct
			, NFunction::TCFunctionNoAlloc<void (t_CData *_pData)> const &_fDestruct
		)
		: m_fAlloc(_fAlloc)
		, m_fFree(_fFree)
		, m_fConstruct(_fConstruct)
		, m_fDestruct(_fDestruct)
	{
		if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
			m_Flags |= EThreadLocalInterfaceFlag_UseFastStorage;
		if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
			m_Flags |= EThreadLocalInterfaceFlag_AlwaysCreated;
		if constexpr ((mc_Flags & EThreadLocalFlag_Inherit) != 0)
			m_Flags |= EThreadLocalInterfaceFlag_Inherit;

		m_Storage = (umint)fg_GetSys()->f_ThreadLocalAlloc(*this, m_ThreadLocalLocal);
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	void TCThreadLocalDynamic<t_CData, t_Flags>::f_Destroy()
	{
		if (m_Storage)
		{
			fg_GetSys()->f_ThreadLocalFree(*this, (void *)m_Storage);
			m_Storage = 0;
		}
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	TCThreadLocalDynamic<t_CData, t_Flags>::~TCThreadLocalDynamic()
	{
		f_Destroy();
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	bool TCThreadLocalDynamic<t_CData, t_Flags>::f_IsValid()
	{
		if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return NSys::fg_Thread_GetLocalAlwaysSetFast(m_ThreadLocalLocal) != nullptr;
			else
				return NSys::fg_Thread_GetLocalFast(m_ThreadLocalLocal) != nullptr;
		}
		else
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return NSys::fg_Thread_GetLocalAlwaysSet(m_ThreadLocalLocal) != nullptr;
			else
				return NSys::fg_Thread_GetLocal(m_ThreadLocalLocal) != nullptr;
		}
		//t_CData *pData = ((t_CData *)fg_GetSys()->f_ThreadLocalGet((void *)m_Storage));
		//return pData != 0;
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	inline_never t_CData *TCThreadLocalDynamic<t_CData, t_Flags>::fp_GetNew()
	{
		t_CData *pData = ((t_CData *)fg_GetSys()->f_ThreadLocalGet((void *)m_Storage));
		if (pData)
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
				NSys::fg_Thread_SetLocalFast(m_ThreadLocalLocal, pData);
			else
				NSys::fg_Thread_SetLocal(m_ThreadLocalLocal, pData);
			return pData;
		}
		pData = (t_CData *)f_CreateData(nullptr, false, NSys::fg_Thread_GetCurrentUID());
		fg_GetSys()->f_ThreadLocalSet((void *)m_Storage, pData);
		return pData;
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	inline_small void TCThreadLocalDynamic<t_CData, t_Flags>::f_ReinitForThread()
	{
		return fg_GetSys()->f_ThreadLocalReinitForThread((void *)m_Storage);
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	inline_small void TCThreadLocalDynamic<t_CData, t_Flags>::f_DestroyForThread()
	{
		return fg_GetSys()->f_ThreadLocalDestroyForThread((void *)m_Storage);
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	inline_small t_CData *TCThreadLocalDynamic<t_CData, t_Flags>::f_Get()
	{
		t_CData *pData;
		if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
			{
				pData = (t_CData *)NSys::fg_Thread_GetLocalAlwaysSetFast(m_ThreadLocalLocal);
				DMibFastCheck(pData);
			}
			else
				pData = (t_CData *)NSys::fg_Thread_GetLocalFast(m_ThreadLocalLocal);
		}
		else
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
			{
				pData = (t_CData *)NSys::fg_Thread_GetLocalAlwaysSet(m_ThreadLocalLocal);
				DMibFastCheck(pData);
			}
			else
				pData = (t_CData *)NSys::fg_Thread_GetLocal(m_ThreadLocalLocal);
		}
		if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
			return pData;
		else
		{
			if (pData) [[likely]]
			{
				//DMibFastCheck(((t_CData *)fg_GetSys()->f_ThreadLocalGet((void *)m_Storage)) == pData);
				return pData;
			}
			return fp_GetNew();
		}
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	inline_small t_CData *TCThreadLocalDynamic<t_CData, t_Flags>::f_TryGet()
	{
		if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSetFast(m_ThreadLocalLocal);
			else
				return (t_CData *)NSys::fg_Thread_GetLocalFast(m_ThreadLocalLocal);
		}
		else
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSet(m_ThreadLocalLocal);
			else
				return (t_CData *)NSys::fg_Thread_GetLocal(m_ThreadLocalLocal);
		}
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	inline_small t_CData *TCThreadLocalDynamic<t_CData, t_Flags>::f_GetForThread(umint _ThreadID)
	{
		auto pData = f_TryGetForThread(_ThreadID);
		DMibFastCheck(pData);
		return pData;
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	inline_small t_CData *TCThreadLocalDynamic<t_CData, t_Flags>::f_TryGetForThread(umint _ThreadID)
	{
		if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSetFast(_ThreadID, m_ThreadLocalLocal);
			else
				return (t_CData *)NSys::fg_Thread_GetLocalFast(_ThreadID, m_ThreadLocalLocal);
		}
		else
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSet(_ThreadID, m_ThreadLocalLocal);
			else
				return (t_CData *)NSys::fg_Thread_GetLocal(_ThreadID, m_ThreadLocalLocal);
		}
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	inline_small TCThreadLocalDynamic<t_CData, t_Flags>::operator t_CData * ()
	{
		return f_Get();
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	inline_small t_CData *TCThreadLocalDynamic<t_CData, t_Flags>::operator -> ()
	{
		return f_Get();
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	inline_small t_CData &TCThreadLocalDynamic<t_CData, t_Flags>::operator & ()
	{
		return *f_Get();
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	void TCThreadLocalDynamic<t_CData, t_Flags>::f_DeleteItem(void *_pItem)
	{
		t_CData *pData = (t_CData *)_pItem;
		m_fDestruct(pData);
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	auto TCThreadLocalDynamic<t_CData, t_Flags>::f_AllocData() -> CSafeAlloc
	{
		if constexpr ((t_Flags & int(EThreadLocalFlag_Inherit)) != 0)
			return CSafeAlloc(this, m_fAlloc());
		return {nullptr, {nullptr, 0}};
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	void TCThreadLocalDynamic<t_CData, t_Flags>::f_FreeData(CSafeAllocMemory const &_Alloc)
	{
		return m_fFree(_Alloc);
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	void *TCThreadLocalDynamic<t_CData, t_Flags>::f_CreateDataCopy(void *_pSource, void *_pMemory)
	{
		if constexpr ((t_Flags & int(EThreadLocalFlag_Inherit)) != 0)
			return m_fConstruct((t_CData *)_pSource, _pMemory, false);
		return nullptr;
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	void *TCThreadLocalDynamic<t_CData, t_Flags>::f_CreateDataMove(void *_pSource, void *_pMemory)
	{
		if constexpr ((t_Flags & int(EThreadLocalFlag_Inherit)) != 0)
			return m_fConstruct((t_CData *)_pSource, _pMemory, true);
		return nullptr;
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	void *TCThreadLocalDynamic<t_CData, t_Flags>::f_CreateData(void *_pMemory, bool _bInitial, umint _ThreadID)
	{
		if ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0 || !_bInitial)
		{
			CSafeAlloc Memory(nullptr, {nullptr, 0});
			if (!_pMemory)
			{
				Memory.m_Memory = m_fAlloc();
				Memory.m_pInterface = this;
				_pMemory = Memory.m_Memory.m_pMemory;
			}

			auto *pReturn = m_fConstruct(nullptr, _pMemory, false);
			Memory.f_Claim();

			return pReturn;
		}
		return nullptr;
	}

#if DMibEnableSafeCheck > 0
	template <typename t_CData, EThreadLocalFlag t_Flags>
	ch8 const *TCThreadLocalDynamic<t_CData, t_Flags>::f_GetName()
	{
		return fg_GetTypeName<t_CData>();
	}
#endif

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template <typename t_CAllocator, typename t_CStr>
	inline TCThreadObject<t_CAllocator, t_CStr>::~TCThreadObject()
	{
		f_Stop();
	}

	template <typename t_CAllocator, typename t_CStr>
	inline TCThreadObject<t_CAllocator, t_CStr>::CCallerObject::~CCallerObject()
	{
	}

	template <typename t_CAllocator, typename t_CStr>
	inline TCThreadObject<t_CAllocator, t_CStr>::TCThreadObject(NStorage::TCUniquePointer<CCallerObject, t_CAllocator> &&_pCallerObject)
		: mp_pCallerObject(fg_Move(_pCallerObject))
	{
	}

	template <typename t_CAllocator, typename t_CStr>
	inline aint TCThreadObject<t_CAllocator, t_CStr>::f_Main()
	{
		return mp_pCallerObject->f_Call(this);
	}

	template <typename t_CAllocator, typename t_CStr>
	NStr::CStr TCThreadObject<t_CAllocator, t_CStr>::f_GetThreadName()
	{
		return f_GetThreadNameRaw();
	}

	template <typename t_CAllocator, typename t_CStr>
	ch8 const *TCThreadObject<t_CAllocator, t_CStr>::f_GetThreadNameRaw()
	{
		return mp_pCallerObject->f_GetName();
	}

	namespace NPrivate
	{
		template <typename t_CFunctionType, typename t_CStr, typename t_CCallerObject, typename t_CThreadObject>
		struct TCCallerObjectImp : public t_CCallerObject
		{
			TCCallerObjectImp(t_CFunctionType const &_FunctionObject, t_CStr const &_Name)
				: m_FunctionObject(_FunctionObject)
				, m_Name(_Name)
			{
			}

			TCCallerObjectImp(t_CFunctionType &&_FunctionObject, t_CStr const &_Name)
				: m_FunctionObject(fg_Move(_FunctionObject))
				, m_Name(_Name)
			{
			}

			virtual aint f_Call(t_CThreadObject *_pThread)
			{
				return m_FunctionObject(_pThread);
			}

			virtual ch8 const *f_GetName()
			{
				return m_Name;
			}

			t_CFunctionType m_FunctionObject;
			t_CStr m_Name;
		};
	}

	template <typename t_CAllocator, typename t_CStr>
	template <typename tf_CFunctionType>
	auto TCThreadObject<t_CAllocator, t_CStr>::fs_StartThread
		(
			tf_CFunctionType &&_FunctionObject
			, t_CStr const &_Name
			, EExecutionPriority _Prio
			, umint _StackSize
			, umint _Affinity
			, EThreadCreateFlag _Flags
		)
		-> NStorage::TCUniquePointer<TCThreadObject, t_CAllocator>
	{
		NStorage::TCUniquePointer<TCThreadObject, t_CAllocator> pThread = fg_Construct
			(
				NStorage::TCUniquePointer<CCallerObject, t_CAllocator>
				(
					fg_Construct<NPrivate::TCCallerObjectImp<NTraits::TCRemoveReference<tf_CFunctionType>, t_CStr, CCallerObject, TCThreadObject>>
					(
						fg_Forward<tf_CFunctionType>(_FunctionObject)
						, _Name
					)
				)
			)
		;
		pThread->f_Start(_Prio, _StackSize, _Affinity, _Flags);

		return pThread;
	}

	template <typename t_CAllocator, typename t_CStr>
	template <typename tf_CFunctionType>
	auto TCThreadObject<t_CAllocator, t_CStr>::fs_StartThread
		(
			tf_CFunctionType *_pFunctionObject
			, t_CStr const &_Name
			, EExecutionPriority _Prio
			, umint _StackSize
			, umint _Affinity
			, EThreadCreateFlag _Flags
		)
		-> NStorage::TCUniquePointer<TCThreadObject, t_CAllocator>
	{
		struct CCallerObjectImp final : public CCallerObject
		{
			CCallerObjectImp(tf_CFunctionType *_pFunctionObject, t_CStr const &_Name)
				: m_pFunctionObject(_pFunctionObject)
				, m_Name(_Name)
			{
			}

			virtual aint f_Call(TCThreadObject *_pThread)
			{
				return (*m_pFunctionObject)(_pThread);
			}

			virtual ch8 const *f_GetName()
			{
				return m_Name;
			}

			tf_CFunctionType *m_pFunctionObject;
			t_CStr m_Name;
		};

		NStorage::TCUniquePointer<TCThreadObject, t_CAllocator> pThread = fg_Construct
			(
				NStorage::TCUniquePointer<CCallerObject, t_CAllocator>(fg_Construct<CCallerObjectImp>(_pFunctionObject, _Name))
			)
		;
		pThread->f_Start(_Prio, _StackSize, _Affinity, _Flags);

		return pThread;
	}
}

namespace NMib::NStorage
{
	template <typename t_CCountType>
	inline TCIntrusiveRefCount<ESharedPointerOption_None, t_CCountType>::TCIntrusiveRefCount()
		: m_RefCount(0)
	{
		DIfRefCountDebugging(m_Debug.f_Construct());
	}

	template <typename t_CCountType>
	inline TCIntrusiveRefCount<ESharedPointerOption_None, t_CCountType>::TCIntrusiveRefCount(t_CCountType _RefCount)
		: m_RefCount(_RefCount)
	{
		DIfRefCountDebugging(m_Debug.f_Construct());
	}

	template <typename t_CCountType>
	inline TCIntrusiveRefCount<ESharedPointerOption_None, t_CCountType>::TCIntrusiveRefCount(TCIntrusiveRefCount const &_Other)
		: m_RefCount(0)
	{
		DIfRefCountDebugging(m_Debug.f_Construct());
	}

	template <typename t_CCountType>
	inline TCIntrusiveRefCount<ESharedPointerOption_None, t_CCountType>::TCIntrusiveRefCount(TCIntrusiveRefCount &&_Other)
		: m_RefCount(0)
	{
		DIfRefCountDebugging(m_Debug.f_Construct());
	}

	template <typename t_CCountType>
	inline auto TCIntrusiveRefCount<ESharedPointerOption_None, t_CCountType>::operator = (TCIntrusiveRefCount const &_Other) -> TCIntrusiveRefCount &
	{
		return *this;
	}

	template <typename t_CCountType>
	inline auto TCIntrusiveRefCount<ESharedPointerOption_None, t_CCountType>::operator = (TCIntrusiveRefCount &&_Other) -> TCIntrusiveRefCount &
	{
		return *this;
	}

#if !DMibConfig_RefCountDebugging
	template <typename t_CCountType>
	inline t_CCountType TCIntrusiveRefCount<ESharedPointerOption_None, t_CCountType>::f_Decrease() const
	{
		t_CCountType Return = m_RefCount.f_FetchSub(1, NAtomic::gc_MemoryOrder_Release);
		DMibFastCheck(Return >= 0);
		if (Return == 0)
		{
#	ifdef DMibSanitizerEnabled_Thread
			m_RefCount.f_Load(NAtomic::gc_MemoryOrder_Acquire);
#	else
			NAtomic::fg_MemoryFence(NAtomic::gc_MemoryOrder_Acquire);
#	endif
		}
		return Return;
	}

	template <typename t_CCountType>
	inline t_CCountType TCIntrusiveRefCount<ESharedPointerOption_None, t_CCountType>::f_Increase
		(
#	if DMibEnableSafeCheck > 0
			bool _bAllowRevive
#	endif
		)
		const
	{
		t_CCountType Return = m_RefCount.f_FetchAdd(1, NAtomic::gc_MemoryOrder_Relaxed);
		DMibFastCheck(Return >= 0 || _bAllowRevive && Return == -1);
		DMibFastCheck(Return < (TCLimitsInt<t_CCountType>::mc_Max - 1));
		return Return;
	}
#endif

	template <typename t_CCountType>
	inline t_CCountType TCIntrusiveRefCount<ESharedPointerOption_None, t_CCountType>::f_Get() const
	{
		return m_RefCount.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
	}

	template <typename t_CCountType>
	inline TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::TCIntrusiveRefCount()
		: m_RefCount(0)
		, m_WeakRefCount(0)
	{
		DIfRefCountDebugging(m_Debug.f_Construct());
	}

	template <typename t_CCountType>
	inline TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::TCIntrusiveRefCount(TCIntrusiveRefCount const &_Other)
		: m_RefCount(0)
		, m_WeakRefCount(0)
	{
		DIfRefCountDebugging(m_Debug.f_Construct());
	}

	template <typename t_CCountType>
	inline TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::TCIntrusiveRefCount(TCIntrusiveRefCount &&_Other)
		: m_RefCount(0)
		, m_WeakRefCount(0)
	{
		DIfRefCountDebugging(m_Debug.f_Construct());
	}

	template <typename t_CCountType>
	inline auto TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::operator = (TCIntrusiveRefCount const &_Other) -> TCIntrusiveRefCount &
	{
		return *this;
	}

	template <typename t_CCountType>
	inline auto TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::operator = (TCIntrusiveRefCount &&_Other) -> TCIntrusiveRefCount &
	{
		return *this;
	}

#if !DMibConfig_RefCountDebugging
	template <typename t_CCountType>
	inline t_CCountType TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::f_Decrease() const
	{
		t_CCountType Return = m_RefCount.f_FetchSub(1, NAtomic::gc_MemoryOrder_Release);
		DMibFastCheck(Return >= 0);
		if (Return == 0)
		{
#	ifdef DMibSanitizerEnabled_Thread
			m_RefCount.f_Load(NAtomic::gc_MemoryOrder_Acquire);
#	else
			NAtomic::fg_MemoryFence(NAtomic::gc_MemoryOrder_Acquire);
#	endif
		}
		return Return;
	}

	template <typename t_CCountType>
	inline t_CCountType TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::f_Increase
		(
#	if DMibEnableSafeCheck > 0
			bool _bAllowRevive
#	endif
		)
		const
	{
		t_CCountType Return = m_RefCount.f_FetchAdd(1, NAtomic::gc_MemoryOrder_Relaxed);
		DMibFastCheck(Return >= 0 || _bAllowRevive && Return == -1);
		DMibFastCheck(Return < (TCLimitsInt<t_CCountType>::mc_Max - 1));

		return Return;
	}

	template <typename t_CCountType>
	inline bool TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::f_IncreaseWhileValid() const
	{
		t_CCountType CurrentValue = m_RefCount.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
		DMibFastCheck(CurrentValue < (TCLimitsInt<t_CCountType>::mc_Max - 1));
		while (CurrentValue >= 0)
		{
			if (m_RefCount.f_CompareExchangeStrong(CurrentValue, CurrentValue + 1, NAtomic::gc_MemoryOrder_Relaxed, NAtomic::gc_MemoryOrder_Relaxed))
				return true;
		}

		return false;
	}

	template <typename t_CCountType>
	inline t_CCountType TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::f_WeakDecrease() const
	{
		t_CCountType Return = m_WeakRefCount.f_FetchSub(1, NAtomic::gc_MemoryOrder_Release);
		if (Return == 0)
		{
#	ifdef DMibSanitizerEnabled_Thread
			m_WeakRefCount.f_Load(NAtomic::gc_MemoryOrder_Acquire);
#	else
			NAtomic::fg_MemoryFence(NAtomic::gc_MemoryOrder_Acquire);
#	endif
		}
		return Return;
	}

	template <typename t_CCountType>
	inline t_CCountType TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::f_WeakIncrease() const
	{
		auto Return = m_WeakRefCount.f_FetchAdd(1, NAtomic::gc_MemoryOrder_Relaxed);
		DMibFastCheck(Return < (TCLimitsInt<t_CCountType>::mc_Max - 1));

		return Return;
	}
#endif

	template <typename t_CCountType>
	inline void TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::f_WeakSetCapturedDelete(NMemory::CCapturedDelete const &_CapturedDelete) const
	{
		*((void **)(this + 1)) = _CapturedDelete.m_pMemory;
		[[maybe_unused]] t_CCountType CurrentValue = m_RefCount.f_Exchange(t_CCountType(-1) - t_CCountType(_CapturedDelete.m_Size));
		DMibFastCheck(CurrentValue == -1);
	}

	template <typename t_CCountType>
	inline NMemory::CCapturedDelete TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::f_WeakGetCapturedDelete() const
	{
		return {*((void * const *)(this + 1)), umint(t_CCountType(-1) - m_RefCount.f_Load(NAtomic::gc_MemoryOrder_Acquire))};
	}

	template <typename t_CCountType>
	inline t_CCountType TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::f_Get() const
	{
		return m_RefCount.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
	}

	template <typename t_CCountType>
	inline t_CCountType TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::f_WeakGet() const
	{
		return m_WeakRefCount.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
	}

	namespace NPrivate
	{
		template <typename t_CType, bool t_bVirtualDestructor, CSharedPointerOptionUnderlying t_Options>
		template <typename ...tfp_CParams>
		inline TCSharedPointerCounter<t_CType, t_bVirtualDestructor, t_Options>::TCSharedPointerCounter(tfp_CParams && ...p_Params)
			requires (NTraits::cIsPlacementNewConstructibleWith<t_CType, tfp_CParams...>)
			: mp_Data(fg_Forward<tfp_CParams>(p_Params)...)
		{
		}

		template <typename t_CType, bool t_bVirtualDestructor, CSharedPointerOptionUnderlying t_Options>
		mark_nodebug mark_artificial inline_always t_CType *TCSharedPointerCounter<t_CType, t_bVirtualDestructor, t_Options>::f_Get()
		{
			return &mp_Data;
		}

		template <typename t_CType, CSharedPointerOptionUnderlying t_Options>
		template <typename ...tfp_CParams>
		inline TCSharedPointerCounter<t_CType, true, t_Options>::TCSharedPointerCounter(tfp_CParams && ...p_Params)
			requires (NTraits::cIsPlacementNewConstructibleWith<t_CType, tfp_CParams...>)
			: mp_Data(fg_Forward<tfp_CParams>(p_Params)...)
		{
		}

		template <typename t_CType, CSharedPointerOptionUnderlying t_Options>
		mark_nodebug mark_artificial inline_always t_CType *TCSharedPointerCounter<t_CType, true, t_Options>::f_Get()
		{
			return &mp_Data;
		}

		template <typename tf_CType, bool tf_bVirtualDestructor, CSharedPointerOptionUnderlying tf_Options>
		tf_CType *fg_GetSharedPointerPointer(TCSharedPointerCounter<tf_CType, tf_bVirtualDestructor, tf_Options> *_pIn)
		{
			static_assert(!cHasIntrusiveRefCount<tf_CType>, "Use DMibDefineSharedPointerType to define type");
			if (_pIn)
				return _pIn->f_Get();
			return nullptr;
		}

		template
		<
			typename tf_CToType
			, typename tf_CType
			, bool tf_bToVirtualDestructor
			, bool tf_bVirtualDestructor
			, CSharedPointerOptionUnderlying tf_ToOptions
			, CSharedPointerOptionUnderlying tf_Options
		>
		auto fg_ConvertSharedPointer
			(
				TCSharedPointerCounter<tf_CType, tf_bVirtualDestructor, tf_Options> *_pIn
				, TCSharedPointerCounter<tf_CToType, tf_bToVirtualDestructor, tf_ToOptions> *_pDummy
			)
			-> TCSharedPointerCounter<tf_CToType, tf_bToVirtualDestructor, tf_ToOptions> *
		{
			static_assert(TCIsValidConversion<tf_CToType, tf_CType, void, void>::mc_Value, "Not a valid conversion");
			static_assert
				(
					!NTraits::cHasVirtualDestructor<tf_CToType> || NTraits::cHasVirtualDestructor<TCSharedPointerCounter<tf_CToType, tf_bToVirtualDestructor, tf_ToOptions>>
					, "No virtual base"
				)
			;
			static_assert
				(
					NTraits::cHasVirtualDestructor<tf_CToType> || !NTraits::cHasVirtualDestructor<TCSharedPointerCounter<tf_CToType, tf_bToVirtualDestructor, tf_ToOptions>>
					, "Virtual base"
				)
			;
			static_assert(tf_ToOptions == tf_Options, "Cannot mix weak support with non-weak support");
			static_assert(alignof(tf_CToType) == alignof(tf_CType), "Cannot mix alignment, use TCIntrusiveRefCount");
			static_assert(!NTraits::cIsVirtualBaseOf<tf_CType, tf_CToType>, "Virtual base classes are not supported, use TCIntrusiveRefCount");

			auto pCovertTo = (TCSharedPointerCounter<tf_CToType, tf_bToVirtualDestructor, tf_ToOptions> *)_pIn;
			DMibFastCheck(pCovertTo->f_Get() == (tf_CToType *)_pIn->f_Get());
			return pCovertTo;
		}
	}
}

#include "Malterlib_Thread_RefcountDebug.hpp"
