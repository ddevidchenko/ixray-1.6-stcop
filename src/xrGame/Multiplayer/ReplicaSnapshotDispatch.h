#pragma once

#include "CombatantReplicas.h"
#include "../../xrNetServer/Multiplayer/ClientCatchUp.h"
#include "../../xrNetServer/Multiplayer/CombatantSnapshot.h"

#include <expected>

namespace Multiplayer
{
// The world owner resolves content and applies each complete record atomically.
// Failure leaves the body unchanged. Callbacks run on the owner turn without reentering these trackers.
class ICombatantReplicaApplier
{
public:
	virtual ~ICombatantReplicaApplier() = default;
	virtual bool ApplySnapshot(const Wire::SnapshotBatchHeader& Header, const Wire::HumanCombatantSnapshot& Snapshot) noexcept = 0;
	virtual bool ApplySnapshot(const Wire::SnapshotBatchHeader& Header, const Wire::StalkerCombatantSnapshot& Snapshot) noexcept = 0;
};

enum class ReplicaSnapshotBatchError
{
	IdentityMismatch,
	Synchronizing,
	StaleTick,
};

class ReplicaSnapshotBatchReport
{
public:
	using RecordResult = std::expected<void, ReplicaSnapshotError>;

	// Results are in the same order as the decoded records.
	[[nodiscard]] xr_span<const RecordResult> GetRecordResults() const noexcept { return xr_span(Results).first(Count); }

private:
	friend std::expected<ReplicaSnapshotBatchReport, ReplicaSnapshotBatchError> DispatchLiveCombatantBatch(
		const Wire::CombatantSnapshotBatch& Batch, const ClientCatchUp& CatchUp, CombatantReplicas& Replicas,
		ICombatantReplicaApplier& Applier) noexcept;

	xr_array<RecordResult, Wire::MaxSnapshotRecords> Results{};
	std::size_t Count = 0;
};

namespace Detail
{
[[nodiscard]] inline ReplicaSnapshotBatchReport::RecordResult DispatchLiveCombatantRecord(JoinIdentity Receiver,
	std::optional<u32> Watermark, const Wire::SnapshotBatchHeader& Header, const Wire::CombatantSnapshot& Record,
	CombatantReplicas& Replicas, ICombatantReplicaApplier& Applier) noexcept
{
	return std::visit([&](const auto& Snapshot) noexcept
	{
		const auto& State = Snapshot.State;
		return Replicas.ApplyLiveSnapshot(Receiver, Watermark,
			{Header.Identity, State.Participant, State.Body, Header.ServerTick, Header.RequiredEventWatermark}, [&]() noexcept
			{
				return Applier.ApplySnapshot(Header, Snapshot);
			});
	}, Record);
}
}

// The owner serializes dispatch with catch-up and lifecycle changes and keeps the batch stable during callbacks.
[[nodiscard]] inline std::expected<ReplicaSnapshotBatchReport, ReplicaSnapshotBatchError> DispatchLiveCombatantBatch(
	const Wire::CombatantSnapshotBatch& Batch, const ClientCatchUp& CatchUp, CombatantReplicas& Replicas,
	ICombatantReplicaApplier& Applier) noexcept
{
	const auto Receiver = CatchUp.GetIdentity();
	const WorldIdentity World{Receiver.ServerSession, Receiver.WorldEpoch};
	const auto& Header = Batch.GetHeader();
	if (!Receiver.IsBound() || Header.Identity != Receiver || Replicas.GetWorldIdentity() != World)
	{
		return std::unexpected(ReplicaSnapshotBatchError::IdentityMismatch);
	}

	const auto Barrier = CatchUp.GetAppliedBarrier();
	if (!Barrier)
	{
		return std::unexpected(ReplicaSnapshotBatchError::Synchronizing);
	}
	if (!IsSequenceNewer(Header.ServerTick, Barrier->SnapshotTick))
	{
		return std::unexpected(ReplicaSnapshotBatchError::StaleTick);
	}

	ReplicaSnapshotBatchReport Report;
	const auto Watermark = CatchUp.GetAppliedWatermark();
	for (const auto& Record : Batch.GetRecords())
	{
		Report.Results[Report.Count++] = Detail::DispatchLiveCombatantRecord(Receiver, Watermark, Header, Record, Replicas, Applier);
	}
	return Report;
}
}
