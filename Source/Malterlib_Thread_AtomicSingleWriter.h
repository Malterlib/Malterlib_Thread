// Copyright © 2020 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Thread/SequenceLock>

namespace NMib::NThread
{
	template <typename t_CType>
	struct TCAtomicSingleWriter
	{
		constexpr TCAtomicSingleWriter() noexcept;
		constexpr TCAtomicSingleWriter(t_CType _Value) noexcept;

		constexpr inline_always t_CType operator = (t_CType _Value) noexcept;

		constexpr bool f_IsLockFree() const noexcept;
		constexpr void f_Store(t_CType _Value, NAtomic::EMemoryOrder _Order = NAtomic::EMemoryOrder_SequentiallyConsistent) noexcept;

		constexpr t_CType f_Load(NAtomic::EMemoryOrder _Order = NAtomic::EMemoryOrder_SequentiallyConsistent) const noexcept;
		constexpr operator t_CType () const noexcept;

		constexpr t_CType f_Exchange(t_CType _Value, NAtomic::EMemoryOrder _Order = NAtomic::EMemoryOrder_SequentiallyConsistent) noexcept;
		constexpr bool f_CompareExchangeWeak(t_CType &_Expected, t_CType _Desired, NAtomic::EMemoryOrder _SuccessOrder, NAtomic::EMemoryOrder _FailureOrder) noexcept;
		constexpr bool f_CompareExchangeWeak(t_CType &_Expected, t_CType _Desired, NAtomic::EMemoryOrder _Order = NAtomic::EMemoryOrder_SequentiallyConsistent) noexcept;
		constexpr bool f_CompareExchangeStrong(t_CType &_Expected, t_CType _Desired, NAtomic::EMemoryOrder _SuccessOrder, NAtomic::EMemoryOrder _FailureOrder) noexcept;
		constexpr bool f_CompareExchangeStrong(t_CType &_Expected, t_CType _Desired, NAtomic::EMemoryOrder _Order = NAtomic::EMemoryOrder_SequentiallyConsistent) noexcept;

		template <typename tf_CType>
		constexpr t_CType f_FetchAdd(tf_CType _Value, NAtomic::EMemoryOrder _Order = NAtomic::EMemoryOrder_SequentiallyConsistent) noexcept;

		template <typename tf_CType>
		constexpr t_CType f_FetchSub(tf_CType _Value, NAtomic::EMemoryOrder _Order = NAtomic::EMemoryOrder_SequentiallyConsistent) noexcept;

		template <typename tf_CType>
		constexpr t_CType f_FetchAnd(tf_CType _Value, NAtomic::EMemoryOrder _Order = NAtomic::EMemoryOrder_SequentiallyConsistent) noexcept;

		template <typename tf_CType>
		constexpr t_CType f_FetchOr(tf_CType _Value, NAtomic::EMemoryOrder _Order = NAtomic::EMemoryOrder_SequentiallyConsistent) noexcept;

		template <typename tf_CType>
		constexpr t_CType f_FetchXor(tf_CType _Value, NAtomic::EMemoryOrder _Order = NAtomic::EMemoryOrder_SequentiallyConsistent) noexcept;

		constexpr t_CType operator ++ () noexcept;
		constexpr t_CType operator ++ (int) noexcept;
		constexpr t_CType operator -- () noexcept;
		constexpr t_CType operator -- (int) noexcept;

		template <typename tf_CType>
		constexpr t_CType operator += (tf_CType _Value) noexcept;

		template <typename tf_CType>
		constexpr t_CType operator -= (tf_CType _Value) noexcept;

		template <typename tf_CType>
		constexpr t_CType operator &= (tf_CType _Value) noexcept;

		template <typename tf_CType>
		constexpr t_CType operator |= (tf_CType _Value) noexcept;

		template <typename tf_CType>
		constexpr t_CType operator ^= (tf_CType _Value) noexcept;

		template <typename tf_CFormatter>
		auto f_CreateStringFormatter(tf_CFormatter &_Formatter) const;

	private:
		TCSequenceLock<t_CType> mp_Data;
	};

	template <typename t_CType>
	using TCAtomicOrSingleWriterAtomic = typename TCChooseType<NAtomic::TCAtomic<t_CType>::mc_bIsAlwaysLockFree, NAtomic::TCAtomic<t_CType>, TCAtomicSingleWriter<t_CType>>::CType;
}

#include "Malterlib_Thread_AtomicSingleWriter.hpp"

