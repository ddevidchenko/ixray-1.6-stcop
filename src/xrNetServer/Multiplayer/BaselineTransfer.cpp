#include "BaselineTransfer.h"
#include "WireEndian.h"

#include <algorithm>

namespace Multiplayer::Wire
{
constexpr std::size_t BaselineIdOffset = 0;
constexpr std::size_t CaptureTickOffset = BaselineIdOffset + sizeof(u32);
constexpr std::size_t WatermarkOffset = CaptureTickOffset + sizeof(u32);
constexpr std::size_t ByteCountOffset = WatermarkOffset + sizeof(u32);
constexpr std::size_t RecordCountOffset = ByteCountOffset + sizeof(u32);
constexpr std::size_t ChunkCountOffset = RecordCountOffset + sizeof(u16);
constexpr std::size_t BeginSize = ChunkCountOffset + sizeof(u16);
constexpr std::size_t ChunkIndexOffset = BaselineIdOffset + sizeof(u32);
constexpr std::size_t ChunkPrefixSize = ChunkIndexOffset + sizeof(u16);
constexpr std::size_t MaxChunkSize = ChunkPrefixSize + MaxBaselineChunkBytes;
constexpr std::size_t EndSize = sizeof(u32);
static_assert(MaxChunkSize <= MaxPayloadSize);

bool IsValidBaselineManifest(const BaselineManifest& Manifest) noexcept
{
	if (Manifest.ByteCount > MaxBaselineBytes || Manifest.RecordCount > MaxBaselineRecords || Manifest.RecordCount > Manifest.ByteCount)
	{
		return false;
	}
	if (Manifest.ByteCount != 0 && Manifest.RecordCount == 0)
	{
		return false;
	}
	return Manifest.ChunkCount == GetBaselineChunkCount(Manifest.ByteCount);
}

std::expected<BaselineBegin, BoundMessageError> DecodeBaselineBegin(xr_span<const u8> Bytes) noexcept
{
	const auto Frame = Detail::DecodeBoundMessage(Bytes, MessageType::BaselineBegin, BeginSize, BeginSize);
	if (!Frame)
	{
		return std::unexpected(Frame.error());
	}
	const auto Payload = Frame->Payload.first<BeginSize>();
	const BaselineManifest Manifest{
		Detail::ReadLittleEndian<u32>(Payload.subspan<BaselineIdOffset, sizeof(u32)>()),
		Detail::ReadLittleEndian<u32>(Payload.subspan<CaptureTickOffset, sizeof(u32)>()),
		Detail::ReadLittleEndian<u32>(Payload.subspan<WatermarkOffset, sizeof(u32)>()),
		Detail::ReadLittleEndian<u32>(Payload.subspan<ByteCountOffset, sizeof(u32)>()),
		Detail::ReadLittleEndian<u16>(Payload.subspan<RecordCountOffset, sizeof(u16)>()),
		Detail::ReadLittleEndian<u16>(Payload.subspan<ChunkCountOffset, sizeof(u16)>())
	};
	if (!IsValidBaselineManifest(Manifest))
	{
		return std::unexpected(BoundMessageError::InvalidPayload);
	}
	return BaselineBegin{Frame->Identity, Frame->MessageSequence, Manifest};
}

std::expected<BaselineChunk, BoundMessageError> DecodeBaselineChunk(xr_span<const u8> Bytes) noexcept
{
	const auto Frame = Detail::DecodeBoundMessage(Bytes, MessageType::BaselineChunk, ChunkPrefixSize + 1, MaxChunkSize);
	if (!Frame)
	{
		return std::unexpected(Frame.error());
	}
	const auto Prefix = Frame->Payload.first<ChunkPrefixSize>();
	const u16 Index = Detail::ReadLittleEndian<u16>(Prefix.subspan<ChunkIndexOffset, sizeof(u16)>());
	if (Index >= MaxBaselineChunks)
	{
		return std::unexpected(BoundMessageError::InvalidPayload);
	}
	const u32 Id = Detail::ReadLittleEndian<u32>(Prefix.first<sizeof(u32)>());
	return BaselineChunk{Frame->Identity, Frame->MessageSequence, Id, Index, Frame->Payload.subspan(ChunkPrefixSize)};
}

std::expected<BaselineEnd, BoundMessageError> DecodeBaselineEnd(xr_span<const u8> Bytes) noexcept
{
	const auto Frame = Detail::DecodeBoundMessage(Bytes, MessageType::BaselineEnd, EndSize, EndSize);
	if (!Frame)
	{
		return std::unexpected(Frame.error());
	}
	const u32 Id = Detail::ReadLittleEndian<u32>(Frame->Payload.first<EndSize>());
	return BaselineEnd{Frame->Identity, Frame->MessageSequence, Id};
}

std::expected<std::size_t, BoundMessageError> EncodeBaselineBegin(const BaselineBegin& Begin, xr_span<u8> Output) noexcept
{
	const auto& Manifest = Begin.Manifest;
	if (!IsValidBaselineManifest(Manifest))
	{
		return std::unexpected(BoundMessageError::InvalidPayload);
	}
	xr_array<u8, BeginSize> Bytes;
	const auto Payload = xr_span(Bytes);
	Detail::WriteLittleEndian(Payload.subspan<BaselineIdOffset, sizeof(u32)>(), Manifest.BaselineId);
	Detail::WriteLittleEndian(Payload.subspan<CaptureTickOffset, sizeof(u32)>(), Manifest.CaptureTick);
	Detail::WriteLittleEndian(Payload.subspan<WatermarkOffset, sizeof(u32)>(), Manifest.EventWatermark);
	Detail::WriteLittleEndian(Payload.subspan<ByteCountOffset, sizeof(u32)>(), Manifest.ByteCount);
	Detail::WriteLittleEndian(Payload.subspan<RecordCountOffset, sizeof(u16)>(), Manifest.RecordCount);
	Detail::WriteLittleEndian(Payload.subspan<ChunkCountOffset, sizeof(u16)>(), Manifest.ChunkCount);
	return Detail::EncodeBoundMessage(MessageType::BaselineBegin, Begin.Identity, Begin.MessageSequence, Payload, Output);
}

std::expected<std::size_t, BoundMessageError> EncodeBaselineChunk(const BaselineChunk& Chunk, xr_span<u8> Output) noexcept
{
	if (Chunk.Data.empty() || Chunk.Data.size() > MaxBaselineChunkBytes || Chunk.ChunkIndex >= MaxBaselineChunks)
	{
		return std::unexpected(BoundMessageError::InvalidPayload);
	}
	xr_array<u8, MaxChunkSize> Bytes;
	const auto Payload = xr_span(Bytes);
	Detail::WriteLittleEndian(Payload.first<sizeof(u32)>(), Chunk.BaselineId);
	Detail::WriteLittleEndian(Payload.subspan<ChunkIndexOffset, sizeof(u16)>(), Chunk.ChunkIndex);
	std::ranges::copy(Chunk.Data, Payload.subspan<ChunkPrefixSize>().begin());
	return Detail::EncodeBoundMessage(MessageType::BaselineChunk, Chunk.Identity, Chunk.MessageSequence, Payload.first(ChunkPrefixSize + Chunk.Data.size()), Output);
}

std::expected<std::size_t, BoundMessageError> EncodeBaselineEnd(BaselineEnd End, xr_span<u8> Output) noexcept
{
	xr_array<u8, EndSize> Payload;
	Detail::WriteLittleEndian(xr_span(Payload), End.BaselineId);
	return Detail::EncodeBoundMessage(MessageType::BaselineEnd, End.Identity, End.MessageSequence, Payload, Output);
}
}
