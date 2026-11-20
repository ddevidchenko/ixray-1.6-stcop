#include "../../../xrGame/Multiplayer/CombatantReplicas.h"
#include "ClientCatchUp.h"

#include <cstdio>
#include <limits>
#include <memory>

namespace ReplicaSnapshotTests
{
using namespace Multiplayer;

constexpr JoinIdentity Receiver{0x123456789abcdef0ull, 0, 7};
constexpr WorldIdentity World{Receiver.ServerSession, Receiver.WorldEpoch};
constexpr ParticipantId Participant{71};
constexpr EntityRef Body{29, 11, 4};

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

static bool ExpectError(const char* Step, const std::expected<void, ReplicaSnapshotError>& Result, ReplicaSnapshotError Error)
{
	return Expect(Step, !Result && Result.error() == Error);
}

static bool CheckLossAndReordering()
{
	auto History = std::make_unique<RetiredCombatantGenerations>();
	CombatantReplicas Replicas(World, *History);
	if (!Expect("the owner can register a body with imported state", Replicas.RegisterBody(World, Participant, Body, 100u)))
	{
		return false;
	}

	int State = 1000;
	int Applies = 0;
	auto Receive = [&](u32 Tick, int Position)
	{
		return Replicas.ApplyLiveSnapshot(Receiver, 46u, {Receiver, Participant, Body, Tick, 46}, [&]() noexcept
		{
			++Applies;
			State = Position;
			return true;
		});
	};

	bool Passed = Expect("duplicate registration preserves the imported tick", Replicas.RegisterBody(World, Participant, Body, 90u));
	Passed &= ExpectError("a snapshot at the already imported tick cannot apply", Receive(100, 9999), ReplicaSnapshotError::StaleTick);
	Passed &= Expect("the first useful snapshot applies", Receive(101, 1010).has_value());
	Passed &= Expect("a complete snapshot recovers from a dropped tick", Receive(103, 1030).has_value() && State == 1030);
	const auto Reordered = Receive(102, 1020);
	Passed &= Expect("a delayed snapshot cannot rewind applied state",
		!Reordered && Reordered.error() == ReplicaSnapshotError::StaleTick && State == 1030);
	const auto Duplicate = Receive(103, 9999);
	Passed &= Expect("the same tick applies at most once for this body",
		!Duplicate && Duplicate.error() == ReplicaSnapshotError::StaleTick && State == 1030 && Applies == 2);
	return Passed;
}

class CatchUpFixture final : public IClientCatchUpApplier
{
public:
	bool ApplyEvent(const CatchUpEvent&) noexcept override { return true; }
	bool ApplyBarrier(JoinIdentity, JoinBarrier) noexcept override { return true; }
};

static bool CheckCausalityAndRetry()
{
	constexpr BaselineBoundary Baseline{4, 100, 46};
	constexpr JoinBarrier Barrier{4, 2, 100, 46};
	constexpr u8 EventBytes[] = {1};
	constexpr ReplicaSnapshot Snapshot{Receiver, Participant, Body, 103, 47};
	constexpr ReplicaSnapshot Independent{Receiver, Participant, Body, 102, 46};
	CatchUpFixture Fixture;
	ClientCatchUp CatchUp(Receiver, Baseline);
	auto History = std::make_unique<RetiredCombatantGenerations>();
	CombatantReplicas Replicas(World, *History);
	if (!Expect("a body has an authoritative initial tick", Replicas.RegisterBody(World, Participant, Body, 100u)))
	{
		return false;
	}

	bool ReadyToApply = false;
	int Attempts = 0;
	auto Receive = [&](const ReplicaSnapshot& Update)
	{
		return Replicas.ApplyLiveSnapshot(Receiver, CatchUp.GetAppliedWatermark(), Update, [&]() noexcept
		{
			++Attempts;
			return ReadyToApply;
		});
	};
	bool Passed = ExpectError("no applied event frontier means snapshots must wait", Receive(Snapshot), ReplicaSnapshotError::EventsPending);
	Passed &= Expect("the baseline and finite barrier establish the applied frontier",
		CatchUp.ConfirmBaselineApplied(Receiver, Baseline).has_value() && CatchUp.DeclareBarrier(Receiver, Barrier).has_value() &&
		CatchUp.CompleteBarrier(Receiver, Barrier, 5, Fixture).has_value());
	Passed &= ExpectError("a snapshot cannot pass a required event", Receive(Snapshot), ReplicaSnapshotError::EventsPending);
	Passed &= Expect("waiting leaves the application callback untouched", Attempts == 0);

	Passed &= Expect("the missing event advances the real catch-up tracker",
		CatchUp.ApplyEvent({Receiver, Baseline.BaselineId, 47, EventBytes}, Fixture).has_value());
	Passed &= ExpectError("failed state application leaves the tick retryable", Receive(Snapshot), ReplicaSnapshotError::ApplicationFailed);
	ReadyToApply = true;
	Passed &= Expect("a complete older update can apply while the newer tick is unapplied", Receive(Independent).has_value());
	Passed &= Expect("the same snapshot can retry after application failure", Receive(Snapshot).has_value() && Attempts == 3);
	return Passed;
}

static bool CheckPerBodyAndWrap()
{
	auto History = std::make_unique<RetiredCombatantGenerations>();
	CombatantReplicas Replicas(World, *History);
	constexpr ParticipantId Other{72};
	constexpr EntityRef Second{30, 12, 1};
	constexpr u32 Last = std::numeric_limits<u32>::max();
	constexpr u32 Half = 0x80000000u;
	if (!Expect("two bodies can be registered independently", Replicas.RegisterBody(World, Participant, Body, Last) &&
		Replicas.RegisterBody(World, Other, Second, Last)))
	{
		return false;
	}

	int Applies = 0;
	auto Apply = [&]() noexcept
	{
		++Applies;
		return true;
	};
	bool Passed = ExpectError("a wrapped required event is still ahead before it applies",
		Replicas.ApplyLiveSnapshot(Receiver, Last, {Receiver, Participant, Body, 0, 0}, Apply), ReplicaSnapshotError::EventsPending);
	Passed &= Expect("the first body's tick wraps and an older event watermark remains covered",
		Replicas.ApplyLiveSnapshot(Receiver, 0u, {Receiver, Participant, Body, 0, Last}, Apply).has_value());
	Passed &= Expect("another body can apply the same tick",
		Replicas.ApplyLiveSnapshot(Receiver, 0u, {Receiver, Other, Second, 0, 0}, Apply).has_value() && Applies == 2);
	Passed &= ExpectError("a tick half a range away cannot replace the applied state",
		Replicas.ApplyLiveSnapshot(Receiver, 0u, {Receiver, Participant, Body, Half, 0}, Apply), ReplicaSnapshotError::StaleTick);
	Passed &= ExpectError("a half-range watermark is ambiguous",
		Replicas.ApplyLiveSnapshot(Receiver, 0u, {Receiver, Participant, Body, 1, Half}, Apply), ReplicaSnapshotError::AmbiguousWatermark);
	Passed &= Expect("an ambiguous watermark does not spend the tick",
		Replicas.ApplyLiveSnapshot(Receiver, 0u, {Receiver, Participant, Body, 1, 0}, Apply).has_value() && Applies == 3);
	return Passed;
}

static bool CheckScopeAndBodyLifetime()
{
	auto History = std::make_unique<RetiredCombatantGenerations>();
	CombatantReplicas Replicas(World, *History);
	if (!Expect("the fixture has a current body", Replicas.RegisterBody(World, Participant, Body)))
	{
		return false;
	}

	int Applies = 0;
	auto Apply = [&]() noexcept
	{
		++Applies;
		return true;
	};
	constexpr JoinIdentity Foreign[] = {
		{Receiver.ServerSession + 1, Receiver.WorldEpoch, Receiver.ConnectionEpoch},
		{Receiver.ServerSession, Receiver.WorldEpoch + 1, Receiver.ConnectionEpoch},
		{Receiver.ServerSession, Receiver.WorldEpoch, Receiver.ConnectionEpoch + 1},
	};
	bool Passed = true;
	for (const auto Peer : Foreign)
	{
		Passed &= ExpectError("the snapshot must match the current admitted receiver",
			Replicas.ApplyLiveSnapshot(Receiver, 0u, {Peer, Participant, Body, 0, 0}, Apply), ReplicaSnapshotError::IdentityMismatch);
	}
	constexpr JoinIdentity Unbound{Receiver.ServerSession, Receiver.WorldEpoch, 0};
	Passed &= ExpectError("equal unbound identities cannot apply state",
		Replicas.ApplyLiveSnapshot(Unbound, 0u, {Unbound, Participant, Body, 0, 0}, Apply), ReplicaSnapshotError::IdentityMismatch);
	Passed &= ExpectError("the local registry must match the receiver's world",
		Replicas.ApplyLiveSnapshot(Foreign[1], 0u, {Foreign[1], Participant, Body, 0, 0}, Apply), ReplicaSnapshotError::IdentityMismatch);

	Passed &= ExpectError("unassigned body identities cannot apply state",
		Replicas.ApplyLiveSnapshot(Receiver, 0u, {Receiver, Participant, {}, 0, 0}, Apply), ReplicaSnapshotError::InvalidReference);
	Passed &= ExpectError("unknown bodies grow no snapshot state",
		Replicas.ApplyLiveSnapshot(Receiver, 0u, {Receiver, Participant, {30, 12, 1}, 0, 0}, Apply), ReplicaSnapshotError::UnknownReference);
	Passed &= ExpectError("the current slot and generation still require the right life",
		Replicas.ApplyLiveSnapshot(Receiver, 0u, {Receiver, Participant, {29, 11, 3}, 0, 0}, Apply), ReplicaSnapshotError::ConflictingReference);
	Passed &= Expect("the first tick can be zero for a body with no applied state", Applies == 0 &&
		Replicas.ApplyLiveSnapshot(Receiver, 0u, {Receiver, Participant, Body, 0, 0}, Apply).has_value());

	Passed &= Expect("the old life advances its applied tick",
		Replicas.ApplyLiveSnapshot(Receiver, 0u, {Receiver, Participant, Body, 100, 0}, Apply).has_value());

	constexpr EntityRef Replacement{Body.ObjectId, Body.Generation + 1, Body.LifeId + 1};
	Passed &= Expect("the new life can reuse the slot", Replicas.Despawn(World, Participant, Body) &&
		Replicas.RegisterBody(World, Participant, Replacement));
	Passed &= ExpectError("a late snapshot for the retired life fails before application",
		Replicas.ApplyLiveSnapshot(Receiver, 0u, {Receiver, Participant, Body, 101, 0}, Apply), ReplicaSnapshotError::StaleReference);
	Passed &= Expect("the new life has its own tick history",
		Replicas.ApplyLiveSnapshot(Receiver, 0u, {Receiver, Participant, Replacement, 1, 0}, Apply).has_value() && Applies == 3);

	constexpr JoinIdentity NextReceiver{Receiver.ServerSession, Receiver.WorldEpoch + 1, Receiver.ConnectionEpoch};
	constexpr WorldIdentity NextWorld{NextReceiver.ServerSession, NextReceiver.WorldEpoch};
	Passed &= Expect("a new round can register the same numeric identity", Replicas.AdvanceWorld(NextWorld.WorldEpoch) &&
		Replicas.RegisterBody(NextWorld, Participant, Replacement, 0u));
	Passed &= ExpectError("a snapshot from the previous round cannot change the new round",
		Replicas.ApplyLiveSnapshot(NextReceiver, 0u, {Receiver, Participant, Replacement, 101, 0}, Apply),
		ReplicaSnapshotError::IdentityMismatch);
	Passed &= Expect("the new round starts with its own ticks",
		Replicas.ApplyLiveSnapshot(NextReceiver, 0u, {NextReceiver, Participant, Replacement, 1, 0}, Apply).has_value() && Applies == 4);
	return Passed;
}

bool Run()
{
	bool Passed = CheckLossAndReordering();
	Passed &= CheckCausalityAndRetry();
	Passed &= CheckPerBodyAndWrap();
	Passed &= CheckScopeAndBodyLifetime();
	if (!Passed)
	{
		return false;
	}
	std::puts("Replica snapshot checks passed");
	return true;
}
}
