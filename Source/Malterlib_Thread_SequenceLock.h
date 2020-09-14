// Copyright © 2020 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Core/Core>
#include <Mib/Atomic/Atomic>

namespace NMib::NThread
{
	template <typename t_CData>
	struct TCSequenceLock
	{
		constexpr TCSequenceLock() = default;
		template <typename tf_CValue>
		constexpr TCSequenceLock(tf_CValue &&_Value);
		~TCSequenceLock() = default;

		constexpr t_CData f_Load(NAtomic::EMemoryOrder _Order = NAtomic::EMemoryOrder_Acquire) const;

		template <typename tf_FFunctor>
		constexpr void f_Mutate(tf_FFunctor &&_fMutate, NAtomic::EMemoryOrder _Order = NAtomic::EMemoryOrder_Acquire);

	private:
		NAtomic::TCAtomic<mint> mp_Sequence;
		t_CData mp_Data;
	};
}

#include "Malterlib_Thread_SequenceLock.hpp"
