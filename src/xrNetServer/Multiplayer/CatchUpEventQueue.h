#pragma once

#include "CatchUpEvent.h"

#include <expected>
#include <optional>

namespace Multiplayer
{
enum class CatchUpEventError
{
	IdentityMismatch,
	BaselineMismatch,
	UnexpectedSequence,
	InvalidEventSize,
	RebaseRequired,
	Empty,
	OutputTooSmall,
};

// The owner serializes queue operations and supplies exclusive, disjoint backing spans.
class CatchUpEventQueue
{
public:
	CatchUpEventQueue(JoinIdentity Identity, BaselineBoundary Baseline, xr_span<u16> Lengths, xr_span<u8> Storage) noexcept;
	CatchUpEventQueue(const CatchUpEventQueue&) = delete;
	CatchUpEventQueue& operator=(const CatchUpEventQueue&) = delete;

	// Frame bytes and peek output must be disjoint from the queue storage.
	[[nodiscard]] std::expected<void, CatchUpEventError> Append(const CatchUpEvent& Event) noexcept;
	// Copies the head without retiring it. The returned event borrows Output.
	[[nodiscard]] std::expected<CatchUpEvent, CatchUpEventError> Peek(xr_span<u8> Output) const noexcept;
	// Retire the peeked head on the same turn the reliable transport queue accepts it.
	[[nodiscard]] std::expected<void, CatchUpEventError> CommitSend(const CatchUpEvent& Event) noexcept;
	[[nodiscard]] u32 GetObservedWatermark() const noexcept { return ObservedWatermark; }
	[[nodiscard]] u32 GetSubmittedWatermark() const noexcept { return SubmittedWatermark; }

private:
	[[nodiscard]] std::optional<CatchUpEventError> CheckScope(const CatchUpEvent& Event) const noexcept;

	const JoinIdentity Identity;
	const u32 BaselineId;
	const xr_span<u16> Lengths;
	const xr_span<u8> Storage;
	std::size_t HeadIndex = 0;
	std::size_t HeadOffset = 0;
	std::size_t PendingCount = 0;
	std::size_t UsedBytes = 0;
	u32 ObservedWatermark;
	u32 SubmittedWatermark;
	bool NeedsRebase = false;
};
}
