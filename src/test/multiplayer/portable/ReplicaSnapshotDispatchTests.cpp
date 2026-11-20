#include "../../../xrGame/Multiplayer/ReplicaSnapshotDispatch.h"

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <memory>

namespace ReplicaSnapshotDispatchTests
{
using namespace Multiplayer;
using namespace Multiplayer::Wire;

constexpr JoinIdentity Receiver{419, 8, 3};
constexpr WorldIdentity World{Receiver.ServerSession, Receiver.WorldEpoch};
constexpr BaselineBoundary Baseline{4, 90, 36};
constexpr JoinBarrier Barrier{4, 2, 100, 36};
constexpr ParticipantId HumanParticipant{17};
constexpr ParticipantId StalkerParticipant{18};
constexpr EntityRef HumanBody{31, 7, 2};
constexpr EntityRef StalkerBody{32, 8, 3};

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

template <class Value, class Error>
static bool HasError(const std::expected<Value, Error>& Outcome, Error Expected) noexcept
{
	return !Outcome && Outcome.error() == Expected;
}

static HumanCombatantSnapshot MakeHuman(EntityRef Body = HumanBody, f32 X = 12.0f)
{
	HumanCombatantSnapshot Snapshot{};
	Snapshot.State.Participant = HumanParticipant;
	Snapshot.State.Body = Body;
	Snapshot.State.BodyProfileId = 42;
	Snapshot.State.Position[0] = X;
	return Snapshot;
}

static StalkerCombatantSnapshot MakeStalker()
{
	StalkerCombatantSnapshot Snapshot{};
	Snapshot.State.Participant = StalkerParticipant;
	Snapshot.State.Body = StalkerBody;
	Snapshot.State.BodyProfileId = 43;
	Snapshot.State.Position[0] = 13.0f;
	Snapshot.AnimationProfileId = 44;
	return Snapshot;
}

static std::expected<CombatantSnapshotBatch, BoundMessageError> BuildBatch(JoinIdentity Scope, u32 Tick, u32 Watermark,
	const HumanCombatantSnapshot& Human, const StalkerCombatantSnapshot* Stalker = nullptr, u32 MessageSequence = 9)
{
	xr_array<u8, HumanCombatantRecordSize> HumanBytes;
	xr_array<u8, StalkerCombatantRecordSize> StalkerBytes;
	const auto HumanRecord = EncodeCombatantSnapshot(Human, HumanBytes);
	if (!HumanRecord)
	{
		return std::unexpected(BoundMessageError::InvalidPayload);
	}

	xr_array<SnapshotRecordView, 2> Records{*HumanRecord, {}};
	std::size_t Count = 1;
	if (Stalker)
	{
		const auto StalkerRecord = EncodeCombatantSnapshot(*Stalker, StalkerBytes);
		if (!StalkerRecord)
		{
			return std::unexpected(BoundMessageError::InvalidPayload);
		}
		Records[Count++] = *StalkerRecord;
	}

	xr_array<u8, MaxMessageSize> Datagram;
	const auto Frame = EncodeSnapshotBatch({Scope, MessageSequence, 91, Tick, 12345, Watermark}, xr_span(Records).first(Count), Datagram);
	if (!Frame)
	{
		return std::unexpected(Frame.error());
	}
	return CombatantSnapshotBatch::Decode(xr_span(Datagram).first(*Frame));
}

class TestWorld final : public IClientCatchUpApplier, public ICombatantReplicaApplier
{
public:
	bool ApplyEvent(const CatchUpEvent&) noexcept override { return AllowEvent; }

	bool ApplyBarrier(JoinIdentity, JoinBarrier) noexcept override
	{
		if (!AllowBarrier)
		{
			return false;
		}
		HumanX = 10.0f;
		return true;
	}

	bool ApplySnapshot(const SnapshotBatchHeader& Header, const HumanCombatantSnapshot& Snapshot) noexcept override
	{
		++HumanAttempts;
		LastHeader = Header;
		if (!AllowHuman)
		{
			return false;
		}
		HumanX = Snapshot.State.Position[0];
		return true;
	}

	bool ApplySnapshot(const SnapshotBatchHeader& Header, const StalkerCombatantSnapshot& Snapshot) noexcept override
	{
		++StalkerAttempts;
		LastHeader = Header;
		StalkerX = Snapshot.State.Position[0];
		AnimationProfile = Snapshot.AnimationProfileId;
		return true;
	}

	bool AllowBarrier = true;
	bool AllowEvent = true;
	bool AllowHuman = true;
	int HumanAttempts = 0;
	int StalkerAttempts = 0;
	f32 HumanX = 0;
	f32 StalkerX = 0;
	u32 AnimationProfile = 0;
	SnapshotBatchHeader LastHeader{};
};

class Fixture
{
public:
	explicit Fixture(BaselineBoundary Capture = Baseline) : CatchUp(Receiver, Capture) {}

	bool Synchronize(BaselineBoundary Capture = Baseline, JoinBarrier Target = Barrier)
	{
		return CatchUp.ConfirmBaselineApplied(Receiver, Capture).has_value() &&
			CatchUp.DeclareBarrier(Receiver, Target).has_value() &&
			CatchUp.CompleteBarrier(Receiver, Target, 7, Applier).has_value();
	}

	auto Dispatch(const CombatantSnapshotBatch& Batch)
	{
		return DispatchLiveCombatantBatch(Batch, CatchUp, Replicas, Applier);
	}

	std::unique_ptr<RetiredCombatantGenerations> History{std::make_unique<RetiredCombatantGenerations>()};
	CombatantReplicas Replicas{World, *History};
	ClientCatchUp CatchUp;
	TestWorld Applier;
};

static bool CheckLiveAfterBarrier()
{
	Fixture Scenario;
	const auto Batch = BuildBatch(Receiver, 102, 36, MakeHuman());
	if (!Expect("wire body and trusted replica are available", Batch &&
		Scenario.Replicas.RegisterBody(World, HumanParticipant, HumanBody, Baseline.CaptureTick) && Scenario.Synchronize()))
	{
		return false;
	}

	const auto Result = Scenario.Dispatch(*Batch);
	return Expect("an eligible wire snapshot is applied through its typed world callback", Result &&
		Result->GetRecordResults().size() == 1 && Result->GetRecordResults()[0] && Scenario.Applier.HumanAttempts == 1 &&
		Scenario.Applier.HumanX == 12.0f && Scenario.Applier.LastHeader == Batch->GetHeader());
}

static bool HasRecordResults(const std::expected<ReplicaSnapshotBatchReport, ReplicaSnapshotBatchError>& Result,
	std::initializer_list<std::optional<ReplicaSnapshotError>> Expected)
{
	return Result && std::ranges::equal(Result->GetRecordResults(), Expected,
		[](const ReplicaSnapshotBatchReport::RecordResult& Actual, std::optional<ReplicaSnapshotError> Error)
		{
			return Error ? HasError(Actual, *Error) : Actual.has_value();
		});
}

static bool CheckBarrierAndScope()
{
	Fixture Scenario;
	const auto Live = BuildBatch(Receiver, 101, 36, MakeHuman());
	if (!Expect("body starts at the baseline tick", Live && Scenario.Replicas.RegisterBody(World, HumanParticipant, HumanBody, 90u)))
	{
		return false;
	}

	bool Passed = Expect("a packet received before baseline import cannot run",
		HasError(Scenario.Dispatch(*Live), ReplicaSnapshotBatchError::Synchronizing));
	Passed &= Expect("baseline import and declaration are accepted",
		Scenario.CatchUp.ConfirmBaselineApplied(Receiver, Baseline).has_value() &&
		Scenario.CatchUp.DeclareBarrier(Receiver, Barrier).has_value());
	Passed &= Expect("the declared barrier cannot release live updates until applied",
		HasError(Scenario.Dispatch(*Live), ReplicaSnapshotBatchError::Synchronizing));

	Scenario.Applier.AllowBarrier = false;
	Passed &= Expect("failed barrier application keeps dispatch closed",
		HasError(Scenario.CatchUp.CompleteBarrier(Receiver, Barrier, 7, Scenario.Applier), ClientCatchUpError::ApplicationFailed) &&
		HasError(Scenario.Dispatch(*Live), ReplicaSnapshotBatchError::Synchronizing));
	Scenario.Applier.AllowBarrier = true;
	Passed &= Expect("retry applies the barrier", Scenario.CatchUp.CompleteBarrier(Receiver, Barrier, 7, Scenario.Applier).has_value());

	for (const u32 Tick : {99u, 100u, 100u + 0x80000000u})
	{
		const auto Stale = BuildBatch(Receiver, Tick, 36, MakeHuman());
		Passed &= Expect("live packets must strictly follow the applied barrier tick",
			Stale && HasError(Scenario.Dispatch(*Stale), ReplicaSnapshotBatchError::StaleTick));
	}

	constexpr JoinIdentity Foreign[]{
		{Receiver.ServerSession + 1, Receiver.WorldEpoch, Receiver.ConnectionEpoch},
		{Receiver.ServerSession, Receiver.WorldEpoch + 1, Receiver.ConnectionEpoch},
		{Receiver.ServerSession, Receiver.WorldEpoch, Receiver.ConnectionEpoch + 1},
	};
	for (const auto Scope : Foreign)
	{
		const auto Other = BuildBatch(Scope, 101, 36, MakeHuman());
		Passed &= Expect("each admitted identity field must match the current tracker",
			Other && HasError(Scenario.Dispatch(*Other), ReplicaSnapshotBatchError::IdentityMismatch));
	}
	Passed &= Expect("the registry and tracker must describe the same world",
		Scenario.Replicas.AdvanceWorld(World.WorldEpoch + 1) &&
		HasError(Scenario.Dispatch(*Live), ReplicaSnapshotBatchError::IdentityMismatch));
	Passed &= Expect("rejected batches never call the world or rewind the barrier's state",
		Scenario.Applier.HumanAttempts == 0 && Scenario.Applier.HumanX == 10.0f);
	return Passed;
}

static bool CheckMixedResultsAndRetry()
{
	Fixture Scenario;
	const auto Stalker = MakeStalker();
	const auto Batch = BuildBatch(Receiver, 101, 37, MakeHuman(), &Stalker);
	if (!Expect("the receiver has both synchronized bodies", Batch && Scenario.Synchronize() &&
		Scenario.Replicas.RegisterBody(World, HumanParticipant, HumanBody, 90u) &&
		Scenario.Replicas.RegisterBody(World, StalkerParticipant, StalkerBody, 90u)))
	{
		return false;
	}

	constexpr u8 EventBytes[] = {1};
	const CatchUpEvent Event{Receiver, Baseline.BaselineId, 37, EventBytes};
	Scenario.Applier.AllowEvent = false;
	bool Passed = Expect("failure to apply the required event holds both records",
		HasError(Scenario.CatchUp.ApplyEvent(Event, Scenario.Applier), ClientCatchUpError::ApplicationFailed) &&
		HasRecordResults(Scenario.Dispatch(*Batch), {ReplicaSnapshotError::EventsPending, ReplicaSnapshotError::EventsPending}) &&
		Scenario.Applier.HumanAttempts == 0 && Scenario.Applier.StalkerAttempts == 0);
	Scenario.Applier.AllowEvent = true;
	Scenario.Applier.AllowHuman = false;
	Passed &= Expect("record application failure leaves the other typed update independent",
		Scenario.CatchUp.ApplyEvent(Event, Scenario.Applier).has_value() &&
		HasRecordResults(Scenario.Dispatch(*Batch), {ReplicaSnapshotError::ApplicationFailed, std::nullopt}) &&
		Scenario.Applier.HumanX == 10.0f && Scenario.Applier.StalkerX == 13.0f && Scenario.Applier.AnimationProfile == 44);

	Scenario.Applier.AllowHuman = true;
	Passed &= Expect("retry applies only the unfinished body and preserves wire metadata",
		HasRecordResults(Scenario.Dispatch(*Batch), {std::nullopt, ReplicaSnapshotError::StaleTick}) &&
		Scenario.Applier.HumanAttempts == 2 && Scenario.Applier.StalkerAttempts == 1 &&
		Scenario.Applier.HumanX == 12.0f && Scenario.Applier.LastHeader == Batch->GetHeader());
	Passed &= Expect("both successfully applied records are stale on replay",
		HasRecordResults(Scenario.Dispatch(*Batch), {ReplicaSnapshotError::StaleTick, ReplicaSnapshotError::StaleTick}) &&
		Scenario.Applier.HumanAttempts == 2 && Scenario.Applier.StalkerAttempts == 1);
	return Passed;
}

static bool CheckIndependentBatchOrdering()
{
	Fixture Scenario;
	const auto Stalker = MakeStalker();
	const auto NewerHuman = BuildBatch(Receiver, 103, 36, MakeHuman(HumanBody, 15.0f), nullptr, 11);
	const auto DelayedMixed = BuildBatch(Receiver, 102, 36, MakeHuman(HumanBody, 13.0f), &Stalker, 10);
	auto UpdatedStalker = Stalker;
	UpdatedStalker.State.Position[0] = 17.0f;
	const auto SameTick = BuildBatch(Receiver, 103, 36, MakeHuman(HumanBody, 15.0f), &UpdatedStalker, 12);
	if (!Expect("independent batches can arrive in a different order than their ticks",
		NewerHuman && DelayedMixed && SameTick && Scenario.Synchronize() &&
		Scenario.Replicas.RegisterBody(World, HumanParticipant, HumanBody) &&
		Scenario.Replicas.RegisterBody(World, StalkerParticipant, StalkerBody)))
	{
		return false;
	}

	bool Passed = Expect("the first batch applies its newest human state",
		HasRecordResults(Scenario.Dispatch(*NewerHuman), {std::nullopt}) && Scenario.Applier.HumanX == 15.0f);
	Passed &= Expect("a delayed batch can still contain the latest useful stalker state",
		HasRecordResults(Scenario.Dispatch(*DelayedMixed), {ReplicaSnapshotError::StaleTick, std::nullopt}) &&
		Scenario.Applier.HumanX == 15.0f && Scenario.Applier.StalkerX == 13.0f);
	Passed &= Expect("another batch at the same tick can still advance a different body",
		HasRecordResults(Scenario.Dispatch(*SameTick), {ReplicaSnapshotError::StaleTick, std::nullopt}) &&
		Scenario.Applier.HumanAttempts == 1 && Scenario.Applier.StalkerAttempts == 2 && Scenario.Applier.StalkerX == 17.0f);
	return Passed;
}

static bool CheckLifecycleDeferral()
{
	Fixture Scenario;
	const auto Stalker = MakeStalker();
	const auto Batch = BuildBatch(Receiver, 103, 36, MakeHuman(), &Stalker);
	if (!Expect("the mixed snapshot references a retired body and a body not seen yet",
		Batch && Scenario.Synchronize() && Scenario.Replicas.RegisterBody(World, HumanParticipant, HumanBody) &&
		Scenario.Replicas.Despawn(World, HumanParticipant, HumanBody)))
	{
		return false;
	}

	bool Passed = Expect("retired and unknown bodies produce different outcomes without application",
		HasRecordResults(Scenario.Dispatch(*Batch), {ReplicaSnapshotError::StaleReference, ReplicaSnapshotError::UnknownReference}) &&
		Scenario.Applier.HumanAttempts == 0 && Scenario.Applier.StalkerAttempts == 0);
	Passed &= Expect("only trusted registration makes the unknown body applicable",
		Scenario.Replicas.RegisterBody(World, StalkerParticipant, StalkerBody) &&
		HasRecordResults(Scenario.Dispatch(*Batch), {ReplicaSnapshotError::StaleReference, std::nullopt}) &&
		Scenario.Applier.HumanAttempts == 0 && Scenario.Applier.StalkerAttempts == 1);
	return Passed;
}

static bool CheckRebaseAndWrap()
{
	constexpr BaselineBoundary Capture{4, 0xfffffffcu, 0xffffffffu};
	constexpr JoinBarrier Target{4, 2, 0xffffffffu, 0xffffffffu};
	Fixture Scenario(Capture);
	const auto Wrapped = BuildBatch(Receiver, 0, 0, MakeHuman());
	const auto Fresh = BuildBatch(Receiver, 3, 0, MakeHuman(HumanBody, 13.0f));
	if (!Expect("the fixture synchronizes just before tick and event wrap",
		Wrapped && Fresh && Scenario.Synchronize(Capture, Target) &&
		Scenario.Replicas.RegisterBody(World, HumanParticipant, HumanBody, Capture.CaptureTick)))
	{
		return false;
	}

	constexpr u8 EventBytes[]{1};
	bool Passed = Expect("wrapped tick is newer but must wait for its wrapped event",
		HasRecordResults(Scenario.Dispatch(*Wrapped), {ReplicaSnapshotError::EventsPending}) &&
		Scenario.Applier.HumanAttempts == 0);
	Passed &= Expect("the zero tick applies after the zero event",
		Scenario.CatchUp.ApplyEvent({Receiver, Capture.BaselineId, 0, EventBytes}, Scenario.Applier).has_value() &&
		HasRecordResults(Scenario.Dispatch(*Wrapped), {std::nullopt}) && Scenario.Applier.HumanAttempts == 1);

	constexpr BaselineBoundary Replacement{5, 1, 0};
	constexpr JoinBarrier NewBarrier{5, 3, 2, 0};
	Passed &= Expect("rebase in the same round closes live dispatch",
		Scenario.CatchUp.Rebase(Receiver, Replacement).has_value() &&
		HasError(Scenario.Dispatch(*Fresh), ReplicaSnapshotBatchError::Synchronizing));
	Passed &= Expect("the replacement releases only ticks after its own barrier",
		Scenario.Synchronize(Replacement, NewBarrier) && HasError(Scenario.Dispatch(*Wrapped), ReplicaSnapshotBatchError::StaleTick) &&
		HasRecordResults(Scenario.Dispatch(*Fresh), {std::nullopt}) && Scenario.Applier.HumanAttempts == 2 &&
		Scenario.Applier.HumanX == 13.0f);
	return Passed;
}

bool Run()
{
	bool Passed = CheckLiveAfterBarrier();
	Passed &= CheckBarrierAndScope();
	Passed &= CheckMixedResultsAndRetry();
	Passed &= CheckIndependentBatchOrdering();
	Passed &= CheckLifecycleDeferral();
	Passed &= CheckRebaseAndWrap();
	if (!Passed)
	{
		return false;
	}
	std::puts("Replica snapshot dispatch checks passed");
	return true;
}
}
