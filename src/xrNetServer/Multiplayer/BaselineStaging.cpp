#include "BaselineStaging.h"

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
	if (!Wire::IsValidBaselineManifest(Message.Manifest))
	{
		return std::unexpected(BaselineStagingError::InvalidManifest);
	}
	if (!Reservation.Covers(Storage.size()) || Message.Manifest.ByteCount > Storage.size())
	{
		return std::unexpected(BaselineStagingError::InsufficientStorage);
	}

	Manifest = Message.Manifest;
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
	Manifest.reset();
	ReceivedChunks.reset();
	Completed = false;
	return true;
}
}
