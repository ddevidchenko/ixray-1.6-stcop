#pragma once

#include "BoundMessage.h"

namespace Multiplayer::Wire
{
inline constexpr u32 MaxBaselineChunkBytes = 1024;
inline constexpr u16 MaxBaselineChunks = 4096;
inline constexpr u16 MaxBaselineRecords = 8192;
inline constexpr u32 MaxBaselineBytes = MaxBaselineChunkBytes * MaxBaselineChunks;

struct BaselineManifest
{
	u32 BaselineId;
	u32 CaptureTick;
	u32 EventWatermark;
	u32 ByteCount;
	u16 RecordCount;
	u16 ChunkCount;

	bool operator==(const BaselineManifest&) const = default;
};

struct BaselineBegin
{
	JoinIdentity Identity;
	u32 MessageSequence;
	BaselineManifest Manifest;
};

// ChunkIndex is zero-based. The transfer owner checks the index and chunk length against the manifest.
// Data borrows the incoming datagram or the caller's outgoing source.
struct BaselineChunk
{
	JoinIdentity Identity;
	u32 MessageSequence;
	u32 BaselineId;
	u16 ChunkIndex;
	xr_span<const u8> Data;
};

struct BaselineEnd
{
	JoinIdentity Identity;
	u32 MessageSequence;
	u32 BaselineId;
};

[[nodiscard]] std::expected<BaselineBegin, BoundMessageError> DecodeBaselineBegin(xr_span<const u8> Bytes) noexcept;
[[nodiscard]] std::expected<BaselineChunk, BoundMessageError> DecodeBaselineChunk(xr_span<const u8> Bytes) noexcept;
[[nodiscard]] std::expected<BaselineEnd, BoundMessageError> DecodeBaselineEnd(xr_span<const u8> Bytes) noexcept;
// Failed encodes preserve the output. Chunk data may overlap the output.
[[nodiscard]] std::expected<std::size_t, BoundMessageError> EncodeBaselineBegin(const BaselineBegin& Begin, xr_span<u8> Output) noexcept;
[[nodiscard]] std::expected<std::size_t, BoundMessageError> EncodeBaselineChunk(const BaselineChunk& Chunk, xr_span<u8> Output) noexcept;
[[nodiscard]] std::expected<std::size_t, BoundMessageError> EncodeBaselineEnd(BaselineEnd End, xr_span<u8> Output) noexcept;
}
