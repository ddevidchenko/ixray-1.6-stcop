#include "../../../xrGame/Multiplayer/ReplicaSnapshotQueue.h"

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

static bool CheckDeferredQueue()
{
	Fixture Scenario;
	ReplicaSnapshotQueue Queue;
	const auto Batch = BuildBatch(Receiver, 101, 37, MakeHuman());
	if (!Expect("the deferred queue fixture has a registered replica", Batch && Scenario.Synchronize() &&
		Scenario.Replicas.RegisterBody(World, HumanParticipant, HumanBody, 100u)))
	{
		return false;
	}

	const auto Received = Queue.Receive(*Batch, Scenario.CatchUp, Scenario.Replicas, Scenario.Applier);
	bool Passed = Expect("an otherwise useful snapshot waiting for an event is retained", Received &&
		Received->Retry == SnapshotRetryStatus::NoRepair && Queue.GetPendingBatchCount() == 1 &&
		Queue.GetPendingRecordCount() == 1 && Scenario.Applier.HumanAttempts == 0);
	constexpr u8 EventBytes[]{1};
	Passed &= Expect("retry releases the retained snapshot after its event applies",
		Scenario.CatchUp.ApplyEvent({Receiver, Baseline.BaselineId, 37, EventBytes}, Scenario.Applier).has_value() &&
		Queue.RetryPending(Scenario.CatchUp, Scenario.Replicas, Scenario.Applier) == SnapshotRetryStatus::NoRepair &&
		Queue.GetPendingBatchCount() == 0 && Queue.GetPendingRecordCount() == 0 && Scenario.Applier.HumanAttempts == 1 &&
		Scenario.Applier.HumanX == 12.0f);
	return Passed;
}

static bool Enqueue(ReplicaSnapshotQueue& Queue, Fixture& Scenario, const std::expected<CombatantSnapshotBatch, BoundMessageError>& Batch,
	SnapshotRetryStatus Expected = SnapshotRetryStatus::NoRepair)
{
	if (!Batch)
	{
		return false;
	}
	const auto Result = Queue.Receive(*Batch, Scenario.CatchUp, Scenario.Replicas, Scenario.Applier);
	return Result && Result->Retry == Expected;
}

static SnapshotRetryStatus Retry(ReplicaSnapshotQueue& Queue, Fixture& Scenario)
{
	return Queue.RetryPending(Scenario.CatchUp, Scenario.Replicas, Scenario.Applier);
}

static bool ApplyTestEvent(Fixture& Scenario, u32 Sequence)
{
	constexpr u8 EventBytes[]{1};
	return Scenario.CatchUp.ApplyEvent({Receiver, Baseline.BaselineId, Sequence, EventBytes}, Scenario.Applier).has_value();
}

static bool CheckQueuedLatestUsefulState()
{
	Fixture Scenario;
	ReplicaSnapshotQueue Queue;
	if (!Expect("latest-state fixture has a synchronized replica", Scenario.Synchronize() &&
		Scenario.Replicas.RegisterBody(World, HumanParticipant, HumanBody, 100u)))
	{
		return false;
	}

	bool Passed = Expect("newer ticks replace the same body's deferred update with the same event requirement",
		Enqueue(Queue, Scenario, BuildBatch(Receiver, 101, 37, MakeHuman(HumanBody, 11.0f))) &&
		Enqueue(Queue, Scenario, BuildBatch(Receiver, 102, 37, MakeHuman(HumanBody, 12.0f))) &&
		Enqueue(Queue, Scenario, BuildBatch(Receiver, 101, 37, MakeHuman(HumanBody, 99.0f))) &&
		Enqueue(Queue, Scenario, BuildBatch(Receiver, 102, 37, MakeHuman(HumanBody, 99.0f))) &&
		Queue.GetPendingBatchCount() == 1 && Queue.GetPendingRecordCount() == 1);
	Passed &= Expect("a different event requirement can leave older state useful sooner",
		Enqueue(Queue, Scenario, BuildBatch(Receiver, 103, 38, MakeHuman(HumanBody, 13.0f))) &&
		Queue.GetPendingRecordCount() == 2 && ApplyTestEvent(Scenario, 37) &&
		Retry(Queue, Scenario) == SnapshotRetryStatus::NoRepair && Scenario.Applier.HumanX == 12.0f &&
		Scenario.Applier.HumanAttempts == 1 && Queue.GetPendingRecordCount() == 1);
	Passed &= Expect("the next applied event releases the later state",
		ApplyTestEvent(Scenario, 38) && Retry(Queue, Scenario) == SnapshotRetryStatus::NoRepair &&
		Scenario.Applier.HumanX == 13.0f && Queue.GetPendingRecordCount() == 0);
	Passed &= Expect("when multiple updates become ready together only the newest is applied",
		Enqueue(Queue, Scenario, BuildBatch(Receiver, 104, 39, MakeHuman(HumanBody, 14.0f))) &&
		Enqueue(Queue, Scenario, BuildBatch(Receiver, 105, 40, MakeHuman(HumanBody, 15.0f))) &&
		ApplyTestEvent(Scenario, 39) && ApplyTestEvent(Scenario, 40) &&
		Retry(Queue, Scenario) == SnapshotRetryStatus::NoRepair && Scenario.Applier.HumanAttempts == 3 &&
		Scenario.Applier.HumanX == 15.0f && Queue.GetPendingRecordCount() == 0);
	Passed &= Expect("an immediate newer application retires obsolete held state",
		Enqueue(Queue, Scenario, BuildBatch(Receiver, 106, 41, MakeHuman(HumanBody, 16.0f))) &&
		Enqueue(Queue, Scenario, BuildBatch(Receiver, 107, 40, MakeHuman(HumanBody, 17.0f))) &&
		Scenario.Applier.HumanX == 17.0f && Queue.GetPendingRecordCount() == 0);
	Passed &= Expect("an eligible copy at the same tick retires another still waiting for events",
		Enqueue(Queue, Scenario, BuildBatch(Receiver, 108, 42, MakeHuman(HumanBody, 18.0f))) &&
		Enqueue(Queue, Scenario, BuildBatch(Receiver, 108, 41, MakeHuman(HumanBody, 18.0f))) &&
		ApplyTestEvent(Scenario, 41) && Retry(Queue, Scenario) == SnapshotRetryStatus::NoRepair &&
		Scenario.Applier.HumanX == 18.0f && Queue.GetPendingRecordCount() == 0);
	return Passed;
}

static bool CheckQueuedLifecycleAndRepair()
{
	Fixture Scenario;
	ReplicaSnapshotQueue Queue;
	const auto Stalker = MakeStalker();
	if (!Expect("lifecycle fixture begins with only the stalker registered", Scenario.Synchronize() &&
		Scenario.Replicas.RegisterBody(World, StalkerParticipant, StalkerBody, 100u)))
	{
		return false;
	}

	bool Passed = Expect("a mixed batch retains only the unknown life and applies the other record immediately",
		Enqueue(Queue, Scenario, BuildBatch(Receiver, 101, 36, MakeHuman(HumanBody, 21.0f), &Stalker)) &&
		Queue.GetPendingBatchCount() == 1 && Queue.GetPendingRecordCount() == 1 && Scenario.Applier.StalkerAttempts == 1);
	Passed &= Expect("retry does not invent a replica while the body remains unknown",
		Retry(Queue, Scenario) == SnapshotRetryStatus::NoRepair && Queue.GetPendingRecordCount() == 1 &&
		Scenario.Replicas.Classify(World, HumanParticipant, HumanBody) == ReplicaBodyState::Unknown && Scenario.Applier.HumanAttempts == 0);
	Passed &= Expect("trusted registration releases the owned record after the receive input is gone",
		Scenario.Replicas.RegisterBody(World, HumanParticipant, HumanBody) &&
		Retry(Queue, Scenario) == SnapshotRetryStatus::NoRepair && Scenario.Applier.HumanX == 21.0f &&
		Scenario.Applier.StalkerAttempts == 1 && Queue.GetPendingRecordCount() == 0);
	Passed &= Expect("a held update drops when its body is retired before the event arrives",
		Enqueue(Queue, Scenario, BuildBatch(Receiver, 102, 37, MakeHuman())) && Queue.GetPendingRecordCount() == 1 &&
		Scenario.Replicas.Despawn(World, HumanParticipant, HumanBody) &&
		Retry(Queue, Scenario) == SnapshotRetryStatus::NoRepair && Queue.GetPendingRecordCount() == 0 &&
		Scenario.Applier.HumanAttempts == 1);

	constexpr EntityRef NewLife{HumanBody.ObjectId, HumanBody.Generation + 1, HumanBody.LifeId + 1};
	Passed &= Expect("a future life can wait for registration", Enqueue(Queue, Scenario, BuildBatch(Receiver, 103, 36, MakeHuman(NewLife))) &&
		Queue.GetPendingRecordCount() == 1 && Scenario.Replicas.RegisterBody(World, HumanParticipant, NewLife));
	Scenario.Applier.AllowHuman = false;
	Passed &= Expect("failed world application requests repair and leaves no automatic retries",
		Retry(Queue, Scenario) == SnapshotRetryStatus::RepairRequired && Queue.GetPendingRecordCount() == 0 &&
		Scenario.Applier.HumanAttempts == 2 && Retry(Queue, Scenario) == SnapshotRetryStatus::NoRepair &&
		Scenario.Applier.HumanAttempts == 2);
	Passed &= Expect("immediate failure likewise reports repair without retention",
		Enqueue(Queue, Scenario, BuildBatch(Receiver, 104, 36, MakeHuman(NewLife)), SnapshotRetryStatus::RepairRequired) &&
		Queue.GetPendingRecordCount() == 0);
	Scenario.Applier.AllowHuman = true;
	Passed &= Expect("repair can resubmit the failed tick", Enqueue(Queue, Scenario, BuildBatch(Receiver, 104, 36, MakeHuman(NewLife))) &&
		Scenario.Applier.HumanAttempts == 4 && Scenario.Applier.HumanX == 12.0f);
	return Passed;
}

static bool CheckQueueCapacity()
{
	Fixture Scenario;
	ReplicaSnapshotQueue Queue;
	if (!Expect("capacity fixture has two tracked bodies", Scenario.Synchronize() &&
		Scenario.Replicas.RegisterBody(World, HumanParticipant, HumanBody) &&
		Scenario.Replicas.RegisterBody(World, StalkerParticipant, StalkerBody)))
	{
		return false;
	}

	bool Passed = true;
	for (u32 Index = 0; Index < 8; ++Index)
	{
		Passed &= Expect("all eight independent event prerequisites fit", Enqueue(Queue, Scenario,
			BuildBatch(Receiver, 101 + Index, 37 + Index, MakeHuman())));
	}
	Passed &= Expect("eight batches occupy the capped queue", Queue.GetPendingBatchCount() == 8 && Queue.GetPendingRecordCount() == 8);
	Passed &= Expect("a replacement can reuse a full slot when its old record is superseded",
		Enqueue(Queue, Scenario, BuildBatch(Receiver, 110, 37, MakeHuman(HumanBody, 31.0f))) && Queue.GetPendingBatchCount() == 8);

	const auto Stalker = MakeStalker();
	constexpr EntityRef Unknown{99, 24, 9};
	const auto Overflow = BuildBatch(Receiver, 111, 36, MakeHuman(Unknown), &Stalker);
	Passed &= Expect("overflow offers repair, preserves old retention, and still applies an eligible body",
		Enqueue(Queue, Scenario, Overflow, SnapshotRetryStatus::RepairRequired) && Queue.GetPendingBatchCount() == 8 &&
		Queue.GetPendingRecordCount() == 8 && Scenario.Applier.StalkerAttempts == 1);
	Passed &= Expect("retry keeps and applies the confirmed replacement", ApplyTestEvent(Scenario, 37) &&
		Retry(Queue, Scenario) == SnapshotRetryStatus::NoRepair && Scenario.Applier.HumanX == 31.0f &&
		Queue.GetPendingRecordCount() == 0);
	Passed &= Expect("freed storage accepts a resubmission and never reapplies the successful body",
		Enqueue(Queue, Scenario, Overflow) && Queue.GetPendingBatchCount() == 1 && Queue.GetPendingRecordCount() == 1 &&
		Scenario.Applier.StalkerAttempts == 1);
	return Passed;
}

static bool CheckQueueScopeAndRebase()
{
	Fixture Scenario;
	ReplicaSnapshotQueue Queue;
	if (!Expect("scope fixture starts synchronized", Scenario.Synchronize() &&
		Scenario.Replicas.RegisterBody(World, HumanParticipant, HumanBody)))
	{
		return false;
	}

	const auto Pending = BuildBatch(Receiver, 140, 37, MakeHuman());
	const JoinIdentity Foreign{Receiver.ServerSession, Receiver.WorldEpoch, Receiver.ConnectionEpoch + 1};
	const auto Wrong = BuildBatch(Foreign, 141, 37, MakeHuman());
	if (!Expect("scope fixtures encode successfully", Pending && Wrong))
	{
		return false;
	}
	bool Passed = Expect("a foreign datagram cannot evict current pending work",
		Enqueue(Queue, Scenario, Pending) && HasError(Queue.Receive(*Wrong, Scenario.CatchUp, Scenario.Replicas, Scenario.Applier),
			ReplicaSnapshotBatchError::IdentityMismatch) && Queue.GetPendingBatchCount() == 1);

	constexpr BaselineBoundary Replacement{5, 105, 37};
	constexpr JoinBarrier NewBarrier{5, 3, 106, 37};
	Passed &= Expect("a completed rebase clears held records even without a call during synchronization",
		Scenario.CatchUp.Rebase(Receiver, Replacement).has_value() && Scenario.Synchronize(Replacement, NewBarrier) &&
		Retry(Queue, Scenario) == SnapshotRetryStatus::NoRepair && Queue.GetPendingRecordCount() == 0 &&
		Scenario.Applier.HumanAttempts == 0);
	Passed &= Expect("new barrier can retain another record",
		Enqueue(Queue, Scenario, BuildBatch(Receiver, 142, 38, MakeHuman())) && Queue.GetPendingRecordCount() == 1);
	ClientCatchUp Reconnected(Foreign, Replacement);
	Passed &= Expect("a new admitted connection clears retained data even in the same world",
		Reconnected.ConfirmBaselineApplied(Foreign, Replacement).has_value() &&
		Reconnected.DeclareBarrier(Foreign, NewBarrier).has_value() &&
		Reconnected.CompleteBarrier(Foreign, NewBarrier, 51, Scenario.Applier).has_value() &&
		Queue.RetryPending(Reconnected, Scenario.Replicas, Scenario.Applier) == SnapshotRetryStatus::NoRepair &&
		Queue.GetPendingRecordCount() == 0 && Scenario.Applier.HumanAttempts == 0);
	const auto Current = BuildBatch(Foreign, 143, 38, MakeHuman());
	Passed &= Expect("the current connection can hold its own update",
		Current && Queue.Receive(*Current, Reconnected, Scenario.Replicas, Scenario.Applier).has_value() &&
		Queue.GetPendingRecordCount() == 1);
	Passed &= Expect("advancing the trusted registry to a new world invalidates held data",
		Scenario.Replicas.AdvanceWorld(World.WorldEpoch + 1) &&
		Queue.RetryPending(Reconnected, Scenario.Replicas, Scenario.Applier) == SnapshotRetryStatus::NoRepair &&
		Queue.GetPendingRecordCount() == 0 && Scenario.Applier.HumanAttempts == 0);
	return Passed;
}

static bool CheckPartialReplacementAtCapacity()
{
	Fixture Scenario;
	ReplicaSnapshotQueue Queue;
	const auto Stalker = MakeStalker();
	if (!Expect("partial replacement fixture retains a mixed batch",
		Scenario.Synchronize() && Scenario.Replicas.RegisterBody(World, HumanParticipant, HumanBody) &&
		Scenario.Replicas.RegisterBody(World, StalkerParticipant, StalkerBody) &&
		Enqueue(Queue, Scenario, BuildBatch(Receiver, 101, 37, MakeHuman(HumanBody, 11.0f), &Stalker))))
	{
		return false;
	}
	bool Passed = true;
	for (u32 Offset = 1; Offset < 8; ++Offset)
	{
		Passed &= Expect("the other prerequisites fill the queue",
			Enqueue(Queue, Scenario, BuildBatch(Receiver, 101 + Offset, 37 + Offset, MakeHuman())));
	}
	Passed &= Expect("an overflowing replacement preserves its predecessor and sibling",
		Enqueue(Queue, Scenario, BuildBatch(Receiver, 109, 37, MakeHuman(HumanBody, 99.0f)), SnapshotRetryStatus::RepairRequired) &&
		Queue.GetPendingBatchCount() == 8 && Queue.GetPendingRecordCount() == 9);
	Passed &= Expect("retry applies both records from the preserved batch", ApplyTestEvent(Scenario, 37) &&
		Retry(Queue, Scenario) == SnapshotRetryStatus::NoRepair && Scenario.Applier.HumanX == 11.0f &&
		Scenario.Applier.StalkerX == 13.0f && Queue.GetPendingBatchCount() == 7);
	return Passed;
}

static bool CheckQueueSerialOrdering()
{
	constexpr BaselineBoundary Capture{4, 0xfffffffcu, 36};
	constexpr JoinBarrier Target{4, 2, 0xfffffffdu, 36};
	Fixture Scenario(Capture);
	ReplicaSnapshotQueue Queue;
	if (!Expect("serial-order fixture starts near wrap", Scenario.Synchronize(Capture, Target) &&
		Scenario.Replicas.RegisterBody(World, HumanParticipant, HumanBody, Capture.CaptureTick)))
	{
		return false;
	}

	bool Passed = Expect("retry orders deferred ticks newest first across wrap",
		Enqueue(Queue, Scenario, BuildBatch(Receiver, 0xffffffffu, 37, MakeHuman(HumanBody, 11.0f))) &&
		Enqueue(Queue, Scenario, BuildBatch(Receiver, 0, 38, MakeHuman(HumanBody, 21.0f))) &&
		ApplyTestEvent(Scenario, 37) && ApplyTestEvent(Scenario, 38) &&
		Retry(Queue, Scenario) == SnapshotRetryStatus::NoRepair && Scenario.Applier.HumanAttempts == 1 &&
		Scenario.Applier.HumanX == 21.0f && Queue.GetPendingRecordCount() == 0);
	constexpr EntityRef Unknown{99, 24, 9};
	Passed &= Expect("an unknown body with an ambiguous event watermark consumes no queue capacity",
		Enqueue(Queue, Scenario, BuildBatch(Receiver, 1, 38u + 0x80000000u, MakeHuman(Unknown))) &&
		Queue.GetPendingRecordCount() == 0 && Scenario.Applier.HumanAttempts == 1);
	return Passed;
}

bool RunQueue()
{
	bool Passed = CheckDeferredQueue();
	Passed &= CheckQueuedLatestUsefulState();
	Passed &= CheckQueuedLifecycleAndRepair();
	Passed &= CheckQueueCapacity();
	Passed &= CheckQueueScopeAndRebase();
	Passed &= CheckPartialReplacementAtCapacity();
	Passed &= CheckQueueSerialOrdering();
	if (!Passed)
	{
		return false;
	}
	std::puts("Replica snapshot queue checks passed");
	return true;
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
