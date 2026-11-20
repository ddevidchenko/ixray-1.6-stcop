#include "CatchUpEventQueue.h"
#include "WireEnvelope.h"

#include <algorithm>
#include <limits>

namespace Multiplayer
{
constexpr std::size_t MaxCatchUpEvents = 4096;
static_assert(Wire::MaxMessageSize <= std::numeric_limits<u16>::max());

// Amount is bounded by the storage size, so at most one wrap is needed.
static std::size_t AdvanceOffset(std::size_t Offset, std::size_t Amount, std::size_t Capacity) noexcept
{
	const std::size_t ToEnd = Capacity - Offset;
	return Amount < ToEnd ? Offset + Amount : Amount - ToEnd;
}

CatchUpEventQueue::CatchUpEventQueue(JoinIdentity Identity, BaselineBoundary Baseline, xr_span<u16> Lengths, xr_span<u8> Storage) noexcept
	: Identity(Identity), BaselineId(Baseline.BaselineId), Lengths(Lengths.first(std::min(Lengths.size(), MaxCatchUpEvents))), Storage(Storage),
	  ObservedWatermark(Baseline.EventWatermark), SubmittedWatermark(Baseline.EventWatermark)
{
}

std::optional<CatchUpEventError> CatchUpEventQueue::CheckScope(const CatchUpEvent& Event) const noexcept
{
	if (!Identity.IsBound() || Event.Identity != Identity)
	{
		return CatchUpEventError::IdentityMismatch;
	}
	if (Event.BaselineId != BaselineId)
	{
		return CatchUpEventError::BaselineMismatch;
	}
	return std::nullopt;
}

std::expected<void, CatchUpEventError> CatchUpEventQueue::Append(const CatchUpEvent& Event) noexcept
{
	if (const auto Error = CheckScope(Event))
	{
		return std::unexpected(*Error);
	}
	if (Event.Sequence != ObservedWatermark + 1u)
	{
		return std::unexpected(CatchUpEventError::UnexpectedSequence);
	}
	const std::size_t Size = Event.Bytes.size();
	if (Size == 0 || Size > Wire::MaxMessageSize)
	{
		return std::unexpected(CatchUpEventError::InvalidEventSize);
	}

	ObservedWatermark = Event.Sequence;
	if (NeedsRebase || PendingCount == Lengths.size() || Size > Storage.size() - UsedBytes)
	{
		NeedsRebase = true;
		return std::unexpected(CatchUpEventError::RebaseRequired);
	}

	const std::size_t Tail = AdvanceOffset(HeadOffset, UsedBytes, Storage.size());
	const std::size_t FirstSize = std::min(Size, Storage.size() - Tail);
	std::ranges::copy(Event.Bytes.first(FirstSize), Storage.subspan(Tail).begin());
	std::ranges::copy(Event.Bytes.subspan(FirstSize), Storage.begin());
	Lengths[(HeadIndex + PendingCount) % Lengths.size()] = static_cast<u16>(Size);
	++PendingCount;
	UsedBytes += Size;
	return {};
}

std::expected<CatchUpEvent, CatchUpEventError> CatchUpEventQueue::Peek(xr_span<u8> Output) const noexcept
{
	if (NeedsRebase)
	{
		return std::unexpected(CatchUpEventError::RebaseRequired);
	}
	if (PendingCount == 0)
	{
		return std::unexpected(CatchUpEventError::Empty);
	}
	const std::size_t Size = Lengths[HeadIndex];
	if (Output.size() < Size)
	{
		return std::unexpected(CatchUpEventError::OutputTooSmall);
	}

	const std::size_t FirstSize = std::min(Size, Storage.size() - HeadOffset);
	std::ranges::copy(Storage.subspan(HeadOffset, FirstSize), Output.begin());
	std::ranges::copy(Storage.first(Size - FirstSize), Output.subspan(FirstSize).begin());
	return CatchUpEvent{Identity, BaselineId, SubmittedWatermark + 1u, Output.first(Size)};
}

std::expected<void, CatchUpEventError> CatchUpEventQueue::CommitSend(const CatchUpEvent& Event) noexcept
{
	if (const auto Error = CheckScope(Event))
	{
		return std::unexpected(*Error);
	}
	if (NeedsRebase)
	{
		return std::unexpected(CatchUpEventError::RebaseRequired);
	}
	if (PendingCount == 0)
	{
		return std::unexpected(CatchUpEventError::Empty);
	}
	if (Event.Sequence != SubmittedWatermark + 1u)
	{
		return std::unexpected(CatchUpEventError::UnexpectedSequence);
	}

	const std::size_t Size = Lengths[HeadIndex];
	HeadOffset = AdvanceOffset(HeadOffset, Size, Storage.size());
	HeadIndex = (HeadIndex + 1) % Lengths.size();
	--PendingCount;
	UsedBytes -= Size;
	SubmittedWatermark = Event.Sequence;
	return {};
}
}
