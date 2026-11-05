// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>

namespace NMib
{
	namespace NPrivate
	{
		struct CThreadLocalContext
		{
			struct CStorageIndex
			{
				CStorageIndex();
				~CStorageIndex();

				NThread::CThreadLocalInterface *m_pInterface;

				umint m_iThreadLocal;
				umint m_LocalThreadLocal;

				DMibListLinkDA_Link(CStorageIndex, m_Link);
			};

			struct CAllocation
			{
				CStorageIndex *m_pStorageIndex;
			};

			using CAllocationPool = NMemory::TCPool<NContainer::TCMapNode<umint, CAllocation>, 128, NThread::CMutual, NMemory::CPoolType_Freeable, NMemory::CAllocator_VirtualNoTracking>;

			struct CPerThread
			{
				struct CCompare
				{
					inline_small umint const &operator () (CPerThread const &_Node) const;
				};

				struct CPointer
				{
					CPointer();
					~CPointer();
					CPointer(CPointer &&_Copy);

					void *m_pPtr;

				private:
					CPointer(CPointer const &_Copy);
				};

				CPerThread(umint _ThreadID, CThreadLocalContext *_pContext);
				~CPerThread();

				NIntrusive::TCAVLLink<> m_Link;

				umint m_ThreadID;
				umint m_DestroyingID;
				bool m_bOnThreadCreated;
#if DMibEnableSafeCheck > 0
				void const *m_pStartAddress = nullptr; // Where the thread started; identifies a foreign thread once it is gone
				ch8 m_Name[64] = {};                   // The last name Malterlib gave the thread; empty for one it only saw attach
				ch8 m_StartSymbol[128] = {};           // Function at the start address, resolved by f_DescribeOtherThreads
				ch8 m_StartModule[128] = {};           // Module of the start address, resolved by f_DescribeOtherThreads
#endif

				NContainer::TCMap<umint, CAllocation, CSort_Default, NMemory::TCPoolReferenceAllocator<CAllocationPool>> m_Created;
				NContainer::TCMap<umint, CAllocation, CSort_Default, NMemory::TCPoolReferenceAllocator<CAllocationPool>> m_CreatedAlwaysCreate;

				NContainer::TCVector<CPointer, NMemory::CAllocator_VirtualNoTracking> m_ThreadLocals;
				NThread::CMutual m_Lock;
			};

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
			void f_EnumThreads(NFunction::TCFunction<void (umint _ThreadID)> const &_fEnumFunc);
			bool f_ThreadDestroyed() const;
			bool f_ThreadCreated();
			void f_PrepareFork();
			void f_ForkedChild();
			void f_ForkedParent();

		private:
			friend struct NMib::CSystem;
			friend struct NStorage::TCAggregateSimple<CThreadLocalContext>;
			CThreadLocalContext();
			~CThreadLocalContext();

			void fp_GrowTable();
			CPerThread *fp_GetPerThreadNew(umint _ThreadID);
			CPerThread *fp_GetPerThread(umint _ThreadID);

			void fp_RestorePerThread(CPerThread *_pPerThread);
			void fp_FreePerThread(CPerThread *_pPerThread);

#ifdef DMibPSupportThreadLocalDestructors
			static void fsp_PerThreadDestructor(void *_pPerThread);
#endif

			NIntrusive::TCAVLTree<&CPerThread::m_Link, CPerThread::CCompare> mp_PerThreadByThreadID;
			NMemory::TCPool<CPerThread, 128, NThread::CNoLock, NMemory::CPoolType_Freeable, NMemory::CAllocator_VirtualNoTracking> mp_PoolPerThread;
			CAllocationPool mp_PoolAllocation;

#if defined(DMibPSupportThreadLocalDestructors) && defined(DMibStaticThreadLocals)
			umint mp_iPerThreadDestructor = TCLimitsInt<umint>::mc_Max;
#endif
			umint mp_iPerThread = TCLimitsInt<umint>::mc_Max;

			umint mp_iThreadLocalCurrentLen = 0;

			NMemory::TCPool<CStorageIndex, 128, NThread::CNoLock, NMemory::CPoolType_Freeable, NMemory::CAllocator_VirtualNoTracking> mp_PoolStorageIndices;

			DMibListLinkD_List(CStorageIndex, m_Link) mp_ThreadLocal_Free; // List of indices that are free
			DMibListLinkD_List(CStorageIndex, m_Link) mp_ThreadLocal_DestroyOrder;

			NThread::CMutual mp_LockContext;
		};
	}
};

#include "Malterlib_Thread_ThreadLocal_Internal.hpp"
