// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>

namespace NMib
{
	namespace NPrivate
	{
		class CThreadLocalContext
		{
			friend class NMib::CSystem;
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

				umint m_iThreadLocal;
				umint m_LocalThreadLocal;

				DMibListLinkDA_Link(CStorageIndex, m_Link);
			};

			struct CAllocation
			{
				CStorageIndex *m_pStorageIndex;
			};

			using CAllocationPool
				= NMemory::TCPool<NContainer::TCMapNode<umint, CAllocation>, 128, NThread::CMutual, NMemory::CPoolType_Freeable, NMemory::CAllocator_VirtualNoTracking>
			;

			class CPerThread
			{
			public:
				class CCompare
				{
				public:
					inline_small umint const &operator () (CPerThread const &_Node) const
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

				umint m_ThreadID;
				umint m_DestroyingID;
				bool m_bOnThreadCreated;
#if DMibEnableSafeCheck > 0
				void const *m_pStartAddress = nullptr; // Where the thread started; identifies a foreign thread once it is gone
				ch8 m_Name[64] = {}; // The last name Malterlib gave the thread; empty for one it only saw attach
				ch8 m_StartSymbol[128] = {}; // Function at the start address, resolved by f_DescribeOtherThreads
				ch8 m_StartModule[128] = {}; // Module of the start address, resolved by f_DescribeOtherThreads
#endif

				NContainer::TCMap<umint, CAllocation, CSort_Default, NMemory::TCPoolReferenceAllocator<CAllocationPool>> m_Created;
				NContainer::TCMap<umint, CAllocation, CSort_Default, NMemory::TCPoolReferenceAllocator<CAllocationPool>> m_CreatedAlwaysCreate;

				CPerThread
					(
						umint _ThreadID
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
			umint m_iPerThreadDestructor = TCLimitsInt<umint>::mc_Max;
#endif
			umint m_iPerThread = TCLimitsInt<umint>::mc_Max;

			umint m_iThreadLocalCurrentLen = 0;

			NMemory::TCPool<CStorageIndex, 128, NThread::CNoLock, NMemory::CPoolType_Freeable, NMemory::CAllocator_VirtualNoTracking> m_PoolStorageIndices;

			DMibListLinkD_List(CStorageIndex, m_Link) m_ThreadLocal_Free; // List of indices that are free
			DMibListLinkD_List(CStorageIndex, m_Link) m_ThreadLocal_DestroyOrder;

			void fp_GrowTable();
			CPerThread *fp_GetPerThreadNew(umint _ThreadID);
			CPerThread *fp_GetPerThread(umint _ThreadID);

			void fp_RestorePerThread(CPerThread *_pPerThread);
			void fp_FreePerThread(CPerThread *_pPerThread);

			#ifdef DMibPSupportThreadLocalDestructors
				static void fs_PerThreadDestructor(void* _pPerThread);
			#endif

		public:
			CStorageIndex *f_Alloc(NThread::CThreadLocalInterface &_Interface, umint &_ThreadLocalLocal);
			void f_Free(NThread::CThreadLocalInterface &_Interface, CStorageIndex *_pStorageIndex);
			void f_ReinitForThread(CStorageIndex *_pStorageIndex);
			void f_DestroyForThread(CStorageIndex *_pStorageIndex);
			void f_CreateThread(umint _ThreadID, umint _ParentThread);
#if DMibEnableSafeCheck > 0
			void f_SetThreadName(umint _ThreadID, ch8 const *_pName);
			void f_DescribeOtherThreads();
#endif
			void f_FreeThread();
			void f_FreeThreadFromNotification();
			void f_RestoreThread();
			void f_Set(CStorageIndex *_pStorageIndex, void *_pValue);
			void *f_Get(CStorageIndex *_pStorageIndex);
			void f_EnumThreads(NFunction::TCFunction<void (umint _ThreadID)> const &_EnumFunc);
			bool f_ThreadDestroyed() const;
			bool f_ThreadCreated();
			void f_PrepareFork();
			void f_ForkedChild();
			void f_ForkedParent();
		};
	}
};
