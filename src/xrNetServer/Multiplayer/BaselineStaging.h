#pragma once

#include "BaselineTransfer.h"
#include "BaselineStagingBudget.h"

#include <bitset>
#include <optional>

namespace Multiplayer
{
enum class BaselineStagingError
{
	IdentityMismatch,
	InvalidManifest,
	InsufficientStorage,
	InvalidState,
	ConflictingManifest,
	BaselineMismatch,
	InvalidChunk,
	ConflictingChunk,
	Incomplete,
};

// Data borrows staging storage while its receiver and world remain current.
struct StagedBaseline
{
	JoinIdentity Identity;
	Wire::BaselineManifest Manifest;
	xr_span<const u8> Data;
};

class BaselineStaging
{
public:
	// The owner keeps Storage exclusive and alive, and returns it on the same turn it destroys the receiver.
	BaselineStaging(JoinIdentity Identity, xr_span<u8> Storage, BaselineStagingReservation&& Reservation) noexcept;
	BaselineStaging(const BaselineStaging&) = delete;
	BaselineStaging& operator=(const BaselineStaging&) = delete;

	[[nodiscard]] std::expected<void, BaselineStagingError> Begin(const Wire::BaselineBegin& Message) noexcept;
	[[nodiscard]] std::expected<void, BaselineStagingError> AddChunk(const Wire::BaselineChunk& Message) noexcept;
	[[nodiscard]] std::expected<StagedBaseline, BaselineStagingError> Finish(const Wire::BaselineEnd& Message) noexcept;
	// Only the connection owner advances the epoch.
	[[nodiscard]] bool AdvanceWorld(u32 WorldEpoch) noexcept;

private:
	[[nodiscard]] bool MatchesIdentity(JoinIdentity Other) const noexcept;
	[[nodiscard]] std::optional<BaselineStagingError> CheckActive(JoinIdentity Other, u32 BaselineId) const noexcept;

	JoinIdentity Identity;
	const xr_span<u8> Storage;
	BaselineStagingReservation Reservation;
	std::optional<Wire::BaselineManifest> Manifest;
	std::bitset<Wire::MaxBaselineChunks> ReceivedChunks;
	bool Completed = false;
};
}
