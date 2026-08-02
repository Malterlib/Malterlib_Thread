// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NThread
{
	template <typename t_CData>
	template <typename tf_CValue>
	constexpr TCSequenceLock<t_CData>::TCSequenceLock(tf_CValue &&_Value)
		: mp_Data(fg_Forward<tf_CValue>(_Value))
	{
	}

	template <typename t_CData>
	DMibSuppressThreadSanitizer constexpr t_CData TCSequenceLock<t_CData>::f_Load(NAtomic::CMemoryOrder _Order) const
	{
		while (true)
		{
			auto SequencePreRead = mp_Sequence.f_Load(fg_Max(_Order, NAtomic::gc_MemoryOrder_Acquire));

			static_assert(NTraits::cIsTriviallyCopyConstructible<t_CData>);

			t_CData Data;
			NMemory::fg_MemCopy(&Data, &mp_Data, sizeof(t_CData));

			auto SequencePostRead = mp_Sequence.f_Load(fg_Max(_Order, NAtomic::gc_MemoryOrder_Acquire));

			if (!(SequencePreRead & 1) && SequencePreRead == SequencePostRead)
				return Data;
		}
	}

	template <typename t_CData>
	template <typename tf_FFunctor>
	DMibSuppressThreadSanitizer constexpr void TCSequenceLock<t_CData>::f_Mutate(tf_FFunctor &&_fMutate, NAtomic::CMemoryOrder _Order)
	{
		[[maybe_unused]] auto SequencePreWrite = mp_Sequence.f_FetchAdd(1, fg_Max(_Order, NAtomic::gc_MemoryOrder_AcquireRelease));

		static_assert(NTraits::cIsTriviallyCopyConstructible<t_CData>);

		t_CData Data;
		NMemory::fg_MemCopy(&Data, &mp_Data, sizeof(t_CData));

		_fMutate(Data);

		NMemory::fg_MemCopy(&mp_Data, &Data, sizeof(t_CData));

		[[maybe_unused]] auto SequencePostWrite = mp_Sequence.f_FetchAdd(1, fg_Max(_Order, NAtomic::gc_MemoryOrder_Release));

		DMibFastCheck(SequencePostWrite = SequencePreWrite + 1); // Only one thread can write value
	}
}
