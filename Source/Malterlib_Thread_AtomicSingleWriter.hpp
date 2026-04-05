// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NThread
{
	template <typename t_CType>
	constexpr TCAtomicSingleWriter<t_CType>::TCAtomicSingleWriter() noexcept // = default;
	{
	}

	template <typename t_CType>
	constexpr TCAtomicSingleWriter<t_CType>::TCAtomicSingleWriter(t_CType _Value) noexcept
		: mp_Data(_Value)
	{
	}

	template <typename t_CType>
	constexpr inline_always t_CType TCAtomicSingleWriter<t_CType>::operator = (t_CType _Value) noexcept
	{
		f_Store(_Value);
		return _Value;
	}

	template <typename t_CType>
	constexpr bool TCAtomicSingleWriter<t_CType>::f_IsLockFree() const noexcept
	{
		return true;
	}

	template <typename t_CType>
	constexpr void TCAtomicSingleWriter<t_CType>::f_Store(t_CType _Value, NAtomic::CMemoryOrder _Order) noexcept
	{
		mp_Data.f_Mutate
			(
				[&](auto &o_Value)
				{
					o_Value = fg_Move(_Value);
				}
				, _Order
			)
		;
	}

	template <typename t_CType>
	constexpr t_CType TCAtomicSingleWriter<t_CType>::f_Load(NAtomic::CMemoryOrder _Order) const noexcept
	{
		return mp_Data.f_Load(_Order);
	}

	template <typename t_CType>
	constexpr TCAtomicSingleWriter<t_CType>::operator t_CType () const noexcept
	{
		return f_Load();
	}

	template <typename t_CType>
	constexpr t_CType TCAtomicSingleWriter<t_CType>::f_Exchange(t_CType _Value, NAtomic::CMemoryOrder _Order) noexcept
	{
		t_CType Return;
		mp_Data.f_Mutate
			(
				[&](auto &o_Value)
				{
					Return = fg_Move(o_Value);
					o_Value = fg_Move(_Value);
				}
				, _Order
			)
		;
		return Return;
	}

	template <typename t_CType>
	constexpr bool TCAtomicSingleWriter<t_CType>::f_CompareExchangeWeak(t_CType &_Expected, t_CType _Desired, NAtomic::CMemoryOrder _SuccessOrder, NAtomic::CMemoryOrder _FailureOrder) noexcept
	{
		return f_CompareExchangeStrong(_Expected, _Desired, fg_Max(_SuccessOrder, _FailureOrder));
	}

	template <typename t_CType>
	constexpr bool TCAtomicSingleWriter<t_CType>::f_CompareExchangeWeak(t_CType &_Expected, t_CType _Desired, NAtomic::CMemoryOrder _Order) noexcept
	{
		return f_CompareExchangeStrong(_Expected, _Desired, _Order);
	}

	template <typename t_CType>
	constexpr bool TCAtomicSingleWriter<t_CType>::f_CompareExchangeStrong(t_CType &_Expected, t_CType _Desired, NAtomic::CMemoryOrder _SuccessOrder, NAtomic::CMemoryOrder _FailureOrder) noexcept
	{
		return f_CompareExchangeStrong(_Expected, _Desired, fg_Max(_SuccessOrder, _FailureOrder));
	}

	template <typename t_CType>
	constexpr bool TCAtomicSingleWriter<t_CType>::f_CompareExchangeStrong(t_CType &_Expected, t_CType _Desired, NAtomic::CMemoryOrder _Order) noexcept
	{
		bool bReturn = true;
		mp_Data.f_Mutate
			(
				[&](auto &o_Value)
				{
					if (o_Value != _Expected)
					{
						_Expected = o_Value;
						bReturn = false;
						return;
					}

					o_Value = _Desired;
				}
				, _Order
			)
		;
		return bReturn;
	}

	// Fetch add
	template <typename t_CType>
	template <typename tf_CType>
	constexpr t_CType TCAtomicSingleWriter<t_CType>::f_FetchAdd(tf_CType _Value, NAtomic::CMemoryOrder _Order) noexcept
	{
		t_CType Return;
		mp_Data.f_Mutate
			(
				[&](auto &o_Value)
				{
					Return = o_Value;
					o_Value += _Value;
				}
				, _Order
			)
		;
		return Return;
	}

	template <typename t_CType>
	template <typename tf_CType>
	constexpr t_CType TCAtomicSingleWriter<t_CType>::f_FetchSub(tf_CType _Value, NAtomic::CMemoryOrder _Order) noexcept
	{
		t_CType Return;
		mp_Data.f_Mutate
			(
				[&](auto &o_Value)
				{
					Return = o_Value;
					o_Value -= _Value;
				}
				, _Order
			)
		;
		return Return;
	}

	template <typename t_CType>
	template <typename tf_CType>
	constexpr t_CType TCAtomicSingleWriter<t_CType>::f_FetchAnd(tf_CType _Value, NAtomic::CMemoryOrder _Order) noexcept
	{
		t_CType Return;
		mp_Data.f_Mutate
			(
				[&](auto &o_Value)
				{
					Return = o_Value;
					o_Value &= _Value;
				}
				, _Order
			)
		;
		return Return;
	}

	template <typename t_CType>
	template <typename tf_CType>
	constexpr t_CType TCAtomicSingleWriter<t_CType>::f_FetchOr(tf_CType _Value, NAtomic::CMemoryOrder _Order) noexcept
	{
		t_CType Return;
		mp_Data.f_Mutate
			(
				[&](auto &o_Value)
				{
					Return = o_Value;
					o_Value |= _Value;
				}
				, _Order
			)
		;
		return Return;
	}

	template <typename t_CType>
	template <typename tf_CType>
	constexpr t_CType TCAtomicSingleWriter<t_CType>::f_FetchXor(tf_CType _Value, NAtomic::CMemoryOrder _Order) noexcept
	{
		t_CType Return;
		mp_Data.f_Mutate
			(
				[&](auto &o_Value)
				{
					Return = o_Value;
					o_Value ^= _Value;
				}
				, _Order
			)
		;
		return Return;
	}

	template <typename t_CType>
	constexpr t_CType TCAtomicSingleWriter<t_CType>::operator ++ () noexcept
	{
		return f_FetchAdd(1) + 1;
	}

	template <typename t_CType>
	constexpr t_CType TCAtomicSingleWriter<t_CType>::operator ++ (int) noexcept
	{
		return f_FetchAdd(1);
	}

	template <typename t_CType>
	constexpr t_CType TCAtomicSingleWriter<t_CType>::operator -- () noexcept
	{
		return f_FetchSub(1)-1;
	}

	template <typename t_CType>
	constexpr t_CType TCAtomicSingleWriter<t_CType>::operator -- (int) noexcept
	{
		return f_FetchSub(1);
	}

	template <typename t_CType>
	template <typename tf_CType>
	constexpr t_CType TCAtomicSingleWriter<t_CType>::operator += (tf_CType _Value) noexcept
	{
		return f_FetchAdd(_Value);
	}

	template <typename t_CType>
	template <typename tf_CType>
	constexpr t_CType TCAtomicSingleWriter<t_CType>::operator -= (tf_CType _Value) noexcept
	{
		return f_FetchSub(_Value);
	}

	template <typename t_CType>
	template <typename tf_CType>
	constexpr t_CType TCAtomicSingleWriter<t_CType>::operator &= (tf_CType _Value) noexcept
	{
		return f_FetchAnd(_Value);
	}

	template <typename t_CType>
	template <typename tf_CType>
	constexpr t_CType TCAtomicSingleWriter<t_CType>::operator |= (tf_CType _Value) noexcept
	{
		return f_FetchOr(_Value);
	}

	template <typename t_CType>
	template <typename tf_CType>
	constexpr t_CType TCAtomicSingleWriter<t_CType>::operator ^= (tf_CType _Value) noexcept
	{
		return f_FetchXor(_Value);
	}

	template <typename t_CType>
	template <typename tf_CFormatter>
	auto TCAtomicSingleWriter<t_CType>::f_CreateStringFormatter(tf_CFormatter &_Formatter) const
	{
		return fg_CreateStringFormatter(_Formatter, this->f_Load());
	}
}
