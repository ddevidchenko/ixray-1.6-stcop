#include "BaselineStaging.h"
#include "SerialSequence.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace Multiplayer
{
BaselineStaging::BaselineStaging(JoinIdentity Identity, xr_span<u8> Storage, BaselineStagingReservation&& Reservation) noexcept
	: Identity(Identity), Storage(Storage), Reservation(std::move(Reservation))
{
}

bool BaselineStaging::MatchesIdentity(JoinIdentity Other) const noexcept
{
	return Identity.IsBound() && Identity == Other;
}

std::optional<BaselineStagingError> BaselineStaging::CheckActive(JoinIdentity Other, u32 BaselineId) const noexcept
{
	if (!MatchesIdentity(Other))
	{
		return BaselineStagingError::IdentityMismatch;
	}
	if (!Manifest || Completed)
	{
		return BaselineStagingError::InvalidState;
	}
	if (BaselineId != Manifest->BaselineId)
	{
		return BaselineStagingError::BaselineMismatch;
	}
	return std::nullopt;
}

std::optional<BaselineStagingError> BaselineStaging::CheckManifest(const Wire::BaselineManifest& Next) const noexcept
{
	if (!Wire::IsValidBaselineManifest(Next))
	{
		return BaselineStagingError::InvalidManifest;
	}
	if (!Reservation.Covers(Storage.size()) || Next.ByteCount > Storage.size())
	{
		return BaselineStagingError::InsufficientStorage;
	}
	return std::nullopt;
}

std::expected<void, BaselineStagingError> BaselineStaging::Begin(const Wire::BaselineBegin& Message) noexcept
{
	if (!MatchesIdentity(Message.Identity))
	{
		return std::unexpected(BaselineStagingError::IdentityMismatch);
	}
	if (Completed)
	{
		return std::unexpected(BaselineStagingError::InvalidState);
	}
	if (Manifest)
	{
		if (*Manifest != Message.Manifest)
		{
			return std::unexpected(BaselineStagingError::ConflictingManifest);
		}
		return {};
	}
	if (const auto Error = CheckManifest(Message.Manifest))
	{
		return std::unexpected(*Error);
	}

	Manifest = Message.Manifest;
	return {};
}

std::expected<void, BaselineStagingError> BaselineStaging::Rebase(const Wire::BaselineBegin& Message) noexcept
{
	if (!MatchesIdentity(Message.Identity))
	{
		return std::unexpected(BaselineStagingError::IdentityMismatch);
	}
	if (!Manifest)
	{
		return std::unexpected(BaselineStagingError::InvalidState);
	}

	const auto& Next = Message.Manifest;
	const bool WatermarkIsCurrentOrNewer = Next.EventWatermark == Manifest->EventWatermark || IsSequenceNewer(Next.EventWatermark, Manifest->EventWatermark);
	if (!IsSequenceNewer(Next.BaselineId, Manifest->BaselineId) || !IsSequenceNewer(Next.CaptureTick, Manifest->CaptureTick) || !WatermarkIsCurrentOrNewer)
	{
		return std::unexpected(BaselineStagingError::StaleBaseline);
	}
	if (const auto Error = CheckManifest(Next))
	{
		return std::unexpected(*Error);
	}

	ClearTransfer();
	Manifest = Next;
	return {};
}

std::expected<void, BaselineStagingError> BaselineStaging::AddChunk(const Wire::BaselineChunk& Message) noexcept
{
	if (const auto Error = CheckActive(Message.Identity, Message.BaselineId))
	{
		return std::unexpected(*Error);
	}
	if (Message.ChunkIndex >= Manifest->ChunkCount)
	{
		return std::unexpected(BaselineStagingError::InvalidChunk);
	}

	constexpr std::size_t Stride = Wire::MaxBaselineChunkBytes;
	const std::size_t Offset = static_cast<std::size_t>(Message.ChunkIndex) * Stride;
	// A valid index in the canonical chunk count starts inside the declared bytes.
	const std::size_t Remaining = static_cast<std::size_t>(Manifest->ByteCount) - Offset;
	const std::size_t Size = std::min(Stride, Remaining);
	if (Message.Data.size() != Size)
	{
		return std::unexpected(BaselineStagingError::InvalidChunk);
	}
	const auto Destination = Storage.subspan(Offset, Size);
	if (ReceivedChunks.test(Message.ChunkIndex))
	{
		if (!std::ranges::equal(Message.Data, Destination))
		{
			return std::unexpected(BaselineStagingError::ConflictingChunk);
		}
		return {};
	}

	std::memmove(Destination.data(), Message.Data.data(), Size);
	ReceivedChunks.set(Message.ChunkIndex);
	return {};
}

std::expected<StagedBaseline, BaselineStagingError> BaselineStaging::Finish(const Wire::BaselineEnd& Message) noexcept
{
	if (const auto Error = CheckActive(Message.Identity, Message.BaselineId))
	{
		return std::unexpected(*Error);
	}
	if (ReceivedChunks.count() != Manifest->ChunkCount)
	{
		return std::unexpected(BaselineStagingError::Incomplete);
	}

	Completed = true;
	return StagedBaseline{Identity, *Manifest, Storage.first(Manifest->ByteCount)};
}

bool BaselineStaging::AdvanceWorld(u32 WorldEpoch) noexcept
{
	if (!Identity.IsBound() || WorldEpoch <= Identity.WorldEpoch)
	{
		return false;
	}
	Identity.WorldEpoch = WorldEpoch;
	ClearTransfer();
	return true;
}

void BaselineStaging::ClearTransfer() noexcept
{
	Manifest.reset();
	ReceivedChunks.reset();
	Completed = false;
}
}
