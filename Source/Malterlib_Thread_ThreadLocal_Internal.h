// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>

namespace NMib
{
	namespace NPrivate
	{
		class CThreadLocalContext
		{
			friend class CSystem;
			friend class NStorage::TCAggregateSimple<CThreadLocalContext>;
			CThreadLocalContext();

			~CThreadLocalContext();

			NThread::CMutual m_LockContext;


		public:
			
			class CStorageIndex
			{
			public:

				CStorageIndex()
				{
				}

				~CStorageIndex()
				{
					m_Link.f_Destruct();
				}

				NThread::CThreadLocalInterface *m_pInterface;

				mint m_iThreadLocal;
				mint m_LocalThreadLocal;

				DMibListLinkDA_Link(CStorageIndex, m_Link);
			};
				
			struct CAllocation
			{
				CStorageIndex *m_pStorageIndex;
			};

			using CAllocationPool
				= NMemory::TCPool<NContainer::TCMapNode<mint, CAllocation>, 128, NThread::CMutual, NMemory::CPoolType_Freeable, NMemory::CAllocator_VirtualNoTracking>
			;
			
			class CPerThread
			{
			public:
				class CCompare
				{
				public:
					inline_small mint const &operator () (CPerThread const &_Node) const
					{
						return _Node.m_ThreadID;
					}
				};

				NIntrusive::TCAVLLink<> m_Link;
				class CPointer
				{
					CPointer(CPointer const &_Copy);
				public:
					void *m_pPtr;
					CPointer()
						: m_pPtr(nullptr)
					{
						
					}
					~CPointer();
					CPointer(CPointer &&_Copy)
					{
						// Transfer the pointer
						m_pPtr = _Copy.m_pPtr;
						// Set the ptr to null so the original does not get deleted
						_Copy.m_pPtr = nullptr;
					}
				};

				mint m_ThreadID;
				mint m_DestroyingID;
				bool m_bOnThreadCreated;

				NContainer::TCMap<mint, CAllocation, CSort_Default, NMemory::TCPoolReferenceAllocator<CAllocationPool>> m_Created;
				NContainer::TCMap<mint, CAllocation, CSort_Default, NMemory::TCPoolReferenceAllocator<CAllocationPool>> m_CreatedAlwaysCreate;

				CPerThread
					(
						mint _ThreadID
						, CThreadLocalContext * _pContext
					)
					: m_Created(CAllocatorConstructTag(), _pContext->m_PoolAllocation)
					, m_CreatedAlwaysCreate(CAllocatorConstructTag(), _pContext->m_PoolAllocation)
					, m_ThreadID(_ThreadID)
					, m_DestroyingID(-1)
					, m_bOnThreadCreated(false)
				{
				}

				~CPerThread()
				{
				}

				NContainer::TCVector<CPointer, NMemory::CAllocator_VirtualNoTracking> m_ThreadLocals;
				NThread::CMutual m_Lock;
			};
		private:

			NIntrusive::TCAVLTree<&CPerThread::m_Link, CPerThread::CCompare> m_PerThreadByThreadID;
			NMemory::TCPool<CPerThread, 128, NThread::CNoLock, NMemory::CPoolType_Freeable, NMemory::CAllocator_VirtualNoTracking> m_PoolPerThread;
			CAllocationPool m_PoolAllocation;
			
#if defined(DMibPSupportThreadLocalDestructors) && defined(DMibStaticThreadLocals)
			mint m_iPerThreadDestructor = TCLimitsInt<mint>::mc_Max;
#endif
			mint m_iPerThread = TCLimitsInt<mint>::mc_Max;

			mint m_iThreadLocalCurrentLen = 0;

			NMemory::TCPool<CStorageIndex, 128, NThread::CNoLock, NMemory::CPoolType_Freeable, NMemory::CAllocator_VirtualNoTracking> m_PoolStorageIndices;

			DMibListLinkD_List(CStorageIndex, m_Link) m_ThreadLocal_Free; // List of indices that are free
			DMibListLinkD_List(CStorageIndex, m_Link) m_ThreadLocal_DestroyOrder;

			void fp_GrowTable();
			CPerThread *fp_GetPerThreadNew(mint _ThreadID);
			CPerThread *fp_GetPerThread(mint _ThreadID);

			void fp_FreePerThread(CPerThread* _pPerThread);

			#ifdef DMibPSupportThreadLocalDestructors
				static void fs_PerThreadDestructor(void* _pPerThread);
			#endif
			
		public:
			CStorageIndex *f_Alloc(NThread::CThreadLocalInterface &_Interface, mint &_ThreadLocalLocal);
			void f_Free(NThread::CThreadLocalInterface &_Interface, CStorageIndex *_pStorageIndex);
			void f_ReinitForThread(CStorageIndex *_pStorageIndex);
			void f_DestroyForThread(CStorageIndex *_pStorageIndex);
			void f_CreateThread(mint _ThreadID, mint _ParentThread);
			void f_FreeThread();
			void f_Set(CStorageIndex *_pStorageIndex, void *_pValue);
			void *f_Get(CStorageIndex *_pStorageIndex);
			void f_EnumThreads(NFunction::TCFunction<void (mint _ThreadID)> const &_EnumFunc);
			bool f_ThreadDestroyed() const;
			bool f_ThreadCreated();
			void f_PrepareFork();
			void f_ForkedChild();
			void f_ForkedParent();
		};
	}
};

