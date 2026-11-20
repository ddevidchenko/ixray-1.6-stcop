#pragma once

#include "ReplicaSnapshotDispatch.h"

#include <algorithm>
#include <bitset>
#include <utility>

namespace Multiplayer
{
enum class SnapshotRetryStatus
{
	NoRepair,
	RepairRequired,
};

struct ReplicaSnapshotReceiveReport
{
	ReplicaSnapshotBatchReport Dispatch;
	SnapshotRetryStatus Retry;
};

// The client owner serializes receipt, retry, catch-up and replica lifecycle changes.
class ReplicaSnapshotQueue
{
public:
	static constexpr std::size_t MaxPendingBatches = 8;

	ReplicaSnapshotQueue() = default;
	ReplicaSnapshotQueue(const ReplicaSnapshotQueue&) = delete;
	ReplicaSnapshotQueue& operator=(const ReplicaSnapshotQueue&) = delete;

	[[nodiscard]] std::expected<ReplicaSnapshotReceiveReport, ReplicaSnapshotBatchError> Receive(const Wire::CombatantSnapshotBatch& Batch,
		const ClientCatchUp& CatchUp, CombatantReplicas& Replicas, ICombatantReplicaApplier& Applier) noexcept
	{
		UpdateScope(CatchUp, Replicas);
		const auto Report = DispatchLiveCombatantBatch(Batch, CatchUp, Replicas, Applier);
		if (!Report)
		{
			return std::unexpected(Report.error());
		}

		const auto& Header = Batch.GetHeader();
		const auto Records = Batch.GetRecords();
		const auto Results = Report->GetRecordResults();
		const auto Watermark = CatchUp.GetAppliedWatermark();
		Selection Deferred;
		xr_array<Selection, MaxPendingBatches> Replaced{};
		bool NeedsRepair = false;
		for (std::size_t Index = 0; Index < Records.size(); ++Index)
		{
			const auto& Result = Results[Index];
			if (Result)
			{
				RetireApplied(Header.ServerTick, Records[Index]);
			}
			else if (ShouldDefer(Result.error(), Header.RequiredEventWatermark, Watermark))
			{
				Deferred[Index] = PlanRetention(Header, Records[Index], Replaced);
			}
			else
			{
				NeedsRepair |= Result.error() == ReplicaSnapshotError::ApplicationFailed;
			}
		}
		if (Deferred.any() && !Retain(Batch, Deferred, Replaced))
		{
			NeedsRepair = true;
		}
		return ReplicaSnapshotReceiveReport{*Report, NeedsRepair ? SnapshotRetryStatus::RepairRequired : SnapshotRetryStatus::NoRepair};
	}

	[[nodiscard]] SnapshotRetryStatus RetryPending(const ClientCatchUp& CatchUp, CombatantReplicas& Replicas,
		ICombatantReplicaApplier& Applier) noexcept
	{
		UpdateScope(CatchUp, Replicas);
		if (!Barrier)
		{
			return SnapshotRetryStatus::NoRepair;
		}

		xr_array<std::size_t, MaxPendingBatches> Slots;
		std::size_t Count = 0;
		for (std::size_t Slot = 0; Slot < Pending.size(); ++Slot)
		{
			if (Pending[Slot])
			{
				Slots[Count++] = Slot;
			}
		}
		const auto Order = xr_span(Slots).first(Count);
		const auto Watermark = CatchUp.GetAppliedWatermark();
		// Distances from the barrier form a total tick order across wrap. Prefer event-ready copies at a shared tick.
		std::ranges::sort(Order, std::ranges::greater{}, [this, Watermark](std::size_t Slot)
		{
			const auto& Header = Pending[Slot]->Batch.GetHeader();
			const bool EventsReady = Watermark && IsSequenceCurrentOrNewer(*Watermark, Header.RequiredEventWatermark);
			return std::pair{Header.ServerTick - Barrier->SnapshotTick, EventsReady};
		});

		bool NeedsRepair = false;
		for (const auto Slot : Order)
		{
			auto& Entry = *Pending[Slot];
			const auto& Header = Entry.Batch.GetHeader();
			const auto Records = Entry.Batch.GetRecords();
			for (std::size_t Index = 0; Index < Records.size(); ++Index)
			{
				if (!Entry.Selected[Index])
				{
					continue;
				}
				const auto Result = Detail::DispatchLiveCombatantRecord(Identity, Watermark, Header, Records[Index], Replicas, Applier);
				if (!Result && ShouldDefer(Result.error(), Header.RequiredEventWatermark, Watermark))
				{
					continue;
				}
				NeedsRepair |= !Result && Result.error() == ReplicaSnapshotError::ApplicationFailed;
				Entry.Selected.reset(Index);
			}
			if (Entry.Selected.none())
			{
				Pending[Slot].reset();
			}
		}
		return NeedsRepair ? SnapshotRetryStatus::RepairRequired : SnapshotRetryStatus::NoRepair;
	}

	[[nodiscard]] std::size_t GetPendingBatchCount() const noexcept
	{
		return static_cast<std::size_t>(std::ranges::count_if(Pending, [](const auto& Entry) { return Entry.has_value(); }));
	}

	[[nodiscard]] std::size_t GetPendingRecordCount() const noexcept
	{
		std::size_t Count = 0;
		for (const auto& Entry : Pending)
		{
			if (Entry)
			{
				Count += Entry->Selected.count();
			}
		}
		return Count;
	}

private:
	using Selection = std::bitset<Wire::MaxSnapshotRecords>;

	struct PendingBatch
	{
		Wire::CombatantSnapshotBatch Batch;
		Selection Selected;
	};

	void UpdateScope(const ClientCatchUp& CatchUp, const CombatantReplicas& Replicas) noexcept
	{
		const auto Current = CatchUp.GetIdentity();
		const WorldIdentity World{Current.ServerSession, Current.WorldEpoch};
		const auto CurrentBarrier = Replicas.GetWorldIdentity() == World ? CatchUp.GetAppliedBarrier() : std::nullopt;
		if (Current == Identity && CurrentBarrier == Barrier)
		{
			return;
		}

		for (auto& Entry : Pending)
		{
			Entry.reset();
		}
		Identity = Current;
		Barrier = CurrentBarrier;
	}

	[[nodiscard]] static bool ShouldDefer(ReplicaSnapshotError Error, u32 Required, std::optional<u32> Watermark) noexcept
	{
		return Error == ReplicaSnapshotError::EventsPending ||
			(Error == ReplicaSnapshotError::UnknownReference &&
				(!Watermark || IsSequenceCurrentOrNewer(Required, *Watermark) || IsSequenceNewer(*Watermark, Required)));
	}

	[[nodiscard]] static std::optional<std::size_t> FindBody(const PendingBatch& Entry, const Wire::CombatantSnapshot& Record) noexcept
	{
		const auto& Body = Wire::GetPublicCombatantState(Record);
		const auto Records = Entry.Batch.GetRecords();
		for (std::size_t Index = 0; Index < Records.size(); ++Index)
		{
			if (Entry.Selected[Index])
			{
				const auto& Held = Wire::GetPublicCombatantState(Records[Index]);
				if (Body.Participant == Held.Participant && Body.Body == Held.Body)
				{
					return Index;
				}
			}
		}
		return std::nullopt;
	}

	void RetireApplied(u32 AppliedTick, const Wire::CombatantSnapshot& Record) noexcept
	{
		for (auto& Entry : Pending)
		{
			if (!Entry || !IsSequenceCurrentOrNewer(AppliedTick, Entry->Batch.GetHeader().ServerTick))
			{
				continue;
			}
			if (const auto Index = FindBody(*Entry, Record))
			{
				Entry->Selected.reset(*Index);
				if (Entry->Selected.none())
				{
					Entry.reset();
				}
			}
		}
	}

	// Each body and event prerequisite pair has at most one retained record.
	[[nodiscard]] bool PlanRetention(const Wire::SnapshotBatchHeader& Header, const Wire::CombatantSnapshot& Record,
		xr_array<Selection, MaxPendingBatches>& Replaced) const noexcept
	{
		for (std::size_t Slot = 0; Slot < Pending.size(); ++Slot)
		{
			const auto& Entry = Pending[Slot];
			if (!Entry || Entry->Batch.GetHeader().RequiredEventWatermark != Header.RequiredEventWatermark)
			{
				continue;
			}
			if (const auto Index = FindBody(*Entry, Record))
			{
				if (!IsSequenceNewer(Header.ServerTick, Entry->Batch.GetHeader().ServerTick))
				{
					return false;
				}
				Replaced[Slot].set(*Index);
				break;
			}
		}
		return true;
	}

	[[nodiscard]] bool Retain(const Wire::CombatantSnapshotBatch& Batch, Selection Deferred,
		const xr_array<Selection, MaxPendingBatches>& Replaced) noexcept
	{
		std::optional<std::size_t> Available;
		for (std::size_t Slot = 0; Slot < Pending.size(); ++Slot)
		{
			if (!Pending[Slot] || (Pending[Slot]->Selected & ~Replaced[Slot]).none())
			{
				Available = Slot;
				break;
			}
		}
		if (!Available)
		{
			return false;
		}

		for (std::size_t Slot = 0; Slot < Pending.size(); ++Slot)
		{
			if (auto& Entry = Pending[Slot])
			{
				Entry->Selected &= ~Replaced[Slot];
				if (Entry->Selected.none())
				{
					Entry.reset();
				}
			}
		}
		Pending[*Available].emplace(Batch, Deferred);
		return true;
	}

	JoinIdentity Identity;
	std::optional<JoinBarrier> Barrier;
	xr_array<std::optional<PendingBatch>, MaxPendingBatches> Pending;
};
}
