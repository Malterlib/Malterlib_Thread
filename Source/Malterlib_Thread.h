// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include "../../Core/Source/Malterlib_Core_PlatformInterface.h"
#include <Mib/Atomic/Atomic>

#if 0
	#define DMibThreadAtomicsAlignment align_cacheline
#else
	#define DMibThreadAtomicsAlignment
#endif

#if DMibConfig_RefcountDebugging
#include <Mib/Container/LinkedList>
#include <Mib/Storage/Aggregate>
#endif

namespace NMib
{
	namespace NThread
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
		
		class CEventAutoResetAggregate
		{
		public:
			void *m_pSemaphore;
#ifndef DMibNoAggregateConstexpr
			constexpr CEventAutoResetAggregate(EAggregateInitialization _Init)
				: m_pSemaphore{nullptr}
			{
			}
			CEventAutoResetAggregate()
			{
			}
#endif
			inline_small void f_ConstructDontCreate()
			{
				m_pSemaphore = nullptr;
			}

			inline_small void f_ConstructIfNotCreated()
			{
				if (!m_pSemaphore)
					m_pSemaphore = NSys::fg_Semaphore_Alloc(0, 1);
				else
					DMibFastCheck(0);
			}

			inline_small void f_ConstructIfNotCreated() volatile
			{
				if (!fg_Volatile(m_pSemaphore))
					fg_Volatile(m_pSemaphore) = NSys::fg_Semaphore_Alloc(0, 1);
				else
					DMibFastCheck(0);
			}
			void f_PrepareFork()
			{
			}

			void f_ForkedChild()
			{
				NSys::fg_Semaphore_ForkedChild(m_pSemaphore);
			}
			
			void f_ForkedParent()
			{
			}
			

			inline_small bint f_IsCreated()
			{
				return m_pSemaphore != 0;
			}

			inline_small bint f_IsCreated() volatile
			{
				return fg_Volatile(m_pSemaphore) != 0;
			}

			inline_small void f_Construct()
			{
				m_pSemaphore = NSys::fg_Semaphore_Alloc(0, 1);
			}

			inline_small void f_Destruct()
			{
				if (m_pSemaphore)
					NSys::fg_Semaphore_Free(m_pSemaphore);
				m_pSemaphore = nullptr;
			}

			inline_small void f_SetSemaphore(void *_pSemaphore)
			{
				if (m_pSemaphore)
					NSys::fg_Semaphore_Free(m_pSemaphore);

				m_pSemaphore = _pSemaphore;
			}
            inline_small void f_Signal()
			{
				NSys::fg_Semaphore_Increase(m_pSemaphore, 1);
			}

			inline_small void f_Wait()
			{
				NSys::fg_Semaphore_Wait(m_pSemaphore);
			}

			// Returns true if the wait timed out
			inline_small bint f_WaitTimeout(fp64 _Timeout)
			{
				return NSys::fg_Semaphore_WaitTimeout(m_pSemaphore, _Timeout);
			}

			inline_small bint f_TryWait()
			{
				return NSys::fg_Semaphore_TryWait(m_pSemaphore);
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
			CEventAutoReset(CEventAutoReset &&_Other)
			{
				m_pSemaphore = _Other.m_pSemaphore;
				_Other.m_pSemaphore = nullptr;
			}
			CEventAutoReset &operator =(CEventAutoReset &&_Other)
			{
				m_pSemaphore = _Other.m_pSemaphore;
				_Other.m_pSemaphore = nullptr;
				return *this;
			}
		};


		class CEventAggregate
		{
		public:
			void *m_pEvent;
#ifndef DMibNoAggregateConstexpr
			constexpr CEventAggregate(EAggregateInitialization _Init)
				: m_pEvent{nullptr}
			{
			}
			CEventAggregate()
			{
			}
#endif
			inline_small void f_ConstructDontCreate()
			{
				m_pEvent = nullptr;
			}

			inline_small void f_ConstructIfNotCreated(bint _bInitialSignal = false)
			{
				if (!m_pEvent)
					m_pEvent = NSys::fg_Event_Alloc(_bInitialSignal);
				else
					DMibFastCheck(0);
			}

			inline_small bint f_IsCreated()
			{
				return m_pEvent != 0;
			}

			void f_PrepareFork()
			{
				NSys::fg_Event_PrepareFork(m_pEvent);
			}
			
			void f_ForkedChild()
			{
				NSys::fg_Event_ForkedChild(m_pEvent);
			}

			void f_ForkedParent()
			{
				NSys::fg_Event_ForkedParent(m_pEvent);
			}

			
			inline_small bint f_IsCreated() volatile
			{
				return fg_Volatile(m_pEvent) != 0;
			}

			inline_small void f_Construct(bint _bInitialSignal = false)
			{
				m_pEvent = NSys::fg_Event_Alloc(_bInitialSignal);
			}

			inline_small void f_Destruct()
			{
				if (m_pEvent)
					NSys::fg_Event_Free(m_pEvent);
				m_pEvent = nullptr;
			}

			inline_small void f_SetEvent(void *_pEvent)
			{
				if (m_pEvent)
					NSys::fg_Event_Free(m_pEvent);

				m_pEvent = _pEvent;
			}

            inline_small void f_SetSignaled()
			{
				NSys::fg_Event_SetSignaled(m_pEvent);
			}

			inline_small void f_ResetSignaled()
			{
				NSys::fg_Event_ResetSignaled(m_pEvent);
			}

			inline_small void f_Wait()
			{
				NSys::fg_Event_Wait(m_pEvent);
			}

			inline_small bint f_WaitTimeout(fp64 _Timeout)
			{
				return NSys::fg_Event_WaitTimeout(m_pEvent, _Timeout);
			}

			inline_small bint f_TryWait()
			{
				return NSys::fg_Event_TryWait(m_pEvent);
			}
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



		class CSemaphoreAggregate
		{
		public:
			void *m_pSemaphore;
#ifndef DMibNoAggregateConstexpr
			constexpr CSemaphoreAggregate(EAggregateInitialization _Init)
				: m_pSemaphore{nullptr}
			{
			}
			CSemaphoreAggregate()
			{
			}
#endif
			inline_small void f_Construct(aint _Initial = 0, aint _Max = TCLimitsInt<aint>::mc_Max)
			{
				m_pSemaphore = NSys::fg_Semaphore_Alloc(_Initial, _Max);
			}

			inline_small void f_Destruct()
			{
				if (m_pSemaphore)
					NSys::fg_Semaphore_Free(m_pSemaphore);
				m_pSemaphore = nullptr;
			}

			void f_PrepareFork()
			{
			}
			
			void f_ForkedChild()
			{
				NSys::fg_Semaphore_ForkedChild(m_pSemaphore);
			}
			
			void f_ForkedParent()
			{
			}
			
			inline_small void f_SetSemaphore(void *_pSemaphore)
			{
				if (m_pSemaphore)
					NSys::fg_Semaphore_Free(m_pSemaphore);

				m_pSemaphore = _pSemaphore;
			}
            inline_small void f_Signal(int _nToSignal)
			{
				NSys::fg_Semaphore_Increase(m_pSemaphore, _nToSignal);
			}
            inline_small void f_Signal()
			{
				NSys::fg_Semaphore_Increase(m_pSemaphore, 1);
			}
			
			inline_small void f_Wait()
			{
				NSys::fg_Semaphore_Wait(m_pSemaphore);
			}

			inline_small bint f_WaitTimeout(fp64 _Timeout)
			{
				return NSys::fg_Semaphore_WaitTimeout(m_pSemaphore, _Timeout);
			}

			inline_small bint f_TryWait()
			{
				return NSys::fg_Semaphore_TryWait(m_pSemaphore);
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

			bint f_OwnsLock() { return true; }

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
		

		typedef void (FLock)(void *_pLock);

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

		class CSpinLockAggregate
		{
		public:
			inline_never void fp_WaitForIt()
			{
				while (m_nLocked.f_TestAndSet(NAtomic::EMemoryOrder_Acquire))
				{
					yield_cpu;
					yield_cpu;
					yield_cpu;
					yield_cpu;
					yield_cpu;
					yield_cpu;
					yield_cpu;
					yield_cpu;
					yield_cpu;
					yield_cpu;
				}
			}
#ifndef DMibNoAggregateConstexpr
			constexpr CSpinLockAggregate(EAggregateInitialization _Init)
				: m_nLocked{0}
#	if DMibEnableSafeCheck > 0
				, m_ThreadID{0}
				, m_AlternateThreadID{0}
#	endif
			{
			}
			CSpinLockAggregate()
			{
			}
#endif

			NAtomic::CAtomicFlagAggregate m_nLocked;
			
#			if DMibEnableSafeCheck > 0
				mint m_ThreadID;				// On windows this is the thread id, unix the pthread
				mint m_AlternateThreadID;	// On windows this is also the thread id, on osx and linux this is the kernel thread id that can be used to match threads in the debugger
#			endif
			

			void f_Construct()
			{
				m_nLocked.f_Clear(NAtomic::EMemoryOrder_Relaxed);
			}

			void f_Destruct()
			{
			}

			void f_Lock()
			{
				if (m_nLocked.f_TestAndSet(NAtomic::EMemoryOrder_Acquire))
					fp_WaitForIt();
				
#				if DMibEnableSafeCheck > 0
					m_ThreadID = NSys::fg_Thread_GetCurrentUID();
					m_AlternateThreadID = NSys::fg_Thread_GetCurrentUIDAlternate();
#				endif
			}

			void f_Unlock()
			{
#				if DMibEnableSafeCheck > 0
					m_ThreadID = 0;
					m_AlternateThreadID = 0;
#				endif
				
				m_nLocked.f_Clear(NAtomic::EMemoryOrder_Release);
			}
		};

		class CSpinLock : public CSpinLockAggregate
		{
		public:
			CSpinLock()
			{
				f_Construct();
			}
			~CSpinLock()
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

			DMibThreadAtomicsAlignment NAtomic::TCAtomicAggregate<mint> m_nLocked;
			t_CEvent m_Event;
			
#ifndef DMibNoAggregateConstexpr
			constexpr TCMutualSimpleAggregate(EAggregateInitialization _Init)
				: m_nLocked{_Init}
				, m_Event{_Init}
			{
			}
			TCMutualSimpleAggregate()
			{
			}
#endif
			
			void f_Construct()
			{
				m_Event.f_Construct();
				m_nLocked.f_Store(0, NAtomic::EMemoryOrder_Relaxed);
			}

			void f_Construct(void * _pSemaphore)
			{
				m_Event.f_Construct(_pSemaphore);
				m_nLocked.f_Store(0, NAtomic::EMemoryOrder_Relaxed);
			}

			void f_Destruct()
			{
				m_Event.f_Destruct();
			}

			void f_Lock()
			{
				if (m_nLocked.f_FetchAdd(1, NAtomic::EMemoryOrder_Acquire) > 0)
					fp_WaitForIt();
			}

			void f_Unlock()
			{
				if (m_nLocked.f_FetchSub(1, NAtomic::EMemoryOrder_Release) > 1)
				{
					// Someone is waiting
					m_Event.f_Signal();
				}
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

			TCMutualSimple(void * _pSemaphore)
			{
				TCMutualSimpleAggregate<t_CEvent>::f_Construct(_pSemaphore);
			}

			~TCMutualSimple()
			{
				TCMutualSimpleAggregate<t_CEvent>::f_Destruct();
			}
		};

		template <typename t_CEvent, bool t_bAllowRecursive>
		class TCMutualAggregate
		{
		public:

			enum
			{
				EAtomicBits = sizeof(aint) * 8
			};
			
			DMibThreadAtomicsAlignment NAtomic::TCAtomicAggregate<mint> m_nLocked;
			mint m_ThreadID;				// On windows this is the thread id, unix the pthread
#			if DMibEnableSafeCheck > 0
				mint m_AlternateThreadID;	// On windows this is also the thread id, on osx and linux this is the kernel thread id that can be used to match threads in the debugger
#			endif
			aint m_nRecurse;
			t_CEvent m_Event;
			
			void f_PrepareFork()
			{
				DMibFastCheck(m_ThreadID == NSys::fg_Thread_GetCurrentUID()); // Must already be locked
				DMibFastCheck(m_nRecurse == 1); // Must already be locked
				
				f_CreateEvent();				
				m_Event.f_PrepareFork();
			}
			
			void f_ForkedChild()
			{
				m_Event.f_ForkedChild();
				
				// Clear out any other threads that might have been waiting for the lock
				//m_nRecurse = 1; // This should be safe to keep
				m_ThreadID = NSys::fg_Thread_GetCurrentUID();
				m_nLocked.f_FetchAnd(~mcp_AtomicMask);
				m_nLocked.f_FetchAdd(1);
			}
			
			void f_ForkedParent()
			{
				m_Event.f_ForkedParent();
			}
			
			inline_never void f_CreateEvent()
			{
				mint Original = m_nLocked.f_FetchOr((mint(1) << (EAtomicBits-2)), NAtomic::EMemoryOrder_Relaxed);
				if (!(Original & (mint(1) << (EAtomicBits-2))))
				{
					fg_Volatile(m_Event).f_ConstructIfNotCreated();
					m_nLocked.f_FetchOr((mint(2) << (EAtomicBits-2)), NAtomic::EMemoryOrder_Release);
				}
				else
				{
					while (!(m_nLocked.f_Load(NAtomic::EMemoryOrder_Acquire) & (mint(2) << (EAtomicBits-2))))
						yield_cpu;
				}
			}
			
			inline_never void fp_WaitForIt()
			{
				f_CreateEvent();
				CDisableLazyCheckoutReturnScope DisableLazy;				
				m_Event.f_Wait();
			}

			inline_never void fp_SignalIt()
			{
				// Wait for it to be created, because it should be
				while (!(m_nLocked.f_Load(NAtomic::EMemoryOrder_Acquire) & (mint(2) << (EAtomicBits-2))))
					yield_cpu;
				// Someone is waiting
				m_Event.f_Signal();
			}
			
			static const mint mcp_AtomicMask = DMibBitRangeOne(0, EAtomicBits-3, mint(1));

		public:
#ifndef DMibNoAggregateConstexpr
			constexpr TCMutualAggregate(EAggregateInitialization _Init)
				: m_nLocked{_Init}
				, m_ThreadID{0}
				, m_nRecurse{0}
				, m_Event{_Init}
			{
			}
			TCMutualAggregate()
			{
			}
#endif
			void f_Construct()
			{
//				m_Event.f_Construct();
//				m_nLocked.f_Construct(mint(2) << (EAtomicBits-2));

				m_Event.f_ConstructDontCreate();
				m_nLocked.f_Store(0, NAtomic::EMemoryOrder_Relaxed);
				m_nRecurse = 0;
				m_ThreadID = 0;
			}

			void f_Construct(void * _pSemaphore)
			{
				if (_pSemaphore)
					m_nLocked.f_Store(mint(3) << (EAtomicBits-2), NAtomic::EMemoryOrder_Relaxed);
				else
					m_nLocked.f_Store(0, NAtomic::EMemoryOrder_Relaxed);
				m_nRecurse = 0;
				m_ThreadID = 0;
				m_Event.f_Construct(_pSemaphore);
			}			

			void f_Destruct()
			{
				
				DMibFastCheck((m_nLocked.f_Load() & mcp_AtomicMask) == 0);
				DMibFastCheck(m_ThreadID == 0);
				m_Event.f_Destruct();
			}

			inline_never bint f_TryLock()
			{
				mint CurrentThread = NSys::fg_Thread_GetCurrentUID();

				if (m_ThreadID == CurrentThread)
				{
					++m_nRecurse;
					return true;
				}
				
				mint Original = m_nLocked.f_Load(NAtomic::EMemoryOrder_Relaxed);
				if (Original & mcp_AtomicMask)
				{
					return false;
				}

				if (m_nLocked.f_CompareExchangeStrong(Original, Original + 1, NAtomic::EMemoryOrder_Acquire, NAtomic::EMemoryOrder_Relaxed))
				{
					m_ThreadID = CurrentThread;
					m_nRecurse = 1;
					return true;
				}
				return false;
			}

			bint f_IsLocked()
			{
				return m_ThreadID != 0;
			}

			inline_never bint f_TryLock(aint _SpinCount)
			{
				mint CurrentThread = NSys::fg_Thread_GetCurrentUID();

				if (m_ThreadID == CurrentThread)
				{
					DMibFastCheck(t_bAllowRecursive);
					++m_nRecurse;
					return true;
				}
				
				while (_SpinCount--)
				{
					mint Original = m_nLocked.f_Load(NAtomic::EMemoryOrder_Relaxed);
					if (Original & mcp_AtomicMask)
					{
						continue;
					}
					if (m_nLocked.f_CompareExchangeWeak(Original, Original + 1, NAtomic::EMemoryOrder_Acquire, NAtomic::EMemoryOrder_Relaxed))
					{
						m_ThreadID = CurrentThread;
						m_nRecurse = 1;
						return true;
					}
				}
				return false;
			}

			inline_never void f_SetLockForThread(mint _ThreadID)
			{
				mint CurrentThread = _ThreadID;

				if (m_ThreadID == CurrentThread)
				{
					DMibFastCheck(t_bAllowRecursive);
					++m_nRecurse;
					return;
				}
				
				// Try to take the lock
				mint nLockedValue = m_nLocked.f_FetchAdd(1);
				mint nLocked = nLockedValue & mcp_AtomicMask;
				
				if (nLocked > 0)
				{
					mint nCreate = nLockedValue >> (EAtomicBits - 2);
					if (nCreate & 2)
					{
						CDisableLazyCheckoutReturnScope DisableLazy;				
						m_Event.f_Wait();
					}
					else
						fp_WaitForIt();
				}

				m_ThreadID = CurrentThread;
				m_nRecurse = 1;
			}

			inline_never void f_Lock()
			{
				mint CurrentThread = NSys::fg_Thread_GetCurrentUID();

				if (m_ThreadID == CurrentThread)
				{
					DMibFastCheck(t_bAllowRecursive);
					++m_nRecurse;
					return;
				}
				
				// Try to take the lock
				mint nLockedValue = m_nLocked.f_FetchAdd(1, NAtomic::EMemoryOrder_Acquire);
				mint nLocked = nLockedValue & mcp_AtomicMask;
				
				if (unlikely(nLocked > 0))
				{
					mint nCreate = nLockedValue >> (EAtomicBits - 2);
					if (nCreate & 2)
					{
						CDisableLazyCheckoutReturnScope DisableLazy;				
						m_Event.f_Wait();
					}
					else
						fp_WaitForIt();
				}

				m_ThreadID = CurrentThread;
#				if DMibEnableSafeCheck > 0
					m_AlternateThreadID = NSys::fg_Thread_GetCurrentUIDAlternate();
#				endif

				m_nRecurse = 1;
			}

			inline_never void f_Lock(aint _SpinCount)
			{
				mint CurrentThread = NSys::fg_Thread_GetCurrentUID();

				if (m_ThreadID == CurrentThread)
				{
					DMibFastCheck(t_bAllowRecursive);
					++m_nRecurse;
					return;
				}

				while (likely(_SpinCount--))
				{
					mint Original = m_nLocked.f_Load(NAtomic::EMemoryOrder_Relaxed);
					if (Original & mcp_AtomicMask)
					{
						yield_cpu;
						continue;
					}
					if (likely(m_nLocked.f_CompareExchangeWeak(Original, Original + 1, NAtomic::EMemoryOrder_Acquire, NAtomic::EMemoryOrder_Relaxed)))
					{
						m_ThreadID = CurrentThread;
						m_nRecurse = 1;
						return;
					}
					yield_cpu;
				}

				
				// Try to take the lock
				mint nLockedValue = m_nLocked.f_FetchAdd(1, NAtomic::EMemoryOrder_Acquire);
				mint nLocked = nLockedValue & mcp_AtomicMask;
				
				if (nLocked > 0)
				{
					mint nCreate = nLockedValue >> (EAtomicBits - 2);
					if (nCreate & 2)
					{
						CDisableLazyCheckoutReturnScope DisableLazy;				
						m_Event.f_Wait();
					}
					else
						fp_WaitForIt();
				}

				m_ThreadID = CurrentThread;
#				if DMibEnableSafeCheck > 0
					m_AlternateThreadID = NSys::fg_Thread_GetCurrentUIDAlternate();
#				endif
				m_nRecurse = 1;
			}

			bint f_OwnsLock()
			{
				mint CurrentThread = NSys::fg_Thread_GetCurrentUID();
				return m_ThreadID == CurrentThread;
			}

			inline_never void f_Unlock()
			{
				DMibFastCheck(m_ThreadID == NSys::fg_Thread_GetCurrentUID());

				if (likely((--m_nRecurse) == 0))
				{
					m_ThreadID = 0;
					mint nLockedValue = m_nLocked.f_FetchSub(1, NAtomic::EMemoryOrder_Release);
					if ((nLockedValue & mcp_AtomicMask) > 1)
					{				
						mint nCreate = nLockedValue >> (EAtomicBits - 2);
						if (nCreate & 2)
							m_Event.f_Signal();
						else
							fp_SignalIt();
					}
				}
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

		template <typename t_CEvent, mint _nSpins, bool t_bAllowRecursive>
		class TCMutualSpinAggregate : public TCMutualAggregate<t_CEvent, t_bAllowRecursive>
		{
		public:
#ifndef DMibNoAggregateConstexpr
			constexpr TCMutualSpinAggregate(EAggregateInitialization _Init)
				: TCMutualAggregate<t_CEvent, t_bAllowRecursive>{_Init}
			{
			}
			TCMutualSpinAggregate()
			{
			}
#endif
			bint f_TryLock()
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
		class TMutual : public TCMutualAggregate<t_CEvent, t_bAllowRecursive>
		{
			TMutual(const TMutual &);
			TMutual &operator = (const TMutual &);
		public:
			TMutual()
			{
				TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_Construct();
			}

			TMutual(void * _pSemaphore)
			{
				TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_Construct(_pSemaphore);
			}
			
			~TMutual()
			{
				TCMutualAggregate<t_CEvent, t_bAllowRecursive>::f_Destruct();
			}
		};

		template <typename t_CEvent, mint _nSpins, bool t_bAllowRecursive>
		class TMutualSpin : public TCMutualSpinAggregate<t_CEvent, _nSpins, t_bAllowRecursive>
		{
			TMutualSpin(const TMutualSpin &);
			TMutualSpin &operator = (const TMutualSpin &);
		public:
			TMutualSpin()
			{
				TCMutualSpinAggregate<t_CEvent, _nSpins, t_bAllowRecursive>::f_Construct();
			}

			TMutualSpin(void * _pSemaphore)
			{
				TCMutualSpinAggregate<t_CEvent, _nSpins, t_bAllowRecursive>::f_Construct(_pSemaphore);
			}
			
			~TMutualSpin()
			{
				TCMutualSpinAggregate<t_CEvent, _nSpins, t_bAllowRecursive>::f_Destruct();
			}
		};

		typedef TCMutualSimpleAggregate<CEventAutoResetAggregate> CMutualSimpleAggregate;
		typedef TCMutualSimple<CEventAutoResetAggregate> CMutualSimple;

		typedef TCMutualAggregate<CEventAutoResetAggregate, true> CMutualAggregate;
		typedef TMutual<CEventAutoResetAggregate, true> CMutual;
		typedef TMutual<CEventAutoResetAggregate, false> CMutualNoRecurse;

		typedef TCMutualSpinAggregate<CEventAutoResetAggregate, 64, true> CMutualSpinAggregate;
		typedef TMutualSpin<CEventAutoResetAggregate, 64, true> CMutualSpin;

        /***************************************************************************************************\
        |ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯|
        | MutualManyRead																				|
        |___________________________________________________________________________________________________|
        \***************************************************************************************************/

		template <typename t_CEventAutoreset, typename t_CEvent, typename t_CBase = TCMutualAggregate<t_CEventAutoreset, true> >
		class TCMutualManyReadAggregate : public t_CBase
		{
		protected:
			using t_CBase::mcp_AtomicMask;
		public:
#ifndef DMibNoAggregateConstexpr
			constexpr TCMutualManyReadAggregate(EAggregateInitialization _Init)
				: t_CBase{_Init}
			{
			}
			TCMutualManyReadAggregate()
			{
			}
#endif
			
			// Lock for write access
			DMibThreadAtomicsAlignment NAtomic::TCAtomicAggregate<mint> m_nReading;
			t_CEvent m_ReadOkEvent;
			t_CEventAutoreset m_WriteOkEvent;
#			if DMibEnableSafeCheck > 0
				DMibThreadAtomicsAlignment NAtomic::TCAtomicAggregate<mint> m_nReadingDebugCheck;
#			endif

			const static mint mc_FlagReadingNotAllowed = DMibBitTyped(sizeof(NAtomic::TCAtomicAggregate<mint>)*8-1, mint);
			const static mint mc_FlagReadOkEventReset = DMibBitTyped(sizeof(NAtomic::TCAtomicAggregate<mint>)*8-2, mint);
			const static mint mc_FlagReadOkEventResetDone = DMibBitTyped(sizeof(NAtomic::TCAtomicAggregate<mint>)*8-3, mint);
			const static mint mc_nReadingMask = ~(mc_FlagReadingNotAllowed | mc_FlagReadOkEventReset | mc_FlagReadOkEventResetDone);


			void f_Construct()
			{
				t_CBase::f_Construct();
				m_nReading.f_Store(mc_FlagReadOkEventReset | mc_FlagReadOkEventResetDone, NAtomic::EMemoryOrder_Relaxed);
				m_ReadOkEvent.f_Construct(0);
				m_WriteOkEvent.f_Construct();
#				if DMibEnableSafeCheck > 0
					m_nReadingDebugCheck.f_Store(0, NAtomic::EMemoryOrder_Relaxed);
#				endif
			}

			void f_Destruct()
			{
				DMibFastCheck((m_nReading.f_Load() & mc_nReadingMask) == 0);
				m_WriteOkEvent.f_Destruct();
				m_ReadOkEvent.f_Destruct();
				t_CBase::f_Destruct();
			}

			void f_PrepareFork()
			{
				t_CBase::f_PrepareFork();
				m_WriteOkEvent.f_PrepareFork();
				m_ReadOkEvent.f_PrepareFork();
			}
			
			void f_ForkedChild()
			{
				m_ReadOkEvent.f_ForkedChild();
				m_WriteOkEvent.f_ForkedChild();
				m_nReading.f_Store(mc_FlagReadOkEventReset | mc_FlagReadOkEventResetDone | mc_FlagReadingNotAllowed);
#				if DMibEnableSafeCheck > 0
					m_nReadingDebugCheck.f_Store(0, NAtomic::EMemoryOrder_Relaxed);
#				endif
				
				t_CBase::f_ForkedChild();
			}
			
			void f_ForkedParent()
			{
				m_ReadOkEvent.f_ForkedParent();
				m_WriteOkEvent.f_ForkedParent();
				t_CBase::f_ForkedParent();
			}
			
			inline_never void f_LockRead()
			{
		RestartLock:
				mint nReading = m_nReading.f_FetchAdd(1, NAtomic::EMemoryOrder_Acquire);
				if (nReading & mc_FlagReadingNotAllowed)
				{
					++nReading;
					while (!(nReading & mc_FlagReadOkEventReset) && (nReading & mc_FlagReadingNotAllowed))
					{
						if (m_nReading.f_CompareExchangeStrong(nReading, nReading | mc_FlagReadOkEventReset, NAtomic::EMemoryOrder_Acquire, NAtomic::EMemoryOrder_Relaxed))
						{
							m_ReadOkEvent.f_ResetSignaled();
							nReading = m_nReading.f_FetchOr(mc_FlagReadOkEventResetDone, NAtomic::EMemoryOrder_Release);
							break;
						}
					}
					if (nReading & mc_FlagReadingNotAllowed)
					{
						f_UnlockReadInternal();
						{
							CDisableLazyCheckoutReturnScope DisableLazy;				
							m_ReadOkEvent.f_Wait();
						}
						goto RestartLock;
					}
				}
#				if DMibEnableSafeCheck > 0
					m_nReadingDebugCheck.f_FetchAdd(1, NAtomic::EMemoryOrder_Relaxed);
#				endif
			}


			inline_never void f_UnlockReadInternal()
			{
				mint nReading = m_nReading.f_FetchSub(1, NAtomic::EMemoryOrder_Acquire);
				if ((nReading & (mc_FlagReadingNotAllowed | mc_nReadingMask)) == (mc_FlagReadingNotAllowed | 1))
				{
					m_WriteOkEvent.f_Signal();
				}
			}

			inline_never void f_UnlockRead()
			{
#				if DMibEnableSafeCheck > 0
					m_nReadingDebugCheck.f_FetchSub(1, NAtomic::EMemoryOrder_Relaxed);
#				endif
				f_UnlockReadInternal();
			}


			inline_never void f_Lock()
			{
				mint CurrentThread = NMib::NSys::fg_Thread_GetCurrentUID();

				if (t_CBase::m_ThreadID == CurrentThread)
				{
					++t_CBase::m_nRecurse;
					return;
				}
				
				// Try to take the lock
				mint nLockedValue = t_CBase::m_nLocked.f_FetchAdd(1, NAtomic::EMemoryOrder_Acquire);
				mint nLocked = nLockedValue & mcp_AtomicMask;
				
				if (nLocked > 0)
				{
					mint nCreate = nLockedValue >> (t_CBase::EAtomicBits - 2);
					if(nCreate & 2)
					{
						CDisableLazyCheckoutReturnScope DisableLazy;				
						t_CBase::m_Event.f_Wait();
					}
					else
						t_CBase::fp_WaitForIt();
				}

				
				mint nReading = m_nReading.f_FetchOr(mc_FlagReadingNotAllowed, NAtomic::EMemoryOrder_Relaxed);
				if ((nReading & mc_nReadingMask) > 0)
				{
					while (1)
					{
						mint nReading = m_nReading.f_FetchOr(mc_FlagReadingNotAllowed, NAtomic::EMemoryOrder_Relaxed);

						if ((nReading & mc_nReadingMask) > 0)
						{
							CDisableLazyCheckoutReturnScope DisableLazy;				
							m_WriteOkEvent.f_Wait();
						}
						else
							break;
					}
				}
				t_CBase::m_ThreadID = CurrentThread;
				t_CBase::m_nRecurse = 1;
				DMibFastCheck(m_nReadingDebugCheck.f_Load() == 0);
			}


			inline_never bool f_TryLock()
			{
				mint CurrentThread = NMib::NSys::fg_Thread_GetCurrentUID();

				if (t_CBase::m_ThreadID == CurrentThread)
				{
					++t_CBase::m_nRecurse;
					return true;
				}
				
				mint Original = t_CBase::m_nLocked.f_Load(NAtomic::EMemoryOrder_Relaxed);
				if (Original & mcp_AtomicMask)
				{
					return false;
				}

				if (t_CBase::m_nLocked.f_CompareExchangeStrong(Original, Original + 1, NAtomic::EMemoryOrder_Acquire, NAtomic::EMemoryOrder_Relaxed))
				{
					t_CBase::m_ThreadID = CurrentThread;
					t_CBase::m_nRecurse = 1;
				}
				else
					return false;

				Original = m_nReading.f_Load(NAtomic::EMemoryOrder_Relaxed);

				if ((Original & mc_nReadingMask) > 0)
				{
					t_CBase::f_Unlock();
					return false;
				}
				
				if (!m_nReading.f_CompareExchangeStrong(Original, Original | mc_FlagReadingNotAllowed, NAtomic::EMemoryOrder_Relaxed))
				{
					t_CBase::f_Unlock();
					return false;
				}
				DMibFastCheck(m_nReadingDebugCheck.f_Load() == 0);
				return true;
			}

			inline_never void f_Unlock()
			{
				DMibFastCheck(t_CBase::m_ThreadID == NMib::NSys::fg_Thread_GetCurrentUID());

				if ((--t_CBase::m_nRecurse) == 0)
				{
					t_CBase::m_ThreadID = 0;
					//mint Bit_Signaled = DMibBitTyped(sizeof(m_nReading)*8-2, mint);
					mint OldReading = m_nReading.f_FetchAnd(~(mc_FlagReadingNotAllowed), NAtomic::EMemoryOrder_AcquireRelease);
					DMibFastCheck((OldReading & mc_FlagReadingNotAllowed));
					if (OldReading & mc_FlagReadOkEventReset) 
					{
						while (!(OldReading & mc_FlagReadOkEventResetDone))
							OldReading = m_nReading.f_FetchAnd(~(mc_FlagReadingNotAllowed), NAtomic::EMemoryOrder_Acquire);
						m_ReadOkEvent.f_SetSignaled();
						m_nReading.f_FetchAnd(~(mc_FlagReadOkEventReset | mc_FlagReadOkEventResetDone), NAtomic::EMemoryOrder_Release);
					}

					mint nLockedValue = t_CBase::m_nLocked.f_FetchSub(1, NAtomic::EMemoryOrder_Release);
					if ((nLockedValue & mcp_AtomicMask) > 1)
					{				
						mint nCreate = nLockedValue >> (t_CBase::EAtomicBits - 2);
						if (nCreate & 2)
							t_CBase::m_Event.f_Signal();
						else
							t_CBase::fp_SignalIt();
					}
				}
			}

		};

		template <typename t_CEventAutoreset,typename t_CEvent, typename t_CBase = TCMutualAggregate<t_CEventAutoreset, true> >
		class TMutualManyRead : public TCMutualManyReadAggregate<t_CEventAutoreset, t_CEvent, t_CBase>
		{
			TMutualManyRead(TMutualManyRead const &);
			TMutualManyRead &operator = (const TMutualManyRead&);
		public:
			TMutualManyRead()
			{
				TCMutualManyReadAggregate<t_CEventAutoreset, t_CEvent, t_CBase>::f_Construct();
			}

			TMutualManyRead(void * _pSemaphore)
			{
				TCMutualManyReadAggregate<t_CEventAutoreset, t_CEvent, t_CBase>::f_Construct(_pSemaphore);
			}
			
			~TMutualManyRead()
			{
				TCMutualManyReadAggregate<t_CEventAutoreset, t_CEvent, t_CBase>::f_Destruct();
			}
		};

		typedef TCMutualManyReadAggregate<CEventAutoResetAggregate, CEventAggregate> CMutualManyReadAggregate;
		typedef TMutualManyRead<CEventAutoResetAggregate, CEventAggregate> CMutualManyRead;

		template <typename t_CBase = TCMutualAggregate<CEventAutoResetAggregate, true> >
		class TCMutualManyReadSpinAggregate : public t_CBase
		{
		protected:
			using t_CBase::mcp_AtomicMask;
		public:
#ifndef DMibNoAggregateConstexpr
			constexpr TCMutualManyReadSpinAggregate(EAggregateInitialization _Init)
				: t_CBase{_Init}
			{
			}
			TCMutualManyReadSpinAggregate()
			{
			}
#endif
			
			// Lock for write access
			DMibThreadAtomicsAlignment NAtomic::TCAtomicAggregate<mint> m_nReading;
#			if DMibEnableSafeCheck > 0
				DMibThreadAtomicsAlignment NAtomic::TCAtomicAggregate<mint> m_nReadingDebugCheck;
#			endif

			const static mint mc_FlagReadingNotAllowed = DMibBitTyped(sizeof(NAtomic::TCAtomicAggregate<mint>)*8-1, mint);
			const static mint mc_nReadingMask = ~(mc_FlagReadingNotAllowed);


			void f_Construct()
			{
				t_CBase::f_Construct();
				m_nReading.f_Store(0, NAtomic::EMemoryOrder_Relaxed);
#				if DMibEnableSafeCheck > 0
					m_nReadingDebugCheck.f_Store(0, NAtomic::EMemoryOrder_Relaxed);
#				endif
			}

			void f_Destruct()
			{
				DMibFastCheck((m_nReading.f_Load() & mc_nReadingMask) == 0);
				t_CBase::f_Destruct();
			}

			inline_never void f_LockRead()
			{
		RestartLock:
				mint nReading = m_nReading.f_FetchAdd(1, NAtomic::EMemoryOrder_Acquire);
				if (nReading & mc_FlagReadingNotAllowed)
				{
					f_UnlockReadInternal();
					while (m_nReading.f_Load(NAtomic::EMemoryOrder_Relaxed) & mc_FlagReadingNotAllowed)
						yield_cpu;
					goto RestartLock;
				}
#				if DMibEnableSafeCheck > 0
					m_nReadingDebugCheck.f_FetchAdd(1, NAtomic::EMemoryOrder_Relaxed);
#				endif
			}


			inline_never void f_UnlockReadInternal()
			{
				m_nReading.f_FetchSub(1, NAtomic::EMemoryOrder_Acquire);
#if 0
				// Possibly reuse parent event here
				if ((nReading & (mc_FlagReadingNotAllowed | mc_nReadingMask)) == (mc_FlagReadingNotAllowed | 1))
				{
					m_WriteOkEvent.f_Signal();
				}
#endif
			}

			inline_never void f_UnlockRead()
			{
#				if DMibEnableSafeCheck > 0
					m_nReadingDebugCheck.f_FetchSub(1, NAtomic::EMemoryOrder_Relaxed);
#				endif
				f_UnlockReadInternal();
			}


			inline_never void f_Lock()
			{
				mint CurrentThread = NMib::NSys::fg_Thread_GetCurrentUID();

				if (t_CBase::m_ThreadID == CurrentThread)
				{
					++t_CBase::m_nRecurse;
					return;
				}
				
				// Try to take the lock
				mint nLockedValue = t_CBase::m_nLocked.f_FetchAdd(1, NAtomic::EMemoryOrder_Acquire);
				mint nLocked = nLockedValue & mcp_AtomicMask;
				
				if (nLocked > 0)
				{
					mint nCreate = nLockedValue >> (t_CBase::EAtomicBits - 2);
					if (nCreate & 2)
					{
						CDisableLazyCheckoutReturnScope DisableLazy;				
						t_CBase::m_Event.f_Wait();
					}
					else
						t_CBase::fp_WaitForIt();
				}

				
				mint nReading = m_nReading.f_FetchOr(mc_FlagReadingNotAllowed, NAtomic::EMemoryOrder_Relaxed);
				if ((nReading & mc_nReadingMask) > 0)
				{
					while (1)
					{
						mint nReading = m_nReading.f_FetchOr(mc_FlagReadingNotAllowed, NAtomic::EMemoryOrder_Relaxed);

						if ((nReading & mc_nReadingMask) > 0)
						{
							yield_cpu;
						}
						else
							break;
					}
				}
				t_CBase::m_ThreadID = CurrentThread;
				t_CBase::m_nRecurse = 1;
				DMibFastCheck(m_nReadingDebugCheck.f_Load() == 0);
			}

			inline_never void f_Unlock()
			{
				DMibFastCheck(t_CBase::m_ThreadID == NMib::NSys::fg_Thread_GetCurrentUID());

				if ((--t_CBase::m_nRecurse) == 0)
				{
					t_CBase::m_ThreadID = 0;
					//mint Bit_Signaled = DMibBitTyped(sizeof(m_nReading)*8-2, mint);
					m_nReading.f_FetchAnd(~(mc_FlagReadingNotAllowed), NAtomic::EMemoryOrder_AcquireRelease);

					mint nLockedValue = t_CBase::m_nLocked.f_FetchSub(1, NAtomic::EMemoryOrder_Release);
					if ((nLockedValue & mcp_AtomicMask) > 1)
					{				
						mint nCreate = nLockedValue >> (t_CBase::EAtomicBits - 2);
						if (nCreate & 2)
							t_CBase::m_Event.f_Signal();
						else
							t_CBase::fp_SignalIt();
					}
				}
			}

		};

		template <typename t_CBase = TCMutualAggregate<CEventAutoResetAggregate, true> >
		class TMutualManyReadSpin : public TCMutualManyReadSpinAggregate<t_CBase>
		{
			TMutualManyReadSpin(TMutualManyReadSpin const &);
			TMutualManyReadSpin &operator = (const TMutualManyReadSpin&);
		public:
			TMutualManyReadSpin()
			{
				TCMutualManyReadSpinAggregate<t_CBase>::f_Construct();
			}

			TMutualManyReadSpin(void * _pSemaphore)
			{
				TCMutualManyReadSpinAggregate<t_CBase>::f_Construct(_pSemaphore);
			}
			
			~TMutualManyReadSpin()
			{
				TCMutualManyReadSpinAggregate<t_CBase>::f_Destruct();
			}
		};

		typedef TCMutualManyReadSpinAggregate<> CMutualManyReadSpinAggregate;
		typedef TMutualManyReadSpin<> CMutualManyReadSpin;
		
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

#		define DMibLock(_ToLock) NMib::NThread::TCScopeLock<decltype(_ToLock)> ScopeLockMutual1(_ToLock)
#		define DMibUnlock(_ToUnlock) NMib::NThread::TCScopeUnlock<decltype(_ToUnlock)> ScopeUnlockMutual1(_ToUnlock)
#		define DMibLockRead(_ToLock) NMib::NThread::TCScopeLockRead<decltype(_ToLock)> ScopeLockReadMutual1(_ToLock)
#		define DMibLockReadLocked(_ToLock) NMib::NThread::TCScopeLockReadLocked<decltype(_ToLock)> ScopeLockReadMutual1(_ToLock)
#		define DMibUnlockRead(_ToUnlock) NMib::NThread::TCScopeUnlockRead<decltype(_ToUnlock)> ScopeUnlockReadMutual1(_ToUnlock)

#		define DMibLockTyped(_Type, _ToLock) NMib::NThread::TCScopeLock<_Type> ScopeLockMutualTyped1(_ToLock)
#		define DMibUnlockTyped(_Type, _ToUnlock) NMib::NThread::TCScopeUnlock<_Type> ScopeUnlockMutualTyped1(_ToUnlock)

#		define DMibLockReadTyped(_Type, _ToLock) NMib::NThread::TCScopeLockRead<_Type> ScopeLockReadMutualTyped1(_ToLock)
#		define DMibUnlockReadTyped(_Type, _ToUnlock) NMib::NThread::TCScopeUnlockRead<_Type> ScopeUnlockReadMutualTyped1(_ToUnlock)


#		ifndef DMibPNoShortCuts
#			define DLock(_ToLock) DMibLock(_ToLock)
#			define DUnlock(_ToLock) DMibUnlock(_ToLock)
#			define DLockTyped(_Type, _ToLock) DMibLockTyped(_Type, _ToLock)
#			define DUnlockTyped(_Type, _ToLock) DMibUnlockTyped(_Type, _ToLock)
#			define DLockRead(_ToLock) DMibLockRead(_ToLock)
#			define DLockReadLocked(_ToLock) DMibLockReadLocked(_ToLock)
#			define DUnlockRead(_ToLock) DMibUnlockRead(_ToLock)
#			define DLockTypedRead(_Type, _ToLock) DMibLockTypedRead(_Type, _ToLock)
#			define DUnlockTypedRead(_Type, _ToLock) DMibUnlockTypedRead(_Type, _ToLock)
#		endif

		template <typename t_tData>
		t_tData fg_Thread_GetDataLocked(const t_tData &_Data, CMutual &_Lock)
		{
			DMibLock(_Lock);
			return _Data;            
		}

        /***************************************************************************************************\
        |ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯|
        | Reportable																						|
        |___________________________________________________________________________________________________|
        \***************************************************************************************************/

		class CSemaphoreReportableAggregate
		{
		public:
			void *m_pSemaphore;

			class CReportListMember
			{
			public:
				CSemaphoreReportableAggregate *m_pReportTo;
				CSemaphoreReportableAggregate *m_pReportFrom;
				DMibListLinkDS_Link(CReportListMember, m_LinkReportFrom);
				DMibListLinkDS_Link(CReportListMember, m_LinkReportTo);
			};

			DMibListLinkDS_List(CReportListMember, m_LinkReportTo) m_ReportTo;
			DMibListLinkDS_List(CReportListMember, m_LinkReportFrom) m_ReportFrom;

            /*ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯*\
            |	Function:			Makes a link from the event to another event			|
            |																				|
            |	Parameters:																	|
            |		_pReportTo:		The parameter to report to								|
            |																				|
            |	Comments:			Make sure that you don't create circle reports. Other	|
            |						than hang when an event gets signaled it could cause	|
            |						a deadlock when calling the ReportTo function. It goes	|
            |						without saying that you have to make sure that one of	|
            |						the objects don't get deleted while in the function		|
            \*_____________________________________________________________________________*/

			void f_ReportTo(CSemaphoreReportableAggregate *_pReportTo);
			void f_ClearReportTo();
			void f_ClearReportFrom();

			void f_PrepareFork()
			{
			}
			void f_ForkedChild()
			{
				NSys::fg_Semaphore_ForkedChild(m_pSemaphore);
			}
			void f_ForkedParent()
			{
			}
			

			inline_small void f_ConstructDontCreate()
			{
				m_pSemaphore = nullptr;
			}

			inline_small void f_ConstructIfNotCreated()
			{
				if (!m_pSemaphore)
					m_pSemaphore = NSys::fg_Semaphore_Alloc(0, 1);
				else
					DMibFastCheck(0);
			}

			inline_small void f_ConstructIfNotCreated() volatile
			{
				if (!fg_Volatile(m_pSemaphore))
					fg_Volatile(m_pSemaphore) = NSys::fg_Semaphore_Alloc(0, 1);
				else
					DMibFastCheck(0);
			}

			inline_small bint f_IsCreated()
			{
				return m_pSemaphore != 0;
			}		

			inline_small bint f_IsCreated() volatile
			{
				return fg_Volatile(m_pSemaphore) != 0;
			}		


			inline_small void f_Construct(const CSemaphoreReportableAggregate &_Other)
			{
				if (_Other.m_pSemaphore)
					m_pSemaphore = NSys::fg_Semaphore_Duplicate(_Other.m_pSemaphore);
				else
					m_pSemaphore = nullptr;
			}

			inline_small void f_Construct()
			{
				m_pSemaphore = NSys::fg_Semaphore_Alloc(0, 1);
			}

			inline_small void f_Construct(mint _Initial, mint _MaximumCount)
			{
				m_pSemaphore = NSys::fg_Semaphore_Alloc(_Initial, _MaximumCount);
			}

			inline_small void f_Destruct()
			{
				f_ClearReportTo();
				f_ClearReportFrom();

				if (m_pSemaphore)
					NSys::fg_Semaphore_Free(m_pSemaphore);
				m_pSemaphore = nullptr;
			}

			inline_small void f_SetSemaphore(void *_pSemaphore)
			{
				if (m_pSemaphore)
					NSys::fg_Semaphore_Free(m_pSemaphore);

				m_pSemaphore = _pSemaphore;
			}

			virtual void f_Signal(int _nToSignal = 1);

			inline_small void f_Wait()
			{
				NSys::fg_Semaphore_Wait(m_pSemaphore);
			}

			inline_small bint f_WaitTimeout(fp64 _Timeout)
			{
				return NSys::fg_Semaphore_WaitTimeout(m_pSemaphore, _Timeout);
			}

			inline_small bint f_TryWait()
			{
				return NSys::fg_Semaphore_TryWait(m_pSemaphore);
			}
		};

		class CEventAutoResetReportable : public CSemaphoreReportableAggregate
		{
			CEventAutoResetReportable &operator = (CEventAutoResetReportable const &);
		public:
			CEventAutoResetReportable()
			{
				f_Construct();
			}
			CEventAutoResetReportable(CEventAutoResetReportable const &_Other)
			{
				f_Construct(_Other);
			}
			~CEventAutoResetReportable()
			{
				f_Destruct();
			}
		};

		class CSemaphoreReportable : public CSemaphoreReportableAggregate
		{
			CSemaphoreReportable &operator = (CSemaphoreReportable const &);
		public:

			CSemaphoreReportable(const CSemaphoreReportable &_Other)
			{
				f_Construct(_Other);
			}
			CSemaphoreReportable()
			{
				f_Construct();
			}
			CSemaphoreReportable(mint _Initial, mint _MaximumCount)
			{
				f_Construct(_Initial, _MaximumCount);
			}
			~CSemaphoreReportable()
			{
				f_Destruct();
			}
			void f_SetSemaphoreOptions(mint _Initial, mint _MaximumCount)
			{
				f_Destruct();
				f_Construct(_Initial, _MaximumCount);
			}
		};

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
			mutable CMutual m_Lock;
			mint m_State;
			aint m_ReturnValue;
			void *m_pThread;
			mint m_ThreadID;
			mint m_ParentThreadID;
			void *m_pThreadDestroyContext;
			bint m_bAutoDestroy;
			bint m_bWaitStart;
			bint m_bLockHeld;

			CEvent m_ThreadQuitEvent;
			
			static aint fsp_ThreadMain(void *_pContext);
			void fp_Cleanup();

			CThread(const CThread &);
			CThread &operator = (const CThread &);

		public:

			// This event will be signaled when the thread is requested to quit
			CEventAutoResetReportable m_EventWantQuit;

			CThread();
			virtual ~CThread();

			virtual bool f_DestroyThread(); // Return true if the thread was destroyed (deleted)
			
			void f_PrepareFork();
			void f_ForkedChild();
			void f_ForkedParent();

            /*ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯*\
            |	Function:			Get the state of the thread								|
            |																				|
            |	Returns:			The state of the thread @See(EThreadState)				|
            |																				|
            |	Comments:			Longer_description_not_mandatory						|
            \*_____________________________________________________________________________*/
			EThreadState f_GetState() const
			{
				DMibLock(m_Lock);
				return (EThreadState)m_State;
			}

			void *f_GetThread() const
			{
				return m_pThread;
			}

			mint f_GetThreadID() const
			{
				return m_ThreadID;
			}

			bint f_CallingFromThread()
			{
				return m_ThreadID == NSys::fg_Thread_GetCurrentUID();
			}

            /*ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯*\
            |	Function:			Starts the thead										|
            |																				|
            |	Parameters:																	|
            |		_StackSize:		The stack size you want to reserve for the thread		|
            |		_Prio:			The priority you want for the thread					|
            |																				|
            |	Comments:			Your overridden Main function will be called			|
            \*_____________________________________________________________________________*/
			virtual void f_Start(EThreadPriority _Prio = EThreadPriority_Normal, mint _StackSize = 0, mint _Affinity = 0, bint _bAutoDestroy = false, bint _bWaitStart = false);

			/*ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯*\
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
			virtual mint f_Stop(bint _bBlock = true);

            /*ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯*\
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
				if (m_State == EThreadState_Stopped)
					return m_ReturnValue;

				DMibError("You are trying to get a return value from a thread that isn't stopped (or has never run)");
			}

            /*ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯*\
            |	Function:			Suspends the thread										|
            |																				|
            |	Comments:			The suspence of the thread is reference counted,		|
            |						so if you call Suspend() twice you will have to call	|
            |						Resume() twice for the execution of the thread to		|
            |						resume.													|
            \*_____________________________________________________________________________*/
			void f_Suspend();

            /*ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯*\
            |	Function:			Resumes the thread										|
            |																				|
            |	Comments:			The suspence of the thread is reference counted,		|
            |						so if you call Suspend() twice you will have to call	|
            |						Resume() twice for the execution of the thread to		|
            |						resume.													|
            \*_____________________________________________________________________________*/
			void f_Resume();

            /*ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯*\
            |	Function:			Sets the priority of the thread							|
            |																				|
            |	Parameters:																	|
            |		_Prio:			The priority @See(EThreadPriority)						|
            |																				|
            |	Comments:			.														|
            \*_____________________________________________________________________________*/
			void f_SetPriority(EThreadPriority _Prio);

            /*ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯ï¾¯*\
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
		class TCThreadObject : private CThread
		{
			DMibClassNoCopyAllowed(TCThreadObject);
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

			NPtr::TCUniquePointer<CCallerObject, t_CAllocator> m_pCallerObject;

			TCThreadObject(NPtr::TCUniquePointer<CCallerObject, t_CAllocator> &&_pCallerObject) 
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
			static NPtr::TCUniquePointer<TCThreadObject, t_CAllocator, TCDynamicPtr<typename t_CAllocator::CPtrHolder, TCThreadObject>, void> 
			fs_StartThread(tf_CFunctionType &&_FunctionObject, const t_CStr &_Name, EThreadPriority _Prio = EThreadPriority_Normal, mint _StackSize = 0, mint _Affinity = 0, bint _bAutoDestroy = false);

			template <typename tf_CFunctionType>
			static NPtr::TCUniquePointer<TCThreadObject, t_CAllocator, TCDynamicPtr<typename t_CAllocator::CPtrHolder, TCThreadObject>, void> 
			fs_StartThread(tf_CFunctionType *_pFunctionObject, const t_CStr &_Name, EThreadPriority _Prio = EThreadPriority_Normal, mint _StackSize = 0, mint _Affinity = 0, bint _bAutoDestroy = false);
			
		};

		typedef TCThreadObject<NMem::CDefaultAllocator, NStr::CStr> CThreadObject;
		typedef TCThreadObject<NMem::CAllocator_NonTrackedHeap, NStr::CStrNonTracked> CThreadObjectNonTracked;

	};

	namespace NPtr
	{
		/////////////////////////////////////////////////////////////////////////
		// Intrusive refcount base

		template <CSharedPointerOptionUnderlaying t_Options>
		class TCSharedPointerIntrusiveBase;

#if DMibConfig_RefcountDebugging
		struct CRefCountDebug
		{
			NThread::CMutual m_Lock;
			NContainer::TCLinkedList<NException::CCallstack, NMem::CAllocator_NonTrackedHeap> m_Callstacks;
			NContainer::TCLinkedList<NException::CCallstack, NMem::CAllocator_NonTrackedHeap> m_WeakCallstacks;

		};
#endif

		template <>
		class TCSharedPointerIntrusiveBase<ESharedPointerOption_None>
		{
			mutable NAtomic::TCAtomic<aint> m_RefCount; // -1 means no references

		protected:
			~TCSharedPointerIntrusiveBase();
		public:

			TCSharedPointerIntrusiveBase()
				: m_RefCount(0)
			{
				DMibRefcountDebuggingOnly(m_Debug.f_Construct());
			}

			TCSharedPointerIntrusiveBase(TCSharedPointerIntrusiveBase const &)
				: m_RefCount(0)
			{
				DMibRefcountDebuggingOnly(m_Debug.f_Construct());
			}

			TCSharedPointerIntrusiveBase(TCSharedPointerIntrusiveBase &&)
				: m_RefCount(0)
			{
				DMibRefcountDebuggingOnly(m_Debug.f_Construct());
			}
			
			TCSharedPointerIntrusiveBase &operator = (TCSharedPointerIntrusiveBase const &)
			{
				return *this;
			}
			
			TCSharedPointerIntrusiveBase &operator = (TCSharedPointerIntrusiveBase &&)
			{
				return *this;
			}
			
#if DMibConfig_RefcountDebugging
			void f_InitialRef(CRefCountDebugReference &o_Reference) const;
			aint f_RefCountDecrease(CRefCountDebugReference &o_Reference) const;
			aint f_RefCountIncrease(CRefCountDebugReference &o_Reference) const;
#else
			aint f_RefCountDecrease() const
			{
				aint Return = m_RefCount.f_FetchSub(1, NAtomic::EMemoryOrder_Release);
				if (Return == 0)
					NAtomic::fg_MemoryFence(NAtomic::EMemoryOrder_Acquire);
				return Return;
			}

			aint f_RefCountIncrease() const
			{
				aint Return = m_RefCount.f_FetchAdd(1, NAtomic::EMemoryOrder_Release);
				DMibFastCheck(Return >= 0);
				return Return;
			}
#endif
	
			aint f_RefCountGet() const
			{
				return m_RefCount.f_Load(NAtomic::EMemoryOrder_Relaxed);
			}

#if DMibConfig_RefcountDebugging
			mutable NAggregate::TCAggregateSimple<CRefCountDebug> m_Debug;
#endif
		};

		template <>
		class TCSharedPointerIntrusiveBase<ESharedPointerOption_SupportWeakPointer>
		{
			mutable NAtomic::TCAtomic<smint> m_RefCount; // -1 means no references
			mutable NAtomic::TCAtomic<smint> m_WeakRefCount; // -1 means no references

		protected:
			~TCSharedPointerIntrusiveBase();

		public:
			TCSharedPointerIntrusiveBase()
				: m_RefCount(0)
				, m_WeakRefCount(0)
			{
				DMibRefcountDebuggingOnly(m_Debug.f_Construct());
			}

			TCSharedPointerIntrusiveBase(TCSharedPointerIntrusiveBase const &)
				: m_RefCount(0)
				, m_WeakRefCount(0)
			{
				DMibRefcountDebuggingOnly(m_Debug.f_Construct());
			}

			TCSharedPointerIntrusiveBase(TCSharedPointerIntrusiveBase &&)
				: m_RefCount(0)
				, m_WeakRefCount(0)
			{
				DMibRefcountDebuggingOnly(m_Debug.f_Construct());
			}
			
			TCSharedPointerIntrusiveBase &operator = (TCSharedPointerIntrusiveBase const &)
			{
				return *this;
			}
			
			TCSharedPointerIntrusiveBase &operator = (TCSharedPointerIntrusiveBase &&)
			{
				return *this;
			}
			
#if DMibConfig_RefcountDebugging
			void f_InitialRef(CRefCountDebugReference &o_Reference) const;
			smint f_RefCountDecrease(CRefCountDebugReference &o_Reference) const;
			smint f_RefCountIncrease(CRefCountDebugReference &o_Reference) const;
			bool f_RefCountIncreaseWhileNot(CRefCountDebugReference &o_Reference, smint _Value) const;
			smint f_WeakRefCountDecrease(CRefCountDebugReference *o_pReference) const;
			smint f_WeakRefCountIncrease(CRefCountDebugReference &o_Reference) const;
#else
			smint f_RefCountDecrease() const
			{
				smint Return = m_RefCount.f_FetchSub(1, NAtomic::EMemoryOrder_Release);
				if (Return == 0)
					NAtomic::fg_MemoryFence(NAtomic::EMemoryOrder_Acquire);
				return Return;
			}

			smint f_RefCountIncrease() const
			{
				aint Return = m_RefCount.f_FetchAdd(1, NAtomic::EMemoryOrder_Release);
				DMibFastCheck(Return >= 0);
				
				return Return;
			}

			bool f_RefCountIncreaseWhileNot(smint _Value) const
			{
				smint CurrentValue = m_RefCount.f_Load(NAtomic::EMemoryOrder_Relaxed);
				while (CurrentValue != _Value)
				{
					if (m_RefCount.f_CompareExchangeStrong(CurrentValue, CurrentValue + 1, NAtomic::EMemoryOrder_Release, NAtomic::EMemoryOrder_Relaxed))
						return true;
				}
				
				return false;
			}

			smint f_WeakRefCountDecrease() const
			{
				smint Return = m_WeakRefCount.f_FetchSub(1, NAtomic::EMemoryOrder_Release);
				if (Return == 0)
					NAtomic::fg_MemoryFence(NAtomic::EMemoryOrder_Acquire);
				return Return;
			}

			smint f_WeakRefCountIncrease() const
			{
				DMibFastCheck(m_RefCount.f_Load() >= -1);
				return m_WeakRefCount.f_FetchAdd(1, NAtomic::EMemoryOrder_Release);
			}
#endif
	
			smint f_RefCountGet() const
			{
				return m_RefCount.f_Load(NAtomic::EMemoryOrder_Relaxed);
			}

			smint f_WeakRefCountGet() const
			{
				return m_WeakRefCount.f_Load(NAtomic::EMemoryOrder_Relaxed);
			}

#if DMibConfig_RefcountDebugging
			mutable NAggregate::TCAggregateSimple<CRefCountDebug> m_Debug;
#endif
		};

		namespace NPrivate
		{
			template <typename t_CType, bool t_bVirtualDestructor, CSharedPointerOptionUnderlaying t_Options>
			class TCSharedPointerCounter : public TCSharedPointerIntrusiveBase<t_Options>
			{
				t_CType m_Data;
			public:
				TCSharedPointerCounter(TCSharedPointerCounter const &_Other)
					: m_Data(_Other.m_Data)
				{
				}
				TCSharedPointerCounter(TCSharedPointerCounter &_Other)
					: m_Data(_Other.m_Data)
				{
				}
				TCSharedPointerCounter(TCSharedPointerCounter volatile &_Other)
					: m_Data(_Other.m_Data)
				{
				}
				TCSharedPointerCounter(TCSharedPointerCounter const volatile &_Other)
					: m_Data(_Other.m_Data)
				{
				}
				TCSharedPointerCounter(TCSharedPointerCounter &&_Other)
					: m_Data(fg_Move(_Other.m_Data))
				{
				}

				template <typename... tfp_CParams>
				TCSharedPointerCounter(tfp_CParams &&...p_Params)
					: m_Data(fg_Forward<tfp_CParams>(p_Params)...)
				{
				}

				t_CType *f_Get()
				{
					return &m_Data;
				}
			};

			template <typename t_CType, CSharedPointerOptionUnderlaying t_Options>
			class TCSharedPointerCounter<t_CType, true, t_Options> : public TCSharedPointerIntrusiveBase<t_Options>
			{
				t_CType m_Data;
			public:
				virtual ~TCSharedPointerCounter()
				{
				}

				TCSharedPointerCounter(TCSharedPointerCounter const &_Other)
					: m_Data(_Other.m_Data)
				{
				}
				TCSharedPointerCounter(TCSharedPointerCounter &_Other)
					: m_Data(_Other.m_Data)
				{
				}
				TCSharedPointerCounter(TCSharedPointerCounter volatile &_Other)
					: m_Data(_Other.m_Data)
				{
				}
				TCSharedPointerCounter(TCSharedPointerCounter const volatile &_Other)
					: m_Data(_Other.m_Data)
				{
				}
				TCSharedPointerCounter(TCSharedPointerCounter &&_Other)
					: m_Data(fg_Move(_Other.m_Data))
				{
				}

				template <typename... tfp_CParams>
				TCSharedPointerCounter(tfp_CParams &&...p_Params)
					: m_Data(fg_Forward<tfp_CParams>(p_Params)...)
				{
				}

				t_CType *f_Get()
				{
					return &m_Data;
				}
			};


			template <typename t_CType, CSharedPointerOptionUnderlaying t_Options>
			class TCChooseSharedPointerTypeImp<t_CType, t_Options, false>
			{
			public:
				typedef TCSharedPointerCounter<t_CType, NTraits::TCHasVirtualDestructor<typename NTraits::TCRemoveQualifiers<t_CType>::CType>::mc_Value, t_Options> CType;
			};

			template <typename tf_CType, bool t_bVirtualDestructor, CSharedPointerOptionUnderlaying t_Options>
			tf_CType *fg_GetSharedPointerPointer(TCSharedPointerCounter<tf_CType, t_bVirtualDestructor, t_Options> *_pIn)
			{
				static_assert(!TCIsMemberCallableWith_f_RefCountIncrease<tf_CType, void (DMibRefcountDebuggingOnly(NPtr::CRefCountDebugReference &o_DebugRef))>::mc_Value, "Use DMibDefineSharedPointerType to define type");
				if (_pIn)
					return _pIn->f_Get();
				return nullptr;
			}			

			template <typename tf_CToType, typename tf_CType, bool tf_bToVirtualDestructor, bool tf_bVirtualDestructor, CSharedPointerOptionUnderlaying tf_ToOptions, CSharedPointerOptionUnderlaying tf_Options>
			TCSharedPointerCounter<tf_CToType, tf_bToVirtualDestructor, tf_ToOptions> *fg_ConvertSharedPointer(TCSharedPointerCounter<tf_CType, tf_bVirtualDestructor, tf_Options> *_pIn, TCSharedPointerCounter<tf_CToType, tf_bToVirtualDestructor, tf_ToOptions> *_pDummy)
			{
				static_assert(TCIsValidConversion<tf_CToType, tf_CType, void, void>::mc_Value, "Not a valid conversion");
				static_assert(!NTraits::TCHasVirtualDestructor<tf_CToType>::mc_Value || NTraits::TCHasVirtualDestructor<TCSharedPointerCounter<tf_CToType, tf_bToVirtualDestructor, tf_ToOptions>>::mc_Value, "No virtual base");
				static_assert(NTraits::TCHasVirtualDestructor<tf_CToType>::mc_Value || !NTraits::TCHasVirtualDestructor<TCSharedPointerCounter<tf_CToType, tf_bToVirtualDestructor, tf_ToOptions>>::mc_Value, "Virtual base");
				static_assert(tf_ToOptions == tf_Options, "Cannot mix weak support with non-weak support");
				
				return (TCSharedPointerCounter<tf_CToType, tf_bToVirtualDestructor, tf_ToOptions> *)_pIn;
			}
		}

	}

}

