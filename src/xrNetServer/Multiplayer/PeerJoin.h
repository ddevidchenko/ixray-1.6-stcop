#pragma once

#include "JoinTypes.h"

#include <chrono>
#include <expected>
#include <optional>

namespace Multiplayer
{
enum class PeerJoinState
{
	Loading,
	Synchronizing,
	ReadySpectator,
	Closing,
};

enum class PeerJoinError
{
	InvalidState,
	IdentityMismatch,
	BarrierMismatch,
	LoadingTimedOut,
	SynchronizationTimedOut,
};

// The connection owner serializes events and supplies monotonic times.
class PeerJoin
{
public:
	using Clock = std::chrono::steady_clock;

	// The connection owner starts this after queuing a successful welcome.
	PeerJoin(JoinIdentity Identity, Clock::time_point AdmittedAt) noexcept;
	PeerJoin(const PeerJoin&) = delete;
	PeerJoin& operator=(const PeerJoin&) = delete;

	[[nodiscard]] PeerJoinState GetState() const noexcept { return State; }
	// Expiry takes precedence at equality and reports a terminal reason once.
	[[nodiscard]] std::optional<PeerJoinError> Expire(Clock::time_point Now) noexcept;
	[[nodiscard]] std::expected<void, PeerJoinError> ConfirmLoaded(JoinIdentity Peer, Clock::time_point Now) noexcept;
	// The server owner declares the outstanding barrier after staging it for this peer.
	[[nodiscard]] std::expected<void, PeerJoinError> DeclareBarrier(JoinIdentity World, JoinBarrier Barrier, Clock::time_point Now) noexcept;
	[[nodiscard]] std::expected<void, PeerJoinError> AcknowledgeBarrier(JoinIdentity Peer, JoinBarrier Barrier, Clock::time_point Now) noexcept;
	// Called by the server owner on a same-map round reset.
	[[nodiscard]] std::expected<void, PeerJoinError> AdvanceWorld(u32 WorldEpoch, Clock::time_point Now) noexcept;

private:
	[[nodiscard]] std::optional<PeerJoinError> CheckPhase(PeerJoinState Required, JoinIdentity Subject, Clock::time_point Now) noexcept;

	JoinIdentity Identity;
	Clock::time_point Deadline;
	PeerJoinState State;
	std::optional<JoinBarrier> DeclaredBarrier;
};
}
