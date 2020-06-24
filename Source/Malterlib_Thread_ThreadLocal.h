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
		};

		class CThreadLocalInterface
		{
		public:

			CThreadLocalInterface()
				: m_Flags(EThreadLocalInterfaceFlag_None)
			{
			}

			virtual ~CThreadLocalInterface(){}
			EThreadLocalInterfaceFlag m_Flags;

			virtual void f_DeleteItem(void *_pItem) = 0;
			virtual void *f_CreateData(void *_pSource, bool _bMove) = 0;
			virtual void *f_CreateData(bool _bInitial) = 0;
#if DMibEnableSafeCheck > 0
			virtual ch8 const* f_GetName() = 0;
#endif
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
			t_CData *fp_GetNew();
			
		public:
			void f_DeleteItem(void *_pItem) override;
			void *f_CreateData(void *_pSource, bool _bMove) override;
			void *f_CreateData(bool _bInitial) override;
			
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
			t_CData *f_TryGetForThread(mint _ThreadID);
			bool f_IsValid();

			inline_small operator t_CData *();
			inline_small t_CData * operator ->();
			inline_small t_CData & operator &();
			
			mint m_ThreadLocalLocal;
			void * m_pStorage; // Index into the thread storage list
			static constexpr EThreadLocalFlag mc_Flags = t_Flags;
		};

		template <typename t_CData, EThreadLocalFlag t_Flags = EThreadLocalFlag_None>
		class TCThreadLocalDynamic : CThreadLocalInterface
		{
			// Disable copy
			TCThreadLocalDynamic(TCThreadLocalDynamic const &);
			TCThreadLocalDynamic&operator =(TCThreadLocalDynamic const &);
			t_CData *fp_GetNew();

		public:
			void f_DeleteItem(void *_pItem) override;
			void *f_CreateData(void *_pSource, bool _bMove) override;
			void *f_CreateData(bool _bInitial) override;
#if DMibEnableSafeCheck > 0
			ch8 const* f_GetName() override;
#endif
			
			TCThreadLocalDynamic(NFunction::TCFunctionNoAlloc<t_CData *(t_CData *_pParent, bool _bMove)> const &_Construct, NFunction::TCFunctionNoAlloc<void (t_CData *_pData)> const &_Destruct);
			~TCThreadLocalDynamic();

			void f_Destroy();
			void f_ReinitForThread();
			void f_DestroyForThread();

			t_CData *f_Get();
			t_CData *f_TryGet();
			t_CData *f_TryGetForThread(mint _ThreadID);
			bool f_IsValid();

			inline_small operator t_CData *();
			inline_small t_CData * operator ->();
			inline_small t_CData & operator &();

			static constexpr EThreadLocalFlag mc_Flags = t_Flags;

			mint m_ThreadLocalLocal;
			void * m_pStorage; // Index into the thread storage list
			NFunction::TCFunctionNoAlloc<t_CData *(t_CData *_pParent, bool _bMove)> m_Construct;
			NFunction::TCFunctionNoAlloc<void (t_CData *_pData)> m_Destruct;
		};
	}
}

