// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>

namespace NMib
{
	namespace NThread
	{
        /***************************************************************************************************\
        |¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯|
        | CThread																							|
        |___________________________________________________________________________________________________|
        \***************************************************************************************************/


		CThread::CThread()
		{
			m_ParentThreadID = 0;
			m_State = EThreadState_None;
			m_ReturnValue = 0;
			m_pThread = nullptr;
			m_ThreadID = 0;
			m_pThreadDestroyContext = nullptr;
			m_bAutoDestroy = false;
			m_bWaitStart = false;
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
			delete this;
			return true;
		}

		CThread::~CThread()
		{
			DMibLockTyped(CMutual, m_Lock);
			if (m_State > 1)
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

			if (pThread->m_bWaitStart)
				pThread->m_EventWantQuit.f_Signal();
			aint Return = pThread->f_Main();

		#ifndef DMibPSupportThreadDestroyNotification
			fg_GetSys()->f_ThreadLocalFreeThread();
		#endif	

			pThread->m_Lock.f_Lock();
			
			pThread->m_ReturnValue = Return;
			pThread->m_State = EThreadState_Stopped;
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

		void CThread::f_Start(EThreadPriority _Prio, mint _StackSize, mint _Affinity, bint _bAutoDestroy, bint _bWaitStart)
		{
			// Make sure that no thread is already running
			{
				DMibLockTyped(CMutual, m_Lock);
				if (m_State > EThreadState_Stopped)
				{
					DMibError("You cannot start a thread that is already running");
				}
				fp_Cleanup();
				m_ThreadQuitEvent.f_ResetSignaled();
				if (_bWaitStart)
					m_EventWantQuit.f_TryWait();
				m_ReturnValue = 0;
				m_ParentThreadID = NSys::fg_Thread_GetCurrentUID();
				m_bAutoDestroy = _bAutoDestroy;
				m_bWaitStart = _bWaitStart;
				NStr::CStr ThreadName;
				auto *pName = f_GetThreadNameRaw();
				if (!pName)
				{
					ThreadName = f_GetThreadName();
					pName = ThreadName;
				}

				m_pThread = NSys::fg_Thread_Create(fsp_ThreadMain, this, _Prio, _StackSize, false, pName, _Affinity, m_ThreadID);
				m_pThreadDestroyContext = NSys::fg_Thread_BeginDestroy(m_pThread);
				m_State = EThreadState_Running;
				if (_bWaitStart)
				{
					m_EventWantQuit.f_Wait();
				}
			}

		}

		ch8 const *CThread::f_GetThreadNameRaw()
		{
			return nullptr;
		}


		mint CThread::f_Stop(bint _bBlock)
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


				switch (m_State)
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
						m_State = EThreadState_EventWantQuit;
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

		void CThread::f_SetPriority(EThreadPriority _Prio)
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
			m_State = EThreadState_None;
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
		


        /***************************************************************************************************\
        |¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯|
        | Reportable																						|
        |___________________________________________________________________________________________________|
        \***************************************************************************************************/

		void CSemaphoreReportableAggregate::f_ReportTo(CSemaphoreReportableAggregate *_pReportTo)
		{
			NPtr::TCUniquePointer<CReportListMember, NMem::CAllocator_NonTrackedHeap> pReportMember = fg_Construct();

			pReportMember->m_pReportTo = _pReportTo;
			pReportMember->m_pReportFrom = this;

			DMibLockTyped(CMutual, fg_GetSys()->m_EventMember_Lock);
			// Lock 
			{
//				DMibLockTyped(CMutual, m_Lock);
				m_ReportTo.f_Insert(*pReportMember);
			}
			{
//				DMibLockTyped(CMutual, _pReportTo->m_Lock);				

				_pReportTo->m_ReportFrom.f_Insert(*pReportMember);
			}
			
			pReportMember.f_Detach();
		}

		void CSemaphoreReportableAggregate::f_ClearReportTo()
		{
			DMibLockTyped(CMutual, fg_GetSys()->m_EventMember_Lock);
			{
				CReportListMember *pMember;
				{
//					DMibLockTyped(CMutual, m_Lock);
					pMember = m_ReportTo.f_Pop();
				}
				while (pMember)
				{

//					CSemaphoreReportableAggregate *pTo = pMember->m_pReportTo;
					{
//						DMibLockTyped(CMutual, pTo->m_Lock);					
						pMember->m_LinkReportFrom.f_Unlink();
					}
					NPtr::TCUniquePointer<CReportListMember, NMem::CAllocator_NonTrackedHeap> pReportMember = fg_Explicit(pMember);
					pReportMember.f_Clear();
					{
//						DMibLockTyped(CMutual, m_Lock);
						pMember = m_ReportTo.f_Pop();
					}
				}
			}
		}

		void CSemaphoreReportableAggregate::f_ClearReportFrom()
		{
			DMibLockTyped(CMutual, fg_GetSys()->m_EventMember_Lock);
			{
				CReportListMember *pMember;
				{
//					DMibLockTyped(CMutual, m_Lock);
					pMember = m_ReportFrom.f_Pop();
				}
				while (pMember)
				{

//					CSemaphoreReportableAggregate *pFrom = pMember->m_pReportFrom;
					{
//						DMibLockTyped(CMutual, pFrom->m_Lock);					
						pMember->m_LinkReportTo.f_Unlink();
					}
					NPtr::TCUniquePointer<CReportListMember, NMem::CAllocator_NonTrackedHeap> pReportMember = fg_Explicit(pMember);
					pReportMember.f_Clear();
					{
//						DMibLockTyp ed(CMutual, m_Lock);
						pMember = m_ReportFrom.f_Pop();
					}
				}
			}
		}

		namespace
		{
			class CCheckRecursive
			{
			public:
				const CSemaphoreReportableAggregate *m_pThis;

				class CCompare
				{
				public:
					inline_small CSemaphoreReportableAggregate const *operator () (CCheckRecursive const &_Node) const
					{
						return _Node.m_pThis;
					}
				};

				NIntrusive::TCAVLLink<> m_Link;
			};
			
			void fg_Signal_CSemaphoreReportableAggregate(CSemaphoreReportableAggregate *_pThis, NIntrusive::TCAVLTree<&CCheckRecursive::m_Link, CCheckRecursive::CCompare> &_Tree, mint _nToSignal)
			{
				if (_Tree.f_FindEqual(_pThis))
					return;

				NSys::fg_Semaphore_Increase(_pThis->m_pSemaphore, _nToSignal);

				if (!_pThis->m_ReportTo.f_IsEmpty())
				{
					CCheckRecursive Check;
					Check.m_pThis = _pThis;
					_Tree.f_Insert(Check);
//					DMibLockTyped(CMutual, _pThis->m_Lock);
					DMibListLinkDS_List(CSemaphoreReportableAggregate::CReportListMember, m_LinkReportTo)::CIterator Iter(_pThis->m_ReportTo);

					while (Iter)
					{
						fg_Signal_CSemaphoreReportableAggregate(Iter->m_pReportTo, _Tree, _nToSignal);
						++Iter;
					}
					_Tree.f_Remove(Check);
				}
			}
		}
        void CSemaphoreReportableAggregate::f_Signal(int _nToSignal)
		{
			DMibLockTyped(CMutual, fg_GetSys()->m_EventMember_Lock);
			NIntrusive::TCAVLTree<&CCheckRecursive::m_Link, CCheckRecursive::CCompare> Recursive;
			fg_Signal_CSemaphoreReportableAggregate(this, Recursive, _nToSignal);
		}
	}

	namespace NPtr
	{
		TCSharedPointerIntrusiveBase<ESharedPointerOption_SupportWeakPointer>::~TCSharedPointerIntrusiveBase()
		{
			DMibCheck(f_RefCountGet() <= 0);
			DMibRefcountDebuggingOnly(if (f_RefCountGet() == 0) m_Debug.f_Destruct());
		}

		TCSharedPointerIntrusiveBase<ESharedPointerOption_None>::~TCSharedPointerIntrusiveBase()
		{
			DMibCheck(f_RefCountGet() <= 0);
			DMibRefcountDebuggingOnly(if (f_RefCountGet() == 0) m_Debug.f_Destruct());
		}
	}
}

