// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>
#include "../../Core/Source/Malterlib_Core_System.h"
#include "Malterlib_Thread_ThreadLocal_Internal.h"

#if defined(DPlatformFamily_Linux) && defined(DMibConfig_LinuxPThreadMonitoring) && defined(DMibAssumeMalterlibHost)
#	define DUseThreadDestroyNotifications
#elif defined(DPlatformFamily_macOS) && defined(DMibConfig_PThreadIntrospection)
#	define DUseThreadDestroyNotifications
#endif

namespace NMib
{
	namespace NPrivate
	{
		constinit NMib::NStorage::TCAggregateSimple<NPrivate::CThreadLocalContext> g_ThreadLocalContext = {DAggregateInit};

		/************************************************************************************************\
		||¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯||
		|| CThreadLocalContext
		||______________________________________________________________________________________________||
		\************************************************************************************************/

		CThreadLocalContext::CThreadLocalContext()
		{
			// Alloc the thread storage index from the system
#if defined(DUseThreadDestroyNotifications)
			mp_iPerThread = NSys::fg_Thread_AllocLocal();
#elif defined(DMibPSupportThreadLocalDestructors) && defined(DMibStaticThreadLocals)
			mp_iPerThreadDestructor = NSys::fg_Thread_AllocLocalWithDestructor(fsp_PerThreadDestructor);
			mp_iPerThread = NSys::fg_Thread_AllocLocal();
#elif defined(DMibPSupportThreadLocalDestructors)
			mp_iPerThread = NSys::fg_Thread_AllocLocalWithDestructor(fsp_PerThreadDestructor);
#else
			mp_iPerThread = NSys::fg_Thread_AllocLocal();
#endif
			DMibFastCheck(mp_iPerThread > 0); // This must succeed
		}

		CThreadLocalContext::~CThreadLocalContext()
		{
			f_FreeThread();
#if DMibEnableSafeCheck > 0
			for (auto iLocal = mp_ThreadLocal_DestroyOrder.f_GetIterator(); iLocal; ++iLocal)
				DMibTraceSafe("{} Thread local for class '{}' not yet destroyed{\n}", this, iLocal->m_pInterface->f_GetName());
#endif
			DMibFastCheck(mp_ThreadLocal_DestroyOrder.f_IsEmpty()); // Anything else means that there are ThreadLocal classes that are still using this context
			// Turns out that lots of threads can still be loaded when you unload an dll
#if DMibEnableSafeCheck > 0 && defined(DMibPSupportAlwaysCreatedThreadLocal)
			if (!fg_GetSys()->f_IsDll())
			{
				NContainer::TCVector<umint, NMemory::CAllocator_VirtualNoTracking> SystemThreads;
				NSys::fg_Thread_EnumOtherThreadsInProcess
					(
						[&](umint _ThreadID)
						{
							SystemThreads.f_Insert(_ThreadID);
						}
					)
				;
				SystemThreads.f_Sort();
				NContainer::TCVector<umint, NMemory::CAllocator_VirtualNoTracking> LocalThreads;
				for (auto &PerThread : mp_PerThreadByThreadID)
				{
					if (SystemThreads.f_BinarySearch(PerThread.m_ThreadID) < 0)
					{
						DMibTraceSafe
							(
								"Thread {} '{}' is still registered but no longer exists; it started at {} in {} ({}){\n}"
								, PerThread.m_ThreadID
								, PerThread.m_Name
								, PerThread.m_pStartAddress
								, PerThread.m_StartSymbol
								, PerThread.m_StartModule
							)
						;
					}
					DMibFastCheck(SystemThreads.f_BinarySearch(PerThread.m_ThreadID) >= 0);
					LocalThreads.f_Insert(PerThread.m_ThreadID);
				}

				/*
				LocalThreads.f_Sort();
				for (auto &SystemThread : SystemThreads)
				{
					DMibFastCheck(LocalThreads.f_BinarySearch(SystemThread) >= 0);
				}*/
			}
#endif

			while (auto pPerThread = mp_PerThreadByThreadID.f_GetRoot())
			{
				NSys::fg_Thread_SetLocal(pPerThread->m_ThreadID, mp_iPerThread, nullptr);
				mp_PerThreadByThreadID.f_Remove(pPerThread);
				mp_PoolPerThread.f_Delete(pPerThread);
			}

			CStorageIndex *pCurrent = mp_ThreadLocal_Free.f_GetFirst();
			while (pCurrent)
			{
				pCurrent->m_Link.f_Destruct();
				mp_PoolStorageIndices.f_Delete(pCurrent);

				pCurrent = mp_ThreadLocal_Free.f_GetFirst();
			}

			NSys::fg_Thread_SetLocal(mp_iPerThread, nullptr);
#if defined(DUseThreadDestroyNotifications)
			NSys::fg_Thread_FreeLocal(mp_iPerThread);
#elif defined(DMibPSupportThreadLocalDestructors) && defined(DMibStaticThreadLocals)
			NSys::fg_Thread_FreeLocalWithDestructor(mp_iPerThreadDestructor);
			NSys::fg_Thread_FreeLocal(mp_iPerThread);
#elif defined(DMibPSupportThreadLocalDestructors)
			NSys::fg_Thread_FreeLocalWithDestructor(mp_iPerThread);
#else
			NSys::fg_Thread_FreeLocal(mp_iPerThread);
#endif
		}

		CThreadLocalContext::CPerThread::CPointer::~CPointer()
		{
			DMibSafeCheck(!m_pPtr, "Pointer must already be deleted");
		}

		void CThreadLocalContext::f_PrepareFork()
		{
			mp_LockContext.f_Lock();
			mp_LockContext.f_PrepareFork();

			mp_PoolAllocation.f_Lock();
			mp_PoolAllocation.f_PrepareFork();
		}

		void CThreadLocalContext::f_ForkedChild()
		{
			// Get rid of all thread locals as they are now defunct
			mp_PerThreadByThreadID.f_Construct();

			mp_PoolAllocation.f_ForkedChild();
			mp_PoolAllocation.f_Unlock();

			mp_LockContext.f_ForkedChild();
			mp_LockContext.f_Unlock();
		}

		void CThreadLocalContext::f_ForkedParent()
		{
			mp_PoolAllocation.f_ForkedParent();
			mp_PoolAllocation.f_Unlock();

			mp_LockContext.f_ForkedParent();
			mp_LockContext.f_Unlock();
		}

#ifdef DMibPSupportThreadLocalDestructors

		void CThreadLocalContext::fsp_PerThreadDestructor(void *_pPerThread)
		{
			CPerThread *pPerThread = (CPerThread *)_pPerThread;

			g_ThreadLocalContext->fp_FreePerThread(pPerThread);
		}

#endif

		void CThreadLocalContext::fp_GrowTable()
		{
			DMibLock(mp_LockContext);
			// Grow 32 at a time
			aint NumNew = 32;
			while (NumNew--)
			{
				CStorageIndex *pIndex = mp_PoolStorageIndices.f_New();
				pIndex->m_iThreadLocal = mp_iThreadLocalCurrentLen;
				pIndex->m_Link.f_Construct();
				mp_ThreadLocal_Free.f_Insert(pIndex);
				++mp_iThreadLocalCurrentLen;
			}

			// Loop through all threadContexts and set the length
			auto Iter = mp_PerThreadByThreadID.f_GetIterator();

			while (Iter)
			{
				{
					DMibLock(Iter->m_Lock);
					Iter->m_ThreadLocals.f_SetLen(mp_iThreadLocalCurrentLen);
				}
				++Iter;
			}
		}

		CThreadLocalContext::CPerThread *CThreadLocalContext::fp_GetPerThreadNew(umint _ThreadID)
		{
			DMibLock(mp_LockContext);

			auto pCurrentThread = mp_PerThreadByThreadID.f_FindEqual(_ThreadID);
			if (pCurrentThread)
			{
				if (_ThreadID == NSys::fg_Thread_GetCurrentUID())
				{
#if defined(DMibPSupportThreadLocalDestructors) && defined(DMibStaticThreadLocals) && !defined(DUseThreadDestroyNotifications)
					NSys::fg_Thread_SetLocalDestructor(_ThreadID, mp_iPerThreadDestructor, pCurrentThread);
#endif
					NSys::fg_Thread_SetLocal(mp_iPerThread, pCurrentThread);
				}

				return pCurrentThread; // Can happen when system destroys thread local and another thread local is dependant
			}
			// Lock for the pool
			// The thread storage does not exist for this thread, lets create it
			CPerThread *pThreadLocal = mp_PoolPerThread.f_New(_ThreadID, this);
			pThreadLocal->m_ThreadLocals.f_SetLen(mp_iThreadLocalCurrentLen);
#if DMibEnableSafeCheck > 0
			pThreadLocal->m_pStartAddress = NSys::fg_Thread_GetStartAddress(_ThreadID);
#endif
			mp_PerThreadByThreadID.f_Insert(pThreadLocal);
			NAtomic::fg_MemoryFence();
#if defined(DMibPSupportThreadLocalDestructors) && defined(DMibStaticThreadLocals) && !defined(DUseThreadDestroyNotifications)
			NSys::fg_Thread_SetLocalDestructor(_ThreadID, mp_iPerThreadDestructor, pThreadLocal);
#endif
			NSys::fg_Thread_SetLocal(_ThreadID, mp_iPerThread, pThreadLocal);
			return pThreadLocal;
		}

		bool CThreadLocalContext::f_ThreadDestroyed() const
		{
#ifdef DPlatformFamily_Linux
			return NSys::fg_Thread_GetLocalsDestroyed(mp_iPerThread);
#elif defined(DPlatformFamily_macOS) && defined(DMibConfig_PThreadIntrospection)
			return NSys::fg_Thread_GetLocalsDestroyed(mp_iPerThread) || (umint)NSys::fg_Thread_GetLocal(mp_iPerThread) == TCLimitsInt<umint>::mc_Max;
#else
			return (umint)NSys::fg_Thread_GetLocal(mp_iPerThread) == TCLimitsInt<umint>::mc_Max;
#endif
		}

		bool CThreadLocalContext::f_ThreadCreated()
		{
			return (umint)NSys::fg_Thread_GetLocal(mp_iPerThread) != 0;
		}

		CThreadLocalContext::CPerThread *CThreadLocalContext::fp_GetPerThread(umint _ThreadID)
		{
			CPerThread *pThreadLocal = (CPerThread *)NSys::fg_Thread_GetLocal(_ThreadID, mp_iPerThread);
			DMibFastCheck(((umint)pThreadLocal != TCLimitsInt<umint>::mc_Max));
			if (pThreadLocal && ((umint)pThreadLocal != TCLimitsInt<umint>::mc_Max))
				return pThreadLocal;
			else
				return fp_GetPerThreadNew(_ThreadID);
		}

		CThreadLocalContext::CStorageIndex *CThreadLocalContext::f_Alloc(NThread::CThreadLocalInterface &_Interface, umint &_ThreadLocalLocal)
		{
			DMibLock(mp_LockContext);
			fp_GetPerThread(NSys::fg_Thread_GetCurrentUID());

			auto pIndex = mp_ThreadLocal_Free.f_Pop();

			if (!pIndex)
			{
				fp_GrowTable();
				pIndex = mp_ThreadLocal_Free.f_Pop();
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
			mp_ThreadLocal_DestroyOrder.f_InsertFirst(pIndex);

#ifdef DMibPSupportAlwaysCreatedThreadLocal
			umint iThreadLocal = pIndex->m_iThreadLocal;
			auto Iter = mp_PerThreadByThreadID.f_GetIterator();
			while (Iter)
			{
				CPerThread *pIter = Iter;
				{
					void *pNewPtr = _Interface.f_CreateData(nullptr, true, pIter->m_ThreadID);
					if (pNewPtr)
					{
						DMibLock(pIter->m_Lock);

						DMibFastCheck(pIter->m_ThreadLocals.f_IsPosValid(iThreadLocal));
						auto &Pointer = pIter->m_ThreadLocals.f_GetArray()[iThreadLocal];
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

			//DMibDTraceSafe("{} - {} = {}" DMibNewLine, DMibPFunction, pIndex->m_iThreadLocal, mp_ThreadLocal_DestroyOrder.f_GetLen());
			_ThreadLocalLocal = pIndex->m_LocalThreadLocal;
			return pIndex;
		}

		void CThreadLocalContext::f_ReinitForThread(CStorageIndex *_pStorageIndex)
		{
			auto &Interface = *_pStorageIndex->m_pInterface;
			DMibLock(mp_LockContext);
			CPerThread *pThreadLocal = fp_GetPerThread(NSys::fg_Thread_GetCurrentUID());

			auto iThreadLocal = _pStorageIndex->m_iThreadLocal;

			DMibFastCheck(pThreadLocal->m_DestroyingID != iThreadLocal); // Circular dependency

			{
				// Lock so m_ThreadLocals is safe
				DMibLock(pThreadLocal->m_Lock);

				DMibFastCheck(pThreadLocal->m_ThreadLocals.f_IsPosValid(iThreadLocal));
				auto &Pointer = pThreadLocal->m_ThreadLocals.f_GetArray()[iThreadLocal];

				if (!Pointer.m_pPtr || !(Interface.m_Flags & NThread::EThreadLocalInterfaceFlag_Inherit))
					return;

				NThread::CThreadLocalInterface::CSafeAlloc Memory(nullptr, {nullptr, 0});
				{
					DMibUnlock(pThreadLocal->m_Lock);
					Memory = Interface.f_AllocData();
				}

				void *pOldPointer = Pointer.m_pPtr;
				void *pNewPtr = Interface.f_CreateDataMove(pOldPointer, Memory.m_Memory.m_pMemory);
				DMibFastCheck(pNewPtr);
				Memory.f_Claim();
				Pointer.m_pPtr = pNewPtr;

				NAtomic::fg_MemoryFence();

				if (Interface.m_Flags & NThread::EThreadLocalInterfaceFlag_UseFastStorage)
					NSys::fg_Thread_SetLocalFast(_pStorageIndex->m_LocalThreadLocal, pNewPtr);
				else
					NSys::fg_Thread_SetLocal(_pStorageIndex->m_LocalThreadLocal, pNewPtr);

				Interface.f_DeleteItem(pOldPointer);
			}
		}

		void CThreadLocalContext::f_DestroyForThread(CStorageIndex *_pStorageIndex)
		{
			DMibLock(mp_LockContext);
			CPerThread *pThreadLocal = fp_GetPerThread(NSys::fg_Thread_GetCurrentUID());

			auto iThreadLocal = _pStorageIndex->m_iThreadLocal;

			DMibFastCheck(pThreadLocal->m_DestroyingID != iThreadLocal); // Circular dependency

			{
				// Lock so m_ThreadLocals is safe
				DMibLock(pThreadLocal->m_Lock);

				DMibFastCheck(pThreadLocal->m_ThreadLocals.f_IsPosValid(iThreadLocal));
				auto &Pointer = pThreadLocal->m_ThreadLocals.f_GetArray()[iThreadLocal];

				if (!Pointer.m_pPtr)
					return;

				auto &Interface = *_pStorageIndex->m_pInterface;

				void *pOldPointer = Pointer.m_pPtr;
				Pointer.m_pPtr = nullptr;

				NAtomic::fg_MemoryFence();

				if (Interface.m_Flags & NThread::EThreadLocalInterfaceFlag_AlwaysCreated)
				{
					DMibFastCheck(pThreadLocal->m_CreatedAlwaysCreate.f_FindEqual(_pStorageIndex->m_iThreadLocal));
					pThreadLocal->m_CreatedAlwaysCreate.f_Remove(_pStorageIndex->m_iThreadLocal);
				}
				else
				{
					DMibFastCheck(pThreadLocal->m_Created.f_FindEqual(_pStorageIndex->m_iThreadLocal));
					pThreadLocal->m_Created.f_Remove(_pStorageIndex->m_iThreadLocal);
				}

				Interface.f_DeleteItem(pOldPointer);

				if (Interface.m_Flags & NThread::EThreadLocalInterfaceFlag_UseFastStorage)
					NSys::fg_Thread_SetLocalFast(pThreadLocal->m_ThreadID, _pStorageIndex->m_LocalThreadLocal, nullptr);
				else
					NSys::fg_Thread_SetLocal(pThreadLocal->m_ThreadID, _pStorageIndex->m_LocalThreadLocal, nullptr);
			}
		}

		void CThreadLocalContext::f_Free(NThread::CThreadLocalInterface &_Interface, CStorageIndex *_pStorageIndex)
		{
			DMibLock(mp_LockContext);
			//DMibDTraceSafe("{} - {} = {}" DMibNewLine, DMibPFunction, _pStorageIndex->m_iThreadLocal, mp_ThreadLocal_DestroyOrder.f_GetLen());

			{
				// Lock for the pool
				// The thread storage does not exist for this thread, lets create it
				auto Iter = mp_PerThreadByThreadID.f_GetIterator();
				while (Iter)
				{
					CPerThread *pIter = Iter;
					DMibLock(pIter->m_Lock);

					DMibFastCheck(pIter->m_ThreadLocals.f_IsPosValid(_pStorageIndex->m_iThreadLocal));
					auto &Pointer = pIter->m_ThreadLocals.f_GetArray()[_pStorageIndex->m_iThreadLocal];
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
#ifdef DMibPSupportSetThreadLocalForOtherThread
						if (_Interface.m_Flags & NThread::EThreadLocalInterfaceFlag_UseFastStorage)
							NSys::fg_Thread_SetLocalFast(pIter->m_ThreadID, _pStorageIndex->m_LocalThreadLocal, nullptr);
						else
							NSys::fg_Thread_SetLocal(pIter->m_ThreadID, _pStorageIndex->m_LocalThreadLocal, nullptr);
#endif
					}

					++Iter;
				}
			}

			mp_ThreadLocal_Free.f_InsertFirst(_pStorageIndex);
			if (_Interface.m_Flags & NThread::EThreadLocalInterfaceFlag_UseFastStorage)
				NSys::fg_Thread_FreeLocalFast(_pStorageIndex->m_LocalThreadLocal);
			else
				NSys::fg_Thread_FreeLocal(_pStorageIndex->m_LocalThreadLocal);
		}

		void CThreadLocalContext::f_EnumThreads(NFunction::TCFunction<void (umint _ThreadID)> const &_EnumFunc)
		{
			DMibLock(mp_LockContext);

			for (auto iThread = mp_PerThreadByThreadID.f_GetIterator(); iThread; ++iThread)
				_EnumFunc(iThread->m_ThreadID);
		}

		void CThreadLocalContext::f_CreateThread(umint _ThreadID, umint _ParentThread)
		{
			{
				// Do a quick check
				auto pAlreadyCreated = (CPerThread *)NSys::fg_Thread_GetLocal(_ThreadID, mp_iPerThread);
				DMibFastCheck((umint)pAlreadyCreated != TCLimitsInt<umint>::mc_Max);
				if (pAlreadyCreated && pAlreadyCreated->m_bOnThreadCreated)
					return;
			}

			DMibLock(mp_LockContext);
			//DMibDTraceSafe("{} - {} from {}" DMibNewLine, DMibPFunction, NSys::fg_Thread_GetCurrentUID(), _ParentThread);

			CPerThread *pCopyTo = fp_GetPerThread(_ThreadID);

			if (pCopyTo->m_bOnThreadCreated)
				return; // Already created

			pCopyTo->m_bOnThreadCreated = true;

			DMibFastCheck(fg_GetSys()->f_ThreadCreated());

			CPerThread *pCopyFrom = mp_PerThreadByThreadID.f_FindEqual(_ParentThread);

			{
				auto Iter = mp_ThreadLocal_DestroyOrder.f_GetIterator();
				Iter.f_Reverse(mp_ThreadLocal_DestroyOrder);
				for (; Iter; --Iter)
				{
					CStorageIndex *pIndex = Iter;

					auto iThreadLocal = pIndex->m_iThreadLocal;

					DMibLock(pCopyTo->m_Lock);

					DMibFastCheck(pCopyTo->m_ThreadLocals.f_IsPosValid(iThreadLocal));
					auto &Pointer = pCopyTo->m_ThreadLocals.f_GetArray()[iThreadLocal];

					if (Pointer.m_pPtr)
						continue;
					auto &Interface = *pIndex->m_pInterface;
					void *pNewPtr = nullptr;

					if ((Interface.m_Flags & NThread::EThreadLocalInterfaceFlag_Inherit) && pCopyFrom)
					{
						{
							DMibUnlock(pCopyTo->m_Lock);
							DMibLock(pCopyFrom->m_Lock);
							DMibFastCheck(pCopyFrom->m_ThreadLocals.f_IsPosValid(iThreadLocal));
							void *pPtr = pCopyFrom->m_ThreadLocals.f_GetArray()[iThreadLocal].m_pPtr;
							if (pPtr)
							{
								NThread::CThreadLocalInterface::CSafeAlloc Memory(nullptr, {nullptr, 0});
								{
									DMibUnlock(pCopyFrom->m_Lock);
									Memory = Interface.f_AllocData();
								}

								pNewPtr = Interface.f_CreateDataCopy(pPtr, Memory.m_Memory.m_pMemory);
								DMibFastCheck(pNewPtr);
								if (pNewPtr)
									Memory.f_Claim();
							}
						}
					}

					if (!pNewPtr)
						pNewPtr = Interface.f_CreateData(nullptr, true, _ThreadID);

					if (pNewPtr)
					{
						Pointer.m_pPtr = pNewPtr;
						if (Interface.m_Flags & NThread::EThreadLocalInterfaceFlag_AlwaysCreated)
							pCopyTo->m_CreatedAlwaysCreate[iThreadLocal].m_pStorageIndex = pIndex;
						else
							pCopyTo->m_Created[iThreadLocal].m_pStorageIndex = pIndex;

						NAtomic::fg_MemoryFence();
#ifdef DMibPSupportSetThreadLocalForOtherThread
						if (Interface.m_Flags & NThread::EThreadLocalInterfaceFlag_UseFastStorage)
							NSys::fg_Thread_SetLocalFast(_ThreadID, pIndex->m_LocalThreadLocal, pNewPtr);
						else
							NSys::fg_Thread_SetLocal(_ThreadID, pIndex->m_LocalThreadLocal, pNewPtr);
#endif
					}
				}
			}
		}

		void CThreadLocalContext::fp_RestorePerThread(CPerThread *_pPerThread)
		{
			NSys::fg_Thread_SetLocal(mp_iPerThread, _pPerThread);

#ifdef DPlatformFamily_macOS
			// The introspection terminate fallback runs after pthread has cleared
			// native TSD. Restore the retained values so destruction has the same
			// thread-local view as the earlier fallback levels.
			DMibLock(_pPerThread->m_Lock);
			auto fRestore = [&](auto &_Created)
				{
					for (auto &Allocation : _Created)
					{
						auto &Index = *Allocation.m_pStorageIndex;
						auto pPointer = _pPerThread->m_ThreadLocals.f_GetArray()[Index.m_iThreadLocal].m_pPtr;
						if (Index.m_pInterface->m_Flags & NThread::EThreadLocalInterfaceFlag_UseFastStorage)
							NSys::fg_Thread_SetLocalFast(Index.m_LocalThreadLocal, pPointer);
						else
							NSys::fg_Thread_SetLocal(Index.m_LocalThreadLocal, pPointer);
					}
				}
			;
			fRestore(_pPerThread->m_Created);
			fRestore(_pPerThread->m_CreatedAlwaysCreate);
#endif
		}

		void CThreadLocalContext::fp_FreePerThread(CPerThread *_pPerThread)
		{
			DMibLock(mp_LockContext);

			if (!_pPerThread)
			{
#if defined(DUseThreadDestroyNotifications) || !defined(DMibPSupportThreadLocalDestructors)
				_pPerThread = (CPerThread *)NSys::fg_Thread_GetLocal(mp_iPerThread);
#else
				return;
#endif
			}

			if (_pPerThread && ((umint)_pPerThread != TCLimitsInt<umint>::mc_Max))
			{
				if (NSys::fg_Thread_GetLocal(mp_iPerThread) == nullptr)
					fp_RestorePerThread(_pPerThread);

				DMibFastCheck(mp_PerThreadByThreadID.f_FindEqual(NSys::fg_Thread_GetCurrentUID()) == _pPerThread);

				while (true)
				{
					DMibLock(_pPerThread->m_Lock);
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

					DMibFastCheck(_pPerThread->m_ThreadLocals.f_IsPosValid(Index.m_iThreadLocal));
					auto &Pointer = _pPerThread->m_ThreadLocals.f_GetArray()[Index.m_iThreadLocal];

					void *pPtr = Pointer.m_pPtr;

					DMibFastCheck(pPtr);

					if (pPtr)
					{
						auto &Interface = *Index.m_pInterface;

						_pPerThread->m_DestroyingID = Index.m_iThreadLocal;
						if (pPtr)
							Interface.f_DeleteItem(pPtr);
//#ifndef DMibPSupportThreadLocalDestructors
						if (Interface.m_Flags & NThread::EThreadLocalInterfaceFlag_UseFastStorage)
							NSys::fg_Thread_SetLocalFast(Index.m_LocalThreadLocal, nullptr);
						else
							NSys::fg_Thread_SetLocal(Index.m_LocalThreadLocal, nullptr);
//#endif
						Pointer.m_pPtr = nullptr;
					}
				}

//#ifndef DMibPSupportThreadLocalDestructors
				if (NSys::fg_Thread_GetLocal(mp_iPerThread) == _pPerThread)
					NSys::fg_Thread_SetLocal(mp_iPerThread, (void *)TCLimitsInt<umint>::mc_Max);
//#endif
				mp_PerThreadByThreadID.f_Remove(_pPerThread);
				mp_PoolPerThread.f_Delete(_pPerThread);

#ifdef DPlatformFamily_Linux
				NSys::fg_Thread_SetLocalsDestroyed(true);
#endif
			}
			else
			{
				DMibFastCheck(mp_PerThreadByThreadID.f_FindEqual(NSys::fg_Thread_GetCurrentUID()) == nullptr);
#ifdef DPlatformFamily_macOS
				NSys::fg_Thread_SetLocal(mp_iPerThread, (void *)TCLimitsInt<umint>::mc_Max);
#endif
			}
		}

#if DMibEnableSafeCheck > 0
		namespace
		{
			template <umint tf_Size>
			void fg_CopyName(ch8 (&_Dest)[tf_Size], ch8 const *_pSource)
			{
				umint iChar = 0;
				if (_pSource)
				{
					for (; _pSource[iChar] && iChar + 1 < tf_Size; ++iChar)
						_Dest[iChar] = _pSource[iChar];
				}
				_Dest[iChar] = 0;
			}
		}

		void CThreadLocalContext::f_SetThreadName(umint _ThreadID, ch8 const *_pName)
		{
			DMibLock(mp_LockContext);

			auto pPerThread = mp_PerThreadByThreadID.f_FindEqual(_ThreadID);
			if (pPerThread)
				fg_CopyName(pPerThread->m_Name, _pName);
		}

		// Resolving needs the debug subsystem, which is destroyed before the thread local context; the caller runs this
		// before the system is marked deleted so that a record left behind can still be described. Resolving can also load
		// the debugger help library under the loader lock, which a thread exiting at the same time holds while its detach
		// waits for mp_LockContext, so the records are copied out and resolved with the lock released
		void CThreadLocalContext::f_DescribeOtherThreads()
		{
			struct CEntry
			{
				umint m_ThreadID;
				void const *m_pStartAddress;
				CStackTraceInfo *m_pInfo;
			};

			constexpr umint c_MaxEntries = 64;
			CEntry Entries[c_MaxEntries];
			umint nEntries = 0;
			umint ThisUID = NSys::fg_Thread_GetCurrentUID();

			{
				DMibLock(mp_LockContext);
				for (auto &PerThread : mp_PerThreadByThreadID)
				{
					if (PerThread.m_ThreadID == ThisUID || !PerThread.m_pStartAddress || nEntries == c_MaxEntries)
						continue;

					Entries[nEntries++] = {PerThread.m_ThreadID, PerThread.m_pStartAddress, nullptr};
				}
			}

			for (umint iEntry = 0; iEntry < nEntries; ++iEntry)
				Entries[iEntry].m_pInfo = NSys::fg_Debug_AquireStackTraceInfo((CMibCodeAddress)Entries[iEntry].m_pStartAddress);

			{
				DMibLock(mp_LockContext);
				for (umint iEntry = 0; iEntry < nEntries; ++iEntry)
				{
					auto pPerThread = Entries[iEntry].m_pInfo ? mp_PerThreadByThreadID.f_FindEqual(Entries[iEntry].m_ThreadID) : nullptr;
					if (!pPerThread)
						continue;

					fg_CopyName(pPerThread->m_StartSymbol, Entries[iEntry].m_pInfo->m_pFunctionName);
					fg_CopyName(pPerThread->m_StartModule, Entries[iEntry].m_pInfo->m_pModuleName);
				}
			}

			for (umint iEntry = 0; iEntry < nEntries; ++iEntry)
			{
				if (Entries[iEntry].m_pInfo)
					NSys::fg_Debug_ReleaseStackTraceInfo(Entries[iEntry].m_pInfo);
			}
		}
#endif

		void CThreadLocalContext::f_FreeThread()
		{
			fp_FreePerThread(nullptr);
		}

		void CThreadLocalContext::f_FreeThreadFromNotification()
		{
			// The final macOS notification fallback arrives after pthread has
			// cleared TSD, so identify the retained record by thread ID rather than
			// relying on the native context key.
			CPerThread *pPerThread;
			{
				DMibLock(mp_LockContext);
				pPerThread = mp_PerThreadByThreadID.f_FindEqual(NSys::fg_Thread_GetCurrentUID());
			}

			if (pPerThread)
				fp_FreePerThread(pPerThread);
			else
				fp_FreePerThread((CPerThread *)TCLimitsInt<umint>::mc_Max);
		}

		void CThreadLocalContext::f_RestoreThread()
		{
			DMibLock(mp_LockContext);

			auto pPerThread = mp_PerThreadByThreadID.f_FindEqual(NSys::fg_Thread_GetCurrentUID());
			if (pPerThread && NSys::fg_Thread_GetLocal(mp_iPerThread) == nullptr)
				fp_RestorePerThread(pPerThread);
		}

		void *CThreadLocalContext::f_Get(CStorageIndex *_pStorageIndex)
		{
			CPerThread *pThreadLocal = fp_GetPerThread(NSys::fg_Thread_GetCurrentUID());

			// Lock so m_ThreadLocals is safe
			DMibLock(pThreadLocal->m_Lock);

			DMibFastCheck(pThreadLocal->m_ThreadLocals.f_IsPosValid(_pStorageIndex->m_iThreadLocal));

			return pThreadLocal->m_ThreadLocals.f_GetArray()[_pStorageIndex->m_iThreadLocal].m_pPtr;
		}

		void CThreadLocalContext::f_Set(CStorageIndex *_pStorageIndex, void *_pValue)
		{
			CPerThread *pThreadLocal = fp_GetPerThread(NSys::fg_Thread_GetCurrentUID());

			auto iThreadLocal = _pStorageIndex->m_iThreadLocal;

			DMibFastCheck(pThreadLocal->m_DestroyingID != iThreadLocal); // Circular dependency

			{
				// Lock so m_ThreadLocals is safe
				DMibLock(pThreadLocal->m_Lock);

				DMibFastCheck(pThreadLocal->m_ThreadLocals.f_IsPosValid(iThreadLocal));
				auto &Pointer = pThreadLocal->m_ThreadLocals.f_GetArray()[iThreadLocal];

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

	void CSystem::f_ThreadLocal_PrepareFork()
	{
		NPrivate::g_ThreadLocalContext->f_PrepareFork();
	}

	void CSystem::f_ThreadLocal_ForkedChild()
	{
		NPrivate::g_ThreadLocalContext->f_ForkedChild();
	}

	void CSystem::f_ThreadLocal_ForkedParent()
	{
		NPrivate::g_ThreadLocalContext->f_ForkedParent();
	}

	void *CSystem::f_ThreadLocalAlloc(NThread::CThreadLocalInterface &_Interface, umint &_ThreadLocalLocal)
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

	void CSystem::f_ThreadLocalFreeThreadFromNotification()
	{
		NPrivate::g_ThreadLocalContext->f_FreeThreadFromNotification();
	}

	void CSystem::f_ThreadLocalRestoreThread()
	{
		NPrivate::g_ThreadLocalContext->f_RestoreThread();
	}

	void CSystem::f_ThreadLocalCreateThread(umint _ThreadID, umint _ParentThreadID)
	{
		NPrivate::g_ThreadLocalContext->f_CreateThread(_ThreadID, _ParentThreadID);
		fg_SystemThreadInit();
	}

#if DMibEnableSafeCheck > 0
	void CSystem::f_ThreadLocalSetThreadName(umint _ThreadID, ch8 const *_pName)
	{
		NPrivate::g_ThreadLocalContext->f_SetThreadName(_ThreadID, _pName);
	}

	void CSystem::f_ThreadLocalDescribeOtherThreads()
	{
		NPrivate::g_ThreadLocalContext->f_DescribeOtherThreads();
	}
#endif

	void CSystem::f_ThreadLocalReinitForThread(void *_pStorageIndex)
	{
		NPrivate::g_ThreadLocalContext->f_ReinitForThread((NPrivate::CThreadLocalContext::CStorageIndex *)_pStorageIndex);
	}

	void CSystem::f_ThreadLocalDestroyForThread(void *_pStorageIndex)
	{
		NPrivate::g_ThreadLocalContext->f_DestroyForThread((NPrivate::CThreadLocalContext::CStorageIndex *)_pStorageIndex);
	}

	void *CSystem::f_ThreadLocalGet(void *_pStorageIndex)
	{
		return NPrivate::g_ThreadLocalContext->f_Get((NPrivate::CThreadLocalContext::CStorageIndex *)_pStorageIndex);
	}

	void CSystem::f_ThreadLocalSet(void *_pStorageIndex, void *_pValue)
	{
		return NPrivate::g_ThreadLocalContext->f_Set((NPrivate::CThreadLocalContext::CStorageIndex *)_pStorageIndex, _pValue);
	}

	void CSystem::f_OnThreadCreated(umint _ThreadID, umint _ParentID)
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
#ifdef DUseThreadDestroyNotifications
#	ifdef DPlatformFamily_macOS
		f_ThreadLocalFreeThreadFromNotification();
#	else
		f_ThreadLocalFreeThread();
#	endif
#elif defined(DMibPSupportThreadLocalDestructors)
		f_ThreadLocalFreeThreadFromNotification();
#else
		f_ThreadLocalFreeThread();
#endif
	}

	void CSystem::fp_ThreadLocalCreate()
	{
		NPrivate::g_ThreadLocalContext.f_Construct();
	}

	void CSystem::fp_ThreadLocalDestroy()
	{
		auto fDestroy = []
			{
				if (!g_bMemoryManagerNeededAfterDestroy)
					NPrivate::g_ThreadLocalContext.f_Destruct();
			}
		;
#if defined(DPlatformFamily_Windows) || defined(DPlatformFamily_Linux) || defined(DPlatformFamily_macOS)
		NSys::fg_Thread_DestroyLocalContext(fDestroy);
#else
		fDestroy();
#endif
	}

	void CSystem::f_ThreadEnum(NFunction::TCFunction<void (umint _ThreadID)> const &_EnumFunc)
	{
		return NPrivate::g_ThreadLocalContext->f_EnumThreads(_EnumFunc);
	}

	bool CSystem::f_ThreadDestroyed() const
	{
		return NPrivate::g_ThreadLocalContext->f_ThreadDestroyed();
	}

	bool CSystem::f_ThreadCreated() const
	{
		return NPrivate::g_ThreadLocalContext->f_ThreadCreated();
	}
} // Namespace NMib
