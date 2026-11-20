#include "PeerJoin.h"

#include <cstdio>
#include <limits>

namespace PeerJoinTests
{
using namespace Multiplayer;
using namespace std::chrono_literals;

constexpr JoinIdentity Identity{0x8877665544332211, 7, 17};
constexpr JoinBarrier Barrier{25, 42, 0xfffffffcu, 9};
constexpr auto AdmittedAt = PeerJoin::Clock::time_point{} + 11s;
constexpr auto LoadedAt = AdmittedAt + 100s;
constexpr auto LoadDeadline = AdmittedAt + 120s;
constexpr auto SyncDeadline = LoadedAt + 30s;
constexpr JoinIdentity NextWorld{Identity.ServerSession, Identity.WorldEpoch + 1, Identity.ConnectionEpoch};

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

static bool CheckReady()
{
	PeerJoin Peer(Identity, AdmittedAt);
	bool Passed = Expect("admitted peer first loads the arena", Peer.GetState() == PeerJoinState::Loading);
	Passed &= Expect("current-world loading confirmation begins synchronization", Peer.ConfirmLoaded(Identity, LoadedAt).has_value() &&
		Peer.GetState() == PeerJoinState::Synchronizing);
	Passed &= Expect("owner can declare a barrier", Peer.DeclareBarrier(Identity, Barrier, LoadedAt + 5s).has_value());
	Passed &= Expect("exact acknowledgement makes a ready spectator", Peer.AcknowledgeBarrier(Identity, Barrier, LoadedAt + 29s).has_value() &&
		Peer.GetState() == PeerJoinState::ReadySpectator);
	Passed &= ExpectError("a repeated acknowledgement cannot report readiness twice",
		Peer.AcknowledgeBarrier(Identity, Barrier, SyncDeadline), PeerJoinError::InvalidState);
	Passed &= Expect("ready spectator is outside the synchronization deadline", !Peer.Expire(SyncDeadline) && Peer.GetState() == PeerJoinState::ReadySpectator);
	return Passed;
}

static bool CheckExactAcknowledgement()
{
	PeerJoin Peer(Identity, AdmittedAt);
	bool Passed = ExpectError("loading peer cannot acknowledge a barrier", Peer.AcknowledgeBarrier(Identity, Barrier, AdmittedAt), PeerJoinError::InvalidState);
	Passed &= ExpectError("owner cannot declare a barrier while the peer is loading", Peer.DeclareBarrier(Identity, Barrier, AdmittedAt), PeerJoinError::InvalidState);
	constexpr JoinIdentity WrongIdentities[] = {
		{Identity.ServerSession + 1, Identity.WorldEpoch, Identity.ConnectionEpoch},
		{Identity.ServerSession, Identity.WorldEpoch + 1, Identity.ConnectionEpoch},
		{Identity.ServerSession, Identity.WorldEpoch, Identity.ConnectionEpoch + 1},
	};
	for (const auto& Wrong : WrongIdentities)
	{
		Passed &= ExpectError("loading confirmation must match the entire connection identity", Peer.ConfirmLoaded(Wrong, LoadedAt), PeerJoinError::IdentityMismatch);
	}
	Passed &= Expect("identity mismatch keeps the peer loading", Peer.GetState() == PeerJoinState::Loading);
	Passed &= Expect("bound loading confirmation starts synchronization", Peer.ConfirmLoaded(Identity, LoadedAt).has_value());
	Passed &= ExpectError("peer cannot supply its own barrier", Peer.AcknowledgeBarrier(Identity, Barrier, LoadedAt), PeerJoinError::InvalidState);
	Passed &= ExpectError("stale owner work cannot declare a barrier", Peer.DeclareBarrier(NextWorld, Barrier, LoadedAt), PeerJoinError::IdentityMismatch);
	Passed &= Expect("owner declares the current barrier", Peer.DeclareBarrier(Identity, Barrier, LoadedAt).has_value());
	auto OtherBarrier = Barrier;
	++OtherBarrier.BarrierId;
	Passed &= ExpectError("second declaration cannot silently replace an outstanding barrier",
		Peer.DeclareBarrier(Identity, OtherBarrier, LoadedAt), PeerJoinError::InvalidState);
	for (const auto& Wrong : WrongIdentities)
	{
		Passed &= ExpectError("ready acknowledgement must match the entire connection identity",
			Peer.AcknowledgeBarrier(Wrong, Barrier, LoadedAt + 1s), PeerJoinError::IdentityMismatch);
	}
	constexpr u32 JoinBarrier::* Fields[] = {&JoinBarrier::BaselineId, &JoinBarrier::BarrierId, &JoinBarrier::SnapshotTick, &JoinBarrier::EventWatermark};
	for (const auto Field : Fields)
	{
		auto Wrong = Barrier;
		++(Wrong.*Field);
		Passed &= ExpectError("each barrier field must match exactly", Peer.AcknowledgeBarrier(Identity, Wrong, LoadedAt + 1s), PeerJoinError::BarrierMismatch);
	}
	Passed &= Expect("wrong acknowledgements leave synchronization pending", Peer.GetState() == PeerJoinState::Synchronizing);
	Passed &= Expect("the preserved exact barrier still yields readiness", Peer.AcknowledgeBarrier(Identity, Barrier, LoadedAt + 2s).has_value() &&
		Peer.GetState() == PeerJoinState::ReadySpectator);
	return Passed;
}

static bool CheckDeadlines()
{
	PeerJoin Loading(Identity, AdmittedAt);
	bool Passed = Expect("loading keeps waiting before the exact deadline", !Loading.Expire(LoadDeadline - 1ms));
	Passed &= ExpectError("load confirmation at the exact deadline expires", Loading.ConfirmLoaded(Identity, LoadDeadline), PeerJoinError::LoadingTimedOut);
	Passed &= Expect("loading failure closes once", Loading.GetState() == PeerJoinState::Closing && !Loading.Expire(LoadDeadline));
	Passed &= ExpectError("a later owner update cannot reopen the failed join", Loading.AdvanceWorld(NextWorld.WorldEpoch, LoadDeadline + 1s), PeerJoinError::InvalidState);

	PeerJoin LoadIdle(Identity, AdmittedAt);
	Passed &= Expect("idle loading peer also expires exactly once", LoadIdle.Expire(LoadDeadline + 1s) == PeerJoinError::LoadingTimedOut &&
		LoadIdle.GetState() == PeerJoinState::Closing && !LoadIdle.Expire(LoadDeadline + 2s));

	PeerJoin Syncing(Identity, AdmittedAt);
	Passed &= Expect("sync deadline fixture finishes loading", Syncing.ConfirmLoaded(Identity, LoadedAt).has_value());
	Passed &= ExpectError("duplicate confirmation cannot renew the synchronization deadline",
		Syncing.ConfirmLoaded(Identity, LoadedAt + 5s), PeerJoinError::InvalidState);
	Passed &= Expect("barrier declaration just before expiry still succeeds", Syncing.DeclareBarrier(Identity, Barrier, SyncDeadline - 1ms).has_value());
	Passed &= Expect("synchronization keeps waiting just before its deadline", !Syncing.Expire(SyncDeadline - 1ms));
	Passed &= ExpectError("exact valid acknowledgement at the sync deadline expires",
		Syncing.AcknowledgeBarrier(Identity, Barrier, SyncDeadline), PeerJoinError::SynchronizationTimedOut);
	Passed &= Expect("synchronization failure closes once", Syncing.GetState() == PeerJoinState::Closing && !Syncing.Expire(SyncDeadline + 1s));
	Passed &= ExpectError("a late acknowledgement cannot restore readiness",
		Syncing.AcknowledgeBarrier(Identity, Barrier, SyncDeadline + 1s), PeerJoinError::InvalidState);

	PeerJoin SyncIdle(Identity, AdmittedAt);
	Passed &= Expect("sync timeout starts when the load is confirmed", SyncIdle.ConfirmLoaded(Identity, LoadDeadline - 1ms).has_value() &&
		!SyncIdle.Expire(LoadDeadline));
	Passed &= Expect("missing barrier also times out exactly once", SyncIdle.Expire(LoadDeadline + 30s - 1ms) == PeerJoinError::SynchronizationTimedOut &&
		SyncIdle.GetState() == PeerJoinState::Closing && !SyncIdle.Expire(LoadDeadline + 30s));
	return Passed;
}

static bool CheckWorldAdvanceDuringLoad()
{
	PeerJoin Peer(Identity, AdmittedAt);
	bool Passed = Expect("same-map round advance leaves a loading peer in loading",
		Peer.AdvanceWorld(NextWorld.WorldEpoch, LoadedAt).has_value() && Peer.GetState() == PeerJoinState::Loading);
	Passed &= ExpectError("old-world loading confirmation cannot start synchronization",
		Peer.ConfirmLoaded(Identity, LoadedAt + 1s), PeerJoinError::IdentityMismatch);
	Passed &= Expect("current-world loading confirmation still works", Peer.ConfirmLoaded(NextWorld, LoadedAt + 1s).has_value());
	Passed &= Expect("new-world barrier may reuse earlier numeric IDs", Peer.DeclareBarrier(NextWorld, Barrier, LoadedAt + 2s).has_value());
	Passed &= ExpectError("old-world ready acknowledgement cannot match the current barrier even with identical numbers",
		Peer.AcknowledgeBarrier(Identity, Barrier, LoadedAt + 3s), PeerJoinError::IdentityMismatch);
	Passed &= Expect("current-world acknowledgement establishes readiness", Peer.AcknowledgeBarrier(NextWorld, Barrier, LoadedAt + 3s).has_value() &&
		Peer.GetState() == PeerJoinState::ReadySpectator);

	PeerJoin Expiring(Identity, AdmittedAt);
	Passed &= Expect("round advance before loading deadline succeeds", Expiring.AdvanceWorld(NextWorld.WorldEpoch, LoadDeadline - 1s).has_value());
	Passed &= ExpectError("round advance does not renew the loading deadline",
		Expiring.ConfirmLoaded(NextWorld, LoadDeadline), PeerJoinError::LoadingTimedOut);
	return Passed;
}

static bool CheckWorldAdvanceAfterLoad()
{
	PeerJoin Peer(Identity, AdmittedAt);
	bool Passed = Expect("round advance fixture declares a barrier", Peer.ConfirmLoaded(Identity, LoadedAt).has_value() &&
		Peer.DeclareBarrier(Identity, Barrier, LoadedAt).has_value());
	Passed &= ExpectError("duplicate world epoch cannot clear the outstanding barrier", Peer.AdvanceWorld(Identity.WorldEpoch, LoadedAt + 1s), PeerJoinError::IdentityMismatch);
	Passed &= ExpectError("earlier world epoch cannot roll back identity", Peer.AdvanceWorld(Identity.WorldEpoch - 1, LoadedAt + 1s), PeerJoinError::IdentityMismatch);
	auto WrongBarrier = Barrier;
	++WrongBarrier.EventWatermark;
	Passed &= ExpectError("refusing stale owner updates preserves the outstanding identity and barrier",
		Peer.AcknowledgeBarrier(Identity, WrongBarrier, LoadedAt + 1s), PeerJoinError::BarrierMismatch);
	Passed &= Expect("advancing the world keeps map loading complete", Peer.AdvanceWorld(NextWorld.WorldEpoch, LoadedAt + 2s).has_value() &&
		Peer.GetState() == PeerJoinState::Synchronizing);
	Passed &= ExpectError("old barrier cannot make the peer ready in the new world",
		Peer.AcknowledgeBarrier(NextWorld, Barrier, LoadedAt + 2s), PeerJoinError::InvalidState);
	Passed &= ExpectError("delayed owner work cannot redeclare the old world's barrier",
		Peer.DeclareBarrier(Identity, Barrier, LoadedAt + 2s), PeerJoinError::IdentityMismatch);
	Passed &= Expect("fresh declaration and acknowledgement succeed before the original deadline",
		Peer.DeclareBarrier(NextWorld, Barrier, SyncDeadline - 1ms).has_value() &&
		Peer.AcknowledgeBarrier(NextWorld, Barrier, SyncDeadline - 1ms).has_value());
	constexpr JoinIdentity ThirdWorld{Identity.ServerSession, NextWorld.WorldEpoch + 1, Identity.ConnectionEpoch};
	Passed &= Expect("a spectator awaiting activation returns to synchronization when its world ends",
		Peer.AdvanceWorld(ThirdWorld.WorldEpoch, SyncDeadline).has_value() && Peer.GetState() == PeerJoinState::Synchronizing);
	Passed &= ExpectError("the previous ready acknowledgement is invalid after the reset",
		Peer.AcknowledgeBarrier(NextWorld, Barrier, SyncDeadline), PeerJoinError::IdentityMismatch);
	Passed &= ExpectError("the new world requires a new barrier even for a previously ready spectator",
		Peer.AcknowledgeBarrier(ThirdWorld, Barrier, SyncDeadline), PeerJoinError::InvalidState);
	Passed &= Expect("a ready peer gets a fresh synchronization budget for the new world", !Peer.Expire(SyncDeadline + 29s));
	Passed &= Expect("the new spectator sync budget still expires", Peer.Expire(SyncDeadline + 30s) == PeerJoinError::SynchronizationTimedOut);

	PeerJoin Expiring(Identity, AdmittedAt);
	Passed &= Expect("reset timeout fixture finishes loading", Expiring.ConfirmLoaded(Identity, LoadedAt).has_value() &&
		Expiring.AdvanceWorld(NextWorld.WorldEpoch, SyncDeadline - 1s).has_value());
	Passed &= ExpectError("an additional round advance at the outstanding deadline expires first",
		Expiring.AdvanceWorld(ThirdWorld.WorldEpoch, SyncDeadline), PeerJoinError::SynchronizationTimedOut);
	return Passed;
}

static bool CheckIdentityAndClockBoundaries()
{
	constexpr JoinIdentity Unbound[] = {{0, 7, 17}, {Identity.ServerSession, 7, 0}};
	bool Passed = true;
	for (const auto& Invalid : Unbound)
	{
		PeerJoin Peer(Invalid, AdmittedAt);
		Passed &= Expect("unbound join starts closed", Peer.GetState() == PeerJoinState::Closing);
		Passed &= ExpectError("an unbound identity cannot start synchronization", Peer.ConfirmLoaded(Invalid, LoadedAt), PeerJoinError::InvalidState);
	}

	constexpr JoinIdentity MaximumWorld{17, std::numeric_limits<u32>::max(), 9};
	PeerJoin NoWrap(MaximumWorld, AdmittedAt);
	Passed &= ExpectError("world epoch must never wrap within the session", NoWrap.AdvanceWorld(0, LoadedAt), PeerJoinError::IdentityMismatch);
	Passed &= Expect("refusing wrap preserves the current world", NoWrap.ConfirmLoaded(MaximumWorld, LoadedAt).has_value());

	constexpr auto Last = PeerJoin::Clock::time_point::max();
	constexpr JoinIdentity InitialWorld{17, 0, 9};
	PeerJoin Edge(InitialWorld, Last - 2s);
	Passed &= Expect("initial world zero is valid and the loading deadline does not overflow",
		Edge.ConfirmLoaded(InitialWorld, Last - 1s).has_value());
	Passed &= Expect("near the clock limit synchronization can still wait safely", !Edge.Expire(Last - 1ms));
	Passed &= Expect("deadline at the maximum clock time expires", Edge.Expire(Last) == PeerJoinError::SynchronizationTimedOut);
	return Passed;
}

bool Run()
{
	bool Passed = CheckReady();
	Passed &= CheckExactAcknowledgement();
	Passed &= CheckDeadlines();
	Passed &= CheckWorldAdvanceDuringLoad();
	Passed &= CheckWorldAdvanceAfterLoad();
	Passed &= CheckIdentityAndClockBoundaries();
	if (!Passed)
	{
		return false;
	}
	std::puts("Peer join checks passed");
	return true;
}
}
