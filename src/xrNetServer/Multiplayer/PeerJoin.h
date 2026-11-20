#pragma once

#include "JoinTypes.h"
#include "../../xrCore/_stl_extensions_nonalloc.h"

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
	BaselineMismatch,
	StaleBaseline,
	RebaseLimitExceeded,
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
	// The server owner selects the initial capture before declaring a barrier.
	[[nodiscard]] std::expected<void, PeerJoinError> SelectBaseline(JoinIdentity Peer, BaselineBoundary Boundary, Clock::time_point Now) noexcept;
	// A successful replacement clears the barrier and preserves the synchronization deadline.
	[[nodiscard]] std::expected<void, PeerJoinError> Rebase(JoinIdentity Peer, BaselineBoundary Boundary, Clock::time_point Now) noexcept;
	// The server owner declares the outstanding barrier after staging it for this peer.
	[[nodiscard]] std::expected<void, PeerJoinError> DeclareBarrier(JoinIdentity World, JoinBarrier Barrier, Clock::time_point Now) noexcept;
	[[nodiscard]] std::expected<void, PeerJoinError> AcknowledgeBarrier(JoinIdentity Peer, JoinBarrier Barrier, Clock::time_point Now) noexcept;
	// Called by the server owner on a same-map round reset.
	[[nodiscard]] std::expected<void, PeerJoinError> AdvanceWorld(u32 WorldEpoch, Clock::time_point Now) noexcept;

private:
	[[nodiscard]] std::optional<PeerJoinError> CheckPhase(PeerJoinState Required, JoinIdentity Subject, Clock::time_point Now) noexcept;

	void ClearBaseline() noexcept;

	JoinIdentity Identity;
	Clock::time_point Deadline;
	PeerJoinState State;
	std::optional<BaselineBoundary> SelectedBaseline;
	std::optional<JoinBarrier> DeclaredBarrier;
	xr_array<std::optional<Clock::time_point>, 3> RebaseTimes{};
	std::size_t NextRebaseSlot = 0;
};
}
