#pragma once

#include "WireEndian.h"

#include <expected>
#include <optional>
#include <utility>

namespace Multiplayer::Legacy
{
inline constexpr u8 SingleTag = 0xe0;
inline constexpr u8 MergedTag = 0xe1;
inline constexpr u8 RawTag = 0xc0;
inline constexpr u8 CompressedTag = 0xc1;
inline constexpr std::size_t HeaderSize = 3;
inline constexpr std::size_t MaxDatagramSize = 32 * 1024;
inline constexpr std::size_t MaxDecodedSize = 32 * 1024;
inline constexpr std::size_t RecordSizeLimit = 16 * 1024;

enum class BodyFraming
{
	Untagged,
	Tagged,
	Checksummed,
};

enum class PacketLayout
{
	Single,
	Merged,
};

enum class BodyEncoding
{
	Raw,
	Compressed,
};

enum class MultipacketError
{
	TruncatedHeader,
	DatagramTooLarge,
	InvalidTag,
	InvalidDecodedSize,
	InvalidCompressionTag,
	TruncatedCompressedBody,
	DecodedSizeMismatch,
	InvalidRecord,
};

struct MultipacketEnvelope
{
	PacketLayout Layout;
	BodyEncoding Encoding;
	u16 DecodedSize;
	// Encoded bodies still include the optional checksum after the compression tag.
	xr_span<const u8> Payload;
};

namespace Detail
{
[[nodiscard]] inline bool IsRecordSizeValid(std::size_t Size) noexcept
{
	return Size >= sizeof(u16) && Size < RecordSizeLimit;
}

[[nodiscard]] inline bool IsDecodedSizeValid(PacketLayout Layout, std::size_t Size) noexcept
{
	switch (Layout)
	{
	case PacketLayout::Single:
		return IsRecordSizeValid(Size);
	case PacketLayout::Merged:
		return Size >= sizeof(u16) * 2 && Size <= MaxDecodedSize;
	}
	return false;
}

struct BodyView
{
	BodyEncoding Encoding;
	xr_span<const u8> Payload;
};

[[nodiscard]] inline std::expected<BodyView, MultipacketError> ReadBody(
	xr_span<const u8> Body, BodyFraming Framing) noexcept
{
	if (Framing == BodyFraming::Untagged)
	{
		return BodyView{BodyEncoding::Raw, Body};
	}
	if (Body.empty())
	{
		return std::unexpected(MultipacketError::TruncatedHeader);
	}
	const u8 Tag = Body.front();
	const auto Payload = Body.subspan(1);
	if (Tag == RawTag)
	{
		return BodyView{BodyEncoding::Raw, Payload};
	}
	if (Tag != CompressedTag)
	{
		return std::unexpected(MultipacketError::InvalidCompressionTag);
	}
	const auto ChecksumSize = Framing == BodyFraming::Checksummed ? sizeof(u32) : 0;
	if (Payload.size() <= ChecksumSize)
	{
		return std::unexpected(MultipacketError::TruncatedCompressedBody);
	}
	return BodyView{BodyEncoding::Compressed, Payload};
}
}

[[nodiscard]] inline std::expected<MultipacketEnvelope, MultipacketError> ReadEnvelope(
	xr_span<const u8> Datagram, BodyFraming Framing = BodyFraming::Checksummed) noexcept
{
	if (Datagram.size() < HeaderSize)
	{
		return std::unexpected(MultipacketError::TruncatedHeader);
	}
	if (Datagram.size() > MaxDatagramSize)
	{
		return std::unexpected(MultipacketError::DatagramTooLarge);
	}
	const u8 Tag = Datagram.front();
	if (Tag != SingleTag && Tag != MergedTag)
	{
		return std::unexpected(MultipacketError::InvalidTag);
	}
	const PacketLayout Layout = Tag == SingleTag ? PacketLayout::Single : PacketLayout::Merged;
	const u16 Size = Wire::Detail::ReadLittleEndian<u16>(Datagram.subspan<1, sizeof(u16)>());
	if (!Detail::IsDecodedSizeValid(Layout, Size))
	{
		return std::unexpected(MultipacketError::InvalidDecodedSize);
	}

	const auto Body = Detail::ReadBody(Datagram.subspan(HeaderSize), Framing);
	if (!Body)
	{
		return std::unexpected(Body.error());
	}
	if (Body->Encoding == BodyEncoding::Raw && Body->Payload.size() != Size)
	{
		return std::unexpected(MultipacketError::DecodedSizeMismatch);
	}
	return MultipacketEnvelope{Layout, Body->Encoding, Size, Body->Payload};
}

// The cursor and its records borrow the immutable decoded bytes for the same lifetime.
class MultipacketRecords
{
public:
	[[nodiscard]] static std::expected<MultipacketRecords, MultipacketError> Validate(
		const MultipacketEnvelope& Envelope, xr_span<const u8> Body) noexcept
	{
		if (Body.size() != Envelope.DecodedSize)
		{
			return std::unexpected(MultipacketError::DecodedSizeMismatch);
		}
		if (!Detail::IsDecodedSizeValid(Envelope.Layout, Body.size()))
		{
			return std::unexpected(MultipacketError::InvalidDecodedSize);
		}

		auto Remaining = Body;
		while (!Remaining.empty())
		{
			if (!Take(Envelope.Layout, Remaining))
			{
				return std::unexpected(MultipacketError::InvalidRecord);
			}
		}
		return MultipacketRecords(Envelope.Layout, Body);
	}

	[[nodiscard]] std::optional<xr_span<const u8>> Next() noexcept
	{
		if (Remaining.empty())
		{
			return {};
		}
		return Take(Layout, Remaining);
	}

private:
	MultipacketRecords(PacketLayout Layout, xr_span<const u8> Records) noexcept : Layout(Layout), Remaining(Records) {}

	[[nodiscard]] static std::optional<xr_span<const u8>> Take(PacketLayout Layout, xr_span<const u8>& Remaining) noexcept
	{
		if (Layout == PacketLayout::Single)
		{
			return std::exchange(Remaining, xr_span<const u8>{});
		}

		constexpr auto LengthSize = sizeof(u16);
		if (Remaining.size() < LengthSize)
		{
			return {};
		}
		const u16 RecordSize = Wire::Detail::ReadLittleEndian<u16>(Remaining.first<LengthSize>());
		if (!Detail::IsRecordSizeValid(RecordSize) || RecordSize > Remaining.size() - LengthSize)
		{
			return {};
		}
		const auto Record = Remaining.subspan(LengthSize, RecordSize);
		Remaining = Remaining.subspan(LengthSize + RecordSize);
		return Record;
	}

	PacketLayout Layout;
	xr_span<const u8> Remaining;
};
}
