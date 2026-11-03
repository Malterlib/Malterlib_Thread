// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NPrivate
{
	inline umint const &CThreadLocalContext::CPerThread::CCompare::operator () (CPerThread const &_Node) const
	{
		return _Node.m_ThreadID;
	}
}
