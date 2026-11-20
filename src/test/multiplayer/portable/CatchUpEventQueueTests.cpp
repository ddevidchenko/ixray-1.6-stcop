#include "CatchUpEventQueue.h"
#include "PeerJoin.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace CatchUpEventQueueTests
{
using namespace Multiplayer;

constexpr JoinIdentity Identity{0x8877665544332211ull, 17, 9};
constexpr BaselineBoundary Baseline{21, 100, 45};
constexpr u8 FirstBytes[] = {0xa4, 0x00, 0xe7};
constexpr u8 SecondBytes[] = {0x17, 0x99};

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

template <class Value>
static bool ExpectError(const char* Step, const std::expected<Value, CatchUpEventError>& Result, CatchUpEventError Error)
{
	if (!Result && Result.error() == Error)
	{
		return true;
	}
	std::fprintf(stderr, "%s: expected %d, got %d\n", Step, static_cast<int>(Error), Result ? -1 : static_cast<int>(Result.error()));
	return false;
}

static bool CheckDelivery()
{
	std::array<u16, 2> Lengths{};
	std::array<u8, 5> Storage{};
	CatchUpEventQueue Queue(Identity, Baseline, Lengths, Storage);
	std::array<u8, 5> Output;
	Output.fill(0xcc);
	const auto Sentinel = Output;
	bool Passed = Expect("baseline watermark seeds the queue", Queue.GetObservedWatermark() == 45 && Queue.GetSubmittedWatermark() == 45);
	Passed &= ExpectError("empty queue exposes no data", Queue.Peek(Output), CatchUpEventError::Empty);
	auto Caller = std::to_array(FirstBytes);
	Passed &= Expect("two contiguous events fit the exact capacity", Queue.Append({Identity, 21, 46, Caller}).has_value() &&
		Queue.Append({Identity, 21, 47, SecondBytes}).has_value());
	Caller.fill(0x31);
	Passed &= ExpectError("short output cannot consume or partially copy the event", Queue.Peek(xr_span(Output).first(2)), CatchUpEventError::OutputTooSmall);
	Passed &= Expect("empty and rejected peeks leave all output bytes untouched", Output == Sentinel);
	const auto First = Queue.Peek(Output);
	Passed &= Expect("queue preserves the original scope and copies the source bytes", First && First->Identity == Identity &&
		First->BaselineId == 21 && First->Sequence == 46 && std::ranges::equal(First->Bytes, FirstBytes) && Output[3] == 0xcc && Output[4] == 0xcc);
	std::array<u8, 3> Retry{};
	const auto Again = Queue.Peek(Retry);
	Passed &= Expect("failed transport submission can retry the same head", Again && Again->Sequence == 46 &&
		std::ranges::equal(Again->Bytes, FirstBytes) && Queue.GetObservedWatermark() == 47 && Queue.GetSubmittedWatermark() == 45);
	Passed &= ExpectError("cannot commit a later event first", Queue.CommitSend({Identity, 21, 47, {}}), CatchUpEventError::UnexpectedSequence);
	Passed &= Expect("successful transport submission advances the watermark", First && Queue.CommitSend(*First).has_value() && Queue.GetSubmittedWatermark() == 46);
	Passed &= ExpectError("duplicate commit cannot remove the next event", Queue.CommitSend({Identity, 21, 46, {}}), CatchUpEventError::UnexpectedSequence);
	const auto Second = Queue.Peek(Output);
	Passed &= Expect("second event follows the first", Second && Second->Sequence == 47 && std::ranges::equal(Second->Bytes, SecondBytes) &&
		Queue.CommitSend(*Second).has_value() && Queue.GetSubmittedWatermark() == 47);
	Passed &= ExpectError("drained queue is empty", Queue.Peek(Output), CatchUpEventError::Empty);
	Passed &= ExpectError("drained queue cannot advance by commit", Queue.CommitSend({Identity, 21, 48, {}}), CatchUpEventError::Empty);
	return Passed;
}

static bool CheckScopeAndFrameSize()
{
	std::array<u16, 1> Lengths{};
	std::array<u8, 1100> Storage{};
	CatchUpEventQueue Queue(Identity, Baseline, Lengths, Storage);
	constexpr JoinIdentity Foreign[] = {
		{Identity.ServerSession + 1, Identity.WorldEpoch, Identity.ConnectionEpoch},
		{Identity.ServerSession, Identity.WorldEpoch + 1, Identity.ConnectionEpoch},
		{Identity.ServerSession, Identity.WorldEpoch, Identity.ConnectionEpoch + 1},
	};
	bool Passed = true;
	for (const auto& Other : Foreign)
	{
		Passed &= ExpectError("events must match session, world, and connection", Queue.Append({Other, 21, 46, FirstBytes}), CatchUpEventError::IdentityMismatch);
	}
	Passed &= ExpectError("events must match the selected baseline", Queue.Append({Identity, 20, 46, FirstBytes}), CatchUpEventError::BaselineMismatch);
	Passed &= ExpectError("baseline already covers its watermark", Queue.Append({Identity, 21, 45, FirstBytes}), CatchUpEventError::UnexpectedSequence);
	Passed &= ExpectError("a gap cannot skip the next event", Queue.Append({Identity, 21, 47, FirstBytes}), CatchUpEventError::UnexpectedSequence);
	Passed &= ExpectError("an empty frame cannot represent a serialized event", Queue.Append({Identity, 21, 46, {}}), CatchUpEventError::InvalidEventSize);
	std::array<u8, 1101> Frame;
	Frame.fill(0xc6);
	Passed &= ExpectError("one byte over the frame cap fails before overflow", Queue.Append({Identity, 21, 46, Frame}), CatchUpEventError::InvalidEventSize);
	Passed &= Expect("rejected inputs preserve both watermarks", Queue.GetObservedWatermark() == 45 && Queue.GetSubmittedWatermark() == 45);
	Passed &= Expect("the full frame size still fits", Queue.Append({Identity, 21, 46, xr_span(Frame).first(1100)}).has_value());
	for (const auto& Other : Foreign)
	{
		Passed &= ExpectError("commit must match session, world, and connection", Queue.CommitSend({Other, 21, 46, {}}), CatchUpEventError::IdentityMismatch);
	}
	Passed &= ExpectError("commit cannot retire an event for an older baseline", Queue.CommitSend({Identity, 20, 46, {}}), CatchUpEventError::BaselineMismatch);
	Passed &= ExpectError("retained event cannot be appended twice", Queue.Append({Identity, 21, 46, FirstBytes}), CatchUpEventError::UnexpectedSequence);
	std::array<u8, 1100> Output{};
	const auto Event = Queue.Peek(Output);
	Passed &= Expect("rejected commits preserve the exact original full frame", Event && Event->Sequence == 46 &&
		std::ranges::equal(Event->Bytes, xr_span(Frame).first(1100)) && Queue.CommitSend(*Event).has_value());
	constexpr JoinIdentity Unassigned{0, 17, 9};
	CatchUpEventQueue Invalid(Unassigned, Baseline, {}, {});
	Passed &= ExpectError("unbound identity never accepts events", Invalid.Append({Unassigned, 21, 46, FirstBytes}), CatchUpEventError::IdentityMismatch);
	return Passed;
}

static bool CheckStorageAndSequenceWrap()
{
	std::array<u16, 3> Lengths{};
	std::array<u8, 7> Storage{};
	constexpr BaselineBoundary NearWrap{21, 100, 0xfffffffeu};
	constexpr u8 Split[] = {0x19, 0x23, 0x00, 0x40};
	CatchUpEventQueue Queue(Identity, NearWrap, Lengths, Storage);
	std::array<u8, 4> Output{};
	const auto Submit = [&](u32 Sequence, xr_span<const u8> Expected)
	{
		const auto Peeked = Queue.Peek(Output);
		return Peeked && Peeked->Sequence == Sequence && std::ranges::equal(Peeked->Bytes, Expected) && Queue.CommitSend(*Peeked).has_value();
	};
	bool Passed = Expect("starts with two consecutive events across sequence wrap", Queue.Append({Identity, 21, 0xffffffffu, FirstBytes}).has_value() &&
		Queue.Append({Identity, 21, 0, SecondBytes}).has_value());
	Passed &= Expect("first submission frees its payload space", Submit(0xffffffffu, FirstBytes));
	Passed &= Expect("new payload splits across the end of storage", Queue.Append({Identity, 21, 1, Split}).has_value());
	Passed &= Expect("wrapped sequence zero is still second", Submit(0, SecondBytes));
	Passed &= Expect("length metadata wraps independently of payload space", Queue.Append({Identity, 21, 2, FirstBytes}).has_value());
	Passed &= ExpectError("split payload still requires complete output capacity", Queue.Peek(xr_span(Output).first(3)), CatchUpEventError::OutputTooSmall);
	Passed &= Expect("wrapped payload reconstructs in order", Submit(1, Split) && Submit(2, FirstBytes));
	Passed &= Expect("submitted and observed advance across wrap", Queue.GetSubmittedWatermark() == 2 && Queue.GetObservedWatermark() == 2);
	return Passed;
}

static bool CheckOverflowAndReplacement()
{
	using namespace std::chrono_literals;
	constexpr auto Now = PeerJoin::Clock::time_point{} + 1s;
	PeerJoin Peer(Identity, Now);
	bool Passed = Expect("owner has a selected baseline while retaining events", Peer.ConfirmLoaded(Identity, Now).has_value() &&
		Peer.SelectBaseline(Identity, Baseline, Now).has_value());
	std::array<u16, 3> Lengths{};
	std::array<u8, 4> Storage{};
	std::array<u8, 4> Output{};
	u32 CaptureWatermark = 0;
	{
		CatchUpEventQueue Queue(Identity, Baseline, Lengths, Storage);
		Passed &= Expect("first event fits the byte budget", Queue.Append({Identity, 21, 46, FirstBytes}).has_value());
		const auto Prior = Storage;
		const auto PriorLengths = Lengths;
		const auto Peeked = Queue.Peek(Output);
		Passed &= Expect("retained event is visible before overflow", Peeked.has_value());
		const auto PriorOutput = Output;
		Passed &= ExpectError("one byte beyond remaining capacity requires a rebase", Queue.Append({Identity, 21, 47, SecondBytes}), CatchUpEventError::RebaseRequired);
		Passed &= Expect("overflow exposes the missing event in the capture watermark", Queue.GetObservedWatermark() == 47 && Queue.GetSubmittedWatermark() == 45);
		Passed &= ExpectError("overflow hides a prefix that can no longer complete the join", Queue.Peek(Output), CatchUpEventError::RebaseRequired);
		Passed &= ExpectError("overflow prevents retirement of the partial stream", Queue.CommitSend({Identity, 21, 46, {}}), CatchUpEventError::RebaseRequired);
		Passed &= ExpectError("overflow does not accept gaps in the observed stream", Queue.Append({Identity, 21, 49, SecondBytes}), CatchUpEventError::UnexpectedSequence);
		Passed &= ExpectError("after overflow valid later data also requires rebase", Queue.Append({Identity, 21, 48, SecondBytes}), CatchUpEventError::RebaseRequired);
		Passed &= Expect("overflow keeps tracking observation without growing storage", Queue.GetObservedWatermark() == 48 && Queue.GetSubmittedWatermark() == 45 &&
			Storage == Prior && Lengths == PriorLengths && Output == PriorOutput);
		CaptureWatermark = Queue.GetObservedWatermark();
	}
	const BaselineBoundary Replacement{22, 101, CaptureWatermark};
	Passed &= Expect("owner can select a newer capture covering the observed tail", CaptureWatermark == 48 && Peer.Rebase(Identity, Replacement, Now + 1s).has_value());
	CatchUpEventQueue Next(Identity, Replacement, Lengths, Storage);
	Passed &= ExpectError("replacement rejects previous baseline events", Next.Append({Identity, 21, 49, FirstBytes}), CatchUpEventError::BaselineMismatch);
	Passed &= ExpectError("replacement starts empty despite reused storage", Next.Peek(Output), CatchUpEventError::Empty);
	Passed &= Expect("replacement submits the next event after its captured watermark", Next.Append({Identity, 22, 49, FirstBytes}).has_value());
	const auto Event = Next.Peek(Output);
	Passed &= Expect("replacement event retains its own scope", Event && Event->BaselineId == 22 && Event->Sequence == 49 && Next.CommitSend(*Event).has_value());
	const JoinBarrier Barrier{22, 13, 102, Next.GetSubmittedWatermark()};
	Passed &= Expect("owner can use submitted watermark in the replacement barrier", Barrier.EventWatermark == 49 &&
		Peer.DeclareBarrier(Identity, Barrier, Now + 2s).has_value());
	return Passed;
}

static bool CheckCountLimit()
{
	std::array<u16, 4097> Lengths{};
	std::array<u8, 4097> Storage{};
	CatchUpEventQueue Queue(Identity, Baseline, Lengths, Storage);
	for (u32 Sequence = 46; Sequence <= 4141; ++Sequence)
	{
		if (!Expect("all 4096 bounded slots can be filled", Queue.Append({Identity, 21, Sequence, xr_span(FirstBytes).first(1)}).has_value()))
		{
			return false;
		}
	}
	bool Passed = ExpectError("an extra provided slot cannot exceed the event limit", Queue.Append({Identity, 21, 4142, xr_span(FirstBytes).first(1)}), CatchUpEventError::RebaseRequired);
	Passed &= Expect("overflow preserves memory beyond the maximum", Storage.back() == 0 && Lengths.back() == 0 && Queue.GetObservedWatermark() == 4142);
	std::array<u16, 1> ShortLengths{};
	std::array<u8, 8> SpareBytes{};
	CatchUpEventQueue ShortQueue(Identity, Baseline, ShortLengths, SpareBytes);
	Passed &= Expect("a one-slot queue accepts its first event", ShortQueue.Append({Identity, 21, 46, FirstBytes}).has_value());
	Passed &= ExpectError("owner-provided count limit holds while byte capacity remains", ShortQueue.Append({Identity, 21, 47, SecondBytes}), CatchUpEventError::RebaseRequired);
	Passed &= Expect("count overflow cannot touch spare bytes", SpareBytes[3] == 0 && ShortQueue.GetObservedWatermark() == 47);
	CatchUpEventQueue NoSlots(Identity, Baseline, {}, {});
	Passed &= ExpectError("a queue without slots immediately requests rebase", NoSlots.Append({Identity, 21, 46, FirstBytes}), CatchUpEventError::RebaseRequired);
	return Passed;
}

bool Run()
{
	bool Passed = CheckDelivery();
	Passed &= CheckScopeAndFrameSize();
	Passed &= CheckStorageAndSequenceWrap();
	Passed &= CheckOverflowAndReplacement();
	Passed &= CheckCountLimit();
	if (!Passed)
	{
		return false;
	}
	std::puts("Catch-up event queue checks passed");
	return true;
}
}
