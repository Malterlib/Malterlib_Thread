// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include "Malterlib_Thread.h"

#include <Mib/Function/Function>

namespace NMib
{
	namespace NThread
	{
		enum EThreadLocalInterfaceFlag
		{
			EThreadLocalInterfaceFlag_None
			, EThreadLocalInterfaceFlag_UseFastStorage = DMibBit(0)
#ifdef DMibPSupportAlwaysCreatedThreadLocal
			, EThreadLocalInterfaceFlag_AlwaysCreated = DMibBit(1)
#else
			, EThreadLocalInterfaceFlag_AlwaysCreated = 0
#endif
			, EThreadLocalInterfaceFlag_Inherit = DMibBit(2)
		};

		struct CThreadLocalInterface
		{
			CThreadLocalInterface()
				: m_Flags(EThreadLocalInterfaceFlag_None)
			{
			}

			virtual ~CThreadLocalInterface(){}

			struct CSafeAllocMemory
			{
				CSafeAllocMemory(void *_pMemory, umint _Size)
					: m_pMemory(_pMemory)
					, m_Size(_Size)
				{
				}

				void *m_pMemory;
				umint m_Size;
			};

			struct CSafeAlloc
			{
				CSafeAlloc(CThreadLocalInterface *_pInterface, CSafeAllocMemory const &_Memory)
					: m_pInterface(_pInterface)
					, m_Memory(_Memory)
				{
				}

				CSafeAlloc(CSafeAlloc &&_Other)
					: m_pInterface(fg_Exchange(_Other.m_pInterface, nullptr))
					, m_Memory(_Other.m_Memory)
				{
				}

				CSafeAlloc &operator = (CSafeAlloc &&_Other)
				{
					m_pInterface = fg_Exchange(_Other.m_pInterface, nullptr);
					m_Memory = _Other.m_Memory;

					return *this;
				}

				~CSafeAlloc()
				{
					if (!m_pInterface)
						return;

					m_pInterface->f_FreeData(m_Memory);
				}

				void f_Claim()
				{
					m_pInterface = nullptr;
				}

				CThreadLocalInterface *m_pInterface;
				CSafeAllocMemory m_Memory;
			};

			virtual void f_DeleteItem(void *_pItem) = 0;
			virtual CSafeAlloc f_AllocData() = 0;
			virtual void f_FreeData(CSafeAllocMemory const &_Alloc) = 0;
			virtual void *f_CreateDataCopy(void *_pSource, void *_pMemory) = 0;
			virtual void *f_CreateDataMove(void *_pSource, void *_pMemory) = 0;
			virtual void *f_CreateData(void *_pMemory, bool _bInitial) = 0;
#if DMibEnableSafeCheck > 0
			virtual ch8 const* f_GetName() = 0;
#endif

			EThreadLocalInterfaceFlag m_Flags;
		};
        /*¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯¯*\
        |	Template:			Implement thread specific storage						|
        |																				|
        |	Parameters:																	|
        |		t_CData:			This is the class you want to store an				|
		|							instance of. You have to inherit this				|
		|							class from CThreadLocal so the						|
		|																				|
		|		t_pIdentifier:		Use this to be able to store diffrent				|
		|							instances of class. Normally you would				|
		|							want to have a seprate class for every				|
		|							place you store thread specific data				|
		|							anyway.												|
        |																				|
        |	Comments:			If you want to store this as a static function			|
		|						variable or as a global variable use TAggregate			|
		|						for this. The thread storage is destroyed when			|
		|						the last instance of it goes out of scope (for every	|
		|						thread).												|
        \*_____________________________________________________________________________*/

		enum EThreadLocalFlag : int32
		{
			EThreadLocalFlag_None,
			EThreadLocalFlag_Inherit = DMibBit(0),
	#ifdef DMibPSupportAlwaysCreatedThreadLocal
			EThreadLocalFlag_AlwaysCreated = DMibBit(1),
	#else
			EThreadLocalFlag_AlwaysCreated = 0,
	#endif
			EThreadLocalFlag_FastThreadLocal = DMibBit(2),
		};

		template <typename t_CData, typename t_CAllocator = NMemory::CAllocator_Heap, EThreadLocalFlag t_Flags = EThreadLocalFlag_None>
		class TCThreadLocal : CThreadLocalInterface
		{
			// Disable copy
			TCThreadLocal(TCThreadLocal const &);
			TCThreadLocal &operator =(TCThreadLocal const &);
			inline_never t_CData *fp_GetNew();

		public:
			void f_DeleteItem(void *_pItem) override;
			CSafeAlloc f_AllocData() override;
			void f_FreeData(CSafeAllocMemory const &_Alloc) override;
			void *f_CreateDataCopy(void *_pSource, void *_pMemory) override;
			void *f_CreateDataMove(void *_pSource, void *_pMemory) override;
			void *f_CreateData(void *_pMemory, bool _bInitial) override;

#if DMibEnableSafeCheck > 0
			ch8 const* f_GetName() override;
#endif

			TCThreadLocal();
			~TCThreadLocal();

			void f_Destroy();
			void f_ReinitForThread();
			void f_DestroyForThread();

			t_CData *f_Get();
			t_CData *f_TryGet();
			t_CData *f_TryGetForThread(umint _ThreadID);
			bool f_IsValid();

			inline_small operator t_CData *();
			inline_small t_CData * operator ->();
			inline_small t_CData & operator &();

			umint m_ThreadLocalLocal;
			umint m_pStorage; // Index into the thread storage list
			static constexpr EThreadLocalFlag mc_Flags = t_Flags;
		};

		template <typename t_CData, EThreadLocalFlag t_Flags = EThreadLocalFlag_None>
		class TCThreadLocalDynamic : CThreadLocalInterface
		{
			// Disable copy
			TCThreadLocalDynamic(TCThreadLocalDynamic const &);
			TCThreadLocalDynamic&operator =(TCThreadLocalDynamic const &);
			inline_never t_CData *fp_GetNew();

		public:
			void f_DeleteItem(void *_pItem) override;
			CSafeAlloc f_AllocData() override;
			void f_FreeData(CSafeAllocMemory const &_Alloc) override;
			void *f_CreateDataCopy(void *_pSource, void *_pMemory) override;
			void *f_CreateDataMove(void *_pSource, void *_pMemory) override;
			void *f_CreateData(void *_pMemory, bool _bInitial) override;
#if DMibEnableSafeCheck > 0
			ch8 const* f_GetName() override;
#endif
			TCThreadLocalDynamic
				(
					NFunction::TCFunctionNoAlloc<CSafeAllocMemory ()> const &_fAlloc
					, NFunction::TCFunctionNoAlloc<void (CSafeAllocMemory const &_Alloc)> const &_fFree
					, NFunction::TCFunctionNoAlloc<t_CData *(t_CData *_pParent, void *_pMemory, bool _bMove)> const &_fConstruct
					, NFunction::TCFunctionNoAlloc<void (t_CData *_pData)> const &_fDestruct
				)
			;
			~TCThreadLocalDynamic();

			void f_Destroy();
			void f_ReinitForThread();
			void f_DestroyForThread();

			t_CData *f_Get();
			t_CData *f_TryGet();
			t_CData *f_TryGetForThread(umint _ThreadID);
			bool f_IsValid();

			inline_small operator t_CData *();
			inline_small t_CData * operator ->();
			inline_small t_CData & operator &();

			static constexpr EThreadLocalFlag mc_Flags = t_Flags;

			umint m_ThreadLocalLocal;
			umint m_pStorage; // Index into the thread storage list
			NFunction::TCFunctionNoAlloc<CSafeAllocMemory ()> m_fAlloc;
			NFunction::TCFunctionNoAlloc<void (CSafeAllocMemory const &_Alloc)> m_fFree;
			NFunction::TCFunctionNoAlloc<t_CData *(t_CData *_pParent, void *_pMemory, bool _bMove)> m_fConstruct;
			NFunction::TCFunctionNoAlloc<void (t_CData *_pData)> m_fDestruct;
		};
	}
}

