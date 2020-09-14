// Copyright © 2020 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

namespace NMib::NThread
{
	template <typename t_CData>
	template <typename tf_CValue>
	constexpr TCSequenceLock<t_CData>::TCSequenceLock(tf_CValue &&_Value)
		: mp_Data(fg_Format<tf_CValue>(_Value))
	{
	}

	template <typename t_CData>
	DMibSuppressThreadSanitizer constexpr t_CData TCSequenceLock<t_CData>::f_Load(NAtomic::EMemoryOrder _Order) const
	{
		while (true)
		{
			auto SequencePreRead = mp_Sequence.f_Load(fg_Max(_Order, NAtomic::EMemoryOrder_Acquire));

			static_assert(NTraits::TCHasTrivialCopyConstructor<t_CData>::mc_Value);

			t_CData Data;
			NMemory::fg_MemCopy(&Data, &mp_Data, sizeof(t_CData));

			auto SequencePostRead = mp_Sequence.f_Load(fg_Max(_Order, NAtomic::EMemoryOrder_Acquire));

			if (!(SequencePreRead & 1) && SequencePreRead == SequencePostRead)
				return Data;
		}
	}

	template <typename t_CData>
	template <typename tf_FFunctor>
	DMibSuppressThreadSanitizer constexpr void TCSequenceLock<t_CData>::f_Mutate(tf_FFunctor &&_fMutate, NAtomic::EMemoryOrder _Order)
	{
		[[maybe_unused]] auto SequencePreWrite = mp_Sequence.f_FetchAdd(1, fg_Max(_Order, NAtomic::EMemoryOrder_AcquireRelease));

		static_assert(NTraits::TCHasTrivialCopyConstructor<t_CData>::mc_Value);

		t_CData Data;
		NMemory::fg_MemCopy(&Data, &mp_Data, sizeof(t_CData));

		_fMutate(Data);

		NMemory::fg_MemCopy(&mp_Data, &Data, sizeof(t_CData));

		[[maybe_unused]] auto SequencePostWrite = mp_Sequence.f_FetchAdd(1, fg_Max(_Order, NAtomic::EMemoryOrder_Release));

		DMibFastCheck(SequencePostWrite = SequencePreWrite + 1); // Only one thread can write value
	}
}
