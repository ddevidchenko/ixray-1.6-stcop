#include "CombatantBarrier.h"
#include "JoinBarrierFields.h"

#include <algorithm>
#include <utility>

namespace Multiplayer::Wire
{
constexpr std::size_t ChunkHeaderSize = Detail::BarrierPayloadSize + sizeof(u32) + 2 * sizeof(u16);
constexpr std::size_t MaxChunkPayloadSize = ChunkHeaderSize + MaxBaselineChunkBytes;
static_assert(ChunkHeaderSize == 24 && MaxChunkPayloadSize <= MaxPayloadSize);
static_assert(StalkerCombatantRecordSize > HumanCombatantRecordSize);

static bool HasValidChunkData(const CombatantBarrierChunk& Chunk) noexcept
{
	if (Chunk.RecordCount == 0)
	{
		return Chunk.ByteCount == 0 && Chunk.ChunkIndex == 0 && Chunk.Data.empty();
	}
	const auto MinBytes = Chunk.RecordCount * (PayloadRecordHeaderSize + HumanCombatantRecordSize);
	const auto MaxBytes = Chunk.RecordCount * (PayloadRecordHeaderSize + StalkerCombatantRecordSize);
	constexpr auto StalkerExtensionBytes = StalkerCombatantRecordSize - HumanCombatantRecordSize;
	if (Chunk.RecordCount > MaxCombatants || Chunk.ByteCount < MinBytes || Chunk.ByteCount > MaxBytes ||
		(Chunk.ByteCount - MinBytes) % StalkerExtensionBytes != 0)
	{
		return false;
	}

	const u32 Offset = Chunk.ChunkIndex * MaxBaselineChunkBytes;
	return Offset < Chunk.ByteCount && Chunk.Data.size() == std::min(MaxBaselineChunkBytes, Chunk.ByteCount - Offset);
}

std::expected<CombatantBarrierChunk, BoundMessageError> DecodeCombatantBarrierChunk(xr_span<const u8> Bytes) noexcept
{
	const auto Frame = Detail::DecodeBoundMessage(Bytes, MessageType::CombatantBarrierChunk, ChunkHeaderSize, MaxChunkPayloadSize);
	if (!Frame)
	{
		return std::unexpected(Frame.error());
	}

	const auto Payload = Frame->Payload;
	// The bound decoder checked the complete fixed header before these reads.
	PayloadReader Fields(Payload.subspan<Detail::BarrierPayloadSize>());
	const CombatantBarrierChunk Chunk{
		Frame->Identity,
		Frame->MessageSequence,
		Detail::ReadJoinBarrier(Payload.first<Detail::BarrierPayloadSize>()),
		*Fields.ReadInteger<u32>(),
		*Fields.ReadInteger<u16>(),
		*Fields.ReadInteger<u16>(),
		Payload.subspan<ChunkHeaderSize>(),
	};
	if (!HasValidChunkData(Chunk))
	{
		return std::unexpected(BoundMessageError::InvalidPayload);
	}
	return Chunk;
}

std::expected<std::size_t, BoundMessageError> EncodeCombatantBarrierChunk(const CombatantBarrierChunk& Chunk, xr_span<u8> Output) noexcept
{
	if (!HasValidChunkData(Chunk))
	{
		return std::unexpected(BoundMessageError::InvalidPayload);
	}

	xr_array<u8, MaxChunkPayloadSize> Bytes;
	const auto Payload = xr_span(Bytes);
	Detail::WriteJoinBarrier(Payload.first<Detail::BarrierPayloadSize>(), Chunk.Barrier);
	PayloadWriter Fields(Payload.subspan<Detail::BarrierPayloadSize>());
	if (!Fields.WriteInteger(Chunk.ByteCount) || !Fields.WriteInteger(Chunk.RecordCount) || !Fields.WriteInteger(Chunk.ChunkIndex) ||
		!Fields.WriteBytes(Chunk.Data))
	{
		return std::unexpected(BoundMessageError::InvalidPayload);
	}
	return Detail::EncodeBoundMessage(MessageType::CombatantBarrierChunk, Chunk.Identity, Chunk.MessageSequence,
		Payload.first(Detail::BarrierPayloadSize + Fields.Written().size()), Output);
}
}

namespace Multiplayer
{
CombatantBarrierSnapshot::CombatantBarrierSnapshot(JoinBarrier Barrier, CombatantBaseline&& Roster) noexcept
	: Barrier(Barrier), Roster(std::move(Roster))
{
}

CombatantBarrierStaging::CombatantBarrierStaging(JoinIdentity Identity, JoinBarrier Barrier, xr_span<u8> Storage,
	BaselineStagingReservation&& Reservation) noexcept
	: Identity(Identity), Barrier(Barrier), Staging(Identity, Storage, std::move(Reservation))
{
}

std::expected<void, CombatantBarrierReceiveError> CombatantBarrierStaging::AddChunk(const Wire::CombatantBarrierChunk& Chunk) noexcept
{
	if (!Identity.IsBound() || Chunk.Identity != Identity)
	{
		return std::unexpected(CombatantBarrierReceiveError{CombatantBarrierScopeError::IdentityMismatch});
	}
	if (Chunk.Barrier != Barrier)
	{
		return std::unexpected(CombatantBarrierReceiveError{CombatantBarrierScopeError::BarrierMismatch});
	}
	if (!Wire::HasValidChunkData(Chunk))
	{
		return std::unexpected(CombatantBarrierReceiveError{BaselineStagingError::InvalidChunk});
	}

	const Wire::BaselineManifest Manifest{
		Barrier.BaselineId, Barrier.SnapshotTick, Barrier.EventWatermark, Chunk.ByteCount, Chunk.RecordCount,
		static_cast<u16>(Wire::GetBaselineChunkCount(Chunk.ByteCount)),
	};
	const auto Started = Staging.Begin({Identity, Chunk.MessageSequence, Manifest});
	if (!Started)
	{
		return std::unexpected(CombatantBarrierReceiveError{Started.error()});
	}
	if (!Chunk.Data.empty())
	{
		const auto Copied = Staging.AddChunk({Identity, Chunk.MessageSequence, Barrier.BaselineId, Chunk.ChunkIndex, Chunk.Data});
		if (!Copied)
		{
			return std::unexpected(CombatantBarrierReceiveError{Copied.error()});
		}
	}
	return {};
}

std::expected<CombatantBarrierSnapshot, CombatantBarrierFinishError> CombatantBarrierStaging::Finish() noexcept
{
	const auto Assembled = Staging.Finish({Identity, 0, Barrier.BaselineId});
	if (!Assembled)
	{
		return std::unexpected(CombatantBarrierFinishError{Assembled.error()});
	}
	auto Roster = CombatantBaseline::Decode(*Assembled, Identity, {Barrier.BaselineId, Barrier.SnapshotTick, Barrier.EventWatermark});
	if (!Roster)
	{
		return std::unexpected(CombatantBarrierFinishError{Roster.error()});
	}
	return CombatantBarrierSnapshot(Barrier, std::move(*Roster));
}
}
