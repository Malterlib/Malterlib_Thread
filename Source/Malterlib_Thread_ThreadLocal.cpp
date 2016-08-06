// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>
#include "../../Core/Source/Malterlib_Core_System.h"
#include "Malterlib_Thread_ThreadLocal_Internal.h"

namespace NMib
{
	
	
	namespace NPrivate
	{
		NMib::NAggregate::TCAggregateSimple<NPrivate::CThreadLocalContext> g_ThreadLocalContext = {DAggregateInit};
		
		/************************************************************************************************\
		||¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯||
		|| CThreadLocalContext
		||______________________________________________________________________________________________||
		\************************************************************************************************/
		
		CThreadLocalContext::CThreadLocalContext()
		{
			// Alloc the thread storage index from the system
			#ifdef DMibPSupportThreadLocalDestructors
				m_iPerThread = NSys::fg_Thread_AllocLocalWithDestructor(fs_PerThreadDestructor);
			#else
				m_iPerThread = NSys::fg_Thread_AllocLocal();
			#endif
			DMibFastCheck(m_iPerThread > 0); // This must succeed
			m_iThreadLocalCurrentLen = 0;
		}

		CThreadLocalContext::~CThreadLocalContext()
		{
			f_FreeThread();
#if DMibEnableSafeCheck > 0
			for (auto iLocal = m_ThreadLocal_DestroyOrder.f_GetIterator(); iLocal; ++iLocal)
			{
				DMibTraceSafe("Thread local for class '{}' not yet destroyed{\n}", iLocal->m_pInterface->f_GetName());
			}
#endif
			DMibFastCheck(m_ThreadLocal_DestroyOrder.f_IsEmpty()); // Anything else means that there are ThreadLocal classes that are still using this context
			// Turns out that lots of threads can still be loaded when you unload an dll
#if DMibEnableSafeCheck > 0 && defined(DMibPSupportAlwaysCreatedThreadLocal)
			NContainer::TCVector<mint, NMem::CAllocator_VirtualNoTracking> SystemThreads;
			{
				NSys::fg_Thread_EnumOtherThreadsInProcess
					(
						[&](mint _ThreadID)
						{
							SystemThreads.f_Insert(_ThreadID);
						}
					)
				;
				SystemThreads.f_Sort();
				NContainer::TCVector<mint, NMem::CAllocator_VirtualNoTracking> LocalThreads;
				for (auto &PerThread : m_lPerThread)
				{
					DMibFastCheck(SystemThreads.f_BinarySearch(PerThread.m_ThreadID) >= 0);
					LocalThreads.f_Insert(PerThread.m_ThreadID);
				}
				LocalThreads.f_Sort();
				for (auto &SystemThread : SystemThreads)
				{
					DMibFastCheck(LocalThreads.f_BinarySearch(SystemThread) >= 0);
				}
			}
#endif

			while (auto pPerThread = m_lPerThread.f_GetRoot())
			{
				m_lPerThread.f_Remove(pPerThread);
				m_PoolPerThread.f_Delete(pPerThread);
			}

			CStorageIndex *pCurrent = m_ThreadLocal_Free.f_GetFirst();
			while (pCurrent)
			{
				pCurrent->m_Link.f_Destruct();
				m_PoolStorageIndices.f_Delete(pCurrent);

				pCurrent = m_ThreadLocal_Free.f_GetFirst();
			}

			NSys::fg_Thread_FreeLocal(m_iPerThread);
		}

		CThreadLocalContext::CPerThread::CPointer::~CPointer()
		{
			DMibSafeCheck(!m_pPtr, "Pointer must already be deleted");
	//		if (m_pPtr)
		//		delete m_pPtr;
		}

	#ifdef DMibPSupportThreadLocalDestructors

		void CThreadLocalContext::fs_PerThreadDestructor(void* _pPerThread)
		{
			CPerThread* pPerThread = (CPerThread*)_pPerThread;

			g_ThreadLocalContext->fp_FreePerThread(pPerThread);
		}

	#endif


		void CThreadLocalContext::fp_GrowTable()
		{
			DMibLock(m_LockContext);
			// Grow 32 at a time
			aint NumNew = 32;				
			while (NumNew--)
			{
				CStorageIndex *pIndex = m_PoolStorageIndices.f_New();
				pIndex->m_iThreadLocal = m_iThreadLocalCurrentLen;
				pIndex->m_Link.f_Construct();
				m_ThreadLocal_Free.f_Insert(pIndex);
				++m_iThreadLocalCurrentLen;
			}

			// Loop through all threadContexts and set the length
			auto Iter = m_lPerThread.f_GetIterator();

			while (Iter)
			{	
				{
					DMibLock(Iter->m_Lock);						
					Iter->m_aThreadLocal.f_SetLen(m_iThreadLocalCurrentLen);
				}
				++Iter;
			}				
		}

		CThreadLocalContext::CPerThread *CThreadLocalContext::fp_GetPerThreadNew(mint _ThreadID)
		{
			DMibLock(m_LockContext);
			
			auto pCurrentThread = m_lPerThread.f_FindEqual(_ThreadID);
			if (pCurrentThread)
				return pCurrentThread; // Can happen when system destroys thread local and another thread local is dependant
			// Lock for the pool
			// The thread storage does not exist for this thread, lets create it
			CPerThread *pThreadLocal = m_PoolPerThread.f_New
				(
					_ThreadID
					, this
				)
			;
			pThreadLocal->m_aThreadLocal.f_SetLen(m_iThreadLocalCurrentLen);
			m_lPerThread.f_Insert(pThreadLocal);
			NAtomic::fg_MemoryFence();
			NSys::fg_Thread_SetLocal(_ThreadID, m_iPerThread, pThreadLocal);
			return pThreadLocal;
		}


		CThreadLocalContext::CPerThread *CThreadLocalContext::fp_GetPerThread(mint _ThreadID)
		{
			CPerThread *pThreadLocal = (CPerThread *)NSys::fg_Thread_GetLocal(_ThreadID, m_iPerThread);
			if (pThreadLocal)
				return pThreadLocal;
			else
				return fp_GetPerThreadNew(_ThreadID);
		}

		CThreadLocalContext::CStorageIndex *CThreadLocalContext::f_Alloc(NThread::CThreadLocalInterface &_Interface, mint &_ThreadLocalLocal)
		{
			DMibLock(m_LockContext);
			fp_GetPerThread(NSys::fg_Thread_GetCurrentUID());

			auto pIndex = m_ThreadLocal_Free.f_Pop();

			if (!pIndex)
			{
				fp_GrowTable();
				pIndex = m_ThreadLocal_Free.f_Pop();
				if (!pIndex)
				{
					// Failed horribly
					DMibError("Failed to allocate thread storage index");
				}
			}
			if (_Interface.m_Flags & NThread::EThreadLocalInterfaceFlag_UseFastStorage)
				pIndex->m_LocalThreadLocal = NSys::fg_Thread_AllocLocalFast();
			else
				pIndex->m_LocalThreadLocal = NSys::fg_Thread_AllocLocal();
			pIndex->m_Link.f_Destruct();
			pIndex->m_pInterface = &_Interface;
			m_ThreadLocal_DestroyOrder.f_InsertFirst(pIndex);

	#ifdef DMibPSupportAlwaysCreatedThreadLocal
			mint iThreadLocal = pIndex->m_iThreadLocal;
			auto Iter = m_lPerThread.f_GetIterator();
			while (Iter)
			{
				CPerThread *pIter = Iter;
				{
					void *pNewPtr = _Interface.f_CreateData(true);
					if (pNewPtr)
					{
						auto &Pointer = pIter->m_aThreadLocal[iThreadLocal];
						DMibSafeCheck(!Pointer.m_pPtr, "Must be empty here");
						Pointer.m_pPtr = pNewPtr;

						if (_Interface.m_Flags & NThread::EThreadLocalInterfaceFlag_AlwaysCreated)
							pIter->m_CreatedAlwaysCreate[iThreadLocal].m_pStorageIndex = pIndex;
						else
							pIter->m_Created[iThreadLocal].m_pStorageIndex = pIndex;

						NAtomic::fg_MemoryFence();
						if (_Interface.m_Flags & NThread::EThreadLocalInterfaceFlag_UseFastStorage)
							NSys::fg_Thread_SetLocalFast(pIter->m_ThreadID, pIndex->m_LocalThreadLocal, pNewPtr);
						else
							NSys::fg_Thread_SetLocal(pIter->m_ThreadID, pIndex->m_LocalThreadLocal, pNewPtr);
					}
				}
				++Iter;
			}
	#endif

			//DMibDTraceSafe("{} - {}" DMibNewLine, DMibPFunction << pIndex->m_iThreadLocal);
			_ThreadLocalLocal = pIndex->m_LocalThreadLocal;
			return pIndex;
		}

		void CThreadLocalContext::f_ReinitForThread(CStorageIndex *_pStorageIndex)
		{
			DMibLock(m_LockContext);
			CPerThread *pThreadLocal = fp_GetPerThread(NSys::fg_Thread_GetCurrentUID());
			
			auto iThreadLocal = _pStorageIndex->m_iThreadLocal;

			DMibFastCheck(pThreadLocal->m_DestroyingID != iThreadLocal); // Circular dependency

			{
				// Lock so m_aThreadLocal is safe
				DMibLock(pThreadLocal->m_Lock);

				auto &Pointer = pThreadLocal->m_aThreadLocal[iThreadLocal];

				if (!Pointer.m_pPtr)
					return;

				auto &Interface = *_pStorageIndex->m_pInterface;

				void *pOldPointer = Pointer.m_pPtr;
				void *pNewPtr = Interface.f_CreateData(pOldPointer, true);
				Pointer.m_pPtr = pNewPtr;

				DMibFastCheck(pNewPtr);
				NAtomic::fg_MemoryFence();

				if (Interface.m_Flags & NThread::EThreadLocalInterfaceFlag_UseFastStorage)
					NSys::fg_Thread_SetLocalFast(_pStorageIndex->m_LocalThreadLocal, pNewPtr);
				else
					NSys::fg_Thread_SetLocal(_pStorageIndex->m_LocalThreadLocal, pNewPtr);

				Interface.f_DeleteItem(pOldPointer);
			}
		}

		void CThreadLocalContext::f_Free(NThread::CThreadLocalInterface &_Interface, CStorageIndex *_pStorageIndex)
		{
			DMibLock(m_LockContext);
			//DMibDTraceSafe("{} - {}" DMibNewLine, DMibPFunction << _pStorageIndex->m_iThreadLocal);
					
			{
				// Lock for the pool
				// The thread storage does not exist for this thread, lets create it
				auto Iter = m_lPerThread.f_GetIterator();
				while (Iter)
				{
					CPerThread *pIter = Iter;
					auto &Pointer = pIter->m_aThreadLocal[_pStorageIndex->m_iThreadLocal];
					void *pPtr = Pointer.m_pPtr;
					Pointer.m_pPtr = nullptr;

					if (pPtr)
					{
						if (_Interface.m_Flags & NThread::EThreadLocalInterfaceFlag_AlwaysCreated)
						{
							DMibFastCheck(pIter->m_CreatedAlwaysCreate.f_FindEqual(_pStorageIndex->m_iThreadLocal));
							pIter->m_CreatedAlwaysCreate.f_Remove(_pStorageIndex->m_iThreadLocal);
						}
						else
						{
							DMibFastCheck(pIter->m_Created.f_FindEqual(_pStorageIndex->m_iThreadLocal));
							pIter->m_Created.f_Remove(_pStorageIndex->m_iThreadLocal);
						}
						_Interface.f_DeleteItem(pPtr);
#ifdef DMibPSupportAlwaysCreatedThreadLocal
						if (_Interface.m_Flags & NThread::EThreadLocalInterfaceFlag_UseFastStorage)
							NSys::fg_Thread_SetLocalFast(pIter->m_ThreadID, _pStorageIndex->m_LocalThreadLocal, nullptr);
						else
							NSys::fg_Thread_SetLocal(pIter->m_ThreadID, _pStorageIndex->m_LocalThreadLocal, nullptr);
#endif
					}

					++Iter;
				}
			}

			m_ThreadLocal_Free.f_Insert(_pStorageIndex);
			if (_Interface.m_Flags & NThread::EThreadLocalInterfaceFlag_UseFastStorage)
				NSys::fg_Thread_FreeLocalFast(_pStorageIndex->m_LocalThreadLocal);
			else
				NSys::fg_Thread_FreeLocal(_pStorageIndex->m_LocalThreadLocal);
		}

		void CThreadLocalContext::f_EnumThreads(NFunction::TCFunction<void (mint _ThreadID)> const &_EnumFunc)
		{
			DMibLock(m_LockContext);

			for (auto iThread = m_lPerThread.f_GetIterator(); iThread; ++iThread)
			{
				_EnumFunc(iThread->m_ThreadID);
			}
		}

		void CThreadLocalContext::f_CreateThread(mint _ThreadID, mint _ParentThread)
		{
			{
				// Do a quick check
				auto pAlreadyCreated = (CPerThread *)NSys::fg_Thread_GetLocal(_ThreadID, m_iPerThread);
				if (pAlreadyCreated && pAlreadyCreated->m_bOnThreadCreated)
					return;
			}

			DMibLock(m_LockContext);
			//DMibDTraceSafe("{} - {} from {}" DMibNewLine, DMibPFunction << NSys::fg_Thread_GetCurrentUID() << _ParentThread);

			CPerThread *pCopyTo = fp_GetPerThread(_ThreadID);

			if (pCopyTo->m_bOnThreadCreated)
				return; // Already created

			pCopyTo->m_bOnThreadCreated = true;

			CPerThread *pCopyFrom = m_lPerThread.f_FindEqual(_ParentThread);

			{
				auto Iter = m_ThreadLocal_DestroyOrder.f_GetIterator();
				Iter.f_Reverse(m_ThreadLocal_DestroyOrder);
				for (; Iter; --Iter)
				{
					CStorageIndex *pIndex = Iter;

					auto iThreadLocal = pIndex->m_iThreadLocal;

					auto &Pointer = pCopyTo->m_aThreadLocal[iThreadLocal];

					if (Pointer.m_pPtr)
						continue;
					void *pPtr = nullptr;
					if (pCopyFrom)
						pPtr = pCopyFrom->m_aThreadLocal[iThreadLocal].m_pPtr;

					auto &Interface = *pIndex->m_pInterface;

					void *pNewPtr = nullptr;
					if (pPtr)
						pNewPtr = Interface.f_CreateData(pPtr, false);
					if (!pNewPtr)
						pNewPtr = Interface.f_CreateData(true);
					if (pNewPtr)
					{
						Pointer.m_pPtr = pNewPtr;

						if (Interface.m_Flags & NThread::EThreadLocalInterfaceFlag_AlwaysCreated)
							pCopyTo->m_CreatedAlwaysCreate[iThreadLocal].m_pStorageIndex = pIndex;
						else
							pCopyTo->m_Created[iThreadLocal].m_pStorageIndex = pIndex;

						NAtomic::fg_MemoryFence();
#ifdef DMibPSupportAlwaysCreatedThreadLocal
						if (Interface.m_Flags & NThread::EThreadLocalInterfaceFlag_UseFastStorage)
							NSys::fg_Thread_SetLocalFast(_ThreadID, pIndex->m_LocalThreadLocal, pNewPtr);
						else
							NSys::fg_Thread_SetLocal(_ThreadID, pIndex->m_LocalThreadLocal, pNewPtr);
#endif
					}
				}
			}
		}

		void CThreadLocalContext::fp_FreePerThread(CPerThread* _pPerThread)
		{
			DMibLock(m_LockContext);
					
			if (!_pPerThread)
			{
				#ifndef DMibPSupportThreadLocalDestructors			
					_pPerThread = (CPerThread *)NSys::fg_Thread_GetLocal(m_iPerThread);
				#else
					return;
				#endif
			}
			

			if (_pPerThread)
			{
				if (NSys::fg_Thread_GetLocal(m_iPerThread) == nullptr)
					NSys::fg_Thread_SetLocal(m_iPerThread, _pPerThread);
					
				DMibFastCheck(m_lPerThread.f_FindEqual(NSys::fg_Thread_GetCurrentUID()) == _pPerThread);

				while (true)
				{
					CStorageIndex *pIndex = nullptr;
					{
						auto pToDelete = _pPerThread->m_Created.f_FindLargest();
						if (!pToDelete)
						{
							pToDelete = _pPerThread->m_CreatedAlwaysCreate.f_FindLargest();
							if (pToDelete)
							{
								pIndex = pToDelete->m_pStorageIndex;
								_pPerThread->m_CreatedAlwaysCreate.f_Remove(pToDelete);
							}
							else
								break;
						}
						else
						{
							pIndex = pToDelete->m_pStorageIndex;
							_pPerThread->m_Created.f_Remove(pToDelete);
						}
					}
					
					auto &Index = *pIndex;
					
					auto &Pointer = _pPerThread->m_aThreadLocal[Index.m_iThreadLocal];

					void *pPtr = Pointer.m_pPtr;

					DMibFastCheck(pPtr);

					if (pPtr)
					{
						auto &Interface = *Index.m_pInterface;
						
						_pPerThread->m_DestroyingID = Index.m_iThreadLocal;
					//#ifndef DMibPSupportThreadLocalDestructors
						if (Interface.m_Flags & NThread::EThreadLocalInterfaceFlag_UseFastStorage)
							NSys::fg_Thread_SetLocalFast(Index.m_LocalThreadLocal, nullptr);
						else
							NSys::fg_Thread_SetLocal(Index.m_LocalThreadLocal, nullptr);
					//#endif
						Pointer.m_pPtr = nullptr;
						if (pPtr)
							Interface.f_DeleteItem(pPtr);
					}

				}

			//#ifndef DMibPSupportThreadLocalDestructors			
				if (NSys::fg_Thread_GetLocal(m_iPerThread) == _pPerThread)
					NSys::fg_Thread_SetLocal(m_iPerThread, nullptr);
			//#endif
				m_lPerThread.f_Remove(_pPerThread);
				m_PoolPerThread.f_Delete(_pPerThread);
			}
			else
			{
				DMibFastCheck(m_lPerThread.f_FindEqual(NSys::fg_Thread_GetCurrentUID()) == nullptr);
			}
		}

		void CThreadLocalContext::f_FreeThread()
		{
			fp_FreePerThread(nullptr);
		}

		void *CThreadLocalContext::f_Get(CStorageIndex *_pStorageIndex)
		{
			CPerThread *pThreadLocal = fp_GetPerThread(NSys::fg_Thread_GetCurrentUID());

			// Lock so m_aThreadLocal is safe
			DMibLock(pThreadLocal->m_Lock);			

			return pThreadLocal->m_aThreadLocal[_pStorageIndex->m_iThreadLocal].m_pPtr;
		}

		void CThreadLocalContext::f_Set(CStorageIndex *_pStorageIndex, void *_pValue)
		{
			DMibLock(m_LockContext);
			CPerThread *pThreadLocal = fp_GetPerThread(NSys::fg_Thread_GetCurrentUID());
			
			auto iThreadLocal = _pStorageIndex->m_iThreadLocal;

			DMibFastCheck(pThreadLocal->m_DestroyingID != iThreadLocal); // Circular dependency

			{
				// Lock so m_aThreadLocal is safe
				DMibLock(pThreadLocal->m_Lock);			

				auto &Pointer = pThreadLocal->m_aThreadLocal[iThreadLocal];

				DMibFastCheck(_pStorageIndex->m_Link.f_IsInList()); // Using ThreadLocal variable in a non-threadsafe way?
				DMibFastCheck(!Pointer.m_pPtr); // You are setting a ThreadLocal that has already been set

				auto &Interface = *_pStorageIndex->m_pInterface;
				Pointer.m_pPtr = _pValue;

				DMibFastCheck(_pValue);

				if (Interface.m_Flags & NThread::EThreadLocalInterfaceFlag_AlwaysCreated)
					pThreadLocal->m_CreatedAlwaysCreate[iThreadLocal].m_pStorageIndex = _pStorageIndex;
				else
					pThreadLocal->m_Created[iThreadLocal].m_pStorageIndex = _pStorageIndex;

				if (Interface.m_Flags & NThread::EThreadLocalInterfaceFlag_UseFastStorage)
					NSys::fg_Thread_SetLocalFast(_pStorageIndex->m_LocalThreadLocal, _pValue);
				else
					NSys::fg_Thread_SetLocal(_pStorageIndex->m_LocalThreadLocal, _pValue);
			}
		}
	}
	
	void *CSystem::f_ThreadLocalAlloc(NThread::CThreadLocalInterface &_Interface, mint &_ThreadLocalLocal)
	{
		return NPrivate::g_ThreadLocalContext->f_Alloc(_Interface, _ThreadLocalLocal);
	}

	void CSystem::f_ThreadLocalFree(NThread::CThreadLocalInterface &_Interface, void *_pStorageIndex)
	{
		NPrivate::g_ThreadLocalContext->f_Free(_Interface, (NPrivate::CThreadLocalContext::CStorageIndex *)_pStorageIndex);
	}

	void CSystem::f_ThreadLocalFreeThread()
	{
		NPrivate::g_ThreadLocalContext->f_FreeThread();
	}

	void CSystem::f_ThreadLocalCreateThread(mint _ThreadID, mint _ParentThreadID)
	{
		NPrivate::g_ThreadLocalContext->f_CreateThread(_ThreadID, _ParentThreadID);
	}

	void CSystem::f_ThreadLocalReinitForThread(void *_pStorageIndex)
	{
		NPrivate::g_ThreadLocalContext->f_ReinitForThread((NPrivate::CThreadLocalContext::CStorageIndex *)_pStorageIndex);
	}

	void *CSystem::f_ThreadLocalGet(void *_pStorageIndex)
	{
		return NPrivate::g_ThreadLocalContext->f_Get((NPrivate::CThreadLocalContext::CStorageIndex *)_pStorageIndex);
	}

	void CSystem::f_ThreadLocalSet(void *_pStorageIndex, void *_pValue)
	{
		return NPrivate::g_ThreadLocalContext->f_Set((NPrivate::CThreadLocalContext::CStorageIndex *)_pStorageIndex, _pValue);
	}
	
	void CSystem::f_OnThreadCreated(mint _ThreadID, mint _ParentID)
	{
		f_MemoryManager_OnThreadCreated(_ThreadID, _ParentID);
		f_ThreadLocalCreateThread(_ThreadID, _ParentID);
	}

	bool CSystem::ms_bDisableMemoryManagerLeakReport = false;

	void CSystem::f_MemoryManager_DisableLeakReport(bool _bDisable)
	{
		ms_bDisableMemoryManagerLeakReport = _bDisable;
	}


	void CSystem::f_OnThreadDestroyed()
	{
		f_ThreadLocalFreeThread();
	}
	
	void CSystem::fp_ThreadLocalCreate()
	{
		NPrivate::g_ThreadLocalContext.f_Construct();
	}
	void CSystem::fp_ThreadLocalDestroy()
	{
		if (!g_bMemoryManagerNeededAfterDestroy)
			NPrivate::g_ThreadLocalContext.f_Destruct();
	}

	void CSystem::f_ThreadEnum(NFunction::TCFunction<void (mint _ThreadID)> const &_EnumFunc)
	{
		return NPrivate::g_ThreadLocalContext->f_EnumThreads(_EnumFunc);
	}


} // Namespace NMib
