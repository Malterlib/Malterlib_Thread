// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

namespace NMib
{
	namespace NThread
	{

		template <typename t_CData, typename t_CAllocator, CThreadLocalFlagUnderlaying t_Flags>
		TCThreadLocal<t_CData, t_CAllocator, t_Flags>::TCThreadLocal()
		{
			if (fs_GetFlags() & EThreadLocalFlag_FastThreadLocal)
				m_Flags |= EThreadLocalInterfaceFlag_UseFastStorage;
			if (fs_GetFlags() & EThreadLocalFlag_AlwaysCreated)
				m_Flags |= EThreadLocalInterfaceFlag_AlwaysCreated;
			m_pStorage = fg_GetSys()->f_ThreadLocalAlloc(*this, m_ThreadLocalLocal);
		}

		template <typename t_CData, typename t_CAllocator, CThreadLocalFlagUnderlaying t_Flags>
		void TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_Destroy()
		{
			if (m_pStorage)
			{
				fg_GetSys()->f_ThreadLocalFree(*this, m_pStorage);
				m_pStorage = nullptr;
			}
		}
		
		template <typename t_CData, typename t_CAllocator, CThreadLocalFlagUnderlaying t_Flags>
		TCThreadLocal<t_CData, t_CAllocator, t_Flags>::~TCThreadLocal()
		{
			f_Destroy();
		}

		template <typename t_CData, typename t_CAllocator, CThreadLocalFlagUnderlaying t_Flags>
		bint TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_IsValid()
		{
			if (fs_GetFlags() & EThreadLocalFlag_FastThreadLocal)
			{
				if (fs_GetFlags() & EThreadLocalFlag_AlwaysCreated)
					return NSys::fg_Thread_GetLocalAlwaysSetFast(m_ThreadLocalLocal) != nullptr;
				else
					return NSys::fg_Thread_GetLocalFast(m_ThreadLocalLocal) != nullptr;
			}
			else
			{
				if (fs_GetFlags() & EThreadLocalFlag_AlwaysCreated)
					return NSys::fg_Thread_GetLocalAlwaysSet(m_ThreadLocalLocal) != nullptr;
				else
					return NSys::fg_Thread_GetLocal(m_ThreadLocalLocal) != nullptr;
			}
			//t_CData *pData = ((t_CData *)fg_GetSys()->f_ThreadLocalGet(m_pStorage));
			//return pData != 0;
		}

		template <typename t_CData, typename t_CAllocator, CThreadLocalFlagUnderlaying t_Flags>
		inline_never t_CData *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::fp_GetNew()
		{
			t_CData *pData = ((t_CData *)fg_GetSys()->f_ThreadLocalGet(m_pStorage));
			if (pData)
			{
				if (fs_GetFlags() & EThreadLocalFlag_FastThreadLocal)
					NSys::fg_Thread_SetLocalFast(m_ThreadLocalLocal, pData);
				else
					NSys::fg_Thread_SetLocal(m_ThreadLocalLocal, pData);
				return pData;
			}
			pData = (t_CData *)f_CreateData(false);
			fg_GetSys()->f_ThreadLocalSet(m_pStorage, pData);
			return pData;
		}

		template <typename t_CData, typename t_CAllocator, CThreadLocalFlagUnderlaying t_Flags>
		inline_small t_CData *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_Get()
		{
			t_CData *pData;
			if (fs_GetFlags() & EThreadLocalFlag_FastThreadLocal)
			{
				if (fs_GetFlags() & EThreadLocalFlag_AlwaysCreated)
					pData = (t_CData *)NSys::fg_Thread_GetLocalAlwaysSetFast(m_ThreadLocalLocal);
				else
					pData = (t_CData *)NSys::fg_Thread_GetLocalFast(m_ThreadLocalLocal);
			}
			else
			{
				if (fs_GetFlags() & EThreadLocalFlag_AlwaysCreated)
					pData = (t_CData *)NSys::fg_Thread_GetLocalAlwaysSet(m_ThreadLocalLocal);
				else
					pData = (t_CData *)NSys::fg_Thread_GetLocal(m_ThreadLocalLocal);
			}
			if (fs_GetFlags() & EThreadLocalFlag_AlwaysCreated)
			{
				return pData;
			}
			else
			{
				if (likely(pData))
				{
					//DMibFastCheck(((t_CData *)fg_GetSys()->f_ThreadLocalGet(m_pStorage)) == pData);
					return pData;
				}
				return fp_GetNew();
			}
		}
		template <typename t_CData, typename t_CAllocator, CThreadLocalFlagUnderlaying t_Flags>
		inline_small t_CData *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_TryGet()
		{
			if (fs_GetFlags() & EThreadLocalFlag_FastThreadLocal)
			{
				if (fs_GetFlags() & EThreadLocalFlag_AlwaysCreated)
					return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSetFast(m_ThreadLocalLocal);
				else
					return (t_CData *)NSys::fg_Thread_GetLocalFast(m_ThreadLocalLocal);
			}
			else
			{
				if (fs_GetFlags() & EThreadLocalFlag_AlwaysCreated)
					return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSet(m_ThreadLocalLocal);
				else
					return (t_CData *)NSys::fg_Thread_GetLocal(m_ThreadLocalLocal);
			}
		}
		template <typename t_CData, typename t_CAllocator, CThreadLocalFlagUnderlaying t_Flags>
		inline_small t_CData *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_TryGetForThread(mint _ThreadID)
		{
			if (fs_GetFlags() & EThreadLocalFlag_FastThreadLocal)
			{
				if (fs_GetFlags() & EThreadLocalFlag_AlwaysCreated)
					return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSetFast(_ThreadID, m_ThreadLocalLocal);
				else
					return (t_CData *)NSys::fg_Thread_GetLocalFast(_ThreadID, m_ThreadLocalLocal);
			}
			else
			{
				if (fs_GetFlags() & EThreadLocalFlag_AlwaysCreated)
					return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSet(_ThreadID, m_ThreadLocalLocal);
				else
					return (t_CData *)NSys::fg_Thread_GetLocal(_ThreadID, m_ThreadLocalLocal);
			}
		}
		

		template <typename t_CData, typename t_CAllocator, CThreadLocalFlagUnderlaying t_Flags>
		inline_small void TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_ReinitForThread()
		{
			return fg_GetSys()->f_ThreadLocalReinitForThread(m_pStorage);
		}


		template <typename t_CData, typename t_CAllocator, CThreadLocalFlagUnderlaying t_Flags>
		inline_small TCThreadLocal<t_CData, t_CAllocator, t_Flags>::operator t_CData *()
		{
			return f_Get();
		}

		template <typename t_CData, typename t_CAllocator, CThreadLocalFlagUnderlaying t_Flags>
		inline_small t_CData * TCThreadLocal<t_CData, t_CAllocator, t_Flags>::operator ->()
		{
			return f_Get();
		}

#ifndef DDocumentation_Doxygen
		template <typename t_CData, typename t_CAllocator, CThreadLocalFlagUnderlaying t_Flags>
		inline_small t_CData & TCThreadLocal<t_CData, t_CAllocator, t_Flags>::operator &()
		{
			return *f_Get();
		}
#endif

		template <typename t_CData, typename t_CAllocator, CThreadLocalFlagUnderlaying t_Flags>
		void TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_DeleteItem(void *_pItem)
		{
			t_CData *pData = (t_CData *)_pItem;
			fg_DeleteObjectDefiniteType(t_CAllocator(), pData, fg_Max(mint(DMibPMemoryCacheLineSize), NTraits::TCAlignmentOf<t_CData>::mc_Value));
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

		template <typename t_CData, typename t_CAllocator, CThreadLocalFlagUnderlaying t_Flags>
		void *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_CreateData(void *_pSource, bool _bMove)
		{
			enum 
			{
				EInherit = (t_Flags & int(EThreadLocalFlag_Inherit)) != 0
			};

			if (_bMove)
			{
				return
					TCChooseType
					<
						NTraits::TCIsConstructorCallableWith<t_CData, t_CData &&>::mc_Value, NPrivate::TCCreateHelperDo, NPrivate::TCCreateHelperDoNot
					>::CType::template fs_CreateHelperMove<t_CData>
					(
						t_CAllocator::f_AllocAligned(sizeof(t_CData), fg_Max(mint(DMibPMemoryCacheLineSize), NTraits::TCAlignmentOf<t_CData>::mc_Value))
						, _pSource
					)
				;
			}
			else
			{
				if (EInherit)
				{
					return TCChooseType<EInherit, NPrivate::TCCreateHelperDo, NPrivate::TCCreateHelperDoNot>::CType::template fs_CreateHelper<t_CData>
						(
							t_CAllocator::f_AllocAligned(sizeof(t_CData), fg_Max(mint(DMibPMemoryCacheLineSize), NTraits::TCAlignmentOf<t_CData>::mc_Value))
							, _pSource
						)
					;
				}
				else
				{
					return nullptr;
				}
			}
		}

		template <typename t_CData, typename t_CAllocator, CThreadLocalFlagUnderlaying t_Flags>
		void *TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_CreateData(bint _bInitial)
		{
			EThreadLocalFlag Flags = fs_GetFlags();
			if ((Flags & EThreadLocalFlag_AlwaysCreated) != 0 || !_bInitial)
				return new(t_CAllocator::f_AllocAligned(sizeof(t_CData), fg_Max(mint(DMibPMemoryCacheLineSize), NTraits::TCAlignmentOf<t_CData>::mc_Value))) t_CData();
			return nullptr;
		}

#if DMibEnableSafeCheck > 0
		template <typename t_CData, typename t_CAllocator, CThreadLocalFlagUnderlaying t_Flags>
		ch8 const* TCThreadLocal<t_CData, t_CAllocator, t_Flags>::f_GetName()
		{
			return fg_GetTypeName<t_CData>();
		}
#endif
		
		///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		template <typename t_CData, CThreadLocalFlagUnderlaying t_Flags>
		TCThreadLocalDynamic<t_CData, t_Flags>::TCThreadLocalDynamic
			(
				NFunction::TCFunctionNoAlloc<t_CData *(t_CData *_pParent, bool _bMove)> const &_Construct
				, NFunction::TCFunctionNoAlloc<void (t_CData *_pData)> const &_Destruct
			)
			: m_Construct(_Construct)
			, m_Destruct(_Destruct)
		{
			if (fs_GetFlags() & EThreadLocalFlag_FastThreadLocal)
				m_Flags |= EThreadLocalInterfaceFlag_UseFastStorage;
			if (fs_GetFlags() & EThreadLocalFlag_AlwaysCreated)
				m_Flags |= EThreadLocalInterfaceFlag_AlwaysCreated;
			
			m_pStorage = fg_GetSys()->f_ThreadLocalAlloc(*this, m_ThreadLocalLocal);
		}

		template <typename t_CData, CThreadLocalFlagUnderlaying t_Flags>
		void TCThreadLocalDynamic<t_CData, t_Flags>::f_Destroy()
		{
			if (m_pStorage)
			{
				fg_GetSys()->f_ThreadLocalFree(*this, m_pStorage);
				m_pStorage = nullptr;
			}
		}


		template <typename t_CData, CThreadLocalFlagUnderlaying t_Flags>
		TCThreadLocalDynamic<t_CData, t_Flags>::~TCThreadLocalDynamic()
		{
			f_Destroy();
		}

		template <typename t_CData, CThreadLocalFlagUnderlaying t_Flags>
		bint TCThreadLocalDynamic<t_CData, t_Flags>::f_IsValid()
		{
			if (fs_GetFlags() & EThreadLocalFlag_FastThreadLocal)
			{
				if (fs_GetFlags() & EThreadLocalFlag_AlwaysCreated)
					return NSys::fg_Thread_GetLocalAlwaysSetFast(m_ThreadLocalLocal) != nullptr;
				else
					return NSys::fg_Thread_GetLocalFast(m_ThreadLocalLocal) != nullptr;
			}
			else
			{
				if (fs_GetFlags() & EThreadLocalFlag_AlwaysCreated)
					return NSys::fg_Thread_GetLocalAlwaysSet(m_ThreadLocalLocal) != nullptr;
				else
					return NSys::fg_Thread_GetLocal(m_ThreadLocalLocal) != nullptr;
			}
			//t_CData *pData = ((t_CData *)fg_GetSys()->f_ThreadLocalGet(m_pStorage));
			//return pData != 0;
		}

		template <typename t_CData, CThreadLocalFlagUnderlaying t_Flags>
		inline_never t_CData *TCThreadLocalDynamic<t_CData, t_Flags>::fp_GetNew()
		{
			t_CData *pData = ((t_CData *)fg_GetSys()->f_ThreadLocalGet(m_pStorage));
			if (pData)
			{
				if (fs_GetFlags() & EThreadLocalFlag_FastThreadLocal)
					NSys::fg_Thread_SetLocalFast(m_ThreadLocalLocal, pData);
				else
					NSys::fg_Thread_SetLocal(m_ThreadLocalLocal, pData);
				return pData;
			}
			pData = (t_CData *)f_CreateData(false);
			fg_GetSys()->f_ThreadLocalSet(m_pStorage, pData);
			return pData;
		}

		template <typename t_CData, CThreadLocalFlagUnderlaying t_Flags>
		inline_small void TCThreadLocalDynamic<t_CData, t_Flags>::f_ReinitForThread()
		{
			return fg_GetSys()->f_ThreadLocalReinitForThread(m_pStorage);
		}

		template <typename t_CData, CThreadLocalFlagUnderlaying t_Flags>
		inline_small t_CData *TCThreadLocalDynamic<t_CData, t_Flags>::f_Get()
		{
			t_CData *pData;
			if (fs_GetFlags() & EThreadLocalFlag_FastThreadLocal)
			{
				if (fs_GetFlags() & EThreadLocalFlag_AlwaysCreated)
					pData = (t_CData *)NSys::fg_Thread_GetLocalAlwaysSetFast(m_ThreadLocalLocal);
				else
					pData = (t_CData *)NSys::fg_Thread_GetLocalFast(m_ThreadLocalLocal);
			}
			else
			{
				if (fs_GetFlags() & EThreadLocalFlag_AlwaysCreated)
					pData = (t_CData *)NSys::fg_Thread_GetLocalAlwaysSet(m_ThreadLocalLocal);
				else
					pData = (t_CData *)NSys::fg_Thread_GetLocal(m_ThreadLocalLocal);
			}
			if (fs_GetFlags() & EThreadLocalFlag_AlwaysCreated)
			{
				return pData;
			}
			else
			{
				if (likely(pData))
				{
					//DMibFastCheck(((t_CData *)fg_GetSys()->f_ThreadLocalGet(m_pStorage)) == pData);
					return pData;
				}
				return fp_GetNew();
			}
		}

		template <typename t_CData, CThreadLocalFlagUnderlaying t_Flags>
		inline_small t_CData *TCThreadLocalDynamic<t_CData, t_Flags>::f_TryGet()
		{
			if (fs_GetFlags() & EThreadLocalFlag_FastThreadLocal)
			{
				if (fs_GetFlags() & EThreadLocalFlag_AlwaysCreated)
					return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSetFast(m_ThreadLocalLocal);
				else
					return (t_CData *)NSys::fg_Thread_GetLocalFast(m_ThreadLocalLocal);
			}
			else
			{
				if (fs_GetFlags() & EThreadLocalFlag_AlwaysCreated)
					return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSet(m_ThreadLocalLocal);
				else
					return (t_CData *)NSys::fg_Thread_GetLocal(m_ThreadLocalLocal);
			}
		}
		
		template <typename t_CData, CThreadLocalFlagUnderlaying t_Flags>
		inline_small t_CData *TCThreadLocalDynamic<t_CData, t_Flags>::f_TryGetForThread(mint _ThreadID)
		{
			if (fs_GetFlags() & EThreadLocalFlag_FastThreadLocal)
			{
				if (fs_GetFlags() & EThreadLocalFlag_AlwaysCreated)
					return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSetFast(_ThreadID, m_ThreadLocalLocal);
				else
					return (t_CData *)NSys::fg_Thread_GetLocalFast(_ThreadID, m_ThreadLocalLocal);
			}
			else
			{
				if (fs_GetFlags() & EThreadLocalFlag_AlwaysCreated)
					return (t_CData *)NSys::fg_Thread_GetLocalAlwaysSet(_ThreadID, m_ThreadLocalLocal);
				else
					return (t_CData *)NSys::fg_Thread_GetLocal(_ThreadID, m_ThreadLocalLocal);
			}
		}
		

		template <typename t_CData, CThreadLocalFlagUnderlaying t_Flags>
		inline_small TCThreadLocalDynamic<t_CData, t_Flags>::operator t_CData *()
		{
			return f_Get();
		}

		template <typename t_CData, CThreadLocalFlagUnderlaying t_Flags>
		inline_small t_CData * TCThreadLocalDynamic<t_CData, t_Flags>::operator ->()
		{
			return f_Get();
		}

#ifndef DDocumentation_Doxygen
		template <typename t_CData, CThreadLocalFlagUnderlaying t_Flags>
		inline_small t_CData & TCThreadLocalDynamic<t_CData, t_Flags>::operator &()
		{
			return *f_Get();
		}
#endif

		template <typename t_CData, CThreadLocalFlagUnderlaying t_Flags>
		void TCThreadLocalDynamic<t_CData, t_Flags>::f_DeleteItem(void *_pItem)
		{
			t_CData *pData = (t_CData *)_pItem;
			m_Destruct(pData);
		}

		template <typename t_CData, CThreadLocalFlagUnderlaying t_Flags>
		void *TCThreadLocalDynamic<t_CData, t_Flags>::f_CreateData(void *_pSource, bool _bMove)
		{
			enum 
			{
				EInherit = (t_Flags & int(EThreadLocalFlag_Inherit)) != 0
			};
			if ((EInherit && _pSource) || _bMove)
				return m_Construct((t_CData *)_pSource, _bMove);
			return nullptr;
		}

		template <typename t_CData, CThreadLocalFlagUnderlaying t_Flags>
		void *TCThreadLocalDynamic<t_CData, t_Flags>::f_CreateData(bint _bInitial)
		{
			EThreadLocalFlag Flags = fs_GetFlags();
			if ((Flags & EThreadLocalFlag_AlwaysCreated) != 0 || !_bInitial)
			{
//				if (!_bInitial)
//					DMibDTrace("Initial Create({}) {}" DMibNewLine, m_pClass << NSys::fg_Thread_GetCurrentUID());
//				else
//					DMibDTrace("Create({}) {}" DMibNewLine, m_pClass << NSys::fg_Thread_GetCurrentUID());
				return m_Construct(nullptr, false);
			}
			return nullptr;
		}

#if DMibEnableSafeCheck > 0
		template <typename t_CData, CThreadLocalFlagUnderlaying t_Flags>
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
			 	, const t_CStr &_Name
			 	, EThreadPriority _Prio
			 	, mint _StackSize
			 	, mint _Affinity
			 	, bint _bAutoDestroy
			)
			-> NPtr::TCUniquePointer<TCThreadObject, t_CAllocator, TCDynamicPtr<typename t_CAllocator::CPtrHolder, TCThreadObject>, void> 
		{
			NPtr::TCUniquePointer<TCThreadObject, t_CAllocator> pThread
				= fg_Construct
				(
				 	NPtr::TCUniquePointer<CCallerObject, t_CAllocator>
				 	(
						fg_Construct<NPrivate::TCCallerObjectImp<typename NTraits::TCRemoveReference<tf_CFunctionType>::CType, t_CStr, CCallerObject, TCThreadObject>>
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
		auto TCThreadObject<t_CAllocator, t_CStr>::fs_StartThread(tf_CFunctionType *_pFunctionObject, const t_CStr &_Name, EThreadPriority _Prio, mint _StackSize, mint _Affinity, bint _bAutoDestroy) 
			-> NPtr::TCUniquePointer<TCThreadObject, t_CAllocator, TCDynamicPtr<typename t_CAllocator::CPtrHolder, TCThreadObject>, void> 
		{
			class CCallerObjectImp : public CCallerObject
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

			NPtr::TCUniquePointer<TCThreadObject, t_CAllocator> pThread = fg_Construct(NPtr::TCUniquePointer<CCallerObject, t_CAllocator>(fg_Construct<CCallerObjectImp>(_pFunctionObject, _Name)));
			pThread->f_Start(_Prio, _StackSize, _Affinity, _bAutoDestroy);

			return pThread;
		}


	}
}
