#include "ClientCatchUp.h"
#include "CatchUpEventQueue.h"
#include "PeerJoin.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace ClientCatchUpTests
{
using namespace Multiplayer;

constexpr JoinIdentity Identity{0x8877665544332211ull, 17, 9};
constexpr BaselineBoundary Baseline{21, 100, 45};
constexpr JoinBarrier Barrier{21, 13, 102, 46};
constexpr u8 Data[] = {0x22, 0x00};
constexpr CatchUpEvent First{Identity, 21, 46, Data};

class FixtureApplier final : public IClientCatchUpApplier
{
public:
	bool ApplyEvent(const CatchUpEvent& Event) noexcept override
	{
		++EventCalls;
		return EventsSucceed && std::ranges::equal(Event.Bytes, Data);
	}

	bool ApplyBarrier(JoinIdentity Peer, JoinBarrier Barrier) noexcept override
	{
		++BarrierCalls;
		LastIdentity = Peer;
		LastBarrier = Barrier;
		return BarrierSucceeds;
	}

	int EventCalls = 0;
	int BarrierCalls = 0;
	bool EventsSucceed = true;
	bool BarrierSucceeds = true;
	JoinIdentity LastIdentity{};
	JoinBarrier LastBarrier{};
};

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

template <class Value>
static bool ExpectError(const char* Step, const std::expected<Value, ClientCatchUpError>& Result, ClientCatchUpError Error)
{
	if (!Result && Result.error() == Error)
	{
		return true;
	}
	std::fprintf(stderr, "%s: expected %d, got %d\n", Step, static_cast<int>(Error), Result ? -1 : static_cast<int>(Result.error()));
	return false;
}

static bool CheckOrderedEvents()
{
	ClientCatchUp Client(Identity, Baseline);
	FixtureApplier Applier;
	bool Passed = Expect("baseline capture has no applied watermark before import", !Client.GetAppliedWatermark());
	Passed &= ExpectError("event cannot apply before baseline import", Client.ApplyEvent(First, Applier), ClientCatchUpError::BaselineNotApplied);
	constexpr JoinIdentity Foreign[] = {
		{Identity.ServerSession + 1, Identity.WorldEpoch, Identity.ConnectionEpoch},
		{Identity.ServerSession, Identity.WorldEpoch + 1, Identity.ConnectionEpoch},
		{Identity.ServerSession, Identity.WorldEpoch, Identity.ConnectionEpoch + 1},
	};
	for (const auto& Other : Foreign)
	{
		Passed &= ExpectError("import confirmation requires all identity fields", Client.ConfirmBaselineApplied(Other, Baseline), ClientCatchUpError::IdentityMismatch);
	}
	Passed &= ExpectError("correct ID with a wrong capture tick cannot confirm import", Client.ConfirmBaselineApplied(Identity, {21, 101, 45}), ClientCatchUpError::BaselineMismatch);
	Passed &= ExpectError("correct ID with a wrong capture watermark cannot confirm import", Client.ConfirmBaselineApplied(Identity, {21, 100, 46}), ClientCatchUpError::BaselineMismatch);
	Passed &= Expect("valid confirmation seeds the capture watermark", Client.ConfirmBaselineApplied(Identity, Baseline).has_value() && Client.GetAppliedWatermark() == 45);
	for (const auto& Other : Foreign)
	{
		Passed &= ExpectError("event from another identity cannot reach the applier", Client.ApplyEvent({Other, 21, 46, Data}, Applier), ClientCatchUpError::IdentityMismatch);
	}
	Passed &= ExpectError("stale baseline event cannot reach the applier", Client.ApplyEvent({Identity, 20, 46, Data}, Applier), ClientCatchUpError::BaselineMismatch);
	Passed &= ExpectError("future event cannot skip a sequence", Client.ApplyEvent({Identity, 21, 47, Data}, Applier), ClientCatchUpError::UnexpectedSequence);
	Passed &= ExpectError("event already covered by capture is skipped", Client.ApplyEvent({Identity, 21, 45, Data}, Applier), ClientCatchUpError::UnexpectedSequence);
	Passed &= Expect("invalid input preserves both application and watermark", Applier.EventCalls == 0 && Client.GetAppliedWatermark() == 45);
	Applier.EventsSucceed = false;
	Passed &= ExpectError("an application failure leaves the event retryable", Client.ApplyEvent(First, Applier), ClientCatchUpError::ApplicationFailed);
	Passed &= Expect("failed application does not advance the watermark", Client.GetAppliedWatermark() == 45 && Applier.EventCalls == 1);
	Applier.EventsSucceed = true;
	Passed &= Expect("retry advances to the successfully applied sequence", Client.ApplyEvent(First, Applier).has_value() &&
		Client.GetAppliedWatermark() == 46 && Applier.EventCalls == 2);
	Passed &= ExpectError("a duplicate successful event cannot apply again", Client.ApplyEvent(First, Applier), ClientCatchUpError::UnexpectedSequence);
	Passed &= Expect("duplicate import confirmation cannot rewind progress", Client.ConfirmBaselineApplied(Identity, Baseline).has_value() &&
		Client.GetAppliedWatermark() == 46 && Applier.EventCalls == 2);
	return Passed;
}

static bool CheckReadiness()
{
	ClientCatchUp Client(Identity, Baseline);
	FixtureApplier Applier;
	bool Passed = ExpectError("completion requires a declared barrier", Client.CompleteBarrier(Identity, Barrier, 27, Applier), ClientCatchUpError::InvalidState);
	Passed &= ExpectError("barrier must name the selected baseline", Client.DeclareBarrier(Identity, {20, 13, 102, 46}), ClientCatchUpError::BaselineMismatch);
	Passed &= ExpectError("barrier tick cannot precede capture", Client.DeclareBarrier(Identity, {21, 13, 99, 46}), ClientCatchUpError::BarrierMismatch);
	Passed &= ExpectError("barrier watermark cannot precede capture", Client.DeclareBarrier(Identity, {21, 13, 102, 44}), ClientCatchUpError::BarrierMismatch);
	Passed &= ExpectError("half-range watermark is ambiguous", Client.DeclareBarrier(Identity, {21, 13, 102, 45 + 0x80000000u}), ClientCatchUpError::BarrierMismatch);
	Passed &= Expect("declaration can arrive before baseline import", Client.DeclareBarrier(Identity, Barrier).has_value());
	Passed &= ExpectError("barrier waits for baseline import", Client.CompleteBarrier(Identity, Barrier, 27, Applier), ClientCatchUpError::BaselineNotApplied);
	Passed &= Expect("baseline confirmation starts event application", Client.ConfirmBaselineApplied(Identity, Baseline).has_value());
	Passed &= ExpectError("barrier waits for all required events", Client.CompleteBarrier(Identity, Barrier, 27, Applier), ClientCatchUpError::BarrierPending);
	Passed &= Expect("barrier has not been applied prematurely", Applier.BarrierCalls == 0);
	Passed &= Expect("duplicate declaration is idempotent", Client.DeclareBarrier(Identity, Barrier).has_value());
	constexpr JoinBarrier Conflict{21, 14, 102, 46};
	Passed &= ExpectError("a competing declaration cannot replace the active barrier", Client.DeclareBarrier(Identity, Conflict), ClientCatchUpError::BarrierMismatch);
	Passed &= ExpectError("completion requires the exact declared barrier", Client.CompleteBarrier(Identity, Conflict, 27, Applier), ClientCatchUpError::BarrierMismatch);
	Passed &= Expect("event application reaches the declared watermark", Client.ApplyEvent(First, Applier).has_value() && Client.GetAppliedWatermark() == 46);
	constexpr CatchUpEvent Later{Identity, 21, 47, Data};
	Passed &= ExpectError("the finite barrier holds later events", Client.ApplyEvent(Later, Applier), ClientCatchUpError::BarrierPending);
	Applier.BarrierSucceeds = false;
	Passed &= ExpectError("failed snapshot application emits no acknowledgement", Client.CompleteBarrier(Identity, Barrier, 27, Applier), ClientCatchUpError::ApplicationFailed);
	Passed &= ExpectError("failed barrier still holds later events", Client.ApplyEvent(Later, Applier), ClientCatchUpError::BarrierPending);
	Applier.BarrierSucceeds = true;
	const auto Ack = Client.CompleteBarrier(Identity, Barrier, 27, Applier);
	Passed &= Expect("successful barrier yields the exact acknowledgement", Ack && Ack->Identity == Identity && Ack->Barrier == Barrier &&
		Ack->MessageSequence == 27 && Applier.LastIdentity == Identity && Applier.LastBarrier == Barrier && Applier.BarrierCalls == 2);
	Passed &= Expect("deferred event can now apply in order", Client.ApplyEvent(Later, Applier).has_value() && Client.GetAppliedWatermark() == 47 && Applier.EventCalls == 2);
	Passed &= Expect("duplicate declaration after later events is still idempotent", Client.DeclareBarrier(Identity, Barrier).has_value());
	const auto Retry = Client.CompleteBarrier(Identity, Barrier, 28, Applier);
	Passed &= Expect("sending the acknowledgement again leaves applied state alone", Retry && Retry->Barrier == Barrier && Retry->MessageSequence == 28 &&
		Applier.BarrierCalls == 2 && Client.GetAppliedWatermark() == 47);
	return Passed;
}

static bool CheckRebase()
{
	ClientCatchUp Client(Identity, Baseline);
	FixtureApplier Applier;
	bool Passed = Expect("first baseline and event apply", Client.ConfirmBaselineApplied(Identity, Baseline).has_value() && Client.ApplyEvent(First, Applier).has_value());
	Passed &= ExpectError("a late declaration cannot rewind applied events", Client.DeclareBarrier(Identity, {21, 13, 102, 45}), ClientCatchUpError::BarrierMismatch);
	Passed &= Expect("current barrier can still complete", Client.DeclareBarrier(Identity, Barrier).has_value() &&
		Client.CompleteBarrier(Identity, Barrier, 27, Applier).has_value());
	Passed &= Expect("event after local readiness advances the frontier", Client.ApplyEvent({Identity, 21, 47, Data}, Applier).has_value());
	constexpr BaselineBoundary Replacement{22, 103, 47};
	constexpr JoinIdentity NextWorld{Identity.ServerSession, Identity.WorldEpoch + 1, Identity.ConnectionEpoch};
	Passed &= ExpectError("same-round replacement cannot switch worlds", Client.Rebase(NextWorld, Replacement), ClientCatchUpError::IdentityMismatch);
	Passed &= ExpectError("replacement must advance the capture tick", Client.Rebase(Identity, {22, 100, 47}), ClientCatchUpError::StaleBaseline);
	Passed &= ExpectError("replacement must cover the applied watermark", Client.Rebase(Identity, {22, 103, 46}), ClientCatchUpError::StaleBaseline);
	Passed &= ExpectError("replacement cannot predate an already applied barrier snapshot", Client.Rebase(Identity, {22, 101, 47}), ClientCatchUpError::StaleBaseline);
	Passed &= Expect("rejected replacement preserves the applied barrier", Client.CompleteBarrier(Identity, Barrier, 28, Applier).has_value() &&
		Client.GetAppliedWatermark() == 47 && Applier.BarrierCalls == 1);
	Passed &= Expect("new capture invalidates previous application and acknowledgement", Client.Rebase(Identity, Replacement).has_value() && !Client.GetAppliedWatermark());
	Passed &= ExpectError("previous barrier cannot be acknowledged after rebase", Client.CompleteBarrier(Identity, Barrier, 29, Applier), ClientCatchUpError::BaselineMismatch);
	Passed &= ExpectError("previous baseline cannot confirm replacement import", Client.ConfirmBaselineApplied(Identity, Baseline), ClientCatchUpError::BaselineMismatch);
	constexpr CatchUpEvent Next{Identity, 22, 48, Data};
	Passed &= ExpectError("new event waits for replacement import", Client.ApplyEvent(Next, Applier), ClientCatchUpError::BaselineNotApplied);
	constexpr JoinBarrier NewBarrier{22, 15, 104, 48};
	Passed &= Expect("replacement uses a fresh barrier and event stream", Client.DeclareBarrier(Identity, NewBarrier).has_value() &&
		Client.ConfirmBaselineApplied(Identity, Replacement).has_value() && Client.ApplyEvent(Next, Applier).has_value());
	const auto Ack = Client.CompleteBarrier(Identity, NewBarrier, 30, Applier);
	Passed &= Expect("replacement acknowledgement names only the new barrier", Ack && Ack->Barrier == NewBarrier && Applier.BarrierCalls == 2);
	return Passed;
}

static bool CheckEmptyCatchUp()
{
	ClientCatchUp Client(Identity, Baseline);
	FixtureApplier Applier;
	constexpr JoinBarrier AtCapture{21, 12, 100, 45};
	bool Passed = Expect("quiet capture supports a barrier without intervening events", Client.ConfirmBaselineApplied(Identity, Baseline).has_value() &&
		Client.DeclareBarrier(Identity, AtCapture).has_value());
	constexpr JoinIdentity Foreign{Identity.ServerSession, Identity.WorldEpoch, Identity.ConnectionEpoch + 1};
	Passed &= ExpectError("another connection cannot complete the exact barrier", Client.CompleteBarrier(Foreign, AtCapture, 4, Applier), ClientCatchUpError::IdentityMismatch);
	Passed &= Expect("empty catch-up supports immediate barrier application", Client.CompleteBarrier(Identity, AtCapture, 4, Applier).has_value() &&
		Applier.EventCalls == 0 && Applier.BarrierCalls == 1 && Client.GetAppliedWatermark() == 45);
	return Passed;
}

static bool CheckSequenceWrap()
{
	constexpr BaselineBoundary PreWrap{21, 0xfffffffeu, 0xffffffffu};
	constexpr JoinBarrier PostWrap{21, 6, 0, 0};
	ClientCatchUp Client(Identity, PreWrap);
	FixtureApplier Applier;
	bool Passed = Expect("barrier can follow a wrapped tick and watermark", Client.ConfirmBaselineApplied(Identity, PreWrap).has_value() &&
		Client.DeclareBarrier(Identity, PostWrap).has_value());
	Passed &= ExpectError("wrap still requires the missing sequence", Client.CompleteBarrier(Identity, PostWrap, 3, Applier), ClientCatchUpError::BarrierPending);
	Passed &= Expect("zero is the next applied sequence after wrap", Client.ApplyEvent({Identity, 21, 0, Data}, Applier).has_value() && Client.GetAppliedWatermark() == 0u);
	Passed &= Expect("wrapped barrier can be acknowledged", Client.CompleteBarrier(Identity, PostWrap, 3, Applier).has_value());
	return Passed;
}

static bool CheckServerHandoff()
{
	using namespace std::chrono_literals;
	constexpr auto Now = PeerJoin::Clock::time_point{} + 1s;
	PeerJoin Server(Identity, Now);
	ClientCatchUp Client(Identity, Baseline);
	FixtureApplier Applier;
	std::array<u16, 1> Lengths{};
	std::array<u8, 2> Storage{};
	std::array<u8, 2> Output{};
	CatchUpEventQueue Queue(Identity, Baseline, Lengths, Storage);
	bool Passed = Expect("server selects the same baseline confirmed by the client", Server.ConfirmLoaded(Identity, Now).has_value() &&
		Server.SelectBaseline(Identity, Baseline, Now).has_value() && Client.ConfirmBaselineApplied(Identity, Baseline).has_value());
	Passed &= Expect("server retains event after the capture", Queue.Append(First).has_value());
	const auto Delivered = Queue.Peek(Output);
	Passed &= Expect("transport accepts the retained event", Delivered && Queue.CommitSend(*Delivered).has_value());
	const JoinBarrier Target{21, 13, 102, Queue.GetSubmittedWatermark()};
	Passed &= Expect("barrier uses the submitted watermark", Target == Barrier && Server.DeclareBarrier(Identity, Target, Now + 1s).has_value() &&
		Client.DeclareBarrier(Identity, Target).has_value());
	Passed &= ExpectError("submitted events alone cannot produce readiness", Client.CompleteBarrier(Identity, Target, 27, Applier), ClientCatchUpError::BarrierPending);
	Passed &= Expect("client applies the delivered event", Delivered && Client.ApplyEvent(*Delivered, Applier).has_value());
	const auto Ack = Client.CompleteBarrier(Identity, Target, 27, Applier);
	std::array<u8, 46> Datagram{};
	const auto Size = Ack ? Wire::EncodeReadyAcknowledgement(*Ack, Datagram) : std::unexpected(Wire::ClientJoinError::InvalidEnvelope);
	const auto Decoded = Size ? Wire::DecodeReadyAcknowledgement(xr_span(Datagram).first(*Size)) : std::unexpected(Wire::ClientJoinError::InvalidEnvelope);
	Passed &= Expect("server accepts the encoded acknowledgement only after client application", Decoded &&
		Server.AcknowledgeBarrier(Decoded->Identity, Decoded->Barrier, Now + 2s).has_value() && Server.GetState() == PeerJoinState::ReadySpectator);
	return Passed;
}

bool Run()
{
	bool Passed = CheckOrderedEvents();
	Passed &= CheckReadiness();
	Passed &= CheckRebase();
	Passed &= CheckEmptyCatchUp();
	Passed &= CheckSequenceWrap();
	Passed &= CheckServerHandoff();
	if (!Passed)
	{
		return false;
	}
	std::puts("Client catch-up checks passed");
	return true;
}
}
