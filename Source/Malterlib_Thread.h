// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>
#include "../../Core/Source/Malterlib_Core_PlatformInterface.h"
#include <Mib/Atomic/Atomic>

#if 0
#	define DMibThreadAtomicsAlignment align_cacheline
#else
#	define DMibThreadAtomicsAlignment
#endif

#if DMibConfig_RefCountDebugging
#	include <Mib/Container/LinkedList>
#	include <Mib/Storage/Aggregate>
#endif

namespace NMib::NThread
{
	struct CDisableLazyCheckoutReturnScope
	{
		CDisableLazyCheckoutReturnScope();
		~CDisableLazyCheckoutReturnScope();
	};

	// Futex-based counting semaphore. The count is saturated at m_Maximum (extra
	// signals are silently dropped). The count and the number of parked waiters
	// are packed into one 64-bit word so that a signal hands over tokens and
	// observes the waiter count in a single atomic operation: as soon as that
	// operation lands, a woken waiter may legally destroy the semaphore
	// (completion-event pattern), so the signaling side must not touch the
	// object afterwards and only issues futex wakes, which are safe on stale
	// addresses. Slow paths are implemented in the Core platform layer.
	struct CSemaphoreAggregate
	{
		// Low 32 bits: count (the futex word); high 32 bits: parked waiters
		NAtomic::TCAtomic<uint64> m_Data;
		uint32 m_Maximum;

		constexpr CSemaphoreAggregate(EAggregateInitialization _Init);
		constexpr CSemaphoreAggregate(EAggregateInitialization _Init, uint32 _Maximum);
		CSemaphoreAggregate();
		inline_small void f_Construct(aint _Initial = 0, aint _Max = TCLimitsInt<aint>::mc_Max);
		inline_small void f_Destruct();
		void f_PrepareFork();
		void f_ForkedChild();
		void f_ForkedParent();
		inline_small void f_Signal(int _nToSignal);
		inline_small void f_Signal();
		inline_small bool f_TryWait();
		inline_small void f_Wait();
		inline_small bool f_WaitTimeout(fp64 _Timeout);

		// External waiters must register before waiting on the count word at zero, then consume tokens with f_TryWait.
		// Registration ensures signals issue futex wakes even when no f_Wait caller is registered.

		inline_small uint32 volatile *f_GetExternalFutexWord();
		inline_small void f_ExternalWaiterRegister();
		inline_small void f_ExternalWaiterUnregister();

	private:
		static constexpr uint64 mcp_ValueMask = TCLimitsInt<uint32>::mc_Max;
		static constexpr uint64 mcp_WaiterOne = uint64(1) << 32;

		inline_small uint32 volatile *fp_GetFutexWord();

		// Implemented in the Core platform layer
		void fp_WaitSlow();
		bool fp_WaitTimeoutSlow(fp64 _Timeout);
		void fp_WakeSlow(uint32 _Delta, uint32 _nWaiters);
	};

	static_assert(sizeof(NAtomic::TCAtomic<uint32>) == sizeof(uint32));
	static_assert(sizeof(NAtomic::TCAtomic<uint64>) == sizeof(uint64));

	struct CEventAutoResetAggregate : public CSemaphoreAggregate
	{
		constexpr CEventAutoResetAggregate(EAggregateInitialization _Init);
		CEventAutoResetAggregate();
		inline_small void f_Construct();
	};

	struct CEventAutoReset : public CEventAutoResetAggregate
	{
		CEventAutoReset();
		~CEventAutoReset();

		CEventAutoReset(CEventAutoReset &&_Other) = delete;
		CEventAutoReset &operator = (CEventAutoReset &&_Other) = delete;
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
	struct CEventAggregate
	{
		NAtomic::TCAtomic<uint32> m_State;

		constexpr CEventAggregate(EAggregateInitialization _Init);
		CEventAggregate();
		void f_PrepareFork();
		void f_ForkedChild();
		void f_ForkedParent();
		inline_small void f_Construct(bool _bInitialSignal = false);
		inline_small void f_Destruct();
		inline_small void f_SetSignaled();
		inline_small void f_ResetSignaled();
		inline_small void f_Wait();
		inline_small bool f_WaitTimeout(fp64 _Timeout);
		inline_small bool f_TryWait();

		/// Whether a waiter is parked on the event, so a test can make the signal race with it.
		inline_small bool f_HasWaiters() const;

	private:
		static constexpr uint32 mcp_FlagSignaled = 0x1;
		static constexpr uint32 mcp_FlagWaiters = 0x2;
		static constexpr uint32 mcp_GenerationOne = 0x4;
		static constexpr uint32 mcp_GenerationMask = ~(mcp_FlagSignaled | mcp_FlagWaiters);

		// Implemented in the Core platform layer
		void fp_WaitSlow();
		bool fp_WaitTimeoutSlow(fp64 _Timeout);
		void fp_WakeAllSlow();
	};

	struct CEvent : public CEventAggregate
	{
		CEvent();
		~CEvent();
	};

	struct CSemaphore : public CSemaphoreAggregate
	{
		CSemaphore(aint _Initial = 0, aint _Max = TCLimitsInt<aint>::mc_Max);
		~CSemaphore();
	};

	struct CScopeLock;
	struct CScopeUnlock;
	struct CScopeLockRead;
	struct CScopeUnlockRead;

	struct CNoLock
	{
		inline_small void f_Construct();
		inline_small void f_Destruct();
		inline_small void f_Lock();
		inline_small void f_Unlock();
		bool f_OwnsLock();
	};

	template <typename t_CLock>
	struct TCScopeLock
	{
		inline_small TCScopeLock(t_CLock &_Lock);
		inline_small ~TCScopeLock();

	private:
		t_CLock &mp_Lock;
		TCScopeLock(TCScopeLock &&_Other);
		TCScopeLock(TCScopeLock const &_Other);
		TCScopeLock &operator = (TCScopeLock &&_Other);
		TCScopeLock &operator = (TCScopeLock const &_Other);
	};

	template <typename t_CLock>
	struct TCScopeUnlock
	{
		inline_small TCScopeUnlock(t_CLock &_Lock);
		inline_small ~TCScopeUnlock();

	private:
		t_CLock &mp_Lock;
		TCScopeUnlock(TCScopeUnlock &&_Other);
		TCScopeUnlock(TCScopeUnlock const &_Other);
		TCScopeUnlock &operator = (TCScopeUnlock &&_Other);
		TCScopeUnlock &operator = (TCScopeUnlock const &_Other);
	};

	template <typename t_CLock>
	struct TCScopeLockRead
	{
		inline_small TCScopeLockRead(t_CLock &_Lock);
		inline_small ~TCScopeLockRead();

	private:
		t_CLock &mp_Lock;
		TCScopeLockRead(TCScopeLockRead &&_Other);
		TCScopeLockRead(TCScopeLockRead const &_Other);
		TCScopeLockRead &operator = (TCScopeLockRead &&_Other);
		TCScopeLockRead &operator = (TCScopeLockRead const &_Other);
	};

	template <typename t_CLock>
	struct TCScopeLockReadLocked
	{
		inline_small TCScopeLockReadLocked(t_CLock &_Lock);
		inline_small ~TCScopeLockReadLocked();

	private:
		t_CLock &mp_Lock;
		bool mp_bWasLocked;
	};

	template <typename t_CLock>
	struct TCScopeUnlockRead
	{
		inline_small TCScopeUnlockRead(t_CLock &_Lock);
		inline_small ~TCScopeUnlockRead();

	private:
		t_CLock &mp_Lock;
		TCScopeUnlockRead(TCScopeUnlockRead &&_Other);
		TCScopeUnlockRead(TCScopeUnlockRead const &_Other);
		TCScopeUnlockRead &operator = (TCScopeUnlockRead &&_Other);
		TCScopeUnlockRead &operator = (TCScopeUnlockRead const &_Other);
	};

	template <typename t_CLock>
	struct TCMovableScopeLock
	{
		inline_small TCMovableScopeLock(t_CLock &_Lock);
		inline_small TCMovableScopeLock(t_CLock &_Lock, bool _bTryLock);
		inline_small ~TCMovableScopeLock();
		inline_small TCMovableScopeLock(TCMovableScopeLock &&_Other);
		inline_small TCMovableScopeLock &operator = (TCMovableScopeLock &&_Other);
		bool f_IsLocked() const;

	private:
		t_CLock *mp_pLock;
		TCMovableScopeLock(TCMovableScopeLock const &_Other);
		TCMovableScopeLock &operator = (TCMovableScopeLock const &_Other);
	};

	template <typename t_CLock>
	struct TCMovableScopeUnlock
	{
		inline_small TCMovableScopeUnlock(t_CLock &_Lock);
		inline_small ~TCMovableScopeUnlock();
		inline_small TCMovableScopeUnlock(TCMovableScopeUnlock &&_Other);
		inline_small TCMovableScopeUnlock &operator = (TCMovableScopeUnlock &&_Other);

	private:
		t_CLock *mp_pLock;
		TCMovableScopeUnlock(TCMovableScopeUnlock const &_Other);
		TCMovableScopeUnlock &operator = (TCMovableScopeUnlock const &_Other);
	};

	template <typename t_CLock>
	struct TCMovableScopeLockRead
	{
		inline_small TCMovableScopeLockRead(t_CLock &_Lock);
		inline_small ~TCMovableScopeLockRead();
		inline_small TCMovableScopeLockRead(TCMovableScopeLockRead &&_Other);
		inline_small TCMovableScopeLockRead &operator = (TCMovableScopeLockRead &&_Other);

	private:
		t_CLock *mp_pLock;
		TCMovableScopeLockRead(TCMovableScopeLockRead const &_Other);
		TCMovableScopeLockRead &operator = (TCMovableScopeLockRead const &_Other);
	};

	template <typename t_CLock>
	struct TCMovableScopeUnlockRead
	{
		inline_small TCMovableScopeUnlockRead(t_CLock &_Lock);
		inline_small ~TCMovableScopeUnlockRead();
		inline_small TCMovableScopeUnlockRead(TCMovableScopeUnlockRead &&_Other);
		inline_small TCMovableScopeUnlockRead &operator = (TCMovableScopeUnlockRead &&_Other);

	private:
		t_CLock *mp_pLock;
		TCMovableScopeUnlockRead(TCMovableScopeUnlockRead const &_Other);
		TCMovableScopeUnlockRead &operator = (TCMovableScopeUnlockRead const &_Other);
	};

	using FLock = void (void *_pLock);

	struct CLockVirtual
	{
		virtual void f_Construct(void *_pSemaphore) = 0;
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
		constexpr CLowLevelLockAggregate(EAggregateInitialization _Init);
		constexpr CLowLevelLockAggregate();

		DMibThreadAtomicsAlignment NAtomic::TCAtomic<CLowLevelLockAggregateLockType> m_Lock;

#if DMibEnableSafeCheck > 0
		umint m_ThreadID;          // On windows this is the thread id, unix the pthread
		umint m_AlternateThreadID; // On windows this is also the thread id, on macOS and linux this is the kernel thread id that can be used to match threads in the debugger
		umint m_nForked;
#endif

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
		void f_Lock();
		void f_LockNoSanitize();
		bool f_Contended() const;
		void f_ForkedChildUnlocked();
		void f_ForkedChildLocked();

		align_cacheline NAtomic::TCAtomic<uint32> m_Contention;
	};

	struct CLowLevelLock : public CLowLevelLockAggregate
	{
		CLowLevelLock();
		~CLowLevelLock();
	};

	struct CLowLevelContendedLock : public CLowLevelContendedLockAggregate
	{
		CLowLevelContendedLock();
		~CLowLevelContendedLock();
	};

	struct CLowLevelRecursiveLockAggregate : public CLowLevelLockAggregate
	{
		void f_Lock();
		void f_LockNoSanitize();
		void f_ForkedChildUnlocked();
		void f_ForkedChildLocked();
		void f_Unlock();
		void f_UnlockNoSanitize();

		NAtomic::TCAtomic<umint> m_ThreadID;
		umint m_nRecurse = 0;
	};

	struct CLowLevelRecursiveLock : public CLowLevelRecursiveLockAggregate
	{
		CLowLevelRecursiveLock();
		~CLowLevelRecursiveLock();
	};

	template <typename t_CEvent>
	struct TCMutualSimpleAggregate
	{
		DMibThreadAtomicsAlignment NAtomic::TCAtomic<umint> m_nLocked;
		t_CEvent m_Event;

		constexpr TCMutualSimpleAggregate(EAggregateInitialization _Init);
		TCMutualSimpleAggregate();
		void f_Construct();
		void f_Destruct();
		void f_Lock();
		void f_Unlock();
		void f_LockRead();
		void f_UnlockRead();

	private:
		inline_never void fp_WaitForIt();
	};

	template <typename t_CEvent>
	struct TCMutualSimple : public TCMutualSimpleAggregate<t_CEvent>
	{
		TCMutualSimple();
		~TCMutualSimple();
	};

	// Pauses, then yields, then sleeps while another thread completes a condition.
	// A larger yield budget delays sleeping at the cost of CPU time.
	struct CThreadSpinWaiter
	{
		static constexpr umint mc_DefaultYields = 140;

		explicit CThreadSpinWaiter(umint _nYieldsBeforeSleep = mc_DefaultYields);

		inline_always void f_Wait();

		inline_never void f_WaitSlow();

		umint m_nWaits = 0;
		umint m_nYieldsBeforeSleep;
	};

	template <typename t_CEvent, bool t_bAllowRecursive>
	struct TCMutualAggregate
	{
		static constexpr aint mc_AtomicBits = sizeof(aint) * 8;

		DMibThreadAtomicsAlignment NAtomic::TCAtomic<umint> m_nLocked;
		NAtomic::TCAtomic<umint> m_ThreadID; // On windows this is the thread id, unix the pthread
#if DMibEnableSafeCheck > 0
		umint m_AlternateThreadID; // On windows this is also the thread id, on macOS and linux this is the kernel thread id that can be used to match threads in the debugger
#endif
		aint m_nRecurse;
		t_CEvent m_Event;

		void f_PrepareFork();
		void f_ForkedChild();
		void f_ForkedParent();
		constexpr TCMutualAggregate(EAggregateInitialization _Init);
		TCMutualAggregate();
		void f_Construct();
		void f_Destruct();
		inline_never bool f_TryLock();
		bool f_IsLocked();
		inline_never bool f_TryLock(aint _SpinCount);
		inline_never void f_SetLockForThread(umint _ThreadID);
		inline_never void f_Lock();
		inline_never void f_Lock(aint _SpinCount);
		bool f_OwnsLock() const;
		bool f_Contended() const;
		inline_never void f_Unlock();
		void f_LockRead();
		void f_UnlockRead();

	protected:
		static constexpr umint mcp_AtomicMask = DMibBitRangeOne(0, mc_AtomicBits - 3, umint(1));

		inline_never void fp_WaitForIt();
		inline_never void fp_SignalIt();
	};

	template <typename t_CEvent, umint t_nSpins, bool t_bAllowRecursive>
	struct TCMutualSpinAggregate : public TCMutualAggregate<t_CEvent, t_bAllowRecursive>
	{
		constexpr TCMutualSpinAggregate(EAggregateInitialization _Init);
		TCMutualSpinAggregate();
		bool f_TryLock();
		void f_Lock();
		void f_LockRead();
	};

	template <typename t_CEvent, bool t_bAllowRecursive>
	struct TCMutual : public TCMutualAggregate<t_CEvent, t_bAllowRecursive>
	{
		TCMutual();
		~TCMutual();

	private:
		TCMutual(TCMutual const &_Other);
		TCMutual &operator = (TCMutual const &_Other);
	};

	template <typename t_CEvent, umint t_nSpins, bool t_bAllowRecursive>
	struct TCMutualSpin : public TCMutualSpinAggregate<t_CEvent, t_nSpins, t_bAllowRecursive>
	{
		TCMutualSpin();
		~TCMutualSpin();

	private:
		TCMutualSpin(TCMutualSpin const &_Other);
		TCMutualSpin &operator = (TCMutualSpin const &_Other);
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
	struct TCMutualManyReadAggregate : public t_CBase
	{
	protected:
		using t_CBase::mcp_AtomicMask;

	public:
		constexpr TCMutualManyReadAggregate(EAggregateInitialization _Init);
		TCMutualManyReadAggregate();

		// Reader count with writer/reader-waiter flags in the top bits; readers
		// park directly on this word via NSys::fg_Futex_*
		DMibThreadAtomicsAlignment NAtomic::TCAtomic<uint32> m_nReading;
		t_CEventAutoreset m_WriteOkEvent;
#if DMibEnableSafeCheck > 0
		DMibThreadAtomicsAlignment NAtomic::TCAtomic<umint> m_nReadingDebugCheck;
#endif

		static constexpr uint32 mc_FlagReadingNotAllowed = DMibBitTyped(31, uint32);
		static constexpr uint32 mc_FlagReadersWaiting = DMibBitTyped(30, uint32);
		static constexpr uint32 mc_nReadingMask = ~(mc_FlagReadingNotAllowed | mc_FlagReadersWaiting);

		void f_Construct();
		void f_Destruct();
		void f_PrepareFork();
		void f_ForkedChild();
		void f_ForkedParent();
		inline_never void f_LockRead();
		inline_never void f_UnlockReadInternal();
		inline_never void f_UnlockRead();
		inline_never void f_Lock();
		inline_never bool f_TryLock();
		inline_never void f_Unlock();
	};

	template <typename t_CEventAutoreset, typename t_CBase = TCMutualAggregate<t_CEventAutoreset, true>>
	struct TCMutualManyRead : public TCMutualManyReadAggregate<t_CEventAutoreset, t_CBase>
	{
		TCMutualManyRead();
		~TCMutualManyRead();

	private:
		TCMutualManyRead(TCMutualManyRead const &_Other);
		TCMutualManyRead &operator = (TCMutualManyRead const &_Other);
	};

	using CMutualManyReadAggregate = TCMutualManyReadAggregate<CEventAutoResetAggregate>;
	using CMutualManyRead = TCMutualManyRead<CEventAutoResetAggregate>;

	struct CScopeUnlock
	{
		template <typename t_CLock>
		struct TCLocker
		{
			inline_small static void fs_Locker(void *_pLock);
		};

		template <typename tf_CLock>
		inline_small CScopeUnlock(tf_CLock &_Lock);
		inline_small ~CScopeUnlock();

	private:
		void *mp_pLock;
		FLock *mp_fLockFunc;
	};

	struct CScopeLockRead
	{
		template <typename t_CLock>
		struct TCLocker
		{
			inline_small static void fs_Locker(void *_pLock);
		};

		template <typename tf_CLock>
		inline_small CScopeLockRead(tf_CLock &_Lock);
		inline_small ~CScopeLockRead();

	private:
		void *mp_pLock;
		FLock *mp_fUnlockFunc;
	};

	struct CScopeUnlockRead
	{
		template <typename t_CLock>
		struct TCLocker
		{
			static void fs_Locker(void *_pLock);
		};

		template <typename tf_CLock>
		CScopeUnlockRead(tf_CLock &_Lock);
		~CScopeUnlockRead();

	private:
		void *mp_pLock;
		FLock *mp_fLockFunc;
	};

#define DMibLock(d_ToLock) NMib::NThread::TCScopeLock<decltype(d_ToLock)> ScopeLockMutual1(d_ToLock)
#define DMibUnlock(d_ToUnlock) NMib::NThread::TCScopeUnlock<decltype(d_ToUnlock)> ScopeUnlockMutual1(d_ToUnlock)
#define DMibLockRead(d_ToLock) NMib::NThread::TCScopeLockRead<decltype(d_ToLock)> ScopeLockReadMutual1(d_ToLock)
#define DMibLockReadLocked(d_ToLock) NMib::NThread::TCScopeLockReadLocked<decltype(d_ToLock)> ScopeLockReadMutual1(d_ToLock)
#define DMibUnlockRead(d_ToUnlock) NMib::NThread::TCScopeUnlockRead<decltype(d_ToUnlock)> ScopeUnlockReadMutual1(d_ToUnlock)

#define DMibLockTyped(d_Type, d_ToLock) NMib::NThread::TCScopeLock<d_Type> ScopeLockMutualTyped1(d_ToLock)
#define DMibUnlockTyped(d_Type, d_ToUnlock) NMib::NThread::TCScopeUnlock<d_Type> ScopeUnlockMutualTyped1(d_ToUnlock)

#define DMibLockReadTyped(d_Type, d_ToLock) NMib::NThread::TCScopeLockRead<d_Type> ScopeLockReadMutualTyped1(d_ToLock)
#define DMibUnlockReadTyped(d_Type, d_ToUnlock) NMib::NThread::TCScopeUnlockRead<d_Type> ScopeUnlockReadMutualTyped1(d_ToUnlock)

#ifndef DMibPNoShortCuts
#	define DLock(d_ToLock) DMibLock(d_ToLock)
#	define DUnlock(d_ToLock) DMibUnlock(d_ToLock)
#	define DLockTyped(d_Type, d_ToLock) DMibLockTyped(d_Type, d_ToLock)
#	define DUnlockTyped(d_Type, d_ToLock) DMibUnlockTyped(d_Type, d_ToLock)
#	define DLockRead(d_ToLock) DMibLockRead(d_ToLock)
#	define DLockReadLocked(d_ToLock) DMibLockReadLocked(d_ToLock)
#	define DUnlockRead(d_ToLock) DMibUnlockRead(d_ToLock)
#	define DLockTypedRead(d_Type, d_ToLock) DMibLockTypedRead(d_Type, d_ToLock)
#	define DUnlockTypedRead(d_Type, d_ToLock) DMibUnlockTypedRead(d_Type, d_ToLock)
#endif

	template <typename tf_CData>
	tf_CData fg_Thread_GetDataLocked(tf_CData const &_Data, CMutual &_Lock);

	enum EThreadState
	{
		EThreadState_None            = 0
		, EThreadState_Stopped       = 1
		, EThreadState_Running       = 2
		, EThreadState_EventWantQuit = 3
	};

	struct CThread
	{
		// This event will be signaled when the thread is requested to quit
		CEventAutoReset m_EventWantQuit;

		CThread();
		virtual ~CThread();

		virtual bool f_DestroyThread(); // Return true if the thread was destroyed (deleted)

		void f_PrepareFork();
		void f_ForkedChild();
		void f_ForkedParent();

		EThreadState f_GetState() const;
		void *f_GetThread() const;
		umint f_GetThreadID() const;
		bool f_CallingFromThread();

		/*¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯*\
		|	Function:			Starts the thead										|
		|																				|
		|	Parameters:																	|
		|		_StackSize:		The stack size you want to reserve for the thread		|
		|		_Prio:			The priority you want for the thread					|
		|																				|
		|	Comments:			Your overridden Main function will be called			|
		\*_____________________________________________________________________________*/
		virtual void f_Start(EExecutionPriority _Prio = EExecutionPriority_Normal, umint _StackSize = 0, umint _Affinity = 0, EThreadCreateFlag _Flags = EThreadCreateFlag_None);

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

		aint f_GetReturnValue();

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

		/*¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯*\
		|	Function:			Sets the priority of the thread, reporting whether the	|
		|						OS allowed it											|
		|																								|
		|	Parameters:																			|
		|		_Prio:			The priority @See(EExecutionPriority)					|
		|																								|
		|	Returns:			true when the priority was set							|
		|																								|
		|	Comments:			For advisory priority changes. Unprivileged processes	|
		|						cannot raise thread priority on most systems.			|
		\*_____________________________________________________________________________*/
		bool f_TrySetPriority(EExecutionPriority _Prio);

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

	private:
		align_cacheline mutable CMutual mp_Lock;
		aint mp_ReturnValue;
		void *mp_pThread;
		umint mp_ThreadID;
		umint mp_ParentThreadID;
		void *mp_pThreadDestroyContext;
		bool mp_bAutoDestroy;
		bool mp_bLockHeld;
		CEvent mp_ThreadQuitEvent;

		align_cacheline NAtomic::TCAtomic<uint32> mp_StateAtomic{EThreadState_None};

		static aint fsp_ThreadMain(void *_pContext);
		void fp_Cleanup();

		CThread(CThread const &_Other);
		CThread &operator = (CThread const &_Other);
	};

	template <typename t_CAllocator, typename t_CStr>
	struct TCThreadObject final : private CThread
	{
		using CThread::f_Resume;
		using CThread::f_Stop;
		using CThread::f_SetPriority;
		using CThread::f_TrySetPriority;
		using CThread::f_GetReturnValue;
		using CThread::f_CallingFromThread;
		using CThread::f_GetState;
		using CThread::m_EventWantQuit;
		using CThread::f_GetThread;
		using CThread::f_GetThreadID;
		using CThread::f_PrepareFork;
		using CThread::f_ForkedParent;
		using CThread::f_ForkedChild;

		~TCThreadObject();

		template <typename tf_CFunctionType>
		static auto fs_StartThread
			(
				tf_CFunctionType &&_FunctionObject
				, t_CStr const &_Name
				, EExecutionPriority _Prio = EExecutionPriority_Normal
				, umint _StackSize = 0
				, umint _Affinity = 0
				, EThreadCreateFlag _Flags = EThreadCreateFlag_None
			)
			-> NStorage::TCUniquePointer<TCThreadObject, t_CAllocator>
		;

		template <typename tf_CFunctionType>
		static auto fs_StartThread
			(
				tf_CFunctionType *_pFunctionObject
				, t_CStr const &_Name
				, EExecutionPriority _Prio = EExecutionPriority_Normal
				, umint _StackSize = 0
				, umint _Affinity = 0
				, EThreadCreateFlag _Flags = EThreadCreateFlag_None
			)
			-> NStorage::TCUniquePointer<TCThreadObject, t_CAllocator>
		;

	private:
		TCThreadObject(TCThreadObject const &_Other) = delete;
		TCThreadObject &operator = (TCThreadObject const &_Other) = delete;

		struct CCallerObject
		{
			virtual ~CCallerObject();

			virtual aint f_Call(TCThreadObject *_pThread) = 0;
			virtual ch8 const *f_GetName() = 0;
		};

		template <typename tf_CObjectType, typename tf_CAllocator, typename ...tfp_CParams>
		friend tf_CObjectType *NMib::fg_ConstructObject(tf_CAllocator &&_Allocator, tfp_CParams && ...p_Params);

		NStorage::TCUniquePointer<CCallerObject, t_CAllocator> mp_pCallerObject;

		TCThreadObject(NStorage::TCUniquePointer<CCallerObject, t_CAllocator> &&_pCallerObject);
		aint f_Main();

		virtual NStr::CStr f_GetThreadName();
		virtual ch8 const *f_GetThreadNameRaw();
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
#	if DMibConfig_RefCountLeakDebugging
		CRefCountDebug();
		~CRefCountDebug();

		static umint ms_Magic;

		umint m_Magic = ms_Magic;
		NAtomic::TCAtomic<umint> m_DestroyLocation = 0b100000000000000000000000000000000u;
#	endif
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

		TCIntrusiveRefCount();
		TCIntrusiveRefCount(t_CCountType _RefCount);
		TCIntrusiveRefCount(TCIntrusiveRefCount const &_Other);
		TCIntrusiveRefCount(TCIntrusiveRefCount &&_Other);
		TCIntrusiveRefCount &operator = (TCIntrusiveRefCount const &_Other);
		TCIntrusiveRefCount &operator = (TCIntrusiveRefCount &&_Other);

#if DMibConfig_RefCountDebugging
		void f_Initial(CRefCountDebugReference &o_Reference) const;
		void f_Remove(CRefCountDebugReference &o_Reference) const;
		t_CCountType f_Decrease(CRefCountDebugReference &o_Reference) const;
		t_CCountType f_Increase
			(
				CRefCountDebugReference &o_Reference
#	if DMibEnableSafeCheck > 0
				, bool _bAllowRevive = false
#	endif
			)
			const
		;
		void f_Move(CRefCountDebugReference &o_SourceReference, CRefCountDebugReference &o_DestinationReference) const;
#else
		t_CCountType f_Decrease() const;
		t_CCountType f_Increase
			(
#	if DMibEnableSafeCheck > 0
				bool _bAllowRevive = false
#	endif
			)
			const
		;
#endif

		t_CCountType f_Get() const;

#if DMibConfig_RefCountDebugging
#	if DMibConfig_RefCountLeakDebugging
		static umint ms_Magic;
		umint m_Magic = ms_Magic;
#	endif
		mutable NStorage::TCAggregateSimple<CRefCountDebug> m_Debug;
#endif
	};

	template <typename t_CCountType>
	struct TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>
	{
		mutable NAtomic::TCAtomic<t_CCountType> m_RefCount;     // -1 means no references
		mutable NAtomic::TCAtomic<t_CCountType> m_WeakRefCount; // -1 means no references

		~TCIntrusiveRefCount();
		TCIntrusiveRefCount();
		TCIntrusiveRefCount(TCIntrusiveRefCount const &_Other);
		TCIntrusiveRefCount(TCIntrusiveRefCount &&_Other);
		TCIntrusiveRefCount &operator = (TCIntrusiveRefCount const &_Other);
		TCIntrusiveRefCount &operator = (TCIntrusiveRefCount &&_Other);

#if DMibConfig_RefCountDebugging
		void f_Initial(CRefCountDebugReference &o_Reference) const;
		void f_Remove(CRefCountDebugReference &o_Reference) const;
		t_CCountType f_Decrease(CRefCountDebugReference &o_Reference) const;
		t_CCountType f_Increase
			(
				CRefCountDebugReference &o_Reference
#	if DMibEnableSafeCheck > 0
				, bool _bAllowRevive = false
#	endif
			)
			const
		;
		void f_WeakInitial(CRefCountDebugReference &o_Reference) const;
		void f_WeakRemove(CRefCountDebugReference &o_Reference) const;
		bool f_IncreaseWhileValid(CRefCountDebugReference &o_Reference) const;
		t_CCountType f_WeakDecrease(CRefCountDebugReference *o_pReference) const;
		t_CCountType f_WeakIncrease(CRefCountDebugReference &o_Reference) const;

		void f_Move(CRefCountDebugReference &o_SourceReference, CRefCountDebugReference &o_DestinationReference) const;
		void f_WeakMove(CRefCountDebugReference &o_SourceReference, CRefCountDebugReference &o_DestinationReference) const;
#else
		t_CCountType f_Decrease() const;
		t_CCountType f_Increase
			(
#	if DMibEnableSafeCheck > 0
				bool _bAllowRevive = false
#	endif
			)
			const
		;
		bool f_IncreaseWhileValid() const;
		t_CCountType f_WeakDecrease() const;
		t_CCountType f_WeakIncrease() const;
#endif

		void f_WeakSetCapturedDelete(NMemory::CCapturedDelete const &_CapturedDelete) const;
		NMemory::CCapturedDelete f_WeakGetCapturedDelete() const;
		t_CCountType f_Get() const;
		t_CCountType f_WeakGet() const;

#if DMibConfig_RefCountDebugging
#	if DMibConfig_RefCountLeakDebugging
		static umint ms_Magic;
		umint m_Magic = ms_Magic;
#	endif
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

			template <typename ...tfp_CParams>
			TCSharedPointerCounter(tfp_CParams && ...p_Params)
				requires (NTraits::cIsPlacementNewConstructibleWith<t_CType, tfp_CParams...>)
			;

			mark_nodebug mark_artificial inline_always t_CType *f_Get();

			TCIntrusiveRefCount<t_Options> m_RefCount;

		private:
			t_CType mp_Data;
		};

		struct CSharedPointerCounterVirtualBase
		{
#ifdef DCompiler_MSVC_Workaround
			virtual ~CSharedPointerCounterVirtualBase();
#else
			virtual ~CSharedPointerCounterVirtualBase() = default;
#endif
		};

		template <typename t_CType, CSharedPointerOptionUnderlying t_Options>
		struct TCSharedPointerCounter<t_CType, true, t_Options> : public CSharedPointerCounterVirtualBase
		{
			TCSharedPointerCounter(TCSharedPointerCounter const &_Other) = default;
			TCSharedPointerCounter(TCSharedPointerCounter &&_Other) = default;

			template <typename ...tfp_CParams>
			TCSharedPointerCounter(tfp_CParams && ...p_Params)
				requires (NTraits::cIsPlacementNewConstructibleWith<t_CType, tfp_CParams...>)
			;

			mark_nodebug mark_artificial inline_always t_CType *f_Get();

			TCIntrusiveRefCount<t_Options> m_RefCount;

		private:
			t_CType mp_Data;
		};

		template <typename t_CType, CSharedPointerOptionUnderlying t_Options>
		struct TCChooseSharedPointerTypeImp<t_CType, t_Options, false>
		{
			using CType = TCSharedPointerCounter<t_CType, NTraits::cHasVirtualDestructor<NTraits::TCRemoveQualifiers<t_CType>>, t_Options>;
		};
	}
}
