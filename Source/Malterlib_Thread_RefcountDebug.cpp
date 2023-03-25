// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#if DMibConfig_RefCountDebugging

#include <Mib/Core/Core>
#include <Mib/Thread/Thread>
#include <Mib/Storage/Pointer>

namespace NMib::NStorage
{
	CRefCountDebugReference::CRefCountDebugReference() = default;

	CRefCountDebugReference::~CRefCountDebugReference()
	{
		DMibFastCheck(!m_pCallstack);
	}

	CRefCountDebugReference::CRefCountDebugReference(CRefCountDebugReference &&_Other)
	{
		m_pCallstack = _Other.m_pCallstack;
		_Other.m_pCallstack = nullptr;
	}

	CRefCountDebugReference &CRefCountDebugReference::operator =(CRefCountDebugReference &&_Other)
	{
		m_pCallstack = _Other.m_pCallstack;
		_Other.m_pCallstack = nullptr;

		return *this;
	}

	void TCIntrusiveRefCount<ESharedPointerOption_None>::f_Initial(CRefCountDebugReference &o_Reference) const
	{
		//DMibFastCheck(m_RefCount.f_Load() == 0);
		DMibFastCheck(!o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			o_Reference.m_pCallstack = &m_Debug->m_Callstacks.f_Insert();
		}
		o_Reference.m_pCallstack->m_CallstackLen = NSys::fg_System_GetStackTrace(o_Reference.m_pCallstack->m_Callstack, 128);
	}

	void TCIntrusiveRefCount<ESharedPointerOption_None>::f_Remove(CRefCountDebugReference &o_Reference) const
	{
		DMibFastCheck(o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			m_Debug->m_Callstacks.f_Remove(*o_Reference.m_pCallstack);
			o_Reference.m_pCallstack = nullptr;
		}
	}

	smint TCIntrusiveRefCount<ESharedPointerOption_None>::f_Decrease(CRefCountDebugReference &o_Reference) const
	{
		DMibFastCheck(o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			m_Debug->m_Callstacks.f_Remove(*o_Reference.m_pCallstack);
			o_Reference.m_pCallstack = nullptr;
		}

		smint Return = m_RefCount.f_FetchSub(1, NAtomic::EMemoryOrder_Release);
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

	smint TCIntrusiveRefCount<ESharedPointerOption_None>::f_Increase
		(
			CRefCountDebugReference &o_Reference
#if DMibEnableSafeCheck > 0
			, bool _bAllowRevive
#endif
		) const
	{
		smint Return = m_RefCount.f_FetchAdd(1, NAtomic::EMemoryOrder_Release);
		DMibFastCheck(Return >= 0 || _bAllowRevive && Return == -1);

		DMibFastCheck(!o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			o_Reference.m_pCallstack = &m_Debug->m_Callstacks.f_Insert();
		}
		o_Reference.m_pCallstack->m_CallstackLen = NSys::fg_System_GetStackTrace(o_Reference.m_pCallstack->m_Callstack, 128);

		return Return;
	}

	void TCIntrusiveRefCount<ESharedPointerOption_None>::f_Move
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
		o_DestinationReference.m_pCallstack->m_CallstackLen = NSys::fg_System_GetStackTrace(o_DestinationReference.m_pCallstack->m_Callstack, 128);
	}

	void TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer>::f_Initial(CRefCountDebugReference &o_Reference) const
	{
		//DMibFastCheck(m_RefCount.f_Load() == 0);
		DMibFastCheck(!o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			o_Reference.m_pCallstack = &m_Debug->m_Callstacks.f_Insert();
		}
		o_Reference.m_pCallstack->m_CallstackLen = NSys::fg_System_GetStackTrace(o_Reference.m_pCallstack->m_Callstack, 128);
	}

	void TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer>::f_Remove(CRefCountDebugReference &o_Reference) const
	{
		DMibFastCheck(o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			m_Debug->m_Callstacks.f_Remove(*o_Reference.m_pCallstack);
			o_Reference.m_pCallstack = nullptr;
		}
	}

	smint TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer>::f_Decrease(CRefCountDebugReference &o_Reference) const
	{
		DMibFastCheck(o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			m_Debug->m_Callstacks.f_Remove(*o_Reference.m_pCallstack);
			o_Reference.m_pCallstack = nullptr;
		}

		smint Return = m_RefCount.f_FetchSub(1, NAtomic::EMemoryOrder_Release);
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

	smint TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer>::f_Increase
		(
			CRefCountDebugReference &o_Reference
#if DMibEnableSafeCheck > 0
			, bool _bAllowRevive
#endif
		) const
	{
		smint Return = m_RefCount.f_FetchAdd(1, NAtomic::EMemoryOrder_Release);
		DMibFastCheck(Return >= 0 || _bAllowRevive && Return == -1);
				
		DMibFastCheck(!o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			o_Reference.m_pCallstack = &m_Debug->m_Callstacks.f_Insert();
		}
		o_Reference.m_pCallstack->m_CallstackLen = NSys::fg_System_GetStackTrace(o_Reference.m_pCallstack->m_Callstack, 128);

		return Return;
	}

	bool TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer>::f_IncreaseWhileValid(CRefCountDebugReference &o_Reference) const
	{
		smint CurrentValue = m_RefCount.f_Load(NAtomic::EMemoryOrder_Relaxed);
		while (CurrentValue >= 0)
		{
			if (m_RefCount.f_CompareExchangeStrong(CurrentValue, CurrentValue + 1, NAtomic::EMemoryOrder_Release, NAtomic::EMemoryOrder_Relaxed))
			{
				DMibFastCheck(!o_Reference.m_pCallstack);
				{
					DMibLock(m_Debug->m_Lock);
					o_Reference.m_pCallstack = &m_Debug->m_Callstacks.f_Insert();
				}
				o_Reference.m_pCallstack->m_CallstackLen = NSys::fg_System_GetStackTrace(o_Reference.m_pCallstack->m_Callstack, 128);
				return true;
			}
		}
				
		return false;
	}

	smint TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer>::f_WeakDecrease(CRefCountDebugReference *o_pReference) const
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

		smint Return = m_WeakRefCount.f_FetchSub(1, NAtomic::EMemoryOrder_Release);
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

	smint TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer>::f_WeakIncrease(CRefCountDebugReference &o_Reference) const
	{
		smint Return = m_WeakRefCount.f_FetchAdd(1, NAtomic::EMemoryOrder_Release);

		DMibFastCheck(!o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			o_Reference.m_pCallstack = &m_Debug->m_WeakCallstacks.f_Insert();
		}
		o_Reference.m_pCallstack->m_CallstackLen = NSys::fg_System_GetStackTrace(o_Reference.m_pCallstack->m_Callstack, 128);

		return Return;
	}

	void TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer>::f_Move
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
		o_DestinationReference.m_pCallstack->m_CallstackLen = NSys::fg_System_GetStackTrace(o_DestinationReference.m_pCallstack->m_Callstack, 128);
	}

	void TCIntrusiveRefCount<ESharedPointerOption_SupportWeakPointer>::f_WeakMove
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
		o_DestinationReference.m_pCallstack->m_CallstackLen = NSys::fg_System_GetStackTrace(o_DestinationReference.m_pCallstack->m_Callstack, 128);
	}
}
#else
mint g_Dummy_RefCountDebug = 0;
#endif

