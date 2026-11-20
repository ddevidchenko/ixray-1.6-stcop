#include "../../../xrGame/Multiplayer/CombatantBaselineImport.h"

#include <algorithm>
#include <cstdio>
#include <memory>

namespace CombatantBaselineImportTests
{
using namespace Multiplayer;
using namespace Multiplayer::Wire;

constexpr JoinIdentity Receiver{0x123456789abcdef0ull, 7, 11};
constexpr WorldIdentity World{Receiver.ServerSession, Receiver.WorldEpoch};
constexpr BaselineBoundary Capture{4, 100, 45};
constexpr ParticipantId HumanParticipant{17};
constexpr ParticipantId StalkerParticipant{18};
constexpr EntityRef HumanBody{31, 8, 2};
constexpr EntityRef StalkerBody{32, 9, 1};

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

static HumanCombatantSnapshot MakeHuman(ParticipantId Participant = HumanParticipant, EntityRef Body = HumanBody, f32 X = 23.0f)
{
	HumanCombatantSnapshot Snapshot{};
	Snapshot.State.Participant = Participant;
	Snapshot.State.Body = Body;
	Snapshot.State.BodyProfileId = 42;
	Snapshot.State.Position[0] = X;
	return Snapshot;
}

static StalkerCombatantSnapshot MakeStalker(ParticipantId Participant = StalkerParticipant, EntityRef Body = StalkerBody)
{
	StalkerCombatantSnapshot Snapshot{};
	Snapshot.State.Participant = Participant;
	Snapshot.State.Body = Body;
	Snapshot.State.BodyProfileId = 43;
	Snapshot.State.Position[0] = 27.0f;
	Snapshot.AnimationProfileId = 44;
	return Snapshot;
}

static std::expected<CombatantBaseline, BaselineRecordsError> DecodeCapture(JoinIdentity Identity, BaselineBoundary Boundary,
	xr_span<const CombatantSnapshot> Records)
{
	xr_array<u8, MaxCombatantBaselineBytes> Bytes;
	const auto Manifest = EncodeCombatantBaseline(Boundary, Records, Bytes);
	if (!Manifest)
	{
		return std::unexpected(BaselineRecordsError::InvalidRecord);
	}
	return CombatantBaseline::Decode({Identity, *Manifest, xr_span(Bytes).first(Manifest->ByteCount)}, Identity, Boundary);
}

class WorldFixture final : public ICombatantBaselineApplier, public IClientCatchUpApplier
{
public:
	bool ApplyBaseline(const CombatantBaseline& Baseline) noexcept override
	{
		++BaselineAttempts;
		if (!AllowImport)
		{
			return false;
		}
		const auto Records = Baseline.GetRecords();
		std::ranges::copy(Records, Bodies.begin());
		Count = Records.size();
		return true;
	}

	bool ApplyEvent(const CatchUpEvent&) noexcept override
	{
		++EventAttempts;
		return true;
	}

	bool ApplyBarrier(JoinIdentity, JoinBarrier) noexcept override
	{
		++BarrierAttempts;
		return true;
	}

	[[nodiscard]] xr_span<const CombatantSnapshot> GetBodies() const noexcept { return xr_span(Bodies).first(Count); }

	bool AllowImport = true;
	int BaselineAttempts = 0;
	int EventAttempts = 0;
	int BarrierAttempts = 0;

private:
	xr_array<CombatantSnapshot, MaxCombatants> Bodies{};
	std::size_t Count = 0;
};

static bool CheckFirstImport()
{
	const CombatantSnapshot Expected[]{MakeHuman(), MakeStalker()};
	const auto Baseline = DecodeCapture(Receiver, Capture, Expected);
	if (!Expect("the public capture decodes before import", Baseline.has_value()))
	{
		return false;
	}

	auto History = std::make_unique<RetiredCombatantGenerations>();
	CombatantReplicas Replicas(World, *History);
	ClientCatchUp CatchUp(Receiver, Capture);
	WorldFixture Applier;
	const auto Result = ImportCombatantBaseline(*Baseline, CatchUp, Replicas, Applier);
	return Expect("successful import publishes the typed world, both replicas and the exact applied watermark",
		Result && Applier.BaselineAttempts == 1 && std::ranges::equal(Applier.GetBodies(), Expected) &&
		Replicas.Classify(World, HumanParticipant, HumanBody) == ReplicaBodyState::Current &&
		Replicas.Classify(World, StalkerParticipant, StalkerBody) == ReplicaBodyState::Current &&
		CatchUp.GetAppliedWatermark() == Capture.EventWatermark);
}

template <class Error>
static bool HasImportError(const std::expected<void, CombatantBaselineImportError>& Result, Error Code)
{
	return !Result && Result.error() == CombatantBaselineImportError{Code};
}

class Scenario
{
public:
	explicit Scenario(BaselineBoundary Selected = Capture) : CatchUp(Receiver, Selected) {}

	auto Import(const CombatantBaseline& Baseline)
	{
		return ImportCombatantBaseline(Baseline, CatchUp, Replicas, Applier);
	}

	std::unique_ptr<RetiredCombatantGenerations> History{std::make_unique<RetiredCombatantGenerations>()};
	CombatantReplicas Replicas{World, *History};
	ClientCatchUp CatchUp;
	WorldFixture Applier;
};

static bool CheckScope()
{
	Scenario Test;
	const CombatantSnapshot Records[]{MakeHuman()};
	constexpr JoinIdentity Foreign[]{
		{Receiver.ServerSession + 1, Receiver.WorldEpoch, Receiver.ConnectionEpoch},
		{Receiver.ServerSession, Receiver.WorldEpoch + 1, Receiver.ConnectionEpoch},
		{Receiver.ServerSession, Receiver.WorldEpoch, Receiver.ConnectionEpoch + 1},
	};
	bool Passed = true;
	for (const auto Identity : Foreign)
	{
		const auto Other = DecodeCapture(Identity, Capture, Records);
		Passed &= Expect("a capture checked for another peer cannot reach the world",
			Other && HasImportError(Test.Import(*Other), ClientCatchUpError::IdentityMismatch));
	}
	constexpr BaselineBoundary Unselected[]{
		{5, 100, 45},
		{4, 101, 45},
		{4, 100, 46},
	};
	for (const auto Boundary : Unselected)
	{
		const auto Other = DecodeCapture(Receiver, Boundary, Records);
		Passed &= Expect("the catch-up tracker must select this exact capture",
			Other && HasImportError(Test.Import(*Other), ClientCatchUpError::BaselineMismatch));
	}
	const auto Baseline = DecodeCapture(Receiver, Capture, Records);
	Passed &= Expect("the registry's world must match the admitted tracker",
		Baseline && Test.Replicas.AdvanceWorld(World.WorldEpoch + 1) &&
		HasImportError(Test.Import(*Baseline), ReplicaBaselineError::WorldMismatch));
	Passed &= Expect("scope failure leaves the capture unapplied",
		!Test.CatchUp.GetAppliedWatermark() && Test.Applier.BaselineAttempts == 0 && Test.Applier.GetBodies().empty());
	return Passed;
}

static bool CheckFailedReplacementAndRetry()
{
	constexpr ParticipantId NewParticipant{19};
	constexpr EntityRef NewHuman{StalkerBody.ObjectId, 11, HumanBody.LifeId + 1};
	constexpr EntityRef NewStalker{HumanBody.ObjectId, 12, 1};
	constexpr BaselineBoundary Replacement{5, 105, 48};
	const CombatantSnapshot Initial[]{MakeHuman(), MakeStalker()};
	const CombatantSnapshot Next[]{MakeHuman(HumanParticipant, NewHuman, 30.0f), MakeStalker(NewParticipant, NewStalker)};
	const auto Before = DecodeCapture(Receiver, Capture, Initial);
	const auto After = DecodeCapture(Receiver, Replacement, Next);
	Scenario Test;
	if (!Expect("a rebase can replace both occupied slots atomically", Before && After && Test.Import(*Before) &&
		Test.CatchUp.Rebase(Receiver, Replacement)))
	{
		return false;
	}

	Test.Applier.AllowImport = false;
	bool Passed = Expect("a rejected world import leaves the accepted replacement retryable",
		HasImportError(Test.Import(*After), ReplicaBaselineError::ApplicationFailed) && Test.Applier.BaselineAttempts == 2 &&
		!Test.CatchUp.GetAppliedWatermark());
	Passed &= Expect("world state and replica history stay intact on application failure",
		std::ranges::equal(Test.Applier.GetBodies(), Initial) &&
		Test.Replicas.Classify(World, HumanParticipant, HumanBody) == ReplicaBodyState::Current &&
		Test.Replicas.Classify(World, StalkerParticipant, StalkerBody) == ReplicaBodyState::Current &&
		Test.Replicas.Classify(World, HumanParticipant, NewHuman) == ReplicaBodyState::Unknown &&
		(*Test.History)[HumanBody.ObjectId] == 0 && (*Test.History)[StalkerBody.ObjectId] == 0);

	Test.Applier.AllowImport = true;
	Passed &= Expect("successful retry commits both new lives and the exact event frontier",
		Test.Import(*After) && Test.Applier.BaselineAttempts == 3 && std::ranges::equal(Test.Applier.GetBodies(), Next) &&
		Test.CatchUp.GetAppliedWatermark() == Replacement.EventWatermark &&
		Test.Replicas.Classify(World, HumanParticipant, NewHuman) == ReplicaBodyState::Current &&
		Test.Replicas.Classify(World, NewParticipant, NewStalker) == ReplicaBodyState::Current);
	Passed &= Expect("both replaced slot generations stay retired",
		(*Test.History)[HumanBody.ObjectId] == HumanBody.Generation && (*Test.History)[StalkerBody.ObjectId] == StalkerBody.Generation &&
		Test.Replicas.Classify(World, HumanParticipant, HumanBody) == ReplicaBodyState::Stale &&
		Test.Replicas.Classify(World, StalkerParticipant, StalkerBody) == ReplicaBodyState::Stale);
	return Passed;
}

static bool CheckConfirmationAndTickSeed()
{
	const CombatantSnapshot Initial[]{MakeHuman()};
	const auto Baseline = DecodeCapture(Receiver, Capture, Initial);
	if (!Expect("the confirmation fixture decodes", Baseline.has_value()))
	{
		return false;
	}

	Scenario Test;
	constexpr JoinBarrier Barrier{4, 7, 101, 46};
	constexpr u8 Bytes[]{1};
	const CatchUpEvent Event{Receiver, Capture.BaselineId, 46, Bytes};
	bool Passed = Expect("barrier may be declared early while events still wait for import",
		Test.CatchUp.DeclareBarrier(Receiver, Barrier) &&
		Test.CatchUp.ApplyEvent(Event, Test.Applier) == std::unexpected(ClientCatchUpError::BaselineNotApplied) &&
		Test.Applier.EventAttempts == 0);
	Passed &= Expect("successful import unlocks events and the exact barrier acknowledgement",
		Test.Import(*Baseline) && Test.CatchUp.ApplyEvent(Event, Test.Applier) && Test.Applier.EventAttempts == 1);
	const auto Ack = Test.CatchUp.CompleteBarrier(Receiver, Barrier, 8, Test.Applier);
	Passed &= Expect("the applied barrier can be acknowledged", Ack && Ack->Identity == Receiver && Ack->Barrier == Barrier &&
		Test.Applier.BarrierAttempts == 1);

	int Applies = 0;
	auto Apply = [&]() noexcept { ++Applies; return true; };
	Passed &= Expect("import seeds the replica at the exact capture tick",
		Test.Replicas.ApplyLiveSnapshot(Receiver, 46u, {Receiver, HumanParticipant, HumanBody, 100, 46}, Apply) ==
			std::unexpected(ReplicaSnapshotError::StaleTick) && Applies == 0);
	Passed &= Expect("a newer live tick is accepted", Test.Replicas.ApplyLiveSnapshot(Receiver, 46u,
		{Receiver, HumanParticipant, HumanBody, 103, 46}, Apply).has_value() && Applies == 1);
	Passed &= Expect("duplicate capture leaves newer events, barrier and replica tick intact", Test.Import(*Baseline) &&
		Test.Applier.BaselineAttempts == 1 && Test.CatchUp.GetAppliedWatermark() == 46 && Test.CatchUp.GetAppliedBarrier() == Barrier &&
		Test.Replicas.ApplyLiveSnapshot(Receiver, 46u, {Receiver, HumanParticipant, HumanBody, 102, 46}, Apply) ==
			std::unexpected(ReplicaSnapshotError::StaleTick) && Applies == 1);

	constexpr BaselineBoundary Rewind{5, 102, 46};
	const auto Older = DecodeCapture(Receiver, Rewind, {});
	Passed &= Expect("a new selection cannot replace earlier live state even by omitting the body",
		Older && Test.CatchUp.Rebase(Receiver, Rewind) && HasImportError(Test.Import(*Older), ReplicaBaselineError::StaleCapture) &&
		Test.Applier.BaselineAttempts == 1 && !Test.CatchUp.GetAppliedWatermark() &&
		Test.Replicas.Classify(World, HumanParticipant, HumanBody) == ReplicaBodyState::Current);

	constexpr BaselineBoundary Updated{6, 106, 48};
	const CombatantSnapshot UpdatedRecords[]{MakeHuman(HumanParticipant, HumanBody, 36.0f)};
	const auto Later = DecodeCapture(Receiver, Updated, UpdatedRecords);
	Passed &= Expect("a retained life is seeded at the replacement capture tick without retirement",
		Later && Test.CatchUp.Rebase(Receiver, Updated) && Test.Import(*Later) &&
		std::ranges::equal(Test.Applier.GetBodies(), UpdatedRecords) && (*Test.History)[HumanBody.ObjectId] == 0 &&
		Test.Replicas.ApplyLiveSnapshot(Receiver, 48u, {Receiver, HumanParticipant, HumanBody, 105, 48}, Apply) ==
			std::unexpected(ReplicaSnapshotError::StaleTick) && Applies == 1);
	return Passed;
}

static bool CheckBodyConflicts()
{
	constexpr EntityRef EqualGenerationWrongLife{StalkerBody.ObjectId, StalkerBody.Generation, StalkerBody.LifeId + 1};
	constexpr EntityRef SameLifeNewSlot{33, StalkerBody.Generation + 1, StalkerBody.LifeId};
	constexpr EntityRef OlderGeneration{33, StalkerBody.Generation - 1, StalkerBody.LifeId + 1};
	constexpr ParticipantId NewParticipant{19};
	constexpr EntityRef ValidNewBody{34, 20, 1};
	Scenario Test;
	if (!Expect("scope fixture has retired and active history",
		Test.Replicas.RegisterBody(World, HumanParticipant, HumanBody) && Test.Replicas.Despawn(World, HumanParticipant, HumanBody) &&
		Test.Replicas.RegisterBody(World, StalkerParticipant, StalkerBody)))
	{
		return false;
	}

	const CombatantSnapshot Retired[]{MakeHuman(NewParticipant, ValidNewBody), MakeHuman()};
	const auto Replay = DecodeCapture(Receiver, Capture, Retired);
	bool Passed = Expect("retired generations reject the entire capture before any valid sibling applies",
		Replay && HasImportError(Test.Import(*Replay), ReplicaBaselineError::StaleBody));
	const CombatantSnapshot Conflict[]{MakeStalker(StalkerParticipant, EqualGenerationWrongLife)};
	const auto SameGeneration = DecodeCapture(Receiver, Capture, Conflict);
	Passed &= Expect("the same active slot generation cannot change life",
		SameGeneration && HasImportError(Test.Import(*SameGeneration), ReplicaBaselineError::ConflictingBody));

	for (const auto Body : {SameLifeNewSlot, OlderGeneration})
	{
		const CombatantSnapshot Moved[]{MakeStalker(StalkerParticipant, Body)};
		const auto Invalid = DecodeCapture(Receiver, Capture, Moved);
		Passed &= Expect("a changed active participant must progress in generation and life",
			Invalid && HasImportError(Test.Import(*Invalid), ReplicaBaselineError::StaleBody));
	}
	Passed &= Expect("rejected records do not touch the world, registry or event frontier",
		Test.Applier.BaselineAttempts == 0 && !Test.CatchUp.GetAppliedWatermark() &&
		Test.Replicas.Classify(World, NewParticipant, ValidNewBody) == ReplicaBodyState::Unknown &&
		Test.Replicas.Classify(World, StalkerParticipant, StalkerBody) == ReplicaBodyState::Current &&
		(*Test.History)[HumanBody.ObjectId] == HumanBody.Generation);
	return Passed;
}

static bool CheckEmptyAndWorldAdvance()
{
	constexpr BaselineBoundary Initial{0xffffffffu, 0xfffffffeu, 0xffffffffu};
	constexpr BaselineBoundary Empty{0, 0, 0};
	const CombatantSnapshot Records[]{MakeHuman()};
	const auto First = DecodeCapture(Receiver, Initial, Records);
	const auto Clear = DecodeCapture(Receiver, Empty, {});
	Scenario Test(Initial);
	if (!Expect("wrap fixture imports a live body", First && Clear && Test.Import(*First)))
	{
		return false;
	}

	bool Passed = Expect("an empty capture is a real replacement across serial wrap", Test.CatchUp.Rebase(Receiver, Empty) &&
		Test.Import(*Clear) && Test.CatchUp.GetAppliedWatermark() == 0 && Test.Applier.GetBodies().empty() &&
		Test.Replicas.Classify(World, HumanParticipant, HumanBody) == ReplicaBodyState::Stale);
	Passed &= Expect("zero watermark still records a completed capture",
		Test.Import(*Clear) && Test.Applier.BaselineAttempts == 2);

	constexpr JoinIdentity Reconnected{Receiver.ServerSession, Receiver.WorldEpoch, Receiver.ConnectionEpoch + 1};
	constexpr BaselineBoundary Earlier{17, 0xffffffffu, 0};
	const CombatantSnapshot Unseen[]{MakeHuman(HumanParticipant, {33, 19, 3})};
	ClientCatchUp Reconnect(Reconnected, Earlier);
	const auto OldCapture = DecodeCapture(Reconnected, Earlier, Unseen);
	Passed &= Expect("the last full capture prevents rewind across a reconnect even when the registry is empty",
		OldCapture && HasImportError(ImportCombatantBaseline(*OldCapture, Reconnect, Test.Replicas, Test.Applier),
			ReplicaBaselineError::StaleCapture) && Test.Applier.BaselineAttempts == 2 && Test.Applier.GetBodies().empty() &&
		!Reconnect.GetAppliedWatermark());

	constexpr JoinIdentity NewReceiver{Receiver.ServerSession, Receiver.WorldEpoch + 1, Receiver.ConnectionEpoch};
	constexpr WorldIdentity NewWorld{NewReceiver.ServerSession, NewReceiver.WorldEpoch};
	ClientCatchUp Next(NewReceiver, Earlier);
	const auto NextBaseline = DecodeCapture(NewReceiver, Earlier, Records);
	Passed &= Expect("an accepted world advance allows numeric identities to recur",
		NextBaseline && Test.Replicas.AdvanceWorld(NewWorld.WorldEpoch) &&
		ImportCombatantBaseline(*NextBaseline, Next, Test.Replicas, Test.Applier) && Next.GetAppliedWatermark() == 0 &&
		Test.Replicas.Classify(NewWorld, HumanParticipant, HumanBody) == ReplicaBodyState::Current &&
		(*Test.History)[HumanBody.ObjectId] == 0);
	return Passed;
}

static bool CheckRegistryInputLimits()
{
	Scenario Test;
	int Calls = 0;
	auto Apply = [&]() noexcept { ++Calls; return true; };
	const ReplicaBodyIdentity Invalid[]{{{}, HumanBody}};
	const ReplicaBodyIdentity Duplicate[]{{HumanParticipant, HumanBody}, {HumanParticipant, StalkerBody}};
	xr_array<ReplicaBodyIdentity, 33> Oversized{};
	for (u32 Index = 0; Index < Oversized.size(); ++Index)
	{
		Oversized[Index] = {{Index + 1}, {static_cast<u16>(Index), Index + 1, 1}};
	}
	bool Passed = Expect("the registry preflights a complete identity list before invoking any write",
		Test.Replicas.ReplaceFromBaseline(World, 100, Invalid, Apply) == std::unexpected(ReplicaBaselineError::InvalidBody) &&
		Test.Replicas.ReplaceFromBaseline(World, 100, Duplicate, Apply) == std::unexpected(ReplicaBaselineError::ConflictingBody) &&
		Test.Replicas.ReplaceFromBaseline(World, 100, Oversized, Apply) == std::unexpected(ReplicaBaselineError::InvalidBody) && Calls == 0);
	Passed &= Expect("exactly 32 distinct identities fit",
		Test.Replicas.ReplaceFromBaseline(World, 100, xr_span(Oversized).first(32), Apply).has_value() && Calls == 1 &&
		Test.Replicas.Classify(World, Oversized[31].Participant, Oversized[31].Body) == ReplicaBodyState::Current);
	return Passed;
}

bool Run()
{
	bool Passed = CheckFirstImport();
	Passed &= CheckScope();
	Passed &= CheckFailedReplacementAndRetry();
	Passed &= CheckConfirmationAndTickSeed();
	Passed &= CheckBodyConflicts();
	Passed &= CheckEmptyAndWorldAdvance();
	Passed &= CheckRegistryInputLimits();
	if (!Passed)
	{
		return false;
	}
	std::puts("Combatant baseline import checks passed");
	return true;
}
}
