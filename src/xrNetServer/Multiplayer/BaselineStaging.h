#pragma once

#include "BaselineTransfer.h"

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

// Data borrows reserved staging storage and is valid until the owner advances the world or releases the storage.
struct StagedBaseline
{
	JoinIdentity Identity;
	Wire::BaselineManifest Manifest;
	xr_span<const u8> Data;
};

class BaselineStaging
{
public:
	// The serialized owner keeps the storage alive and reserves it for this receiver.
	BaselineStaging(JoinIdentity Identity, xr_span<u8> Storage) noexcept;
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
	std::optional<Wire::BaselineManifest> Manifest;
	std::bitset<Wire::MaxBaselineChunks> ReceivedChunks;
	bool Completed = false;
};
}
