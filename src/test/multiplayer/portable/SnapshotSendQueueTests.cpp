#include "SnapshotSendQueue.h"

#include <algorithm>
#include <cstdio>

namespace SnapshotSendQueueTests
{
using namespace Multiplayer;
using namespace Multiplayer::Wire;

constexpr JoinIdentity Receiver{75, 12, 4};
constexpr SnapshotBatchHeader FirstHeader{Receiver, 100, 37, 0xffffffffu, 270, 33};
constexpr SnapshotBatchHeader NextHeader{Receiver, 0xffffffffu, 38, 0, 294, 34};

static bool Expect(const char* Name, bool Passed) noexcept
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Name);
	}
	return Passed;
}

static StalkerCombatantSnapshot MakeStalker(std::size_t Index, f32 Position) noexcept
{
	StalkerCombatantSnapshot Result{};
	Result.State.Participant = {static_cast<u32>(Index + 1)};
	Result.State.Body = {static_cast<u16>(Index), static_cast<u32>(Index + 100), 1};
	Result.State.BodyProfileId = 3;
	Result.State.Position[0] = Position;
	Result.AnimationProfileId = 2;
	return Result;
}

static SnapshotSendResult CheckFrame(SnapshotSendQueue& Queue, u64 NowMs, SnapshotBatchHeader Header,
	xr_span<const CombatantSnapshot> Expected, bool& Passed, SnapshotSendAttempt Outcome = SnapshotSendAttempt::Accepted)
{
	return Queue.SubmitNext(NowMs, [&](xr_span<const u8> Bytes) noexcept
	{
		const auto Batch = CombatantSnapshotBatch::Decode(Bytes);
		Passed &= Expect("the emitted frame has the expected header and record order", Batch && Batch->GetHeader() == Header &&
			std::ranges::equal(Batch->GetRecords(), Expected));
		return Outcome;
	});
}

static bool CheckReplacement()
{
	xr_array<CombatantSnapshot, 12> Initial;
	xr_array<CombatantSnapshot, 12> Latest;
	for (std::size_t Index = 0; Index < Initial.size(); ++Index)
	{
		Initial[Index] = MakeStalker(Index, 4.0f);
		Latest[Index] = MakeStalker(Index, 6.0f);
	}

	SnapshotSendQueue Queue(Receiver, 50);
	bool Passed = Expect("a twelve-stalker capture occupies two datagrams", Queue.Stage(FirstHeader, Initial, 10) == 2);
	Passed &= Expect("the first eleven records submit together",
		CheckFrame(Queue, 11, FirstHeader, xr_span(Initial).first(11), Passed) == SnapshotSendResult::Submitted);

	auto Source = Latest;
	Passed &= Expect("a complete newer capture replaces the unsent remainder across tick wrap", Queue.Stage(NextHeader, Source, 12) == 2);
	Source.fill(MakeStalker(20, -1.0f));
	Passed &= Expect("the replacement sends its first packet, with owned bytes",
		CheckFrame(Queue, 13, NextHeader, xr_span(Latest).first(11), Passed) == SnapshotSendResult::Submitted);
	auto Tail = NextHeader;
	Tail.MessageSequence = 0;
	Passed &= Expect("the disjoint tail survives replacement and packet sequence wrap",
		CheckFrame(Queue, 14, Tail, xr_span(Latest).last(1), Passed) == SnapshotSendResult::Submitted);
	const auto Unexpected = [&](xr_span<const u8>) noexcept
	{
		Passed &= Expect("no obsolete tail can remain queued", false);
		return SnapshotSendAttempt::Accepted;
	};
	Passed &= Expect("the queue empties after the two replacement packets", Queue.SubmitNext(15, Unexpected) == SnapshotSendResult::Empty);
	return Passed;
}

static bool CheckBoundsAndInvalidCapture()
{
	xr_array<CombatantSnapshot, 33> Source;
	for (std::size_t Index = 0; Index < Source.size(); ++Index)
	{
		Source[Index] = MakeStalker(Index, 3.0f);
	}
	const auto Full = xr_span(Source).first(32);
	SnapshotSendQueue Queue(Receiver, 50);
	bool Passed = Expect("32 maximum-size records fit in three bounded packets", Queue.Stage(FirstHeader, Full, 10) == 3);
	Passed &= Expect("pending metrics count all bytes including the three frame headers",
		Queue.Inspect(11) == SnapshotSendQueue::Status{3, 3100, 1});
	Passed &= Expect("33 records cannot replace a valid capture",
		Queue.Stage(NextHeader, Source, 12) == std::unexpected(SnapshotStageError::InvalidCapture));

	auto OtherScope = NextHeader;
	++OtherScope.Identity.WorldEpoch;
	Passed &= Expect("another world cannot replace current queued work",
		Queue.Stage(OtherScope, Full, 12) == std::unexpected(SnapshotStageError::ScopeMismatch));
	OtherScope = NextHeader;
	++OtherScope.Identity.ConnectionEpoch;
	Passed &= Expect("another connection cannot replace current queued work",
		Queue.Stage(OtherScope, Full, 12) == std::unexpected(SnapshotStageError::ScopeMismatch));
	OtherScope = NextHeader;
	++OtherScope.Identity.ServerSession;
	Passed &= Expect("another session cannot replace current queued work",
		Queue.Stage(OtherScope, Full, 12) == std::unexpected(SnapshotStageError::ScopeMismatch));

	auto Invalid = Source;
	std::get<StalkerCombatantSnapshot>(Invalid[31]).AnimationProfileId = 0;
	Passed &= Expect("a bad record in the third packet invalidates the whole candidate",
		Queue.Stage(NextHeader, xr_span(Invalid).first(32), 12) == std::unexpected(SnapshotStageError::InvalidCapture));
	Invalid[31] = MakeStalker(0, 9.0f);
	Passed &= Expect("duplicate participants across packets invalidate the complete capture",
		Queue.Stage(NextHeader, xr_span(Invalid).first(32), 12) == std::unexpected(SnapshotStageError::InvalidCapture));
	auto& Conflict = std::get<StalkerCombatantSnapshot>(Invalid[31]).State;
	Conflict.Participant = {44};
	Conflict.Body.Generation = 81;
	Passed &= Expect("different generations cannot share one object slot in a capture",
		Queue.Stage(NextHeader, xr_span(Invalid).first(32), 12) == std::unexpected(SnapshotStageError::InvalidCapture));
	Passed &= Expect("rejected captures preserve packet count, bytes and age", Queue.Inspect(13) == SnapshotSendQueue::Status{3, 3100, 3});

	auto Header = FirstHeader;
	for (const auto Records : {Full.first(11), Full.subspan(11, 11), Full.last(10)})
	{
		Passed &= Expect("after invalid updates the previous capture still sends every original body",
			CheckFrame(Queue, 13, Header, Records, Passed) == SnapshotSendResult::Submitted);
		++Header.MessageSequence;
	}

	xr_array<CombatantSnapshot, 12> Mixed;
	std::ranges::copy(Full.first(9), Mixed.begin());
	for (std::size_t Index = 9; Index < Mixed.size(); ++Index)
	{
		Mixed[Index] = HumanCombatantSnapshot{MakeStalker(Index, 8.0f).State};
	}
	Passed &= Expect("a corrected capture at the rejected tick can pack mixed records by their actual size",
		Queue.Stage(NextHeader, Mixed, 14) == 1 && Queue.Inspect(14) == SnapshotSendQueue::Status{1, 1078, 0});
	Passed &= Expect("the mixed packet decodes with all twelve bodies",
		CheckFrame(Queue, 15, NextHeader, Mixed, Passed) == SnapshotSendResult::Submitted);
	return Passed;
}

static bool CheckLifetimeAndPressure()
{
	xr_array<CombatantSnapshot, 12> Capture;
	for (std::size_t Index = 0; Index < Capture.size(); ++Index)
	{
		Capture[Index] = MakeStalker(Index, 7.0f);
	}
	SnapshotSendQueue Queue(Receiver, 5);
	bool Passed = Expect("the original capture is queued", Queue.Stage(FirstHeader, Capture, 20) == 2);
	Passed &= Expect("a pressure drop consumes exactly the attempted datagram",
		CheckFrame(Queue, 21, FirstHeader, xr_span(Capture).first(11), Passed, SnapshotSendAttempt::Dropped) == SnapshotSendResult::Dropped);
	Passed &= Expect("the disjoint tail remains within its original budget", Queue.Inspect(21) == SnapshotSendQueue::Status{1, 144, 1});

	auto Tail = FirstHeader;
	++Tail.MessageSequence;
	Passed &= Expect("a later packet can submit independently just before the deadline",
		CheckFrame(Queue, 24, Tail, xr_span(Capture).last(1), Passed) == SnapshotSendResult::Submitted);
	Passed &= Expect("draining does not permit the same tick to be queued again",
		Queue.Stage(FirstHeader, Capture, 24) == std::unexpected(SnapshotStageError::StaleCapture));
	Passed &= Expect("a later tick can stage after the drain", Queue.Stage(NextHeader, Capture, 25) == 2);

	auto Unordered = NextHeader;
	++Unordered.SnapshotId;
	++Unordered.MessageSequence;
	Passed &= Expect("a new message or snapshot ID cannot replace the same capture tick",
		Queue.Stage(Unordered, Capture, 28) == std::unexpected(SnapshotStageError::StaleCapture));
	Unordered.ServerTick = 0x80000000u;
	Passed &= Expect("an ambiguous half-range tick cannot refresh pending work",
		Queue.Stage(Unordered, Capture, 29) == std::unexpected(SnapshotStageError::StaleCapture));
	Passed &= Expect("an earlier tick cannot refresh pending work after wrap",
		Queue.Stage(FirstHeader, Capture, 29) == std::unexpected(SnapshotStageError::StaleCapture));
	int Calls = 0;
	const auto Send = [&](xr_span<const u8>) noexcept
	{
		++Calls;
		return SnapshotSendAttempt::Accepted;
	};
	Passed &= Expect("expiry at the exact age discards the entire capture without a native call",
		Queue.SubmitNext(30, Send) == SnapshotSendResult::Expired && Calls == 0 &&
		Queue.Inspect(30) == SnapshotSendQueue::Status{0, 0, 0});
	Passed &= Expect("expiry preserves the most recent tick",
		Queue.Stage(NextHeader, Capture, 31) == std::unexpected(SnapshotStageError::StaleCapture));

	auto Header = NextHeader;
	Header.ServerTick = 1;
	Passed &= Expect("a subsequent capture stages after expiry", Queue.Stage(Header, Capture, 31) == 2);
	++Header.ServerTick;
	Passed &= Expect("an empty current roster replaces pending packets and advances the tick",
		Queue.Stage(Header, {}, 32) == 0 && Queue.SubmitNext(32, Send) == SnapshotSendResult::Empty && Calls == 0 &&
		Queue.Stage(Header, Capture, 32) == std::unexpected(SnapshotStageError::StaleCapture));

	++Header.ServerTick;
	Passed &= Expect("a fresh capture can follow an empty roster", Queue.Stage(Header, Capture, 33) == 2);
	Passed &= Expect("transport failure requests connection closure",
		CheckFrame(Queue, 33, Header, xr_span(Capture).first(11), Passed, SnapshotSendAttempt::Failed) == SnapshotSendResult::CloseRequired);
	++Header.ServerTick;
	Passed &= Expect("terminal transport failure stops this peer from staging or sending again",
		Queue.Stage(Header, Capture, 34) == std::unexpected(SnapshotStageError::TransportFailed) &&
		Queue.SubmitNext(34, Send) == SnapshotSendResult::CloseRequired && Calls == 0);

	SnapshotSendQueue Unbound({}, 5);
	auto UnboundHeader = Header;
	UnboundHeader.Identity = {};
	Passed &= Expect("an unbound queue cannot stage even an empty capture",
		Unbound.Stage(UnboundHeader, {}, 34) == std::unexpected(SnapshotStageError::ScopeMismatch));
	return Passed;
}

bool Run()
{
	bool Passed = CheckReplacement();
	Passed &= CheckBoundsAndInvalidCapture();
	Passed &= CheckLifetimeAndPressure();
	if (Passed)
	{
		std::puts("Snapshot send queue checks passed");
	}
	return Passed;
}
}
