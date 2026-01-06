// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#if DMibConfig_RefCountDebugging

#include <Mib/Core/Core>
#include <Mib/Thread/Thread>
#include <Mib/Storage/Pointer>

namespace NMib::NStorage
{
	template <typename t_CCountType>
	void TCIntrusiveRefCount<ESharedPointerOption_None, t_CCountType>::f_Initial(CRefCountDebugReference &o_Reference) const
	{
		//DMibFastCheck(m_RefCount.f_Load() == 0);
		DMibFastCheck(!o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			o_Reference.m_pCallstack = &m_Debug->m_Callstacks.f_Insert();
		}
		o_Reference.m_pCallstack->f_Capture();
	}

	template <typename t_CCountType>
	void TCIntrusiveRefCount<ESharedPointerOption_None, t_CCountType>::f_Remove(CRefCountDebugReference &o_Reference) const
	{
		DMibFastCheck(o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			m_Debug->m_Callstacks.f_Remove(*o_Reference.m_pCallstack);
			o_Reference.m_pCallstack = nullptr;
		}
	}

	template <typename t_CCountType>
	t_CCountType TCIntrusiveRefCount<ESharedPointerOption_None, t_CCountType>::f_Decrease(CRefCountDebugReference &o_Reference) const
	{
		DMibFastCheck(o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			m_Debug->m_Callstacks.f_Remove(*o_Reference.m_pCallstack);
			o_Reference.m_pCallstack = nullptr;
		}

		t_CCountType Return = m_RefCount.f_FetchSub(1, NAtomic::EMemoryOrder_Release);
		DMibFastCheck(Return >= 0);
		if (Return == 0)
		{
#ifdef DMibSanitizerEnabled_Thread
			m_RefCount.f_Load(NAtomic::EMemoryOrder_Acquire);
#else
			NAtomic::fg_MemoryFence(NAtomic::EMemoryOrder_Acquire);
#endif
		}

		return Return;
	}

	template <typename t_CCountType>
	t_CCountType TCIntrusiveRefCount<ESharedPointerOption_None, t_CCountType>::f_Increase
		(
			CRefCountDebugReference &o_Reference
#if DMibEnableSafeCheck > 0
			, bool _bAllowRevive
#endif
		) const
	{
		t_CCountType Return = m_RefCount.f_FetchAdd(1, NAtomic::EMemoryOrder_Release);
		DMibFastCheck(Return >= 0 || _bAllowRevive && Return == -1);
		DMibFastCheck(Return < (TCLimitsInt<t_CCountType>::mc_Max - 1));

		DMibFastCheck(!o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			o_Reference.m_pCallstack = &m_Debug->m_Callstacks.f_Insert();
		}
		o_Reference.m_pCallstack->f_Capture();

		return Return;
	}

	template <typename t_CCountType>
	void TCIntrusiveRefCount<ESharedPointerOption_None, t_CCountType>::f_Move
		(
			CRefCountDebugReference &o_SourceReference
			, CRefCountDebugReference &o_DestinationReference
		) const
	{
		DMibFastCheck(o_SourceReference.m_pCallstack);
		DMibFastCheck(!o_DestinationReference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			m_Debug->m_Callstacks.f_Remove(*o_SourceReference.m_pCallstack);
			o_SourceReference.m_pCallstack = nullptr;
			o_DestinationReference.m_pCallstack = &m_Debug->m_Callstacks.f_Insert();
		}
		o_DestinationReference.m_pCallstack->f_Capture();
	}

	template <typename t_CCountType>
	void TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::f_Initial(CRefCountDebugReference &o_Reference) const
	{
		//DMibFastCheck(m_RefCount.f_Load() == 0);
		DMibFastCheck(!o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			o_Reference.m_pCallstack = &m_Debug->m_Callstacks.f_Insert();
		}
		o_Reference.m_pCallstack->f_Capture();
	}

	template <typename t_CCountType>
	void TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::f_Remove(CRefCountDebugReference &o_Reference) const
	{
		DMibFastCheck(o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			m_Debug->m_Callstacks.f_Remove(*o_Reference.m_pCallstack);
			o_Reference.m_pCallstack = nullptr;
		}
	}

	template <typename t_CCountType>
	t_CCountType TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::f_Decrease(CRefCountDebugReference &o_Reference) const
	{
		DMibFastCheck(o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			m_Debug->m_Callstacks.f_Remove(*o_Reference.m_pCallstack);
			o_Reference.m_pCallstack = nullptr;
		}

		t_CCountType Return = m_RefCount.f_FetchSub(1, NAtomic::EMemoryOrder_Release);
		DMibFastCheck(Return >= 0);
		if (Return == 0)
		{
#ifdef DMibSanitizerEnabled_Thread
			m_RefCount.f_Load(NAtomic::EMemoryOrder_Acquire);
#else
			NAtomic::fg_MemoryFence(NAtomic::EMemoryOrder_Acquire);
#endif
		}

		return Return;
	}

	template <typename t_CCountType>
	t_CCountType TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::f_Increase
		(
			CRefCountDebugReference &o_Reference
#if DMibEnableSafeCheck > 0
			, bool _bAllowRevive
#endif
		) const
	{
		t_CCountType Return = m_RefCount.f_FetchAdd(1, NAtomic::EMemoryOrder_Release);
		DMibFastCheck(Return >= 0 || _bAllowRevive && Return == -1);
		DMibFastCheck(Return < (TCLimitsInt<t_CCountType>::mc_Max - 1));

		DMibFastCheck(!o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			o_Reference.m_pCallstack = &m_Debug->m_Callstacks.f_Insert();
		}
		o_Reference.m_pCallstack->f_Capture();

		return Return;
	}

	template <typename t_CCountType>
	bool TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::f_IncreaseWhileValid(CRefCountDebugReference &o_Reference) const
	{
		t_CCountType CurrentValue = m_RefCount.f_Load(NAtomic::EMemoryOrder_Relaxed);
		DMibFastCheck(CurrentValue < (TCLimitsInt<t_CCountType>::mc_Max - 1));

		while (CurrentValue >= 0)
		{
			if (m_RefCount.f_CompareExchangeStrong(CurrentValue, CurrentValue + 1, NAtomic::EMemoryOrder_Release, NAtomic::EMemoryOrder_Relaxed))
			{
				DMibFastCheck(!o_Reference.m_pCallstack);
				{
					DMibLock(m_Debug->m_Lock);
					o_Reference.m_pCallstack = &m_Debug->m_Callstacks.f_Insert();
				}
				o_Reference.m_pCallstack->f_Capture();
				return true;
			}
		}

		return false;
	}

	template <typename t_CCountType>
	t_CCountType TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::f_WeakDecrease(CRefCountDebugReference *o_pReference) const
	{
		if (o_pReference)
		{
			DMibFastCheck(o_pReference->m_pCallstack);
			{
				DMibLock(m_Debug->m_Lock);
				m_Debug->m_WeakCallstacks.f_Remove(*o_pReference->m_pCallstack);
				o_pReference->m_pCallstack = nullptr;
			}
		}

		t_CCountType Return = m_WeakRefCount.f_FetchSub(1, NAtomic::EMemoryOrder_Release);
		if (Return == 0)
		{
#ifdef DMibSanitizerEnabled_Thread
			m_WeakRefCount.f_Load(NAtomic::EMemoryOrder_Acquire);
#else
			NAtomic::fg_MemoryFence(NAtomic::EMemoryOrder_Acquire);
#endif
		}

		return Return;
	}

	template <typename t_CCountType>
	t_CCountType TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::f_WeakIncrease(CRefCountDebugReference &o_Reference) const
	{
		t_CCountType Return = m_WeakRefCount.f_FetchAdd(1, NAtomic::EMemoryOrder_Release);
		DMibFastCheck(Return < (TCLimitsInt<t_CCountType>::mc_Max - 1));

		DMibFastCheck(!o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			o_Reference.m_pCallstack = &m_Debug->m_WeakCallstacks.f_Insert();
		}
		o_Reference.m_pCallstack->f_Capture();

		return Return;
	}

	template <typename t_CCountType>
	void TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::f_Move
		(
			CRefCountDebugReference &o_SourceReference
			, CRefCountDebugReference &o_DestinationReference
		) const
	{
		DMibFastCheck(o_SourceReference.m_pCallstack);
		DMibFastCheck(!o_DestinationReference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			m_Debug->m_Callstacks.f_Remove(*o_SourceReference.m_pCallstack);
			o_SourceReference.m_pCallstack = nullptr;
			o_DestinationReference.m_pCallstack = &m_Debug->m_Callstacks.f_Insert();
		}
		o_DestinationReference.m_pCallstack->f_Capture();
	}

	template <typename t_CCountType>
	void TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::f_WeakMove
		(
			CRefCountDebugReference &o_SourceReference
			, CRefCountDebugReference &o_DestinationReference
		) const
	{
		DMibFastCheck(!o_DestinationReference.m_pCallstack);
		DMibFastCheck(o_SourceReference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			m_Debug->m_WeakCallstacks.f_Remove(*o_SourceReference.m_pCallstack);
			o_SourceReference.m_pCallstack = nullptr;
			o_DestinationReference.m_pCallstack = &m_Debug->m_WeakCallstacks.f_Insert();
		}
		o_DestinationReference.m_pCallstack->f_Capture();
	}

	template <typename t_CCountType>
	void TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::f_WeakInitial(CRefCountDebugReference &o_Reference) const
	{
		DMibFastCheck(!o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			o_Reference.m_pCallstack = &m_Debug->m_WeakCallstacks.f_Insert();
		}
		o_Reference.m_pCallstack->f_Capture();
	}

	template <typename t_CCountType>
	void TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer, t_CCountType>::f_WeakRemove(CRefCountDebugReference &o_Reference) const
	{
		DMibFastCheck(o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			m_Debug->m_WeakCallstacks.f_Remove(*o_Reference.m_pCallstack);
			o_Reference.m_pCallstack = nullptr;
		}
	}
}
#endif

