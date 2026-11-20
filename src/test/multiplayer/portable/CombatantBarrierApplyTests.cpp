#include "../../../xrGame/Multiplayer/CombatantBarrierApply.h"
#include "../../../xrGame/Multiplayer/CombatantBaselineImport.h"
#include "../../../xrGame/Multiplayer/ReplicaSnapshotDispatch.h"
#include "CatchUpDespawn.h"
#include "PeerJoin.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <utility>

namespace CombatantBarrierApplyTests
{
using namespace Multiplayer;
using namespace Multiplayer::Wire;

constexpr JoinIdentity Receiver{0x8877665544332211ull, 7, 11};
constexpr WorldIdentity World{Receiver.ServerSession, Receiver.WorldEpoch};
constexpr BaselineBoundary Capture{4, 100, 45};
constexpr JoinBarrier Target{4, 3, 103, 46};
constexpr ParticipantId Human{17};
constexpr ParticipantId Stalker{18};
constexpr EntityRef HumanBody{31, 8, 2};
constexpr EntityRef OldStalker{32, 9, 1};
constexpr EntityRef NewStalker{32, 10, 2};

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

static HumanCombatantSnapshot MakeHuman(f32 X)
{
	HumanCombatantSnapshot Snapshot{};
	Snapshot.State.Participant = Human;
	Snapshot.State.Body = HumanBody;
	Snapshot.State.BodyProfileId = 42;
	Snapshot.State.Position[0] = X;
	return Snapshot;
}

static StalkerCombatantSnapshot MakeStalker(EntityRef Body)
{
	StalkerCombatantSnapshot Snapshot{};
	Snapshot.State.Participant = Stalker;
	Snapshot.State.Body = Body;
	Snapshot.State.BodyProfileId = 43;
	Snapshot.AnimationProfileId = 44;
	return Snapshot;
}

static std::optional<CombatantBaseline> BuildBaseline(JoinIdentity Identity, BaselineBoundary Boundary, xr_span<const CombatantSnapshot> Records)
{
	xr_array<u8, MaxCombatantBaselineBytes> Bytes;
	const auto Manifest = EncodeCombatantBaseline(Boundary, Records, Bytes);
	if (!Manifest)
	{
		return std::nullopt;
	}
	auto Baseline = CombatantBaseline::Decode({Identity, *Manifest, xr_span(Bytes).first(Manifest->ByteCount)}, Identity, Boundary);
	if (!Baseline)
	{
		return std::nullopt;
	}
	return std::move(*Baseline);
}

// Application fixtures use a single real fragment. Chunk assembly limits and ordering have a separate suite.
static std::optional<CombatantBarrierSnapshot> StageBarrier(JoinIdentity Identity, JoinBarrier Barrier, xr_span<const CombatantSnapshot> Records)
{
	xr_array<u8, 1024> Stream;
	const auto Manifest = EncodeCombatantBaseline({Barrier.BaselineId, Barrier.SnapshotTick, Barrier.EventWatermark}, Records, Stream);
	if (!Manifest)
	{
		return std::nullopt;
	}
	xr_array<u8, MaxMessageSize> Datagram;
	const auto Size = EncodeCombatantBarrierChunk({Identity, 10, Barrier, Manifest->ByteCount, Manifest->RecordCount, 0,
		xr_span(Stream).first(Manifest->ByteCount)}, Datagram);
	if (!Size)
	{
		return std::nullopt;
	}
	const auto Chunk = DecodeCombatantBarrierChunk(xr_span(Datagram).first(*Size));
	if (!Chunk)
	{
		return std::nullopt;
	}

	xr_array<u8, 1024> Storage;
	BaselineStagingBudget Budget(Storage.size(), 1);
	CombatantBarrierStaging Staging(Identity, Barrier, Storage, Budget.TryReserve(Storage.size()).value());
	if (!Staging.AddChunk(*Chunk))
	{
		return std::nullopt;
	}
	auto Staged = Staging.Finish();
	if (!Staged)
	{
		return std::nullopt;
	}
	return std::move(*Staged);
}

class WorldFixture final : public ICombatantBaselineApplier, public ICombatantBarrierApplier, public IClientCatchUpApplier,
	public ICombatantReplicaApplier
{
public:
	explicit WorldFixture(CombatantReplicas& Replicas) : Replicas(Replicas) {}

	bool ApplyBaseline(const CombatantBaseline& Baseline) noexcept override
	{
		Replace(Baseline.GetRecords());
		return true;
	}

	bool ApplyBarrier(const CombatantBarrierSnapshot& Snapshot) noexcept override
	{
		++BarrierAttempts;
		if (!AllowBarrier)
		{
			return false;
		}
		Replace(Snapshot.GetRecords());
		return true;
	}

	bool ApplyEvent(const CatchUpEvent& Event) noexcept override
	{
		const auto Decoded = DecodeCatchUpDespawn(Event.Bytes);
		if (!Decoded)
		{
			return false;
		}
		const auto& Despawn = Decoded->GetMessage();
		const auto Current = GetRecords();
		const auto Match = std::ranges::find_if(Current, [&Despawn](const PublicCombatantState& State)
		{
			return State.Participant == Despawn.Participant && State.Body == Despawn.Body;
		}, GetPublicCombatantState);
		if (Match == Current.end() || !Replicas.Despawn({Despawn.Identity.ServerSession, Despawn.Identity.WorldEpoch},
			Despawn.Participant, Despawn.Body))
		{
			return false;
		}
		const auto Index = static_cast<std::size_t>(Match - Current.begin());
		std::ranges::copy(Current.subspan(Index + 1), Records.begin() + Index);
		--Count;
		++EventApplications;
		return true;
	}

	bool ApplyBarrier(JoinIdentity, JoinBarrier) noexcept override { return false; }

	bool ApplySnapshot(const SnapshotBatchHeader&, const HumanCombatantSnapshot& Snapshot) noexcept override
	{
		const auto Current = xr_span(Records).first(Count);
		const auto Match = std::ranges::find(Current, Snapshot.State.Body, [](const CombatantSnapshot& Record)
		{
			return GetPublicCombatantState(Record).Body;
		});
		if (Match == Current.end())
		{
			return false;
		}
		*Match = Snapshot;
		++LiveApplications;
		return true;
	}

	bool ApplySnapshot(const SnapshotBatchHeader&, const StalkerCombatantSnapshot&) noexcept override { return false; }

	[[nodiscard]] xr_span<const CombatantSnapshot> GetRecords() const noexcept { return xr_span(Records).first(Count); }

	bool AllowBarrier = true;
	int BarrierAttempts = 0;
	int EventApplications = 0;
	int LiveApplications = 0;

private:
	void Replace(xr_span<const CombatantSnapshot> Replacement) noexcept
	{
		std::ranges::copy(Replacement, Records.begin());
		Count = Replacement.size();
	}

	CombatantReplicas& Replicas;
	xr_array<CombatantSnapshot, MaxCombatants> Records{};
	std::size_t Count = 0;
};

class Scenario
{
public:
	explicit Scenario(BaselineBoundary Boundary = Capture) : CatchUp(Receiver, Boundary) {}

	bool Import(BaselineBoundary Boundary, xr_span<const CombatantSnapshot> Records)
	{
		const auto Baseline = BuildBaseline(Receiver, Boundary, Records);
		return Baseline && ImportCombatantBaseline(*Baseline, CatchUp, Replicas, Applier).has_value();
	}

	auto Apply(const CombatantBarrierSnapshot& Snapshot, u32 MessageSequence = 20)
	{
		return ApplyCombatantBarrier(Snapshot, MessageSequence, CatchUp, Replicas, Applier);
	}

	std::unique_ptr<RetiredCombatantGenerations> History{std::make_unique<RetiredCombatantGenerations>()};
	CombatantReplicas Replicas{World, *History};
	ClientCatchUp CatchUp;
	WorldFixture Applier{Replicas};
};

static bool CheckFirstApplication()
{
	constexpr JoinBarrier AtCapture{4, 3, 100, 45};
	const CombatantSnapshot Expected[]{MakeHuman(23.0f), MakeStalker(OldStalker)};
	const auto Staged = StageBarrier(Receiver, AtCapture, Expected);
	Scenario Test;
	if (!Expect("barrier fixture stages the roster at an applied baseline watermark",
		Staged && Test.Import(Capture, Expected) && Test.CatchUp.DeclareBarrier(Receiver, AtCapture)))
	{
		return false;
	}

	const auto Ack = Test.Apply(*Staged);
	return Expect("applying the staged roster yields its exact acknowledgement",
		Ack && Ack->Barrier == AtCapture && Ack->Identity == Receiver && Ack->MessageSequence == 20 &&
		Test.CatchUp.GetAppliedBarrier() == AtCapture && Test.Applier.BarrierAttempts == 1 &&
		std::ranges::equal(Test.Applier.GetRecords(), Expected));
}

template <class Error>
static bool HasApplyError(const std::expected<ReadyAcknowledgement, CombatantBarrierApplyError>& Result, Error Code)
{
	return !Result && Result.error() == CombatantBarrierApplyError{Code};
}

static std::expected<void, ClientCatchUpError> ApplyDespawn(Scenario& Test, u32 Sequence, u32 Tick, EntityRef Body)
{
	xr_array<u8, MaxMessageSize> Datagram;
	const auto Size = EncodeCatchUpDespawn({Receiver, Sequence, Capture.BaselineId, Sequence, Sequence, Tick, Stalker, Body}, Datagram);
	const auto Decoded = Size ? DecodeCatchUpDespawn(xr_span(Datagram).first(*Size)) : std::unexpected(BoundMessageError::InvalidEnvelope);
	if (!Decoded)
	{
		return std::unexpected(ClientCatchUpError::ApplicationFailed);
	}
	return Test.CatchUp.ApplyEvent(Decoded->AsEvent(), Test.Applier);
}

static std::optional<CombatantSnapshotBatch> BuildLive(u32 Tick, u32 Watermark, f32 X)
{
	xr_array<u8, HumanCombatantRecordSize> Body;
	const auto Record = EncodeCombatantSnapshot(MakeHuman(X), Body);
	if (!Record)
	{
		return std::nullopt;
	}
	xr_array<u8, MaxMessageSize> Datagram;
	const auto Size = EncodeSnapshotBatch({Receiver, Tick, Tick, Tick, 12345, Watermark}, xr_span(&*Record, 1), Datagram);
	if (!Size)
	{
		return std::nullopt;
	}
	auto Batch = CombatantSnapshotBatch::Decode(xr_span(Datagram).first(*Size));
	if (!Batch)
	{
		return std::nullopt;
	}
	return std::move(*Batch);
}

static bool CheckWatermarkAndApplication()
{
	const CombatantSnapshot Initial[]{MakeHuman(10.0f), MakeStalker(OldStalker)};
	const CombatantSnapshot AtBarrier[]{MakeHuman(20.0f), MakeStalker(NewStalker)};
	const auto Snapshot = StageBarrier(Receiver, Target, AtBarrier);
	const auto Live = BuildLive(104, 46, 30.0f);
	Scenario Test;
	if (!Expect("the barrier and subsequent live fixture decode", Snapshot && Live))
	{
		return false;
	}

	bool Passed = Expect("a staged snapshot needs a declaration",
		HasApplyError(Test.Apply(*Snapshot), ClientCatchUpError::InvalidState));
	Passed &= Expect("a declared barrier still needs a committed baseline",
		Test.CatchUp.DeclareBarrier(Receiver, Target) && HasApplyError(Test.Apply(*Snapshot), ClientCatchUpError::BaselineNotApplied));
	Passed &= Expect("baseline import cannot release a barrier before its event",
		Test.Import(Capture, Initial) && HasApplyError(Test.Apply(*Snapshot), ClientCatchUpError::BarrierPending) &&
		Test.Applier.BarrierAttempts == 0 && std::ranges::equal(Test.Applier.GetRecords(), Initial));
	Passed &= Expect("the required wire event retires the old body",
		ApplyDespawn(Test, 46, 102, OldStalker) && Test.CatchUp.GetAppliedWatermark() == 46 && Test.Applier.EventApplications == 1 &&
		Test.Applier.GetRecords().size() == 1 && Test.Replicas.Classify(World, Stalker, OldStalker) == ReplicaBodyState::Stale);

	const CombatantSnapshot Revived[]{MakeHuman(19.0f), MakeStalker(OldStalker)};
	const auto Retired = StageBarrier(Receiver, Target, Revived);
	Passed &= Expect("a staged barrier cannot resurrect a body retired by catch-up",
		Retired && HasApplyError(Test.Apply(*Retired), ReplicaBaselineError::StaleBody) && Test.Applier.BarrierAttempts == 0);
	Test.Applier.AllowBarrier = false;
	Passed &= Expect("world rejection leaves the barrier retryable and the earlier world unchanged",
		HasApplyError(Test.Apply(*Snapshot), ReplicaBaselineError::ApplicationFailed) && Test.Applier.BarrierAttempts == 1 &&
		!Test.CatchUp.GetAppliedBarrier() && Test.CatchUp.GetAppliedWatermark() == 46 &&
		std::ranges::equal(Test.Applier.GetRecords(), xr_span(Initial).first(1)) &&
		Test.Replicas.Classify(World, Stalker, NewStalker) == ReplicaBodyState::Unknown &&
		(*Test.History)[OldStalker.ObjectId] == OldStalker.Generation);
	Passed &= Expect("later events and live packets are still gated after the failure",
		ApplyDespawn(Test, 47, 105, NewStalker) == std::unexpected(ClientCatchUpError::BarrierPending) &&
		DispatchLiveCombatantBatch(*Live, Test.CatchUp, Test.Replicas, Test.Applier) == std::unexpected(ReplicaSnapshotBatchError::Synchronizing) &&
		Test.Applier.EventApplications == 1 && Test.Applier.LiveApplications == 0);

	Test.Applier.AllowBarrier = true;
	const auto Ack = Test.Apply(*Snapshot, 28);
	Passed &= Expect("retry applies the typed roster and returns the exact acknowledgement",
		Ack && Ack->Barrier == Target && Ack->Identity == Receiver && Ack->MessageSequence == 28 &&
		Test.CatchUp.GetAppliedBarrier() == Target && Test.Applier.BarrierAttempts == 2 &&
		std::ranges::equal(Test.Applier.GetRecords(), AtBarrier) && Test.Replicas.Classify(World, Stalker, NewStalker) == ReplicaBodyState::Current);
	int PrematureApplications = 0;
	auto ApplyBody = [&]() noexcept { ++PrematureApplications; return true; };
	Passed &= Expect("the retained body's own tick is seeded at the barrier",
		Test.Replicas.ApplyLiveSnapshot(Receiver, 46u, {Receiver, Human, HumanBody, 103, 46}, ApplyBody) ==
			std::unexpected(ReplicaSnapshotError::StaleTick) && PrematureApplications == 0);

	constexpr PeerJoin::Clock::time_point Now{};
	PeerJoin Server(Receiver, Now);
	const auto Synchronizing = Now + std::chrono::seconds(1);
	Passed &= Expect("server waits for an acknowledgement to its exact declaration", Server.ConfirmLoaded(Receiver, Synchronizing) &&
		Server.SelectBaseline(Receiver, Capture, Synchronizing) && Server.DeclareBarrier(Receiver, Target, Synchronizing) &&
		Server.GetState() == PeerJoinState::Synchronizing);
	xr_array<u8, MaxMessageSize> Datagram;
	const auto Size = Ack ? EncodeReadyAcknowledgement(*Ack, Datagram) : std::unexpected(BoundMessageError::InvalidPayload);
	const auto Received = Size ? DecodeReadyAcknowledgement(xr_span(Datagram).first(*Size)) : std::unexpected(BoundMessageError::InvalidEnvelope);
	Passed &= Expect("the encoded acknowledgement advances the server to ready spectator", Received && Received->MessageSequence == 28 &&
		Server.AcknowledgeBarrier(Received->Identity, Received->Barrier, Synchronizing) && Server.GetState() == PeerJoinState::ReadySpectator);

	for (const u32 Tick : {102u, 103u})
	{
		const auto Older = BuildLive(Tick, 46, 99.0f);
		Passed &= Expect("live packets at or behind the applied barrier cannot rewind the world",
			Older && DispatchLiveCombatantBatch(*Older, Test.CatchUp, Test.Replicas, Test.Applier) ==
				std::unexpected(ReplicaSnapshotBatchError::StaleTick));
	}
	const auto Updated = DispatchLiveCombatantBatch(*Live, Test.CatchUp, Test.Replicas, Test.Applier);
	Passed &= Expect("the next live tick can apply after successful barrier application",
		Updated && Updated->GetRecordResults().size() == 1 && Updated->GetRecordResults()[0] &&
		Test.Applier.LiveApplications == 1 && GetPublicCombatantState(Test.Applier.GetRecords().front()).Position[0] == 30.0f);
	Passed &= Expect("later events resume under the same catch-up selection",
		ApplyDespawn(Test, 47, 105, NewStalker) && Test.Applier.EventApplications == 2 && Test.CatchUp.GetAppliedWatermark() == 47);

	const auto Retry = Test.Apply(*Snapshot, 29);
	const CombatantSnapshot Latest[]{MakeHuman(30.0f)};
	Passed &= Expect("acknowledgement retry preserves later live and lifecycle state",
		Retry && Retry->Barrier == Target && Retry->Identity == Receiver && Retry->MessageSequence == 29 &&
		Test.Applier.BarrierAttempts == 2 && Test.CatchUp.GetAppliedWatermark() == 47 &&
		std::ranges::equal(Test.Applier.GetRecords(), Latest) && Test.Replicas.Classify(World, Stalker, NewStalker) == ReplicaBodyState::Stale);

	Passed &= Expect("duplicate acknowledgement still requires the registry's current world",
		Test.Replicas.AdvanceWorld(World.WorldEpoch + 1) &&
		HasApplyError(Test.Apply(*Snapshot), ReplicaBaselineError::WorldMismatch) && Test.Applier.BarrierAttempts == 2);
	return Passed;
}

static bool CheckScopeAndRebase()
{
	const CombatantSnapshot Records[]{MakeHuman(20.0f)};
	constexpr JoinBarrier First{4, 3, 103, 45};
	const auto Old = StageBarrier(Receiver, First, Records);
	Scenario Test;
	if (!Expect("scope fixture has the original declaration and baseline", Old && Test.Import(Capture, Records) &&
		Test.CatchUp.DeclareBarrier(Receiver, First)))
	{
		return false;
	}
	constexpr JoinIdentity Foreign[]{
		{Receiver.ServerSession + 1, Receiver.WorldEpoch, Receiver.ConnectionEpoch},
		{Receiver.ServerSession, Receiver.WorldEpoch + 1, Receiver.ConnectionEpoch},
		{Receiver.ServerSession, Receiver.WorldEpoch, Receiver.ConnectionEpoch + 1},
	};
	bool Passed = true;
	for (const auto Identity : Foreign)
	{
		const auto Snapshot = StageBarrier(Identity, First, Records);
		Passed &= Expect("foreign capture fails the admitted tracker scope",
			Snapshot && HasApplyError(Test.Apply(*Snapshot), ClientCatchUpError::IdentityMismatch));
	}
	constexpr JoinBarrier OtherBarriers[]{
		{4, 4, 103, 45},
		{4, 3, 104, 45},
		{4, 3, 103, 46},
	};
	for (const auto Barrier : OtherBarriers)
	{
		const auto Snapshot = StageBarrier(Receiver, Barrier, Records);
		Passed &= Expect("all barrier fields must match the declaration",
			Snapshot && HasApplyError(Test.Apply(*Snapshot), ClientCatchUpError::BarrierMismatch));
	}

	Passed &= Expect("the exact original barrier can apply before rebase", Test.Apply(*Old).has_value() &&
		Test.CatchUp.GetAppliedBarrier() == First && Test.Applier.BarrierAttempts == 1);

	constexpr BaselineBoundary Replacement{5, 105, 46};
	constexpr JoinBarrier SameBarrierId{5, 3, 105, 46};
	const auto New = StageBarrier(Receiver, SameBarrierId, Records);
	Passed &= Expect("replacement invalidates old staged data even when the barrier ID recurs",
		New && Test.CatchUp.Rebase(Receiver, Replacement) && Test.CatchUp.DeclareBarrier(Receiver, SameBarrierId) &&
		HasApplyError(Test.Apply(*Old), ClientCatchUpError::BaselineMismatch) &&
		HasApplyError(Test.Apply(*New), ClientCatchUpError::BaselineNotApplied) && Test.Applier.BarrierAttempts == 1);
	Passed &= Expect("the exact new capture and staged barrier can complete",
		New && Test.Import(Replacement, Records) && Test.Apply(*New).has_value() && Test.Applier.BarrierAttempts == 2 &&
		Test.CatchUp.GetAppliedBarrier() == SameBarrierId);
	return Passed;
}

static bool CheckEmptyWrappedBarrier()
{
	constexpr BaselineBoundary Initial{4, 0xfffffffeu, 0xffffffffu};
	constexpr JoinBarrier Empty{4, 0, 0, 0xffffffffu};
	const CombatantSnapshot Idle[]{MakeHuman(10.0f)};
	const auto Snapshot = StageBarrier(Receiver, Empty, {});
	Scenario Test(Initial);
	if (!Expect("empty fixture starts before the tick wrap",
		Snapshot && Test.Import(Initial, Idle) && Test.CatchUp.DeclareBarrier(Receiver, Empty)))
	{
		return false;
	}
	bool Passed = Expect("empty barrier retires old bodies at the wrapped tick",
		Test.Apply(*Snapshot).has_value() && Test.Applier.GetRecords().empty() &&
		Test.Replicas.Classify(World, Human, HumanBody) == ReplicaBodyState::Stale && Test.CatchUp.GetAppliedBarrier() == Empty);

	constexpr JoinIdentity Reconnected{Receiver.ServerSession, Receiver.WorldEpoch, Receiver.ConnectionEpoch + 1};
	constexpr BaselineBoundary Earlier{5, 0xffffffffu, 0xffffffffu};
	auto NewBody = MakeHuman(11.0f);
	NewBody.State.Body = {34, 25, 3};
	const CombatantSnapshot Unseen[]{NewBody};
	const auto Old = BuildBaseline(Reconnected, Earlier, Unseen);
	ClientCatchUp Reconnect(Reconnected, Earlier);
	const auto Restore = Old ? ImportCombatantBaseline(*Old, Reconnect, Test.Replicas, Test.Applier) :
		std::unexpected(CombatantBaselineImportError{ClientCatchUpError::InvalidState});
	Passed &= Expect("an empty barrier still prevents a same-world reconnect from rewinding the capture",
		!Restore && Restore.error() == CombatantBaselineImportError{ReplicaBaselineError::StaleCapture} &&
		Test.Applier.GetRecords().empty() && !Reconnect.GetAppliedWatermark());
	return Passed;
}

bool Run()
{
	bool Passed = CheckFirstApplication();
	Passed &= CheckWatermarkAndApplication();
	Passed &= CheckScopeAndRebase();
	Passed &= CheckEmptyWrappedBarrier();
	if (!Passed)
	{
		return false;
	}
	std::puts("Combatant barrier application checks passed");
	return true;
}
}
