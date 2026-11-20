#pragma once

#include "CatchUpEvent.h"
#include "ClientJoin.h"

#include <optional>

namespace Multiplayer
{
// The owner applies checked data atomically. A failed call leaves world state unchanged.
class IClientCatchUpApplier
{
public:
	virtual ~IClientCatchUpApplier() = default;
	virtual bool ApplyEvent(const CatchUpEvent& Event) noexcept = 0;
	virtual bool ApplyBarrier(JoinIdentity Peer, JoinBarrier Barrier) noexcept = 0;
};

enum class ClientCatchUpError
{
	IdentityMismatch,
	BaselineMismatch,
	BaselineNotApplied,
	UnexpectedSequence,
	BarrierMismatch,
	BarrierPending,
	InvalidState,
	ApplicationFailed,
	StaleBaseline,
};

// The owner serializes operations and application callbacks must not reenter the tracker.
class ClientCatchUp
{
public:
	ClientCatchUp(JoinIdentity Identity, BaselineBoundary Baseline) noexcept;
	ClientCatchUp(const ClientCatchUp&) = delete;
	ClientCatchUp& operator=(const ClientCatchUp&) = delete;

	[[nodiscard]] std::optional<u32> GetAppliedWatermark() const noexcept { return AppliedWatermark; }
	// Called after the selected capture has been imported successfully.
	[[nodiscard]] std::expected<void, ClientCatchUpError> ConfirmBaselineApplied(JoinIdentity Peer, BaselineBoundary Applied) noexcept;
	[[nodiscard]] std::expected<void, ClientCatchUpError> ApplyEvent(const CatchUpEvent& Event, IClientCatchUpApplier& Applier) noexcept;
	[[nodiscard]] std::expected<void, ClientCatchUpError> DeclareBarrier(JoinIdentity Peer, JoinBarrier Barrier) noexcept;
	// Once applied, the same barrier can produce an acknowledgement for a transport retry.
	[[nodiscard]] std::expected<Wire::ReadyAcknowledgement, ClientCatchUpError> CompleteBarrier(JoinIdentity Peer, JoinBarrier Barrier,
		u32 MessageSequence, IClientCatchUpApplier& Applier) noexcept;
	[[nodiscard]] std::expected<void, ClientCatchUpError> Rebase(JoinIdentity Peer, BaselineBoundary Replacement) noexcept;

private:
	[[nodiscard]] bool MatchesIdentity(JoinIdentity Peer) const noexcept;
	[[nodiscard]] std::optional<ClientCatchUpError> CheckScope(JoinIdentity Peer, u32 BaselineId) const noexcept;

	const JoinIdentity Identity;
	BaselineBoundary Baseline;
	std::optional<u32> AppliedWatermark;
	std::optional<JoinBarrier> DeclaredBarrier;
	bool BarrierApplied = false;
};
}
