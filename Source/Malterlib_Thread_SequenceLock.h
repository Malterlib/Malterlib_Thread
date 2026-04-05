// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

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

		constexpr t_CData f_Load(NAtomic::CMemoryOrder _Order = NAtomic::gc_MemoryOrder_Acquire) const;

		template <typename tf_FFunctor>
		constexpr void f_Mutate(tf_FFunctor &&_fMutate, NAtomic::CMemoryOrder _Order = NAtomic::gc_MemoryOrder_Acquire);

	private:
		NAtomic::TCAtomic<umint> mp_Sequence;
		t_CData mp_Data;
	};
}

#include "Malterlib_Thread_SequenceLock.hpp"
