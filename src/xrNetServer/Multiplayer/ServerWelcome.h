#pragma once

#include "CompatibilityId.h"

#include <expected>

namespace Multiplayer::Wire
{
enum class ServerWelcomeError
{
	InvalidEnvelope,
	WrongMessageType,
	UnboundWelcome,
	TruncatedPayload,
	InvalidMapId,
	InvalidRates,
	TrailingPayload,
	OutputTooSmall,
};

struct WelcomeRates
{
	u32 GameplayMilliHz = 0;
	u32 PhysicsMilliHz = 0;
	u32 SnapshotMilliHz = 0;
	u32 InputMilliHz = 0;

	bool operator==(const WelcomeRates&) const = default;
};

struct WelcomeRules
{
	u32 RespawnDelayMs = 0;
	u32 SpawnProtectionMs = 0;
	u32 RoundTimeLimitMs = 0;
	u32 RoundFragLimit = 0;

	bool operator==(const WelcomeRules&) const = default;
};

struct ServerWelcome
{
	u64 ServerSession = 0;
	u32 WorldEpoch = 0;
	u32 ConnectionEpoch = 0;
	u32 MessageSequence = 0;
	CompatibilityId ContentId{};
	CompatibilityId RulesId{};
	xr_string_view MapId;
	WelcomeRates Rates;
	WelcomeRules Rules;
};

// MapId borrows the supplied complete message.
[[nodiscard]] std::expected<ServerWelcome, ServerWelcomeError> DecodeServerWelcome(xr_span<const u8> Bytes) noexcept;

// MapId may overlap the output. A failed encode leaves the output unchanged.
[[nodiscard]] std::expected<std::size_t, ServerWelcomeError> EncodeServerWelcome(const ServerWelcome& Welcome, xr_span<u8> Output) noexcept;
}
