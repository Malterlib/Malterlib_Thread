// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>
#include <Mib/Container/Vector>
#include <Mib/Thread/Event>
#include <Mib/Thread/ThreadObject>

DMibAppNoClass;
DMibPMain;

extern "C"
{
	module_export uint32 calling_convention_c fg_TestSetAnotherThreadLocal()
	{
		NMib::NContainer::TCVector<mint> ThreadLocals;
		mint iThreadLocal;
		do
		{
			iThreadLocal = NMib::NSys::fg_Thread_AllocLocal();
			ThreadLocals.f_InsertLast(iThreadLocal);
		}
		while (iThreadLocal < 32 || iThreadLocal % 32);

		uint32 Result = 0;
#if defined(DPlatformFamily_Linux) && defined(DMibAssumeGlibc) && !defined(DMibStaticThreadLocals)
		if (iThreadLocal >= 1024)
			Result |= 1;
#endif

		NMib::NAtomic::TCAtomic<mint> ThreadID{0};
		NMib::NAtomic::TCAtomic<void *> pThreadValue{nullptr};
		mint Value = 0x12345678;
		NMib::NThread::CEvent ThreadReady;
		NMib::NThread::CEvent ReadValue;
		auto pThread = NMib::NThread::CThreadObject::fs_StartThread
			(
				[&] (NMib::NThread::CThreadObject *) -> aint
				{
					ThreadID.f_Store(NMib::NSys::fg_Thread_GetCurrentUID(), NMib::NAtomic::EMemoryOrder_Release);
					ThreadReady.f_SetSignaled();
					ReadValue.f_Wait();
					pThreadValue.f_Store(NMib::NSys::fg_Thread_GetLocal(iThreadLocal), NMib::NAtomic::EMemoryOrder_Release);
					NMib::NSys::fg_Thread_SetLocal(iThreadLocal, nullptr);
					return 0;
				}
				, "Set another thread's local storage from DLL"
			)
		;

		ThreadReady.f_Wait();
		mint TargetThreadID = ThreadID.f_Load(NMib::NAtomic::EMemoryOrder_Acquire);
		NMib::NSys::fg_Thread_SetLocal(TargetThreadID, iThreadLocal, &Value);
		if (NMib::NSys::fg_Thread_GetLocal(TargetThreadID, iThreadLocal) != &Value)
			Result |= 2;

		ReadValue.f_SetSignaled();
		pThread->f_Stop();
		pThread.f_Clear();

		if (pThreadValue.f_Load(NMib::NAtomic::EMemoryOrder_Acquire) != &Value)
			Result |= 4;

		for (mint iLocal = ThreadLocals.f_GetLen(); iLocal-- > 0;)
			NMib::NSys::fg_Thread_FreeLocal(ThreadLocals[iLocal]);

		return Result;
	}
}
