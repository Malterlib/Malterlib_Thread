// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>
#include "../../Core/Source/Malterlib_Core_PlatformInterface.h"
#include <Mib/Atomic/Atomic>

#if 0
	#define DMibThreadAtomicsAlignment align_cacheline
#else
	#define DMibThreadAtomicsAlignment
#endif

#if DMibConfig_RefCountDebugging
#include <Mib/Container/LinkedList>
#include <Mib/Storage/Aggregate>
#endif

namespace NMib::NThread
{
	struct CDisableLazyCheckoutReturnScope
	{
		CDisableLazyCheckoutReturnScope()
		{
			NMib::NSys::fg_Mem_DisableLazyReturnCheckout();
		}

		~CDisableLazyCheckoutReturnScope()
		{
			NMib::NSys::fg_Mem_EnableLazyReturnCheckout();
		}
	};

	// Futex-based counting semaphore. The count is saturated at m_Maximum (extra
	// signals are silently dropped). The count and the number of parked waiters
	// are packed into one 64-bit word so that a signal hands over tokens and
	// observes the waiter count in a single atomic operation: as soon as that
	// operation lands, a woken waiter may legally destroy the semaphore
	// (completion-event pattern), so the signaling side must not touch the
	// object afterwards and only issues futex wakes, which are safe on stale
	// addresses. Slow paths are implemented in the Core platform layer.
	class CSemaphoreAggregate
	{
	public:
		// Low 32 bits: count (the futex word); high 32 bits: parked waiters
		NAtomic::TCAtomic<uint64> m_Data;
		uint32 m_Maximum;

		static constexpr uint64 mcp_ValueMask = TCLimitsInt<uint32>::mc_Max;
		static constexpr uint64 mcp_WaiterOne = uint64(1) << 32;

		constexpr CSemaphoreAggregate(EAggregateInitialization _Init)
			: CSemaphoreAggregate{_Init, 0}
		{
		}

		constexpr CSemaphoreAggregate(EAggregateInitialization _Init, uint32 _Maximum)
			: m_Data{0}
			, m_Maximum{_Maximum}
		{
		}

		CSemaphoreAggregate()
		{
		}

		// Waiters futex-wait on the count half of m_Data
		inline_small uint32 volatile *fp_GetFutexWord()
		{
#ifdef DMibPLittleEndian
			return (uint32 volatile *)&m_Data;
#else
			return (uint32 volatile *)&m_Data + 1;
#endif
		}

		inline_small void f_Construct(aint _Initial = 0, aint _Max = TCLimitsInt<aint>::mc_Max)
		{
			m_Data.f_Store((uint64)(uint32)fg_Min<umint>((umint)_Initial, TCLimitsInt<uint32>::mc_Max), NAtomic::gc_MemoryOrder_Relaxed);
			m_Maximum = (uint32)fg_Min<umint>((umint)_Max, TCLimitsInt<uint32>::mc_Max);
		}

		inline_small void f_Destruct()
		{
		}

		void f_PrepareFork()
		{
		}

		void f_ForkedChild()
		{
			// The lock word is plain memory; waiters do not exist in the child
			m_Data.f_Store(m_Data.f_Load(NAtomic::gc_MemoryOrder_Relaxed) & mcp_ValueMask, NAtomic::gc_MemoryOrder_Relaxed);
		}

		void f_ForkedParent()
		{
		}

		inline_small void f_Signal(int _nToSignal)
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

		inline_small void f_Signal()
		{
			f_Signal(1);
		}

		inline_small bool f_TryWait()
		{
			uint64 Old = m_Data.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
			while ((uint32)Old > 0)
			{
				if (m_Data.f_CompareExchangeWeak(Old, Old - 1, NAtomic::gc_MemoryOrder_Acquire, NAtomic::gc_MemoryOrder_Relaxed))
					return true;
			}
			return false;
		}

		inline_small void f_Wait()
		{
			if (f_TryWait()) [[likely]]
				return;

			fp_WaitSlow();
		}

		// Returns true if the wait timed out
		inline_small bool f_WaitTimeout(fp64 _Timeout)
		{
			if (f_TryWait())
				return false;

			return fp_WaitTimeoutSlow(_Timeout);
		}

		// Support for a waiter that parks in a kernel facility of its own — an io_uring futex wait
		// on the count word — instead of f_Wait. Registration is what makes signals issue futex
		// wakes on the word while the external wait is armed; without it a signal sees no waiters
		// and skips the wake. The external wait must use the word from f_GetExternalFutexWord and
		// wait while its value is zero, and the waiter consumes tokens with f_TryWait when it runs

		inline_small uint32 volatile *f_GetExternalFutexWord()
		{
			return fp_GetFutexWord();
		}

		inline_small void f_ExternalWaiterRegister()
		{
			m_Data.f_FetchAdd(mcp_WaiterOne, NAtomic::gc_MemoryOrder_Relaxed);
		}

		inline_small void f_ExternalWaiterUnregister()
		{
			m_Data.f_FetchSub(mcp_WaiterOne, NAtomic::gc_MemoryOrder_Relaxed);
		}

		// Implemented in the Core platform layer
		void fp_WaitSlow();
		bool fp_WaitTimeoutSlow(fp64 _Timeout);
		void fp_WakeSlow(uint32 _Delta, uint32 _nWaiters);
	};

	static_assert(sizeof(NAtomic::TCAtomic<uint32>) == sizeof(uint32));
	static_assert(sizeof(NAtomic::TCAtomic<uint64>) == sizeof(uint64));

	struct CEventAutoResetAggregate : public CSemaphoreAggregate
	{
		constexpr CEventAutoResetAggregate(EAggregateInitialization _Init)
			: CSemaphoreAggregate{_Init, 1}
		{
		}

		CEventAutoResetAggregate()
		{
		}

		inline_small void f_Construct()
		{
			CSemaphoreAggregate::f_Construct(0, 1);
		}
	};

	class CEventAutoReset : public CEventAutoResetAggregate
	{
	public:
		CEventAutoReset()
		{
			f_Construct();
		}
		~CEventAutoReset()
		{
			f_Destruct();
		}
		CEventAutoReset(CEventAutoReset &&_Other) = delete;
		CEventAutoReset &operator =(CEventAutoReset &&_Other) = delete;
	};

	// Futex-based manual-reset event. The signaled flag, the waiters-present
	// flag and a signal generation share one word so that setting the event is a
	// single atomic operation: as soon as it lands, a woken waiter may legally
	// destroy the event (completion-event pattern), so the signaling side must
	// not touch the object afterwards and only issues futex wakes, which are
	// safe on stale addresses. The generation makes waiters that were parked at
	// signal time complete even when a reset lands before they are scheduled,
	// matching manual-reset event semantics. Slow paths are implemented in the
	// Core platform layer.
	class CEventAggregate
	{
	public:
		NAtomic::TCAtomic<uint32> m_State;

		static constexpr uint32 mcp_FlagSignaled = 0x1;
		static constexpr uint32 mcp_FlagWaiters = 0x2;
		static constexpr uint32 mcp_GenerationOne = 0x4;
		static constexpr uint32 mcp_GenerationMask = ~(mcp_FlagSignaled | mcp_FlagWaiters);

		constexpr CEventAggregate(EAggregateInitialization _Init)
			: m_State{0}
		{
		}

		CEventAggregate()
		{
		}

		void f_PrepareFork()
		{
		}

		void f_ForkedChild()
		{
			// Waiters do not exist in the child
			m_State.f_Store(m_State.f_Load(NAtomic::gc_MemoryOrder_Relaxed) & ~mcp_FlagWaiters, NAtomic::gc_MemoryOrder_Relaxed);
		}

		void f_ForkedParent()
		{
		}

		inline_small void f_Construct(bool _bInitialSignal = false)
		{
			m_State.f_Store(_bInitialSignal ? mcp_FlagSignaled : 0u, NAtomic::gc_MemoryOrder_Relaxed);
		}

		inline_small void f_Destruct()
		{
		}

		inline_small void f_SetSignaled()
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
				if (m_State.f_CompareExchangeWeak(Old, ((Old + mcp_GenerationOne) & ~mcp_FlagWaiters) | mcp_FlagSignaled, NAtomic::gc_MemoryOrder_AcquireRelease, NAtomic::gc_MemoryOrder_Relaxed))
					break;
			}

			if (Old & mcp_FlagWaiters) [[unlikely]]
				fp_WakeAllSlow();
		}

		inline_small void f_ResetSignaled()
		{
			// Keep the waiters flag and the generation so waiters parked at an
			// earlier signal still complete
			m_State.f_FetchAnd(~mcp_FlagSignaled, NAtomic::gc_MemoryOrder_AcquireRelease);
		}

		inline_small void f_Wait()
		{
			if (f_TryWait()) [[likely]]
				return;

			fp_WaitSlow();
		}

		// Returns true if the wait timed out
		inline_small bool f_WaitTimeout(fp64 _Timeout)
		{
			if (f_TryWait())
				return false;

			return fp_WaitTimeoutSlow(_Timeout);
		}

		inline_small bool f_TryWait()
		{
			return (m_State.f_Load(NAtomic::gc_MemoryOrder_Acquire) & mcp_FlagSignaled) != 0;
		}

		// Implemented in the Core platform layer
		void fp_WaitSlow();
		bool fp_WaitTimeoutSlow(fp64 _Timeout);
		void fp_WakeAllSlow();
	};

	class CEvent : public CEventAggregate
	{
	public:
		CEvent()
		{
			f_Construct();
		}
		~CEvent()
		{
			f_Destruct();
		}
	};

	class CSemaphore : public CSemaphoreAggregate
	{
	public:
		CSemaphore(aint _Initial = 0, aint _Max = TCLimitsInt<aint>::mc_Max)
		{
			f_Construct(_Initial, _Max);
		}
		~CSemaphore()
		{
			f_Destruct();
		}
	};


	class CScopeLock;
	class CScopeUnlock;
	class CScopeLockRead;
	class CScopeUnlockRead;

	class CNoLock
	{
	public:
		inline_small void f_Construct()
		{
		}
		inline_small void f_Destruct()
		{
		}

		static inline_small void f_Lock()
		{
		}

		static inline_small void f_Unlock()
		{
		}

		bool f_OwnsLock() { return true; }

	};

	template <typename t_CLock>
	class TCScopeLock
	{
		t_CLock &m_Lock;
		TCScopeLock(TCScopeLock &&_Other);
		TCScopeLock(TCScopeLock const &_Other);
		TCScopeLock & operator = (TCScopeLock &&_Other);
		TCScopeLock & operator = (TCScopeLock const &_Other);
	public:

		inline_small TCScopeLock(t_CLock &_Lock)
			: m_Lock(_Lock)
		{
			_Lock.f_Lock();
		}

		inline_small ~TCScopeLock()
		{
			m_Lock.f_Unlock();
		}
	};

	template <typename t_CLock>
	class TCScopeUnlock
	{
		t_CLock &m_Lock;
		TCScopeUnlock(TCScopeUnlock &&_Other);
		TCScopeUnlock(TCScopeUnlock const &_Other);
		TCScopeUnlock & operator = (TCScopeUnlock &&_Other);
		TCScopeUnlock & operator = (TCScopeUnlock const &_Other);
	public:
		inline_small TCScopeUnlock(t_CLock &_Lock)
			: m_Lock(_Lock)
		{
			_Lock.f_Unlock();
		}

		inline_small ~TCScopeUnlock()
		{
			m_Lock.f_Lock();
		}
	};

	template <typename t_CLock>
	class TCScopeLockRead
	{
		t_CLock &m_Lock;
		TCScopeLockRead(TCScopeLockRead &&_Other);
		TCScopeLockRead(TCScopeLockRead const &_Other);
		TCScopeLockRead & operator = (TCScopeLockRead &&_Other);
		TCScopeLockRead & operator = (TCScopeLockRead const &_Other);
	public:

		inline_small TCScopeLockRead(t_CLock &_Lock)
			: m_Lock(_Lock)
		{
			_Lock.f_LockRead();
		}

		inline_small ~TCScopeLockRead()
		{
			m_Lock.f_UnlockRead();
		}

	};

	template <typename t_CLock>
	class TCScopeLockReadLocked
	{
		t_CLock &m_Lock;
		bool m_bWasLocked;
	public:

		inline_small TCScopeLockReadLocked(t_CLock &_Lock)
			: m_Lock(_Lock)
		{
			if (_Lock.f_OwnsLock())
				m_bWasLocked = true;
			else
			{
				m_bWasLocked = false;
				_Lock.f_LockRead();
			}
		}

		inline_small ~TCScopeLockReadLocked()
		{
			if (!m_bWasLocked)
				m_Lock.f_UnlockRead();
		}
	};

	template <typename t_CLock>
	class TCScopeUnlockRead
	{
		t_CLock &m_Lock;
		TCScopeUnlockRead(TCScopeUnlockRead &&_Other);
		TCScopeUnlockRead(TCScopeUnlockRead const &_Other);
		TCScopeUnlockRead & operator = (TCScopeUnlockRead &&_Other);
		TCScopeUnlockRead & operator = (TCScopeUnlockRead const &_Other);
	public:
		inline_small TCScopeUnlockRead(t_CLock &_Lock)
			: m_Lock(_Lock)
		{
			_Lock.f_UnlockRead();
		}

		inline_small ~TCScopeUnlockRead()
		{
			m_Lock.f_LockRead();
		}
	};


	template <typename t_CLock>
	class TCMovableScopeLock
	{
		t_CLock *m_pLock;
		TCMovableScopeLock(TCMovableScopeLock const &_Other);
		TCMovableScopeLock & operator = (TCMovableScopeLock const &_Other);
	public:

		inline_small TCMovableScopeLock(t_CLock &_Lock)
			: m_pLock(&_Lock)
		{
			_Lock.f_Lock();
		}
		inline_small TCMovableScopeLock(t_CLock &_Lock, bool _bTryLock)
			: m_pLock(nullptr)
		{
			if (_bTryLock)
			{
				if (_Lock.f_TryLock())
					m_pLock = &_Lock;
			}
			else
			{
				m_pLock = &_Lock;
				_Lock.f_Lock();
			}
		}

		inline_small ~TCMovableScopeLock()
		{
			if (m_pLock)
				m_pLock->f_Unlock();
		}

		inline_small TCMovableScopeLock(TCMovableScopeLock && _Other)
			: m_pLock(_Other.m_pLock)
		{
			_Other.m_pLock = nullptr;
		}

		inline_small TCMovableScopeLock & operator = (TCMovableScopeLock && _Other)
		{
			m_pLock = _Other.m_pLock;
			_Other.m_pLock = nullptr;
			return *this;
		}

		bool f_IsLocked() const
		{
			return m_pLock != nullptr;
		}

	};

	template <typename t_CLock>
	class TCMovableScopeUnlock
	{
		t_CLock * m_pLock;
		TCMovableScopeUnlock(TCMovableScopeUnlock const &_Other);
		TCMovableScopeUnlock & operator = (TCMovableScopeUnlock const &_Other);
	public:
		inline_small TCMovableScopeUnlock(t_CLock & _Lock)
			: m_pLock(&_Lock)
		{
			_Lock.f_Unlock();
		}

		inline_small ~TCMovableScopeUnlock()
		{
			if (m_pLock)
				m_pLock->f_Lock();
		}

		inline_small TCMovableScopeUnlock(TCMovableScopeUnlock && _Other)
			: m_pLock(_Other.m_pLock)
		{
			_Other.m_pLock = nullptr;
		}

		inline_small TCMovableScopeUnlock & operator = (TCMovableScopeUnlock && _Other)
		{
			m_pLock = _Other.m_pLock;
			_Other.m_pLock = nullptr;
			return *this;
		}
	};

	template <typename t_CLock>
	class TCMovableScopeLockRead
	{
		t_CLock *m_pLock;
		TCMovableScopeLockRead(TCMovableScopeLockRead const &_Other);
		TCMovableScopeLockRead & operator = (TCMovableScopeLockRead const &_Other);
	public:

		inline_small TCMovableScopeLockRead(t_CLock & _Lock)
			: m_pLock(&_Lock)
		{
			_Lock.f_LockRead();
		}

		inline_small ~TCMovableScopeLockRead()
		{
			if (m_pLock)
				m_pLock->f_UnlockRead();
		}

		inline_small TCMovableScopeLockRead(TCMovableScopeLockRead && _Other)
			: m_pLock(_Other.m_pLock)
		{
			_Other.m_pLock = nullptr;
		}

		inline_small TCMovableScopeLockRead & operator = (TCMovableScopeLockRead && _Other)
		{
			m_pLock = _Other.m_pLock;
			_Other.m_pLock = nullptr;
			return *this;
		}
	};

	template <typename t_CLock>
	class TCMovableScopeUnlockRead
	{
		t_CLock *m_pLock;
		TCMovableScopeUnlockRead(TCMovableScopeUnlockRead const &_Other);
		TCMovableScopeUnlockRead & operator =(TCMovableScopeUnlockRead const &_Other);
	public:
		inline_small TCMovableScopeUnlockRead(t_CLock &_Lock)
			: m_pLock(&_Lock)
		{
			_Lock.f_UnlockRead();
		}

		inline_small ~TCMovableScopeUnlockRead()
		{
			if (m_pLock)
				m_pLock->f_LockRead();
		}

		inline_small TCMovableScopeUnlockRead(TCMovableScopeUnlockRead && _Other)
			: m_pLock(_Other.m_pLock)
		{
			_Other.m_pLock = nullptr;
		}

		inline_small TCMovableScopeUnlockRead & operator = (TCMovableScopeUnlockRead && _Other)
		{
			m_pLock = _Other.m_pLock;
			_Other.m_pLock = nullptr;
			return *this;
		}
	};

	using FLock = void (void *_pLock);

	class CLockVirtual
	{
	public:

		virtual void f_Construct(void * _pSemaphore) = 0;
		virtual void f_Construct() = 0;
		virtual void f_Destruct() = 0;
		virtual void f_Lock() = 0;
		virtual void f_Unlock() = 0;
		virtual void f_LockRead() = 0;
		virtual void f_UnlockRead() = 0;
	};
#if defined(DPlatformFamily_Linux) || defined(DPlatformFamily_Windows) || defined(DPlatformFamily_macOS)
	using CLowLevelLockAggregateLockType = uint32;
#endif

	struct CLowLevelLockAggregate
	{
		constexpr CLowLevelLockAggregate(EAggregateInitialization _Init)
			: m_Lock{0}
#	if DMibEnableSafeCheck > 0
			, m_ThreadID{0}
			, m_AlternateThreadID{0}
			, m_nForked{0}
#	endif
		{
		}
		constexpr CLowLevelLockAggregate()
			: m_Lock{0}
#	if DMibEnableSafeCheck > 0
			, m_ThreadID{0}
			, m_AlternateThreadID{0}
			, m_nForked{0}
#	endif
		{
		}

		DMibThreadAtomicsAlignment NAtomic::TCAtomic<CLowLevelLockAggregateLockType> m_Lock;

#		if DMibEnableSafeCheck > 0
			umint m_ThreadID;				// On windows this is the thread id, unix the pthread
			umint m_AlternateThreadID;	// On windows this is also the thread id, on macOS and linux this is the kernel thread id that can be used to match threads in the debugger
			umint m_nForked;
#		endif

		void f_ForkedChildUnlocked();
		void f_ForkedChildLocked();
		void f_Construct();
		void f_Destruct();
		void f_Lock();
		bool f_TryLock();
		void f_Unlock();
		void f_LockNoSanitize();
		bool f_TryLockNoSanitize();
		void f_UnlockNoSanitize();
	};

	struct CLowLevelContendedLockAggregate : public CLowLevelLockAggregate
	{
		void f_Lock()
		{
			if (!CLowLevelLockAggregate::f_TryLock())
			{
				++m_Contention;
				CLowLevelLockAggregate::f_Lock();
				--m_Contention;
			}
		}

		void f_LockNoSanitize()
		{
			if (!CLowLevelLockAggregate::f_TryLockNoSanitize())
			{
				++m_Contention;
				CLowLevelLockAggregate::f_LockNoSanitize();
				--m_Contention;
			}
		}

		bool f_Contended() const
		{
			return m_Contention.f_Load(NAtomic::gc_MemoryOrder_Relaxed) > 0;
		}


		void f_ForkedChildUnlocked()
		{
			CLowLevelLockAggregate::f_ForkedChildUnlocked();
			m_Contention = 0;
		}

		void f_ForkedChildLocked()
		{
			CLowLevelLockAggregate::f_ForkedChildLocked();
			m_Contention = 0;
		}

		align_cacheline NAtomic::TCAtomic<uint32> m_Contention;
	};

	struct CLowLevelLock : public CLowLevelLockAggregate
	{
		CLowLevelLock()
		{
			f_Construct();
		}

		~CLowLevelLock()
		{
			f_Destruct();
		}
	};

	struct CLowLevelContendedLock : public CLowLevelContendedLockAggregate
	{
		CLowLevelContendedLock()
		{
			f_Construct();
		}

		~CLowLevelContendedLock()
		{
			f_Destruct();
		}
	};

	struct CLowLevelRecursiveLockAggregate : public CLowLevelLockAggregate
	{
		void f_Lock()
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

		void f_LockNoSanitize()
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

		void f_ForkedChildUnlocked()
		{
			CLowLevelLockAggregate::f_ForkedChildUnlocked();

			m_ThreadID.f_Store(0, NAtomic::gc_MemoryOrder_Relaxed);
			m_nRecurse = 0;
		}

		void f_ForkedChildLocked()
		{
			CLowLevelLockAggregate::f_ForkedChildLocked();

			m_ThreadID.f_Store(NSys::fg_Thread_GetCurrentUID(), NAtomic::gc_MemoryOrder_Relaxed);
			m_nRecurse = 1;
		}

		void f_Unlock()
		{
			DMibFastCheck(m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == NSys::fg_Thread_GetCurrentUID());

			if ((--m_nRecurse) == 0) [[likely]]
			{
				m_ThreadID.f_Store(0, NAtomic::gc_MemoryOrder_Relaxed);
				CLowLevelLockAggregate::f_Unlock();
			}
		}

		void f_UnlockNoSanitize()
		{
			DMibFastCheck(m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == NSys::fg_Thread_GetCurrentUID());

			if ((--m_nRecurse) == 0) [[likely]]
			{
				m_ThreadID.f_Store(0, NAtomic::gc_MemoryOrder_Relaxed);
				CLowLevelLockAggregate::f_UnlockNoSanitize();
			}
		}

		NAtomic::TCAtomic<umint> m_ThreadID;
		umint m_nRecurse = 0;
	};

	struct CLowLevelRecursiveLock : public CLowLevelRecursiveLockAggregate
	{
		CLowLevelRecursiveLock()
		{
			f_Construct();
		}

		~CLowLevelRecursiveLock()
		{
			f_Destruct();
		}
	};

	template <typename t_CEvent>
	class TCMutualSimpleAggregate
	{
	public:
		inline_never void fp_WaitForIt()
		{
			CDisableLazyCheckoutReturnScope DisableLazy;
			m_Event.f_Wait();
		}

		DMibThreadAtomicsAlignment NAtomic::TCAtomic<umint> m_nLocked;
		t_CEvent m_Event;

		constexpr TCMutualSimpleAggregate(EAggregateInitialization _Init)
			: m_nLocked{0}
			, m_Event{_Init}
		{
		}

		TCMutualSimpleAggregate()
		{
		}

		void f_Construct()
		{
			DMibSanitizerAnnotate_MutexCreate(this, __tsan_mutex_not_static);

			m_Event.f_Construct();
			m_nLocked.f_Store(0, NAtomic::gc_MemoryOrder_Relaxed);
		}

		void f_Destruct()
		{
			DMibSanitizerAnnotate_MutexDestroy(this, 0);
			m_Event.f_Destruct();
		}

		void f_Lock()
		{
			DMibSanitizerAnnotate_MutexPreLock(this, 0);
			if (m_nLocked.f_FetchAdd(1, NAtomic::gc_MemoryOrder_Acquire) > 0)
				fp_WaitForIt();
			DMibSanitizerAnnotate_MutexPostLock(this, 0, 1);
		}

		void f_Unlock()
		{
			DMibSanitizerAnnotate_MutexPreUnlock(this, 0);
			if (m_nLocked.f_FetchSub(1, NAtomic::gc_MemoryOrder_Release) > 1)
			{
				// Someone is waiting
				m_Event.f_Signal();
			}
			DMibSanitizerAnnotate_MutexPostUnlock(this, 0);
		}

		void f_LockRead()
		{
			f_Lock();
		}
		void f_UnlockRead()
		{
			f_Unlock();
		}
	};

	template <typename t_CEvent>
	class TCMutualSimple : public TCMutualSimpleAggregate<t_CEvent>
	{
	public:
		TCMutualSimple()
		{
			TCMutualSimpleAggregate<t_CEvent>::f_Construct();
		}

		~TCMutualSimple()
		{
			TCMutualSimpleAggregate<t_CEvent>::f_Destruct();
		}
	};

	struct CThreadSpinWaiter
	{
		inline_always void f_Wait()
		{
			if (++m_nWaits < 100)
			{
				yield_cpu;
				return;
			}

			f_WaitSlow();
		}

		inline_never void f_WaitSlow();

		umint m_nWaits = 0;
	};

	template <typename t_CEvent, bool t_bAllowRecursive>
	class TCMutualAggregate
	{
	public:

		enum
		{
			EAtomicBits = sizeof(aint) * 8
		};

		DMibThreadAtomicsAlignment NAtomic::TCAtomic<umint> m_nLocked;
		NAtomic::TCAtomic<umint> m_ThreadID;				// On windows this is the thread id, unix the pthread
#		if DMibEnableSafeCheck > 0
			umint m_AlternateThreadID;	// On windows this is also the thread id, on macOS and linux this is the kernel thread id that can be used to match threads in the debugger
#		endif
		aint m_nRecurse;
		t_CEvent m_Event;

		void f_PrepareFork()
		{
			DMibFastCheck(m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == NSys::fg_Thread_GetCurrentUID()); // Must already be locked
			DMibFastCheck(m_nRecurse == 1); // Must already be locked

			m_Event.f_PrepareFork();
		}

		void f_ForkedChild()
		{
			m_Event.f_ForkedChild();

			// Clear out any other threads that might have been waiting for the lock
			//m_nRecurse = 1; // This should be safe to keep
			m_ThreadID.f_Store(NSys::fg_Thread_GetCurrentUID(), NAtomic::gc_MemoryOrder_Relaxed);
			m_nLocked.f_FetchAnd(~mcp_AtomicMask);
			m_nLocked.f_FetchAdd(1);
		}

		void f_ForkedParent()
		{
			m_Event.f_ForkedParent();
		}

		inline_never void fp_WaitForIt()
		{
			CDisableLazyCheckoutReturnScope DisableLazy;
			m_Event.f_Wait();
		}

		inline_never void fp_SignalIt()
		{
			// Someone is waiting
			m_Event.f_Signal();
		}

		static const umint mcp_AtomicMask = DMibBitRangeOne(0, EAtomicBits-3, umint(1));

	public:
		constexpr TCMutualAggregate(EAggregateInitialization _Init)
			: m_nLocked{0}
			, m_ThreadID{0}
			, m_nRecurse{0}
			, m_Event{_Init}
#		if DMibEnableSafeCheck > 0
			, m_AlternateThreadID(0)
#		endif
		{
		}

		TCMutualAggregate()
		{
		}

		void f_Construct()
		{
//				m_Event.f_Construct();
//				m_nLocked.f_Construct(umint(2) << (EAtomicBits-2));
			DMibSanitizerAnnotate_MutexCreate(this, __tsan_mutex_write_reentrant | __tsan_mutex_not_static);

			m_Event.f_Construct();
			m_nLocked.f_Store(0, NAtomic::gc_MemoryOrder_Relaxed);
			m_nRecurse = 0;
			m_ThreadID = 0;
#		if DMibEnableSafeCheck > 0
			m_AlternateThreadID = 0;
#		endif
		}

		void f_Destruct()
		{
			DMibSanitizerAnnotate_MutexDestroy(this, 0);

			DMibFastCheck((m_nLocked.f_Load() & mcp_AtomicMask) == 0);
			DMibFastCheck(m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == 0);
			m_Event.f_Destruct();
		}

		inline_never bool f_TryLock()
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

		bool f_IsLocked()
		{
			return m_ThreadID.f_Load() != 0;
		}

		inline_never bool f_TryLock(aint _SpinCount)
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
				{
					continue;
				}
				if (m_nLocked.f_CompareExchangeWeak(Original, Original + 1, NAtomic::gc_MemoryOrder_Acquire, NAtomic::gc_MemoryOrder_Relaxed))
				{
					m_ThreadID.f_Store(CurrentThread, NAtomic::gc_MemoryOrder_Relaxed);
#		if DMibEnableSafeCheck > 0
					m_AlternateThreadID = NSys::fg_Thread_GetCurrentUIDAlternate();
#		endif
					m_nRecurse = 1;

					DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant | __tsan_mutex_try_lock, 1);
					return true;
				}
			}

			DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant | __tsan_mutex_try_lock | __tsan_mutex_try_lock_failed, 1);
			return false;
		}

		inline_never void f_SetLockForThread(umint _ThreadID)
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
#		if DMibEnableSafeCheck > 0
			m_AlternateThreadID = NSys::fg_Thread_GetCurrentUIDAlternate();
#		endif
			m_nRecurse = 1;
		}

		inline_never void f_Lock()
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
#			if DMibEnableSafeCheck > 0
				m_AlternateThreadID = NSys::fg_Thread_GetCurrentUIDAlternate();
#			endif

			m_nRecurse = 1;

			DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant, 1);
		}

		inline_never void f_Lock(aint _SpinCount)
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
#		if DMibEnableSafeCheck > 0
					m_AlternateThreadID = NSys::fg_Thread_GetCurrentUIDAlternate();
#		endif
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
#			if DMibEnableSafeCheck > 0
				m_AlternateThreadID = NSys::fg_Thread_GetCurrentUIDAlternate();
#			endif
			m_nRecurse = 1;

			DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant, 1);
		}

		bool f_OwnsLock() const
		{
			umint CurrentThread = NSys::fg_Thread_GetCurrentUID();
			return m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == CurrentThread;
		}

		bool f_Contended() const
		{
			DMibFastCheck(f_OwnsLock());
			return (m_nLocked.f_Load(NAtomic::gc_MemoryOrder_Relaxed) & mcp_AtomicMask) > 1;
		}

		inline_never void f_Unlock()
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

		void f_LockRead()
		{
			f_Lock();
		}
		void f_UnlockRead()
		{
			f_Unlock();
		}
	};

	template <typename t_CEvent, umint _nSpins, bool t_bAllowRecursive>
	class TCMutualSpinAggregate : public TCMutualAggregate<t_CEvent, t_bAllowRecursive>
	{
	public:
		constexpr TCMutualSpinAggregate(EAggregateInitialization _Init)
			: TCMutualAggregate<t_CEvent, t_bAllowRecursive>{_Init}
		{
		}
		TCMutualSpinAggregate()
		{
		}

		bool f_TryLock()
		{
			return TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_TryLock(_nSpins);
		}
		void f_Lock()
		{
			return TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_Lock(_nSpins);
		}
		void f_LockRead()
		{
			f_Lock();
		}
	};

	template <typename t_CEvent, bool t_bAllowRecursive>
	class TCMutual : public TCMutualAggregate<t_CEvent, t_bAllowRecursive>
	{
		TCMutual(const TCMutual &);
		TCMutual &operator = (const TCMutual &);
	public:
		TCMutual()
		{
			TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_Construct();
		}

		~TCMutual()
		{
			TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_Destruct();
		}
	};

	template <typename t_CEvent, umint _nSpins, bool t_bAllowRecursive>
	class TCMutualSpin : public TCMutualSpinAggregate<t_CEvent, _nSpins, t_bAllowRecursive>
	{
		TCMutualSpin(const TCMutualSpin &);
		TCMutualSpin &operator = (const TCMutualSpin &);
	public:
		TCMutualSpin()
		{
			TCMutualSpinAggregate<t_CEvent, _nSpins, t_bAllowRecursive>::f_Construct();
		}

		~TCMutualSpin()
		{
			TCMutualSpinAggregate<t_CEvent, _nSpins, t_bAllowRecursive>::f_Destruct();
		}
	};

	using CMutualSimpleAggregate = TCMutualSimpleAggregate<CEventAutoResetAggregate>;
	using CMutualSimple = TCMutualSimple<CEventAutoResetAggregate>;
	using CMutualAggregate = TCMutualAggregate<CEventAutoResetAggregate, true>;
	using CMutual = TCMutual<CEventAutoResetAggregate, true>;
	using CMutualNoRecurse = TCMutual<CEventAutoResetAggregate, false>;
	using CMutualSpinAggregate = TCMutualSpinAggregate<CEventAutoResetAggregate, 64, true>;
	using CMutualSpin = TCMutualSpin<CEventAutoResetAggregate, 64, true>;

	/***************************************************************************************************\
	|¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯|
	| MutualManyRead																				|
	|___________________________________________________________________________________________________|
	\***************************************************************************************************/

	template <typename t_CEventAutoreset, typename t_CBase = TCMutualAggregate<t_CEventAutoreset, true>>
	class TCMutualManyReadAggregate : public t_CBase
	{
	protected:
		using t_CBase::mcp_AtomicMask;
	public:
		constexpr TCMutualManyReadAggregate(EAggregateInitialization _Init)
			: t_CBase{_Init}
		{
		}

		TCMutualManyReadAggregate()
		{
		}

		// Reader count with writer/reader-waiter flags in the top bits; readers
		// park directly on this word via NSys::fg_Futex_*
		DMibThreadAtomicsAlignment NAtomic::TCAtomic<uint32> m_nReading;
		t_CEventAutoreset m_WriteOkEvent;
#		if DMibEnableSafeCheck > 0
			DMibThreadAtomicsAlignment NAtomic::TCAtomic<umint> m_nReadingDebugCheck;
#		endif

		const static uint32 mc_FlagReadingNotAllowed = DMibBitTyped(31, uint32);
		const static uint32 mc_FlagReadersWaiting = DMibBitTyped(30, uint32);
		const static uint32 mc_nReadingMask = ~(mc_FlagReadingNotAllowed | mc_FlagReadersWaiting);


		void f_Construct()
		{
			t_CBase::f_Construct();
			m_nReading.f_Store(0, NAtomic::gc_MemoryOrder_Relaxed);
			m_WriteOkEvent.f_Construct();
#			if DMibEnableSafeCheck > 0
				m_nReadingDebugCheck.f_Store(0, NAtomic::gc_MemoryOrder_Relaxed);
#			endif
		}

		void f_Destruct()
		{
			DMibFastCheck((m_nReading.f_Load() & mc_nReadingMask) == 0);
			m_WriteOkEvent.f_Destruct();
			t_CBase::f_Destruct();
		}

		void f_PrepareFork()
		{
			t_CBase::f_PrepareFork();
			m_WriteOkEvent.f_PrepareFork();
		}

		void f_ForkedChild()
		{
			m_WriteOkEvent.f_ForkedChild();
			m_nReading.f_Store(mc_FlagReadingNotAllowed);
#			if DMibEnableSafeCheck > 0
				m_nReadingDebugCheck.f_Store(0, NAtomic::gc_MemoryOrder_Relaxed);
#			endif

			t_CBase::f_ForkedChild();
		}

		void f_ForkedParent()
		{
			m_WriteOkEvent.f_ForkedParent();
			t_CBase::f_ForkedParent();
		}

		inline_never void f_LockRead()
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
#			if DMibEnableSafeCheck > 0
				m_nReadingDebugCheck.f_FetchAdd(1, NAtomic::gc_MemoryOrder_Relaxed);
#			endif

			DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant | __tsan_mutex_read_lock, 1);
		}


		inline_never void f_UnlockReadInternal()
		{
			uint32 nReading = m_nReading.f_FetchSub(1, NAtomic::gc_MemoryOrder_AcquireRelease);
			if ((nReading & (mc_FlagReadingNotAllowed | mc_nReadingMask)) == (mc_FlagReadingNotAllowed | 1))
			{
				m_WriteOkEvent.f_Signal();
			}
		}

		inline_never void f_UnlockRead()
		{
			DMibSanitizerAnnotate_MutexPreUnlock(this, __tsan_mutex_read_lock);
			umint CurrentThread = NMib::NSys::fg_Thread_GetCurrentUID();

			if (t_CBase::m_ThreadID.f_Load(NAtomic::gc_MemoryOrder_Relaxed) == CurrentThread)
			{
				f_Unlock();
				return;
			}

#			if DMibEnableSafeCheck > 0
				m_nReadingDebugCheck.f_FetchSub(1, NAtomic::gc_MemoryOrder_Relaxed);
#			endif
			f_UnlockReadInternal();
			DMibSanitizerAnnotate_MutexPostUnlock(this, __tsan_mutex_read_lock);
		}


		inline_never void f_Lock()
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
#		if DMibEnableSafeCheck > 0
			t_CBase::m_AlternateThreadID = NSys::fg_Thread_GetCurrentUIDAlternate();
#		endif
			t_CBase::m_nRecurse = 1;
			DMibFastCheck(m_nReadingDebugCheck.f_Load() == 0);

			DMibSanitizerAnnotate_MutexPostLock(this, __tsan_mutex_write_reentrant, 1);
		}


		inline_never bool f_TryLock()
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
#		if DMibEnableSafeCheck > 0
				t_CBase::m_AlternateThreadID = NSys::fg_Thread_GetCurrentUIDAlternate();
#		endif
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

		inline_never void f_Unlock()
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

	};

	template <typename t_CEventAutoreset, typename t_CBase = TCMutualAggregate<t_CEventAutoreset, true>>
	class TCMutualManyRead : public TCMutualManyReadAggregate<t_CEventAutoreset, t_CBase>
	{
		TCMutualManyRead(TCMutualManyRead const &);
		TCMutualManyRead &operator = (const TCMutualManyRead&);
	public:
		TCMutualManyRead()
		{
			TCMutualManyReadAggregate<t_CEventAutoreset, t_CBase>::f_Construct();
		}

		~TCMutualManyRead()
		{
			TCMutualManyReadAggregate<t_CEventAutoreset, t_CBase>::f_Destruct();
		}
	};

	using CMutualManyReadAggregate = TCMutualManyReadAggregate<CEventAutoResetAggregate>;
	using CMutualManyRead = TCMutualManyRead<CEventAutoResetAggregate>;

	class CScopeUnlock
	{
		void *m_pLock;
		FLock *m_pLockFunc;
	public:

		template <typename t_Lock>
		class TLocker
		{
		public:
			inline_small static void fs_Locker(void *_pLock)
			{
				((t_Lock *)_pLock)->f_Lock();
			}
		};

		template <typename t_Lock>
		inline_small CScopeUnlock(t_Lock &_Lock)
		{
			_Lock.f_Unlock();
			m_pLock = &_Lock;
			m_pLockFunc = TLocker<t_Lock>::fs_Locker;
		}

		inline_small ~CScopeUnlock()
		{
			m_pLockFunc(m_pLock);
		}
	};

	class CScopeLockRead
	{
		void *m_pLock;
		FLock *m_pUnlockFunc;
	public:

		template <typename t_Lock>
		class TLocker
		{
		public:
			inline_small static void fs_Locker(void *_pLock)
			{
				((t_Lock *)_pLock)->f_UnlockRead();
			}
		};

		template <typename t_Lock>
		inline_small CScopeLockRead(t_Lock &_Lock)
		{
			_Lock.f_LockRead();
			m_pLock = &_Lock;
			m_pUnlockFunc = TLocker<t_Lock>::fs_Locker;
		}

		inline_small ~CScopeLockRead()
		{
			m_pUnlockFunc(m_pLock);
		}
	};

	class CScopeUnlockRead
	{
		void *m_pLock;
		FLock *m_pLockFunc;
	public:

		template <typename t_Lock>
		class TLocker
		{
		public:
			static void fs_Locker(void *_pLock)
			{
				((t_Lock *)_pLock)->f_LockRead();
			}
		};

		template <typename t_Lock>
		CScopeUnlockRead(t_Lock &_Lock)
		{
			_Lock.f_UnlockRead();
			m_pLock = &_Lock;
			m_pLockFunc = TLocker<t_Lock>::fs_Locker;
		}

		~CScopeUnlockRead()
		{
			m_pLockFunc(m_pLock);
		}
	};

/*		template <class t_LockType>
	t_LockType GetLockType()*/

#	define DMibLock(_ToLock) NMib::NThread::TCScopeLock<decltype(_ToLock)> ScopeLockMutual1(_ToLock)
#	define DMibUnlock(_ToUnlock) NMib::NThread::TCScopeUnlock<decltype(_ToUnlock)> ScopeUnlockMutual1(_ToUnlock)
#	define DMibLockRead(_ToLock) NMib::NThread::TCScopeLockRead<decltype(_ToLock)> ScopeLockReadMutual1(_ToLock)
#	define DMibLockReadLocked(_ToLock) NMib::NThread::TCScopeLockReadLocked<decltype(_ToLock)> ScopeLockReadMutual1(_ToLock)
#	define DMibUnlockRead(_ToUnlock) NMib::NThread::TCScopeUnlockRead<decltype(_ToUnlock)> ScopeUnlockReadMutual1(_ToUnlock)

#	define DMibLockTyped(_Type, _ToLock) NMib::NThread::TCScopeLock<_Type> ScopeLockMutualTyped1(_ToLock)
#	define DMibUnlockTyped(_Type, _ToUnlock) NMib::NThread::TCScopeUnlock<_Type> ScopeUnlockMutualTyped1(_ToUnlock)

#	define DMibLockReadTyped(_Type, _ToLock) NMib::NThread::TCScopeLockRead<_Type> ScopeLockReadMutualTyped1(_ToLock)
#	define DMibUnlockReadTyped(_Type, _ToUnlock) NMib::NThread::TCScopeUnlockRead<_Type> ScopeUnlockReadMutualTyped1(_ToUnlock)


#	ifndef DMibPNoShortCuts
#		define DLock(_ToLock) DMibLock(_ToLock)
#		define DUnlock(_ToLock) DMibUnlock(_ToLock)
#		define DLockTyped(_Type, _ToLock) DMibLockTyped(_Type, _ToLock)
#		define DUnlockTyped(_Type, _ToLock) DMibUnlockTyped(_Type, _ToLock)
#		define DLockRead(_ToLock) DMibLockRead(_ToLock)
#		define DLockReadLocked(_ToLock) DMibLockReadLocked(_ToLock)
#		define DUnlockRead(_ToLock) DMibUnlockRead(_ToLock)
#		define DLockTypedRead(_Type, _ToLock) DMibLockTypedRead(_Type, _ToLock)
#		define DUnlockTypedRead(_Type, _ToLock) DMibUnlockTypedRead(_Type, _ToLock)
#	endif

	template <typename t_tData>
	t_tData fg_Thread_GetDataLocked(const t_tData &_Data, CMutual &_Lock)
	{
		DMibLock(_Lock);
		return _Data;
	}

	enum EThreadState
	{
		 EThreadState_None = 0
		,EThreadState_Stopped = 1
		,EThreadState_Running = 2
		,EThreadState_EventWantQuit = 3
	};

	class CThread
	{
	private:
		align_cacheline mutable CMutual m_Lock;
		aint m_ReturnValue;
		void *m_pThread;
		umint m_ThreadID;
		umint m_ParentThreadID;
		void *m_pThreadDestroyContext;
		bool m_bAutoDestroy;
		bool m_bLockHeld;
		CEvent m_ThreadQuitEvent;

		align_cacheline NAtomic::TCAtomic<uint32> m_StateAtomic{EThreadState_None};

		static aint fsp_ThreadMain(void *_pContext);
		void fp_Cleanup();

		CThread(const CThread &);
		CThread &operator = (const CThread &);

	public:

		// This event will be signaled when the thread is requested to quit
		CEventAutoReset m_EventWantQuit;

		CThread();
		virtual ~CThread();

		virtual bool f_DestroyThread(); // Return true if the thread was destroyed (deleted)

		void f_PrepareFork();
		void f_ForkedChild();
		void f_ForkedParent();

		/*¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯*\
		|	Function:			Get the state of the thread								|
		|																				|
		|	Returns:			The state of the thread @See(EThreadState)				|
		|																				|
		|	Comments:			Longer_description_not_mandatory						|
		\*_____________________________________________________________________________*/
		EThreadState f_GetState() const
		{
			return (EThreadState)m_StateAtomic.f_Load();
		}

		void *f_GetThread() const
		{
			return m_pThread;
		}

		umint f_GetThreadID() const
		{
			return m_ThreadID;
		}

		bool f_CallingFromThread()
		{
			return m_ThreadID == NSys::fg_Thread_GetCurrentUID();
		}

		/*¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯*\
		|	Function:			Starts the thead										|
		|																				|
		|	Parameters:																	|
		|		_StackSize:		The stack size you want to reserve for the thread		|
		|		_Prio:			The priority you want for the thread					|
		|																				|
		|	Comments:			Your overridden Main function will be called			|
		\*_____________________________________________________________________________*/
		virtual void f_Start(EExecutionPriority _Prio = EExecutionPriority_Normal, umint _StackSize = 0, umint _Affinity = 0, bool _bAutoDestroy = false);

		/*¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯*\
		|	Function:			Stops the thread										|
		|																				|
		|	Parameters:																	|
		|		_bBlock:		Blocks until the thread is stopped						|
		|																				|
		|	Returns:			The return value is the return value for the thread,	|
		|						but only if you specified the stop to block				|
		|																				|
		|	Comments:			Longer_description_not_mandatory						|
		\*_____________________________________________________________________________*/
		virtual umint f_Stop(bool _bBlock = true);

		/*¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯*\
		|	Function:			Returns the return value for a stopped thread			|
		|																				|
		|	Returns:			The return value of the stopped thread					|
		|																				|
		|	Comments:			If the thread isn't in the stopped state this function	|
		|						will throw an exception									|
		\*_____________________________________________________________________________*/
		aint f_GetReturnValue()
		{
			DMibLockTyped(CMutual, m_Lock);
			if (m_StateAtomic.f_Load() == EThreadState_Stopped)
				return m_ReturnValue;

			DMibError("You are trying to get a return value from a thread that isn't stopped (or has never run)");
		}

		/*¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯*\
		|	Function:			Suspends the thread										|
		|																				|
		|	Comments:			The suspence of the thread is reference counted,		|
		|						so if you call Suspend() twice you will have to call	|
		|						Resume() twice for the execution of the thread to		|
		|						resume.													|
		\*_____________________________________________________________________________*/
		void f_Suspend();

		/*¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯*\
		|	Function:			Resumes the thread										|
		|																				|
		|	Comments:			The suspence of the thread is reference counted,		|
		|						so if you call Suspend() twice you will have to call	|
		|						Resume() twice for the execution of the thread to		|
		|						resume.													|
		\*_____________________________________________________________________________*/
		void f_Resume();

		/*¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯*\
		|	Function:			Sets the priority of the thread							|
		|																				|
		|	Parameters:																	|
		|		_Prio:			The priority @See(EExecutionPriority)					|
		|																				|
		|	Comments:			.														|
		\*_____________________________________________________________________________*/
		void f_SetPriority(EExecutionPriority _Prio);

		/*¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯*\
		|	Function:			The main function of the thread. You will have to		|
		|						override this function to be able to do anything with	|
		|						your thread.											|
		|																				|
		|	Returns:			The overridden function should return its thread return	|
		|						value.													|
		|																				|
		|	Comments:			You should check for									|
		|						m_State == EThreadState_EventWantQuit in your overridden|
		|						function. m_EventWantQuit will be signaled once when a	|
		|						quit has been requested for the thread.					|
		\*_____________________________________________________________________________*/
		virtual aint f_Main() = 0;

		virtual NStr::CStr f_GetThreadName() = 0;
		virtual ch8 const *f_GetThreadNameRaw();

	};

	template <typename t_CAllocator, typename t_CStr>
	class TCThreadObject final : private CThread
	{
		TCThreadObject(TCThreadObject const &) = delete;
		TCThreadObject &operator = (TCThreadObject const &) = delete;

		class CCallerObject
		{
		public:
			virtual ~CCallerObject()
			{
			}
			virtual aint f_Call(TCThreadObject *_pThread) = 0;
			virtual ch8 const * f_GetName() = 0;
		};

		template <typename tf_ObjectType, typename tf_CAllocator, typename... tfp_CParams>
		friend tf_ObjectType *NMib::fg_ConstructObject(tf_CAllocator &&_Allocator, tfp_CParams&&... p_Params);

		NStorage::TCUniquePointer<CCallerObject, t_CAllocator> m_pCallerObject;

		TCThreadObject(NStorage::TCUniquePointer<CCallerObject, t_CAllocator> &&_pCallerObject)
			: m_pCallerObject(fg_Move(_pCallerObject))
		{
		}

		aint f_Main()
		{
			return m_pCallerObject->f_Call(this);
		}

		virtual NStr::CStr f_GetThreadName();
		virtual ch8 const *f_GetThreadNameRaw();

	public:

		using CThread::f_Resume;
		using CThread::f_Stop;
		using CThread::f_SetPriority;
		using CThread::f_GetReturnValue;
		using CThread::f_CallingFromThread;
		using CThread::f_GetState;
		using CThread::m_EventWantQuit;
		using CThread::f_GetThread;
		using CThread::f_GetThreadID;
		using CThread::f_PrepareFork;
		using CThread::f_ForkedParent;
		using CThread::f_ForkedChild;

		~TCThreadObject()
		{
			f_Stop();
		}

		template <typename tf_CFunctionType>
		static NStorage::TCUniquePointer<TCThreadObject, t_CAllocator>
		fs_StartThread
			(
				tf_CFunctionType &&_FunctionObject
				, const t_CStr &_Name
				, EExecutionPriority _Prio = EExecutionPriority_Normal
				, umint _StackSize = 0
				, umint _Affinity = 0
				, bool _bAutoDestroy = false
			)
		;

		template <typename tf_CFunctionType>
		static NStorage::TCUniquePointer<TCThreadObject, t_CAllocator>
		fs_StartThread
			(
				tf_CFunctionType *_pFunctionObject
				, const t_CStr &_Name
				, EExecutionPriority _Prio = EExecutionPriority_Normal
				, umint _StackSize = 0
				, umint _Affinity = 0
				, bool _bAutoDestroy = false
			)
		;

	};

	using CThreadObject = TCThreadObject<NMemory::CDefaultAllocator, NStr::CStr>;
	using CThreadObjectNonTracked = TCThreadObject<NMemory::CAllocator_NonTrackedHeap, NStr::CStrNonTracked>;
};

namespace NMib::NStorage
{
	/////////////////////////////////////////////////////////////////////////
	// Intrusive refcount base

	template <CSharedPointerOptionUnderlying t_Options, typename t_CCountType>
	struct TCIntrusiveRefCount;

	using CIntrusiveRefCount = TCIntrusiveRefCount<>;
	using CIntrusiveRefCountWithWeak = TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer>;

#if DMibConfig_RefCountDebugging
	struct CRefCountDebug
	{
#if DMibConfig_RefCountLeakDebugging
		CRefCountDebug();
		~CRefCountDebug();

		static umint ms_Magic;

		umint m_Magic = ms_Magic;
		NAtomic::TCAtomic<umint> m_DestroyLocation = 0b100000000000000000000000000000000u;
#endif
		NThread::CLowLevelLock m_Lock;
		NContainer::TCLinkedList<NException::CCallstack, NMemory::CAllocator_NonTrackedHeap> m_Callstacks;
		NContainer::TCLinkedList<NException::CCallstack, NMemory::CAllocator_NonTrackedHeap> m_WeakCallstacks;
	};
#endif

	template <typename t_CCountType>
	struct TCIntrusiveRefCount<ESharedPointerOption_None, t_CCountType>
	{
		mutable NAtomic::TCAtomic<t_CCountType> m_RefCount; // -1 means no references

		~TCIntrusiveRefCount();

		TCIntrusiveRefCount()
			: m_RefCount(0)
		{
			DIfRefCountDebugging(m_Debug.f_Construct());
		}

		TCIntrusiveRefCount(t_CCountType _RefCount)
			: m_RefCount(_RefCount)
		{
			DIfRefCountDebugging(m_Debug.f_Construct());
		}

		TCIntrusiveRefCount(TCIntrusiveRefCount const &)
			: m_RefCount(0)
		{
			DIfRefCountDebugging(m_Debug.f_Construct());
		}

		TCIntrusiveRefCount(TCIntrusiveRefCount &&)
			: m_RefCount(0)
		{
			DIfRefCountDebugging(m_Debug.f_Construct());
		}

		TCIntrusiveRefCount &operator = (TCIntrusiveRefCount const &)
		{
			return *this;
		}

		TCIntrusiveRefCount &operator = (TCIntrusiveRefCount &&)
		{
			return *this;
		}

#if DMibConfig_RefCountDebugging
		void f_Initial(CRefCountDebugReference &o_Reference) const;
		void f_Remove(CRefCountDebugReference &o_Reference) const;
		t_CCountType f_Decrease(CRefCountDebugReference &o_Reference) const;
		t_CCountType f_Increase
			(
				CRefCountDebugReference &o_Reference
#if DMibEnableSafeCheck > 0
				, bool _bAllowRevive = false
#endif
			) const;
		void f_Move(CRefCountDebugReference &o_SourceReference, CRefCountDebugReference &o_DestinationReference) const;
#else
		t_CCountType f_Decrease() const
		{
			t_CCountType Return = m_RefCount.f_FetchSub(1, NAtomic::gc_MemoryOrder_Release);
			DMibFastCheck(Return >= 0);
			if (Return == 0)
			{
#ifdef DMibSanitizerEnabled_Thread
				m_RefCount.f_Load(NAtomic::gc_MemoryOrder_Acquire);
#else
				NAtomic::fg_MemoryFence(NAtomic::gc_MemoryOrder_Acquire);
#endif
			}
			return Return;
		}

		t_CCountType f_Increase
			(
#if DMibEnableSafeCheck > 0
				bool _bAllowRevive = false
#endif
			) const
		{
			t_CCountType Return = m_RefCount.f_FetchAdd(1, NAtomic::gc_MemoryOrder_Relaxed);
			DMibFastCheck(Return >= 0 || _bAllowRevive && Return == -1);
			DMibFastCheck(Return < (TCLimitsInt<t_CCountType>::mc_Max - 1));
			return Return;
		}
#endif

		t_CCountType f_Get() const
		{
			return m_RefCount.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
		}

#if DMibConfig_RefCountDebugging
#if DMibConfig_RefCountLeakDebugging
		static umint ms_Magic;
		umint m_Magic = ms_Magic;
#endif
		mutable NStorage::TCAggregateSimple<CRefCountDebug> m_Debug;
#endif
	};

	template <typename t_CCountType>
	struct TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>
	{
		mutable NAtomic::TCAtomic<t_CCountType> m_RefCount; // -1 means no references
		mutable NAtomic::TCAtomic<t_CCountType> m_WeakRefCount; // -1 means no references

		~TCIntrusiveRefCount();
		TCIntrusiveRefCount()
			: m_RefCount(0)
			, m_WeakRefCount(0)
		{
			DIfRefCountDebugging(m_Debug.f_Construct());
		}

		TCIntrusiveRefCount(TCIntrusiveRefCount const &)
			: m_RefCount(0)
			, m_WeakRefCount(0)
		{
			DIfRefCountDebugging(m_Debug.f_Construct());
		}

		TCIntrusiveRefCount(TCIntrusiveRefCount &&)
			: m_RefCount(0)
			, m_WeakRefCount(0)
		{
			DIfRefCountDebugging(m_Debug.f_Construct());
		}

		TCIntrusiveRefCount &operator = (TCIntrusiveRefCount const &)
		{
			return *this;
		}

		TCIntrusiveRefCount &operator = (TCIntrusiveRefCount &&)
		{
			return *this;
		}

#if DMibConfig_RefCountDebugging
		void f_Initial(CRefCountDebugReference &o_Reference) const;
		void f_Remove(CRefCountDebugReference &o_Reference) const;
		t_CCountType f_Decrease(CRefCountDebugReference &o_Reference) const;
		t_CCountType f_Increase
			(
				CRefCountDebugReference &o_Reference
#if DMibEnableSafeCheck > 0
				, bool _bAllowRevive = false
#endif
			) const
		;
		void f_WeakInitial(CRefCountDebugReference &o_Reference) const;
		void f_WeakRemove(CRefCountDebugReference &o_Reference) const;
		bool f_IncreaseWhileValid(CRefCountDebugReference &o_Reference) const;
		t_CCountType f_WeakDecrease(CRefCountDebugReference *o_pReference) const;
		t_CCountType f_WeakIncrease(CRefCountDebugReference &o_Reference) const;

		void f_Move(CRefCountDebugReference &o_SourceReference, CRefCountDebugReference &o_DestinationReference) const;
		void f_WeakMove(CRefCountDebugReference &o_SourceReference, CRefCountDebugReference &o_DestinationReference) const;
#else
		t_CCountType f_Decrease() const
		{
			t_CCountType Return = m_RefCount.f_FetchSub(1, NAtomic::gc_MemoryOrder_Release);
			DMibFastCheck(Return >= 0);
			if (Return == 0)
			{
#ifdef DMibSanitizerEnabled_Thread
				m_RefCount.f_Load(NAtomic::gc_MemoryOrder_Acquire);
#else
				NAtomic::fg_MemoryFence(NAtomic::gc_MemoryOrder_Acquire);
#endif
			}
			return Return;
		}

		t_CCountType f_Increase
			(
#if DMibEnableSafeCheck > 0
				bool _bAllowRevive = false
#endif
			) const
		{
			t_CCountType Return = m_RefCount.f_FetchAdd(1, NAtomic::gc_MemoryOrder_Relaxed);
			DMibFastCheck(Return >= 0 || _bAllowRevive && Return == -1);
			DMibFastCheck(Return < (TCLimitsInt<t_CCountType>::mc_Max - 1));

			return Return;
		}

		bool f_IncreaseWhileValid() const
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

		t_CCountType f_WeakDecrease() const
		{
			t_CCountType Return = m_WeakRefCount.f_FetchSub(1, NAtomic::gc_MemoryOrder_Release);
			if (Return == 0)
			{
#ifdef DMibSanitizerEnabled_Thread
				m_WeakRefCount.f_Load(NAtomic::gc_MemoryOrder_Acquire);
#else
				NAtomic::fg_MemoryFence(NAtomic::gc_MemoryOrder_Acquire);
#endif
			}
			return Return;
		}

		t_CCountType f_WeakIncrease() const
		{
			auto Return = m_WeakRefCount.f_FetchAdd(1, NAtomic::gc_MemoryOrder_Relaxed);
			DMibFastCheck(Return < (TCLimitsInt<t_CCountType>::mc_Max - 1));

			return Return;
		}
#endif

		void f_WeakSetCapturedDelete(NMemory::CCapturedDelete const &_CapturedDelete) const
		{
			*((void **)(this+1)) = _CapturedDelete.m_pMemory;
			[[maybe_unused]] t_CCountType CurrentValue = m_RefCount.f_Exchange(t_CCountType(-1) - t_CCountType(_CapturedDelete.m_Size));
			DMibFastCheck(CurrentValue == -1);
		}

		NMemory::CCapturedDelete f_WeakGetCapturedDelete() const
		{
			return {*((void * const *)(this+1)), umint(t_CCountType(-1) -m_RefCount.f_Load(NAtomic::gc_MemoryOrder_Acquire))};
		}

		t_CCountType f_Get() const
		{
			return m_RefCount.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
		}

		t_CCountType f_WeakGet() const
		{
			return m_WeakRefCount.f_Load(NAtomic::gc_MemoryOrder_Relaxed);
		}

#if DMibConfig_RefCountDebugging
#if DMibConfig_RefCountLeakDebugging
		static umint ms_Magic;
		umint m_Magic = ms_Magic;
#endif
		mutable NStorage::TCAggregateSimple<CRefCountDebug> m_Debug;
#endif
	};

#if DMibConfig_RefCountDebugging && DMibConfig_RefCountLeakDebugging
	template <>
	umint TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, smint>::ms_Magic;

	template <>
	umint TCIntrusiveRefCount<ESharedPointerOption_None, smint>::ms_Magic;

	template <>
	umint TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, int32>::ms_Magic;

	template <>
	umint TCIntrusiveRefCount<ESharedPointerOption_None, int32>::ms_Magic;
#endif

	namespace NPrivate
	{
		template <typename t_CType, bool t_bVirtualDestructor, CSharedPointerOptionUnderlying t_Options>
		struct TCSharedPointerCounter
		{
			TCSharedPointerCounter(TCSharedPointerCounter const &_Other) = default;
			TCSharedPointerCounter(TCSharedPointerCounter &&_Other) = default;

			template <typename... tfp_CParams>
			TCSharedPointerCounter(tfp_CParams &&...p_Params)
				requires (NTraits::cIsPlacementNewConstructibleWith<t_CType, tfp_CParams...>)
				: m_Data(fg_Forward<tfp_CParams>(p_Params)...)
			{
			}

			mark_nodebug mark_artificial inline_always t_CType *f_Get()
			{
				return &m_Data;
			}

			TCIntrusiveRefCount<t_Options> m_RefCount;

		private:
			t_CType m_Data;
		};

		struct CSharedPointerCounterVirtualBase
		{
#ifdef DCompiler_MSVC_Workaround
			virtual ~CSharedPointerCounterVirtualBase()
			{
			}
#else
			virtual ~CSharedPointerCounterVirtualBase() = default;
#endif
		};

		template <typename t_CType, CSharedPointerOptionUnderlying t_Options>
		struct TCSharedPointerCounter<t_CType, true, t_Options> : public CSharedPointerCounterVirtualBase
		{
			TCSharedPointerCounter(TCSharedPointerCounter const &_Other) = default;
			TCSharedPointerCounter(TCSharedPointerCounter &&_Other) = default;

			template <typename... tfp_CParams>
			TCSharedPointerCounter(tfp_CParams &&...p_Params)
				requires (NTraits::cIsPlacementNewConstructibleWith<t_CType, tfp_CParams...>)
				: m_Data(fg_Forward<tfp_CParams>(p_Params)...)
			{
			}

			mark_nodebug mark_artificial inline_always t_CType *f_Get()
			{
				return &m_Data;
			}

			TCIntrusiveRefCount<t_Options> m_RefCount;

		private:
			t_CType m_Data;
		};


		template <typename t_CType, CSharedPointerOptionUnderlying t_Options>
		class TCChooseSharedPointerTypeImp<t_CType, t_Options, false>
		{
		public:
			using CType = TCSharedPointerCounter<t_CType, NTraits::cHasVirtualDestructor<NTraits::TCRemoveQualifiers<t_CType>>, t_Options>;
		};

		template <typename tf_CType, bool t_bVirtualDestructor, CSharedPointerOptionUnderlying t_Options>
		tf_CType *fg_GetSharedPointerPointer(TCSharedPointerCounter<tf_CType, t_bVirtualDestructor, t_Options> *_pIn)
		{
			static_assert
				(
					!cHasIntrusiveRefCount<tf_CType>
					, "Use DMibDefineSharedPointerType to define type"
				)
			;
			if (_pIn)
				return _pIn->f_Get();
			return nullptr;
		}

		template <typename tf_CToType, typename tf_CType, bool tf_bToVirtualDestructor, bool tf_bVirtualDestructor, CSharedPointerOptionUnderlying tf_ToOptions, CSharedPointerOptionUnderlying tf_Options>
		TCSharedPointerCounter<tf_CToType, tf_bToVirtualDestructor, tf_ToOptions> *fg_ConvertSharedPointer(TCSharedPointerCounter<tf_CType, tf_bVirtualDestructor, tf_Options> *_pIn, TCSharedPointerCounter<tf_CToType, tf_bToVirtualDestructor, tf_ToOptions> *_pDummy)
		{
			static_assert(TCIsValidConversion<tf_CToType, tf_CType, void, void>::mc_Value, "Not a valid conversion");
			static_assert(!NTraits::cHasVirtualDestructor<tf_CToType> || NTraits::cHasVirtualDestructor<TCSharedPointerCounter<tf_CToType, tf_bToVirtualDestructor, tf_ToOptions>>, "No virtual base");
			static_assert(NTraits::cHasVirtualDestructor<tf_CToType> || !NTraits::cHasVirtualDestructor<TCSharedPointerCounter<tf_CToType, tf_bToVirtualDestructor, tf_ToOptions>>, "Virtual base");
			static_assert(tf_ToOptions == tf_Options, "Cannot mix weak support with non-weak support");
			static_assert(alignof(tf_CToType) == alignof(tf_CType), "Cannot mix alignment, use TCIntrusiveRefCount");
			static_assert(!NTraits::cIsVirtualBaseOf<tf_CType, tf_CToType>, "Virtual base classes are not supported, use TCIntrusiveRefCount");

			auto pCovertTo = (TCSharedPointerCounter<tf_CToType, tf_bToVirtualDestructor, tf_ToOptions> *)_pIn;
			DMibFastCheck(pCovertTo->f_Get() == (tf_CToType *)_pIn->f_Get());
			return pCovertTo;
		}
	}
}
