// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

namespace NMib::NThread
{
	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	TCThreadLocal<t_CData, t_CAllocator, t_Flags>::TCThreadLocal()
	{
		if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
			m_Flags |= EThreadLocalInterfaceFlag_UseFastStorage;
		if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
			m_Flags |= EThreadLocalInterfaceFlag_AlwaysCreated;
		if constexpr ((mc_Flags & EThreadLocalFlag_Inherit) != 0)
			m_Flags |= EThreadLocalInterfaceFlag_Inherit;

		m_pStorage = (umint)fg_GetSys()->f_ThreadLocalAlloc(*this, m_ThreadLocalLocal);
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	void TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_Destroy()
	{
		if (m_pStorage)
		{
			fg_GetSys()->f_ThreadLocalFree(*this, (void *)m_pStorage);
			m_pStorage = 0;
		}
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	TCThreadLocal<t_CData, t_CAllocator, t_Flags>::~TCThreadLocal()
	{
		f_Destroy();
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	bool TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_IsValid()
	{
		if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return NSys::fg_Thread_GetLocalAlwaysSetFast(m_ThreadLocalLocal) != nullptr;
			else
				return NSys::fg_Thread_GetLocalFast(m_ThreadLocalLocal) != nullptr;
		}
		else
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return NSys::fg_Thread_GetLocalAlwaysSet(m_ThreadLocalLocal) != nullptr;
			else
				return NSys::fg_Thread_GetLocal(m_ThreadLocalLocal) != nullptr;
		}
		//t_CData *pData = ((t_CData *)fg_GetSys()->f_ThreadLocalGet((void *)m_pStorage));
		//return pData != 0;
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	inline_never t_CData *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::fp_GetNew()
	{
		t_CData *pData = ((t_CData *)fg_GetSys()->f_ThreadLocalGet((void *)m_pStorage));
		if (pData)
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
				NSys::fg_Thread_SetLocalFast(m_ThreadLocalLocal, pData);
			else
				NSys::fg_Thread_SetLocal(m_ThreadLocalLocal, pData);
			return pData;
		}
		pData = (t_CData *)f_CreateData(nullptr, false);
		fg_GetSys()->f_ThreadLocalSet((void *)m_pStorage, pData);
		return pData;
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	inline_small t_CData *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_Get()
	{
		t_CData *pData;
		if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
			{
				pData = (t_CData *)NSys::fg_Thread_GetLocalAlwaysSetFast(m_ThreadLocalLocal);
				DMibFastCheck(pData);
			}
			else
				pData = (t_CData *)NSys::fg_Thread_GetLocalFast(m_ThreadLocalLocal);
		}
		else
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
			{
				pData = (t_CData *)NSys::fg_Thread_GetLocalAlwaysSet(m_ThreadLocalLocal);
				DMibFastCheck(pData);
			}
			else
				pData = (t_CData *)NSys::fg_Thread_GetLocal(m_ThreadLocalLocal);
		}
		if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
		{
			return pData;
		}
		else
		{
			if (pData) [[likely]]
			{
				//DMibFastCheck(((t_CData *)fg_GetSys()->f_ThreadLocalGet((void *)m_pStorage)) == pData);
				return pData;
			}
			return fp_GetNew();
		}
	}
	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	inline_small t_CData *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_TryGet()
	{
		if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSetFast(m_ThreadLocalLocal);
			else
				return (t_CData *)NSys::fg_Thread_GetLocalFast(m_ThreadLocalLocal);
		}
		else
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSet(m_ThreadLocalLocal);
			else
				return (t_CData *)NSys::fg_Thread_GetLocal(m_ThreadLocalLocal);
		}
	}
	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	inline_small t_CData *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_TryGetForThread(umint _ThreadID)
	{
		if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSetFast(_ThreadID, m_ThreadLocalLocal);
			else
				return (t_CData *)NSys::fg_Thread_GetLocalFast(_ThreadID, m_ThreadLocalLocal);
		}
		else
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSet(_ThreadID, m_ThreadLocalLocal);
			else
				return (t_CData *)NSys::fg_Thread_GetLocal(_ThreadID, m_ThreadLocalLocal);
		}
	}


	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	inline_small void TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_ReinitForThread()
	{
		return fg_GetSys()->f_ThreadLocalReinitForThread((void *)m_pStorage);
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	inline_small void TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_DestroyForThread()
	{
		return fg_GetSys()->f_ThreadLocalDestroyForThread((void *)m_pStorage);
	}


	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	inline_small TCThreadLocal<t_CData, t_CAllocator, t_Flags>::operator t_CData *()
	{
		return f_Get();
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	inline_small t_CData * TCThreadLocal<t_CData, t_CAllocator, t_Flags>::operator ->()
	{
		return f_Get();
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	inline_small t_CData & TCThreadLocal<t_CData, t_CAllocator, t_Flags>::operator &()
	{
		return *f_Get();
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	void TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_DeleteItem(void *_pItem)
	{
		t_CData *pData = (t_CData *)_pItem;
		fg_DeleteObjectDefiniteType(t_CAllocator(), pData, fg_Max(umint(DMibPMemoryCacheLineSize), alignof(t_CData)));
	}

	namespace NPrivate
	{
		class TCCreateHelperDo
		{
		public:
			template <typename tf_CType>
			static void *fs_CreateHelper(void *_pDest, void *_pSource)
			{
				tf_CType const *pSource = (tf_CType const *)_pSource;
				tf_CType *pData = new(_pDest) tf_CType(*pSource);
				return pData;
			}
			template <typename tf_CType>
			static void *fs_CreateHelperMove(void *_pDest, void *_pSource)
			{
				tf_CType *pSource = (tf_CType *)_pSource;
				tf_CType *pData = new(_pDest) tf_CType(fg_Move(*pSource));
				return pData;
			}
		};

		class TCCreateHelperDoNot
		{
		public:
			template <typename tf_CType>
			static void *fs_CreateHelper(void *_pDest, void *_pSource)
			{
				return nullptr;
			}
			template <typename tf_CType>
			static void *fs_CreateHelperMove(void *_pDest, void *_pSource)
			{
				return nullptr;
			}
		};
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	auto TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_AllocData() -> CSafeAlloc
	{
		constexpr bool c_bInherit = (t_Flags & int(EThreadLocalFlag_Inherit)) != 0;

		if constexpr (c_bInherit)
			return CSafeAlloc(this, {t_CAllocator::f_AllocAligned(sizeof(t_CData), fg_Max(umint(DMibPMemoryCacheLineSize), alignof(t_CData))), sizeof(t_CData)});

		return CSafeAlloc(nullptr, {nullptr, 0});
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	void TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_FreeData(CSafeAllocMemory const &_Alloc)
	{
		return t_CAllocator::f_Free(_Alloc.m_pMemory, sizeof(t_CData));
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	void *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_CreateDataCopy(void *_pSource, void *_pMemory)
	{
		constexpr bool c_bInherit = (t_Flags & int(EThreadLocalFlag_Inherit)) != 0;

		if constexpr (c_bInherit)
		{
			t_CData const *pSource = (t_CData const *)_pSource;
			t_CData *pData = new (_pMemory) t_CData(*pSource);
			return pData;
		}

		return nullptr;
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	void *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_CreateDataMove(void *_pSource, void *_pMemory)
	{
		constexpr bool c_bInherit = (t_Flags & int(EThreadLocalFlag_Inherit)) != 0;

		if constexpr (c_bInherit)
		{
			t_CData *pSource = (t_CData *)_pSource;
			t_CData *pData = new (_pMemory) t_CData(fg_Move(*pSource));
			return pData;
		}

		return nullptr;
	}

	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	void *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_CreateData(void *_pMemory, bool _bInitial)
	{
		if ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0 || !_bInitial)
		{
			if (!_pMemory)
				_pMemory = t_CAllocator::f_AllocAligned(sizeof(t_CData), fg_Max(umint(DMibPMemoryCacheLineSize), alignof(t_CData)));
			return new(_pMemory) t_CData();
		}
		return nullptr;
	}

#if DMibEnableSafeCheck > 0
	template <typename t_CData, typename t_CAllocator, EThreadLocalFlag t_Flags>
	ch8 const* TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_GetName()
	{
		return fg_GetTypeName<t_CData>();
	}
#endif

	///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template <typename t_CData, EThreadLocalFlag t_Flags>
	TCThreadLocalDynamic<t_CData, t_Flags>::TCThreadLocalDynamic
		(
			NFunction::TCFunctionNoAlloc<CSafeAllocMemory ()> const &_fAlloc
			, NFunction::TCFunctionNoAlloc<void (CSafeAllocMemory const &_Alloc)> const &_fFree
			, NFunction::TCFunctionNoAlloc<t_CData *(t_CData *_pParent, void *_pMemory, bool _bMove)> const &_fConstruct
			, NFunction::TCFunctionNoAlloc<void (t_CData *_pData)> const &_fDestruct
		)
		: m_fAlloc(_fAlloc)
		, m_fFree(_fFree)
		, m_fConstruct(_fConstruct)
		, m_fDestruct(_fDestruct)
	{
		if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
			m_Flags |= EThreadLocalInterfaceFlag_UseFastStorage;
		if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
			m_Flags |= EThreadLocalInterfaceFlag_AlwaysCreated;
		if constexpr ((mc_Flags & EThreadLocalFlag_Inherit) != 0)
			m_Flags |= EThreadLocalInterfaceFlag_Inherit;

		m_pStorage = (umint)fg_GetSys()->f_ThreadLocalAlloc(*this, m_ThreadLocalLocal);
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	void TCThreadLocalDynamic<t_CData, t_Flags>::f_Destroy()
	{
		if (m_pStorage)
		{
			fg_GetSys()->f_ThreadLocalFree(*this, (void *)m_pStorage);
			m_pStorage = 0;
		}
	}


	template <typename t_CData, EThreadLocalFlag t_Flags>
	TCThreadLocalDynamic<t_CData, t_Flags>::~TCThreadLocalDynamic()
	{
		f_Destroy();
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	bool TCThreadLocalDynamic<t_CData, t_Flags>::f_IsValid()
	{
		if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return NSys::fg_Thread_GetLocalAlwaysSetFast(m_ThreadLocalLocal) != nullptr;
			else
				return NSys::fg_Thread_GetLocalFast(m_ThreadLocalLocal) != nullptr;
		}
		else
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return NSys::fg_Thread_GetLocalAlwaysSet(m_ThreadLocalLocal) != nullptr;
			else
				return NSys::fg_Thread_GetLocal(m_ThreadLocalLocal) != nullptr;
		}
		//t_CData *pData = ((t_CData *)fg_GetSys()->f_ThreadLocalGet((void *)m_pStorage));
		//return pData != 0;
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	inline_never t_CData *TCThreadLocalDynamic<t_CData, t_Flags>::fp_GetNew()
	{
		t_CData *pData = ((t_CData *)fg_GetSys()->f_ThreadLocalGet((void *)m_pStorage));
		if (pData)
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
				NSys::fg_Thread_SetLocalFast(m_ThreadLocalLocal, pData);
			else
				NSys::fg_Thread_SetLocal(m_ThreadLocalLocal, pData);
			return pData;
		}
		pData = (t_CData *)f_CreateData(nullptr, false);
		fg_GetSys()->f_ThreadLocalSet((void *)m_pStorage, pData);
		return pData;
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	inline_small void TCThreadLocalDynamic<t_CData, t_Flags>::f_ReinitForThread()
	{
		return fg_GetSys()->f_ThreadLocalReinitForThread((void *)m_pStorage);
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	inline_small void TCThreadLocalDynamic<t_CData, t_Flags>::f_DestroyForThread()
	{
		return fg_GetSys()->f_ThreadLocalDestroyForThread((void *)m_pStorage);
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	inline_small t_CData *TCThreadLocalDynamic<t_CData, t_Flags>::f_Get()
	{
		t_CData *pData;
		if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
			{
				pData = (t_CData *)NSys::fg_Thread_GetLocalAlwaysSetFast(m_ThreadLocalLocal);
				DMibFastCheck(pData);
			}
			else
				pData = (t_CData *)NSys::fg_Thread_GetLocalFast(m_ThreadLocalLocal);
		}
		else
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
			{
				pData = (t_CData *)NSys::fg_Thread_GetLocalAlwaysSet(m_ThreadLocalLocal);
				DMibFastCheck(pData);
			}
			else
				pData = (t_CData *)NSys::fg_Thread_GetLocal(m_ThreadLocalLocal);
		}
		if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
		{
			return pData;
		}
		else
		{
			if (pData) [[likely]]
			{
				//DMibFastCheck(((t_CData *)fg_GetSys()->f_ThreadLocalGet((void *)m_pStorage)) == pData);
				return pData;
			}
			return fp_GetNew();
		}
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	inline_small t_CData *TCThreadLocalDynamic<t_CData, t_Flags>::f_TryGet()
	{
		if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSetFast(m_ThreadLocalLocal);
			else
				return (t_CData *)NSys::fg_Thread_GetLocalFast(m_ThreadLocalLocal);
		}
		else
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSet(m_ThreadLocalLocal);
			else
				return (t_CData *)NSys::fg_Thread_GetLocal(m_ThreadLocalLocal);
		}
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	inline_small t_CData *TCThreadLocalDynamic<t_CData, t_Flags>::f_TryGetForThread(umint _ThreadID)
	{
		if constexpr ((mc_Flags & EThreadLocalFlag_FastThreadLocal) != 0)
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSetFast(_ThreadID, m_ThreadLocalLocal);
			else
				return (t_CData *)NSys::fg_Thread_GetLocalFast(_ThreadID, m_ThreadLocalLocal);
		}
		else
		{
			if constexpr ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0)
				return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSet(_ThreadID, m_ThreadLocalLocal);
			else
				return (t_CData *)NSys::fg_Thread_GetLocal(_ThreadID, m_ThreadLocalLocal);
		}
	}


	template <typename t_CData, EThreadLocalFlag t_Flags>
	inline_small TCThreadLocalDynamic<t_CData, t_Flags>::operator t_CData *()
	{
		return f_Get();
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	inline_small t_CData * TCThreadLocalDynamic<t_CData, t_Flags>::operator ->()
	{
		return f_Get();
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	inline_small t_CData & TCThreadLocalDynamic<t_CData, t_Flags>::operator &()
	{
		return *f_Get();
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	void TCThreadLocalDynamic<t_CData, t_Flags>::f_DeleteItem(void *_pItem)
	{
		t_CData *pData = (t_CData *)_pItem;
		m_fDestruct(pData);
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	auto TCThreadLocalDynamic<t_CData, t_Flags>::f_AllocData() -> CSafeAlloc
	{
		if constexpr ((t_Flags & int(EThreadLocalFlag_Inherit)) != 0)
			return CSafeAlloc(this, m_fAlloc());
		return {nullptr, {nullptr, 0}};
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	void TCThreadLocalDynamic<t_CData, t_Flags>::f_FreeData(CSafeAllocMemory const &_Alloc)
	{
		return m_fFree(_Alloc);
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	void *TCThreadLocalDynamic<t_CData, t_Flags>::f_CreateDataCopy(void *_pSource, void *_pMemory)
	{
		if constexpr ((t_Flags & int(EThreadLocalFlag_Inherit)) != 0)
			return m_fConstruct((t_CData *)_pSource, _pMemory, false);
		return nullptr;
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	void *TCThreadLocalDynamic<t_CData, t_Flags>::f_CreateDataMove(void *_pSource, void *_pMemory)
	{
		if constexpr ((t_Flags & int(EThreadLocalFlag_Inherit)) != 0)
			return m_fConstruct((t_CData *)_pSource, _pMemory, true);
		return nullptr;
	}

	template <typename t_CData, EThreadLocalFlag t_Flags>
	void *TCThreadLocalDynamic<t_CData, t_Flags>::f_CreateData(void *_pMemory, bool _bInitial)
	{
		if ((mc_Flags & EThreadLocalFlag_AlwaysCreated) != 0 || !_bInitial)
		{
			CSafeAlloc Memory(nullptr, {nullptr, 0});
			if (!_pMemory)
			{
				Memory.m_Memory = m_fAlloc();
				Memory.m_pInterface = this;
				_pMemory = Memory.m_Memory.m_pMemory;
			}

			auto *pReturn = m_fConstruct(nullptr, _pMemory, false);
			Memory.f_Claim();

			return pReturn;
		}
		return nullptr;
	}

#if DMibEnableSafeCheck > 0
	template <typename t_CData, EThreadLocalFlag t_Flags>
	ch8 const* TCThreadLocalDynamic<t_CData, t_Flags>::f_GetName()
	{
		return fg_GetTypeName<t_CData>();
	}
#endif

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	template <typename t_CAllocator, typename t_CStr>
	NStr::CStr TCThreadObject<t_CAllocator, t_CStr>::f_GetThreadName()
	{
		return f_GetThreadNameRaw();
	}

	template <typename t_CAllocator, typename t_CStr>
	ch8 const *TCThreadObject<t_CAllocator, t_CStr>::f_GetThreadNameRaw()
	{
		return m_pCallerObject->f_GetName();
	}


	namespace NPrivate
	{

		template <typename t_CFunctionType, typename t_CStr, typename t_CCallerObject, typename t_CThreadObject>
		class TCCallerObjectImp : public t_CCallerObject
		{
		public:
			t_CFunctionType m_FunctionObject;
			t_CStr m_Name;
			TCCallerObjectImp(const t_CFunctionType &_FunctionObject, const t_CStr &_Name)
				: m_FunctionObject(_FunctionObject)
				, m_Name(_Name)
			{
			}
			TCCallerObjectImp(t_CFunctionType &&_FunctionObject, const t_CStr &_Name)
				: m_FunctionObject(fg_Move(_FunctionObject))
				, m_Name(_Name)
			{
			}
			virtual aint f_Call(t_CThreadObject *_pThread)
			{
				return m_FunctionObject(_pThread);
			}
			virtual ch8 const *f_GetName()
			{
				return m_Name;
			}

		};
	}

	template <typename t_CAllocator, typename t_CStr>
	template <typename tf_CFunctionType>
	auto
	//void
	TCThreadObject<t_CAllocator, t_CStr>::fs_StartThread
		(
			tf_CFunctionType &&_FunctionObject
			, t_CStr const &_Name
			, EExecutionPriority _Prio
			, umint _StackSize
			, umint _Affinity
			, bool _bAutoDestroy
		)
		-> NStorage::TCUniquePointer<TCThreadObject, t_CAllocator>
	{
		NStorage::TCUniquePointer<TCThreadObject, t_CAllocator> pThread
			= fg_Construct
			(
				NStorage::TCUniquePointer<CCallerObject, t_CAllocator>
				(
					fg_Construct<NPrivate::TCCallerObjectImp<NTraits::TCRemoveReference<tf_CFunctionType>, t_CStr, CCallerObject, TCThreadObject>>
					(
						fg_Forward<tf_CFunctionType>(_FunctionObject)
						, _Name
					)
				)
			)
		;
		pThread->f_Start(_Prio, _StackSize, _Affinity, _bAutoDestroy);

		return pThread;
	}

	template <typename t_CAllocator, typename t_CStr>
	template <typename tf_CFunctionType>
	auto TCThreadObject<t_CAllocator, t_CStr>::fs_StartThread
		(
			tf_CFunctionType *_pFunctionObject
			, t_CStr const &_Name
			, EExecutionPriority _Prio
			, umint _StackSize
			, umint _Affinity
			, bool _bAutoDestroy
		)
		-> NStorage::TCUniquePointer<TCThreadObject, t_CAllocator>
	{
		class CCallerObjectImp final : public CCallerObject
		{
		public:
			tf_CFunctionType *m_pFunctionObject;
			t_CStr m_Name;
			CCallerObjectImp(tf_CFunctionType *_pFunctionObject, const t_CStr &_Name) : m_pFunctionObject(_pFunctionObject), m_Name(_Name)
			{
			}
			virtual aint f_Call(TCThreadObject *_pThread)
			{
				return (*m_pFunctionObject)(_pThread);
			}
			virtual ch8 const *f_GetName()
			{
				return m_Name;
			}

		};

		NStorage::TCUniquePointer<TCThreadObject, t_CAllocator> pThread
			= fg_Construct(NStorage::TCUniquePointer<CCallerObject, t_CAllocator>(fg_Construct<CCallerObjectImp>(_pFunctionObject, _Name)))
		;
		pThread->f_Start(_Prio, _StackSize, _Affinity, _bAutoDestroy);

		return pThread;
	}
}

#include "Malterlib_Thread_RefcountDebug.hpp"
