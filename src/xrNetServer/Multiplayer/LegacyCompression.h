#pragma once

#include "LegacyMultipacket.h"

#include <lzo/lzoconf.h>

#include <algorithm>

namespace Multiplayer::Legacy
{
// CRC-32/ISO-HDLC over just the encoded block. Callers bound the block to the datagram limit.
[[nodiscard]] inline u32 PackedChecksum(xr_span<const u8> Block) noexcept
{
	return static_cast<u32>(lzo_crc32(0, Block.data(), static_cast<lzo_uint>(Block.size())));
}

// The codec callbacks receive disjoint input and output spans and return zero on failure.
template <typename Encoder>
[[nodiscard]] std::size_t EncodeBody(xr_span<const u8> Input, xr_span<u8> Output, BodyFraming Framing, Encoder&& Encode) noexcept
{
	const auto RawPrefix = Framing == BodyFraming::Untagged ? 0u : 1u;
	Output = Output.first(std::min(Output.size(), MaxDatagramSize - HeaderSize));
	if (Input.empty() || Output.size() < RawPrefix || Input.size() > Output.size() - RawPrefix)
	{
		return 0;
	}
	if (RawPrefix != 0)
	{
		const auto PackedPrefix = RawPrefix + (Framing == BodyFraming::Checksummed ? sizeof(u32) : 0);
		if (Input.size() > PackedPrefix)
		{
			const auto BlockBuffer = Output.subspan(PackedPrefix);
			const auto Produced = Encode(Input, BlockBuffer);
			if (Produced != 0 && Produced <= BlockBuffer.size() && Produced < Input.size() - PackedPrefix)
			{
				Output.front() = CompressedTag;
				if (Framing == BodyFraming::Checksummed)
				{
					Wire::Detail::WriteLittleEndian<u32>(Output.subspan<1, sizeof(u32)>(), PackedChecksum(BlockBuffer.first(Produced)));
				}
				return PackedPrefix + Produced;
			}
		}
		Output.front() = RawTag;
	}
	std::ranges::copy(Input, Output.begin() + RawPrefix);
	return RawPrefix + Input.size();
}

template <typename Decoder>
[[nodiscard]] std::size_t DecodeBody(xr_span<const u8> Input, xr_span<u8> Output, BodyFraming Framing, Decoder&& Decode) noexcept
{
	if (Input.size() > MaxDatagramSize - HeaderSize || Output.empty())
	{
		return 0;
	}
	Output = Output.first(std::min(Output.size(), MaxDecodedSize));
	const auto Body = Detail::ReadBody(Input, Framing);
	if (!Body)
	{
		return 0;
	}
	auto Payload = Body->Payload;
	if (Body->Encoding == BodyEncoding::Raw)
	{
		if (Payload.empty() || Payload.size() > Output.size())
		{
			return 0;
		}
		std::ranges::copy(Payload, Output.begin());
		return Payload.size();
	}
	if (Framing == BodyFraming::Checksummed)
	{
		const u32 StoredChecksum = Wire::Detail::ReadLittleEndian<u32>(Payload.first<sizeof(u32)>());
		Payload = Payload.subspan(sizeof(u32));
		if (StoredChecksum != PackedChecksum(Payload))
		{
			return 0;
		}
	}
	const auto Produced = Decode(Payload, Output);
	return Produced <= Output.size() ? Produced : 0;
}
}
