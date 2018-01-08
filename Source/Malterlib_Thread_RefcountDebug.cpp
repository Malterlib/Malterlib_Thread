// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#if DMibConfig_RefcountDebugging

#include <Mib/Core/Core>
#include <Mib/Thread/Thread>
#include <Mib/Storage/Pointer>

namespace NMib::NPtr
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

	void TCSharedPointerIntrusiveBase<ESharedPointerOption_None>::f_InitialRef(CRefCountDebugReference &o_Reference) const
	{
		//DMibFastCheck(m_RefCount.f_Load() == 0);
		DMibFastCheck(!o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			o_Reference.m_pCallstack = &m_Debug->m_Callstacks.f_Insert();
		}
		o_Reference.m_pCallstack->m_CallstackLen = NSys::fg_System_GetStackTrace(o_Reference.m_pCallstack->m_Callstack, 128);
	}

	aint TCSharedPointerIntrusiveBase<ESharedPointerOption_None>::f_RefCountDecrease(CRefCountDebugReference &o_Reference) const
	{
		DMibFastCheck(o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			m_Debug->m_Callstacks.f_Remove(*o_Reference.m_pCallstack);
			o_Reference.m_pCallstack = nullptr;
		}

		aint Return = m_RefCount.f_FetchSub(1, NAtomic::EMemoryOrder_Release);
		if (Return == 0)
			NAtomic::fg_MemoryFence(NAtomic::EMemoryOrder_Acquire);

		DMibRefcountDebuggingOnly(if (Return == 0) m_Debug.f_Destruct());

		return Return;
	}

	aint TCSharedPointerIntrusiveBase<ESharedPointerOption_None>::f_RefCountIncrease(CRefCountDebugReference &o_Reference) const
	{
		aint Return = m_RefCount.f_FetchAdd(1, NAtomic::EMemoryOrder_Release);
		DMibFastCheck(Return >= 0);

		DMibFastCheck(!o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			o_Reference.m_pCallstack = &m_Debug->m_Callstacks.f_Insert();
		}
		o_Reference.m_pCallstack->m_CallstackLen = NSys::fg_System_GetStackTrace(o_Reference.m_pCallstack->m_Callstack, 128);

		return Return;
	}

	void TCSharedPointerIntrusiveBase<ESharedPointerOption_SupportWeakPointer>::f_InitialRef(CRefCountDebugReference &o_Reference) const
	{
		//DMibFastCheck(m_RefCount.f_Load() == 0);
		DMibFastCheck(!o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			o_Reference.m_pCallstack = &m_Debug->m_Callstacks.f_Insert();
		}
		o_Reference.m_pCallstack->m_CallstackLen = NSys::fg_System_GetStackTrace(o_Reference.m_pCallstack->m_Callstack, 128);
	}

	smint TCSharedPointerIntrusiveBase<ESharedPointerOption_SupportWeakPointer>::f_RefCountDecrease(CRefCountDebugReference &o_Reference) const
	{
		DMibFastCheck(o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			m_Debug->m_Callstacks.f_Remove(*o_Reference.m_pCallstack);
			o_Reference.m_pCallstack = nullptr;
		}

		smint Return = m_RefCount.f_FetchSub(1, NAtomic::EMemoryOrder_Release);
		if (Return == 0)
			NAtomic::fg_MemoryFence(NAtomic::EMemoryOrder_Acquire);

		return Return;
	}

	smint TCSharedPointerIntrusiveBase<ESharedPointerOption_SupportWeakPointer>::f_RefCountIncrease(CRefCountDebugReference &o_Reference) const
	{
		aint Return = m_RefCount.f_FetchAdd(1, NAtomic::EMemoryOrder_Release);
		DMibFastCheck(Return >= 0);
				
		DMibFastCheck(!o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			o_Reference.m_pCallstack = &m_Debug->m_Callstacks.f_Insert();
		}
		o_Reference.m_pCallstack->m_CallstackLen = NSys::fg_System_GetStackTrace(o_Reference.m_pCallstack->m_Callstack, 128);

		return Return;
	}

	bool TCSharedPointerIntrusiveBase<ESharedPointerOption_SupportWeakPointer>::f_RefCountIncreaseWhileValid(CRefCountDebugReference &o_Reference) const
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

	smint TCSharedPointerIntrusiveBase<ESharedPointerOption_SupportWeakPointer>::f_WeakRefCountDecrease(CRefCountDebugReference *o_pReference) const
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
			NAtomic::fg_MemoryFence(NAtomic::EMemoryOrder_Acquire);

		DMibRefcountDebuggingOnly(if (Return == 0) m_Debug.f_Destruct());

		return Return;
	}

	smint TCSharedPointerIntrusiveBase<ESharedPointerOption_SupportWeakPointer>::f_WeakRefCountIncrease(CRefCountDebugReference &o_Reference) const
	{
		DMibFastCheck(m_RefCount.f_Load() >= -1);
		aint Return = m_WeakRefCount.f_FetchAdd(1, NAtomic::EMemoryOrder_Release);

		DMibFastCheck(!o_Reference.m_pCallstack);
		{
			DMibLock(m_Debug->m_Lock);
			o_Reference.m_pCallstack = &m_Debug->m_WeakCallstacks.f_Insert();
		}
		o_Reference.m_pCallstack->m_CallstackLen = NSys::fg_System_GetStackTrace(o_Reference.m_pCallstack->m_Callstack, 128);

		return Return;
	}
}
#else
mint g_Dummy_RefcountDebug = 0;
#endif

