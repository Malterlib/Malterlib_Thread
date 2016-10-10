// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

namespace NMib
{
	namespace NPrivate
	{
		class CThreadLocalContext
		{
			friend class CSystem;
			friend class NAggregate::TCAggregateSimple<CThreadLocalContext>;
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

			typedef NMem::TCPool<NContainer::TCMapTreeMember<mint, CAllocation>, 128, NThread::CMutual, NMem::CPoolType_Freeable, NMem::CAllocator_VirtualNoTracking> CAllocationPool;
			
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

				DMibIntrusiveLink(CPerThread, NIntrusive::TCAVLLink<>, m_Link);
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
				bint m_bOnThreadCreated;

				NContainer::TCMap<mint, CAllocation, CSort_Default, NMem::TCPoolReferenceAllocator<CAllocationPool>> m_Created;
				NContainer::TCMap<mint, CAllocation, CSort_Default, NMem::TCPoolReferenceAllocator<CAllocationPool>> m_CreatedAlwaysCreate;

				CPerThread
					(
						mint _ThreadID
						, CThreadLocalContext * _pContext
					)
					: m_Created(NMem::CAllocatorConstructTag(), _pContext->m_PoolAllocation)
					, m_CreatedAlwaysCreate(NMem::CAllocatorConstructTag(), _pContext->m_PoolAllocation)
					, m_ThreadID(_ThreadID)
					, m_DestroyingID(-1)
					, m_bOnThreadCreated(false)
				{
				}

				~CPerThread()
				{
				}

				NContainer::TCVector<CPointer, NMem::CAllocator_VirtualNoTracking> m_aThreadLocal;
				NThread::CMutual m_Lock;
			};
		private:

			NIntrusive::TCAVLTree<CPerThread::CLinkTraits_m_Link, CPerThread::CCompare> m_lPerThread;
			NMem::TCPool<CPerThread, 128, NThread::CNoLock, NMem::CPoolType_Freeable, NMem::CAllocator_VirtualNoTracking> m_PoolPerThread;
			CAllocationPool m_PoolAllocation;
			
			mint m_iPerThread;

			NThread::CMutual m_LockResizePerThread;
			mint m_iThreadLocalCurrentLen;

			NMem::TCPool<CStorageIndex, 128, NThread::CNoLock, NMem::CPoolType_Freeable, NMem::CAllocator_VirtualNoTracking> m_PoolStorageIndices;

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
			void f_CreateThread(mint _ThreadID, mint _ParentThread);
			void f_FreeThread();
			void f_Set(CStorageIndex *_pStorageIndex, void *_pValue);
			void *f_Get(CStorageIndex *_pStorageIndex);
			void f_EnumThreads(NFunction::TCFunction<void (mint _ThreadID)> const &_EnumFunc);
			bool f_ThreadDestroyed() const;
		};
	}

};

