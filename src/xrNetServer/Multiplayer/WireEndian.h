#pragma once

#include "../../xrCore/ScalarTypes.h"
#include "../../xrCore/_stl_extensions_nonalloc.h"

#include <concepts>

namespace Multiplayer::Wire::Detail
{
template <std::unsigned_integral Value>
Value ReadLittleEndian(xr_span<const u8, sizeof(Value)> Field) noexcept
{
	u64 Result = 0;
	for (std::size_t Index = 0; Index < Field.size(); ++Index)
	{
		Result |= static_cast<u64>(Field[Index]) << (Index * 8);
	}
	return static_cast<Value>(Result);
}

template <std::unsigned_integral Value>
void WriteLittleEndian(xr_span<u8, sizeof(Value)> Field, Value Number) noexcept
{
	for (std::size_t Index = 0; Index < Field.size(); ++Index)
	{
		Field[Index] = static_cast<u8>(static_cast<u64>(Number) >> (Index * 8));
	}
}
}
