#pragma once

#include "CombatantCaptureApply.h"
#include "../../xrNetServer/Multiplayer/ClientCatchUp.h"
#include "../../xrNetServer/Multiplayer/CombatantBarrier.h"

#include <expected>
#include <variant>

namespace Multiplayer
{
class ICombatantBarrierApplier
{
public:
	virtual ~ICombatantBarrierApplier() = default;
	// Resolve content and replace the public roster atomically. Failure leaves the previous world intact.
	// The callback must not reenter the application or either tracker.
	virtual bool ApplyBarrier(const CombatantBarrierSnapshot& Snapshot) noexcept = 0;
};

using CombatantBarrierApplyError = std::variant<ClientCatchUpError, ReplicaBaselineError>;

// The client owner serializes application with catch-up, lifecycle and live updates.
[[nodiscard]] inline std::expected<Wire::ReadyAcknowledgement, CombatantBarrierApplyError> ApplyCombatantBarrier(
	const CombatantBarrierSnapshot& Snapshot, u32 MessageSequence, ClientCatchUp& CatchUp, CombatantReplicas& Replicas,
	ICombatantBarrierApplier& Applier) noexcept
{
	const auto Receiver = CatchUp.GetIdentity();
	const WorldIdentity World{Receiver.ServerSession, Receiver.WorldEpoch};
	if (Replicas.GetWorldIdentity() != World)
	{
		return std::unexpected(CombatantBarrierApplyError{ReplicaBaselineError::WorldMismatch});
	}

	const auto Barrier = Snapshot.GetBarrier();
	std::expected<void, ReplicaBaselineError> ReplicasApplied;
	const auto Ack = CatchUp.CompleteBarrier(Snapshot.GetIdentity(), Barrier, MessageSequence, [&]() noexcept
	{
		ReplicasApplied = Detail::ApplyCombatantCapture(Replicas, World, Barrier.SnapshotTick, Snapshot.GetRecords(), [&]() noexcept
		{
			return Applier.ApplyBarrier(Snapshot);
		});
		return ReplicasApplied.has_value();
	});
	if (!ReplicasApplied)
	{
		return std::unexpected(CombatantBarrierApplyError{ReplicasApplied.error()});
	}
	if (!Ack)
	{
		return std::unexpected(CombatantBarrierApplyError{Ack.error()});
	}
	return *Ack;
}
}
