#include "PeerJoin.h"
#include "BaselineStaging.h"

#include <cstdio>

namespace PeerRebaseTests
{
using namespace Multiplayer;
using namespace std::chrono_literals;

constexpr JoinIdentity Identity{0x8877665544332211ull, 17, 9};
constexpr JoinIdentity NextWorld{Identity.ServerSession, Identity.WorldEpoch + 1, Identity.ConnectionEpoch};
constexpr auto AdmittedAt = PeerJoin::Clock::time_point{} + 11s;
constexpr auto LoadedAt = AdmittedAt + 10s;
constexpr BaselineBoundary Initial{21, 100, 45};
constexpr BaselineBoundary Replacement{22, 105, 46};
constexpr JoinBarrier FirstBarrier{21, 80, 104, 46};
constexpr JoinBarrier NextBarrier{22, 81, 105, 46};

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

static bool ExpectError(const char* Step, const std::expected<void, PeerJoinError>& Result, PeerJoinError Error)
{
	if (!Result && Result.error() == Error)
	{
		return true;
	}
	std::fprintf(stderr, "%s: expected %d, got %d\n", Step, static_cast<int>(Error), Result ? -1 : static_cast<int>(Result.error()));
	return false;
}

static bool CheckSelectionAndBoundary()
{
	PeerJoin Peer(Identity, AdmittedAt);
	bool Passed = ExpectError("a loading peer cannot select a baseline", Peer.SelectBaseline(Identity, Initial, AdmittedAt), PeerJoinError::InvalidState);
	Passed &= ExpectError("a loading peer cannot rebase", Peer.Rebase(Identity, Initial, AdmittedAt), PeerJoinError::InvalidState);
	Passed &= Expect("peer is ready to select its initial baseline", Peer.ConfirmLoaded(Identity, LoadedAt).has_value());
	Passed &= ExpectError("barrier requires an explicit selection", Peer.DeclareBarrier(Identity, FirstBarrier, LoadedAt), PeerJoinError::InvalidState);
	Passed &= ExpectError("initial selection cannot be treated as a rebase", Peer.Rebase(Identity, Initial, LoadedAt), PeerJoinError::InvalidState);
	Passed &= ExpectError("wrong world cannot select a baseline", Peer.SelectBaseline(NextWorld, Initial, LoadedAt), PeerJoinError::IdentityMismatch);
	Passed &= Expect("current-world selection succeeds", Peer.SelectBaseline(Identity, Initial, LoadedAt).has_value());
	Passed &= ExpectError("a second selection cannot bypass rebase checks", Peer.SelectBaseline(Identity, Replacement, LoadedAt), PeerJoinError::InvalidState);
	Passed &= ExpectError("the barrier belongs to the selected baseline", Peer.DeclareBarrier(Identity, NextBarrier, LoadedAt), PeerJoinError::BaselineMismatch);
	Passed &= ExpectError("snapshot tick cannot precede capture", Peer.DeclareBarrier(Identity, {21, 80, 99, 45}, LoadedAt), PeerJoinError::BarrierMismatch);
	Passed &= ExpectError("event watermark cannot precede capture", Peer.DeclareBarrier(Identity, {21, 80, 101, 44}, LoadedAt), PeerJoinError::BarrierMismatch);
	Passed &= ExpectError("half-range tick is ambiguous", Peer.DeclareBarrier(Identity, {21, 80, 100 + 0x80000000u, 45}, LoadedAt), PeerJoinError::BarrierMismatch);
	Passed &= ExpectError("half-range watermark is ambiguous", Peer.DeclareBarrier(Identity, {21, 80, 101, 45 + 0x80000000u}, LoadedAt), PeerJoinError::BarrierMismatch);
	Passed &= Expect("barrier at the captured tick and watermark makes the peer ready",
		Peer.DeclareBarrier(Identity, {21, 80, 100, 45}, LoadedAt).has_value() &&
		Peer.AcknowledgeBarrier(Identity, {21, 80, 100, 45}, LoadedAt).has_value() && Peer.GetState() == PeerJoinState::ReadySpectator);
	Passed &= ExpectError("a ready peer is outside the current synchronization attempt", Peer.Rebase(Identity, Replacement, LoadedAt + 1s), PeerJoinError::InvalidState);
	return Passed;
}

static bool CheckReplacement()
{
	PeerJoin Peer(Identity, AdmittedAt);
	bool Passed = Expect("loaded peer selects its first baseline", Peer.ConfirmLoaded(Identity, LoadedAt).has_value() &&
		Peer.SelectBaseline(Identity, Initial, LoadedAt).has_value());
	Passed &= Expect("owner declares a barrier for the selected baseline", Peer.DeclareBarrier(Identity, FirstBarrier, LoadedAt).has_value());
	Passed &= ExpectError("stale replacement cannot clear the old barrier", Peer.Rebase(Identity, {22, 99, 46}, LoadedAt + 1s), PeerJoinError::StaleBaseline);
	Passed &= ExpectError("earlier event watermark cannot replace", Peer.Rebase(Identity, {22, 105, 44}, LoadedAt + 1s), PeerJoinError::StaleBaseline);
	Passed &= ExpectError("replacement must cover the declared barrier tick", Peer.Rebase(Identity, {22, 103, 46}, LoadedAt + 1s), PeerJoinError::StaleBaseline);
	Passed &= ExpectError("replacement must cover the declared barrier watermark", Peer.Rebase(Identity, {22, 105, 45}, LoadedAt + 1s), PeerJoinError::StaleBaseline);
	Passed &= ExpectError("foreign replacement cannot consume this peer's attempt", Peer.Rebase(NextWorld, Replacement, LoadedAt + 1s), PeerJoinError::IdentityMismatch);
	Passed &= ExpectError("rejected requests preserve the declared barrier", Peer.AcknowledgeBarrier(Identity, {21, 79, 101, 45}, LoadedAt + 1s), PeerJoinError::BarrierMismatch);
	Passed &= Expect("newer baseline replaces the selected one in the same round", Peer.Rebase(Identity, Replacement, LoadedAt + 1s).has_value());
	Passed &= ExpectError("old acknowledgement cannot make the peer ready", Peer.AcknowledgeBarrier(Identity, FirstBarrier, LoadedAt + 2s), PeerJoinError::InvalidState);
	Passed &= ExpectError("delayed owner work cannot redeclare the old barrier", Peer.DeclareBarrier(Identity, FirstBarrier, LoadedAt + 2s), PeerJoinError::BaselineMismatch);
	Passed &= Expect("replacement gets a barrier", Peer.DeclareBarrier(Identity, NextBarrier, LoadedAt + 3s).has_value());
	Passed &= ExpectError("old acknowledgement also fails once the replacement barrier is declared", Peer.AcknowledgeBarrier(Identity, FirstBarrier, LoadedAt + 3s), PeerJoinError::BarrierMismatch);
	Passed &= Expect("only the replacement acknowledgement makes the peer ready",
		Peer.AcknowledgeBarrier(Identity, NextBarrier, LoadedAt + 4s).has_value() && Peer.GetState() == PeerJoinState::ReadySpectator);
	return Passed;
}

static bool ChooseThreeRebases(PeerJoin& Peer)
{
	if (!Peer.ConfirmLoaded(Identity, LoadedAt) || !Peer.SelectBaseline(Identity, Initial, LoadedAt) ||
		!Peer.Rebase(Identity, {22, 101, 45}, LoadedAt + 1s) || !Peer.Rebase(Identity, {23, 102, 45}, LoadedAt + 8s))
	{
		return false;
	}
	return ExpectError("stale selection before the third attempt spends no retry place", Peer.Rebase(Identity, {23, 102, 45}, LoadedAt + 9s), PeerJoinError::StaleBaseline) &&
		ExpectError("foreign selection before the third attempt spends no retry place", Peer.Rebase(NextWorld, {24, 103, 45}, LoadedAt + 9s), PeerJoinError::IdentityMismatch) &&
		Peer.Rebase(Identity, {24, 103, 45}, LoadedAt + 20s).has_value();
}

static bool CheckLimitAndDeadline()
{
	PeerJoin Peer(Identity, AdmittedAt);
	bool Passed = Expect("three valid replacements fit before the first barrier", ChooseThreeRebases(Peer));
	Passed &= ExpectError("duplicate replacement leaves a full retry window untouched", Peer.Rebase(Identity, {24, 103, 45}, LoadedAt + 21s), PeerJoinError::StaleBaseline);
	Passed &= Expect("a stale request leaves the peer synchronizing", Peer.GetState() == PeerJoinState::Synchronizing);
	constexpr JoinBarrier LastBarrier{24, 80, 103, 45};
	Passed &= Expect("the third attempt may declare a barrier", Peer.DeclareBarrier(Identity, LastBarrier, LoadedAt + 21s).has_value());
	Passed &= ExpectError("fourth valid replacement closes the join", Peer.Rebase(Identity, {25, 104, 45}, LoadedAt + 22s), PeerJoinError::RebaseLimitExceeded);
	Passed &= ExpectError("closing rejects even the exact previous barrier acknowledgement", Peer.AcknowledgeBarrier(Identity, LastBarrier, LoadedAt + 23s), PeerJoinError::InvalidState);
	Passed &= Expect("exhausted budget reports one terminal failure", Peer.GetState() == PeerJoinState::Closing && !Peer.Expire(LoadedAt + 30s));
	Passed &= ExpectError("later retry does not repeat the terminal failure", Peer.Rebase(Identity, {25, 104, 45}, LoadedAt + 31s), PeerJoinError::InvalidState);

	PeerJoin Timed(Identity, AdmittedAt);
	Passed &= Expect("deadline fixture performs three replacements", ChooseThreeRebases(Timed));
	Passed &= Expect("rebasing preserves the original synchronization window", !Timed.Expire(LoadedAt + 30s - 1ms));
	Passed &= ExpectError("deadline equality wins over rebase limit", Timed.Rebase(Identity, {25, 104, 45}, LoadedAt + 30s), PeerJoinError::SynchronizationTimedOut);
	Passed &= Expect("time-out also closes the peer", Timed.GetState() == PeerJoinState::Closing);
	return Passed;
}

static bool StartNextWorld(PeerJoin& Peer)
{
	constexpr JoinBarrier Barrier{24, 15, 108, 52};
	return ChooseThreeRebases(Peer) && Peer.DeclareBarrier(Identity, Barrier, LoadedAt + 21s).has_value() &&
		Peer.AcknowledgeBarrier(Identity, Barrier, LoadedAt + 22s).has_value() && Peer.AdvanceWorld(NextWorld.WorldEpoch, LoadedAt + 35s).has_value() &&
		Peer.SelectBaseline(NextWorld, Initial, LoadedAt + 35s).has_value();
}

static bool CheckRollingWindow()
{
	PeerJoin Before(Identity, AdmittedAt);
	bool Passed = Expect("ready peer starts a new world with a fresh baseline", StartNextWorld(Before));
	Passed &= ExpectError("world reset does not clear recent retry history", Before.Rebase(NextWorld, Replacement, LoadedAt + 61s - 1ms), PeerJoinError::RebaseLimitExceeded);

	PeerJoin Exact(Identity, AdmittedAt);
	Passed &= Expect("rolling-window fixture reaches the next world", StartNextWorld(Exact));
	Passed &= Expect("oldest attempt expires at exactly sixty seconds", Exact.Rebase(NextWorld, Replacement, LoadedAt + 61s).has_value());
	Passed &= ExpectError("only one place opens when the oldest attempt expires", Exact.Rebase(NextWorld, {23, 106, 46}, LoadedAt + 62s), PeerJoinError::RebaseLimitExceeded);
	return Passed;
}

static bool CheckNearClockLimit()
{
	constexpr auto Last = PeerJoin::Clock::time_point::max();
	constexpr auto Start = Last - 12s;
	PeerJoin Peer(Identity, Start - 1s);
	bool Passed = Expect("retry fixture can synchronize near the clock limit", Peer.ConfirmLoaded(Identity, Start).has_value() &&
		Peer.SelectBaseline(Identity, Initial, Start).has_value() && Peer.Rebase(Identity, {22, 101, 45}, Start + 1s).has_value() &&
		Peer.Rebase(Identity, {23, 102, 45}, Start + 2s).has_value() && Peer.Rebase(Identity, {24, 103, 45}, Start + 3s).has_value());
	Passed &= ExpectError("unrepresentable end of the retry window still keeps all three attempts", Peer.Rebase(Identity, {25, 104, 45}, Last - 1ms),
		PeerJoinError::RebaseLimitExceeded);
	return Passed;
}

static bool CheckReceiverAgreement()
{
	PeerJoin Peer(Identity, AdmittedAt);
	BaselineStagingBudget Budget(0, 1);
	BaselineStaging Receiver(Identity, {}, Budget.TryReserve(0).value());
	constexpr Wire::BaselineBegin Before{Identity, 1, {0xffffffffu, 123, 0xfffffffdu, 0, 0, 0}};
	constexpr Wire::BaselineBegin After{Identity, 2, {0, 124, 0xfffffffeu, 0, 0, 0}};
	bool Passed = Expect("owner and receiver select the same pre-wrap baseline", Peer.ConfirmLoaded(Identity, LoadedAt).has_value() &&
		Peer.SelectBaseline(Identity, Before.Manifest.GetBoundary(), LoadedAt).has_value() && Receiver.Begin(Before).has_value());
	Passed &= Expect("selected boundary matches the first capture", Peer.DeclareBarrier(Identity, {0xffffffffu, 8, 123, 0xfffffffdu}, LoadedAt).has_value());
	Passed &= Expect("owner and receiver agree on the wrapped boundary", Peer.Rebase(Identity, After.Manifest.GetBoundary(), LoadedAt + 1s).has_value() &&
		Receiver.Rebase(After).has_value() && Receiver.Finish({Identity, 3, 0}).has_value());
	constexpr JoinBarrier Barrier{0, 9, 125, 0};
	Passed &= Expect("wrapped baseline can establish exact readiness", Peer.DeclareBarrier(Identity, Barrier, LoadedAt + 2s).has_value() &&
		Peer.AcknowledgeBarrier(Identity, Barrier, LoadedAt + 2s).has_value());
	return Passed;
}

bool Run()
{
	bool Passed = CheckSelectionAndBoundary();
	Passed &= CheckReplacement();
	Passed &= CheckLimitAndDeadline();
	Passed &= CheckRollingWindow();
	Passed &= CheckNearClockLimit();
	Passed &= CheckReceiverAgreement();
	if (!Passed)
	{
		return false;
	}
	std::puts("Peer rebase checks passed");
	return true;
}
}
