#pragma once

#include "../../ScalarTypes.h"
#include "../../_stl_extensions_nonalloc.h"

#include <lzo/lzo1x.h>

#include <limits>
#include <optional>
#include <utility>

namespace Lzo
{
[[nodiscard]] inline bool Initialize() noexcept
{
	static const bool Initialized = lzo_init() == LZO_E_OK;
	return Initialized;
}

[[nodiscard]] inline std::optional<std::size_t> MaxCompressedSize(std::size_t InputSize) noexcept
{
	// LZO's documented worst-case size for an LZO1X block.
	const auto Overhead = InputSize / 16 + 64 + 3;
	if (InputSize == 0 || Overhead > std::numeric_limits<std::size_t>::max() - InputSize)
	{
		return {};
	}
	return InputSize + Overhead;
}

// Zero reports failure. Work holds LZO1X_999_MEM_COMPRESS bytes at lzo_align_t alignment.
// The writable spans must be disjoint from each other and from the inputs.
[[nodiscard]] inline std::size_t CompressBlock(
	xr_span<const u8> Input, xr_span<u8> Output, lzo_voidp Work, xr_span<const u8> Dictionary = {}) noexcept
{
	const auto Bound = MaxCompressedSize(Input.size());
	if (!Bound || *Bound > Output.size() || !std::in_range<lzo_uint>(*Bound) ||
		!std::in_range<lzo_uint>(Dictionary.size()) || !Work || !Initialize())
	{
		return 0;
	}
	// The native compressor writes the length and does not use it as a capacity.
	lzo_uint Produced = 0;
	const int Result = Dictionary.empty()
		? lzo1x_999_compress(Input.data(), static_cast<lzo_uint>(Input.size()), Output.data(), &Produced, Work)
		: lzo1x_999_compress_dict(Input.data(), static_cast<lzo_uint>(Input.size()), Output.data(), &Produced, Work,
			Dictionary.data(), static_cast<lzo_uint>(Dictionary.size()));
	if (Result != LZO_E_OK || Produced > *Bound)
	{
		return 0;
	}
	return Produced;
}

// Zero reports failure. The scratch output may change on a native decode error.
[[nodiscard]] inline std::size_t DecompressBlock(
	xr_span<const u8> Input, xr_span<u8> Output, xr_span<const u8> Dictionary = {}) noexcept
{
	if (Input.empty() || Output.empty() || !std::in_range<lzo_uint>(Input.size()) ||
		!std::in_range<lzo_uint>(Output.size()) || !std::in_range<lzo_uint>(Dictionary.size()) || !Initialize())
	{
		return 0;
	}
	lzo_uint Produced = static_cast<lzo_uint>(Output.size());
	const int Result = Dictionary.empty()
		? lzo1x_decompress_safe(Input.data(), static_cast<lzo_uint>(Input.size()), Output.data(), &Produced, nullptr)
		: lzo1x_decompress_dict_safe(Input.data(), static_cast<lzo_uint>(Input.size()), Output.data(), &Produced, nullptr,
			Dictionary.data(), static_cast<lzo_uint>(Dictionary.size()));
	if (Result != LZO_E_OK || Produced > Output.size())
	{
		return 0;
	}
	return Produced;
}
}
