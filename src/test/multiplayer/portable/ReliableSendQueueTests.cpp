#include "ReliableSendQueue.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>

namespace ReliableSendQueueTests
{
using namespace Multiplayer;

static bool Expect(bool Condition, const char* Message) noexcept
{
	if (!Condition)
	{
		std::fprintf(stderr, "%s\n", Message);
	}
	return Condition;
}

static bool CheckRetryAndOrder()
{
	constexpr u8 First[] = {0xaa, 0x00, 0x15};
	constexpr u8 Second[] = {0xbb, 0x10};
	xr_array<ReliableSendQueue::Slot, 2> Slots;
	ReliableSendQueue Queue(Slots, 5, 50);
	auto Source = std::to_array(First);

	bool Passed = Expect(Queue.Enqueue(Source, 100) == ReliableEnqueueResult::Queued, "the first reliable frame enters the queue");
	Passed &= Expect(Queue.Enqueue(Second, 102) == ReliableEnqueueResult::Queued, "two frames fill the byte limit exactly");
	Source.fill(0);

	bool FirstOffered = false;
	const auto Refused = Queue.SubmitNext(104, [&](xr_span<const u8> Bytes) noexcept
	{
		FirstOffered = std::ranges::equal(Bytes, First);
		return ReliableSendAttempt::Backpressured;
	});
	Passed &= Expect(FirstOffered && Refused == ReliableSendResult::Backpressured, "a refused native send offers owned bytes and reports pressure");
	Passed &= Expect(Queue.Inspect(104) == ReliableSendQueue::Status{2, 5, 4, ReliableSendFailure::None},
		"refusal preserves the pending frames and original age");

	int Attempts = 0;
	const auto Accept = [&](xr_span<const u8> Bytes) noexcept
	{
		const auto Expected = Attempts++ == 0 ? xr_span<const u8>(First) : xr_span<const u8>(Second);
		Passed &= Expect(std::ranges::equal(Bytes, Expected), "successful submissions retry the same head before later frames");
		return ReliableSendAttempt::Accepted;
	};
	Passed &= Expect(Queue.SubmitNext(106, Accept) == ReliableSendResult::Submitted && Attempts == 1, "one turn submits at most one frame");
	Passed &= Expect(Queue.Inspect(106) == ReliableSendQueue::Status{1, 2, 4, ReliableSendFailure::None}, "the oldest age now refers to the second frame");
	Passed &= Expect(Queue.SubmitNext(107, Accept) == ReliableSendResult::Submitted && Attempts == 2, "the second frame follows the retried head");
	Passed &= Expect(Queue.SubmitNext(108, Accept) == ReliableSendResult::Empty && Attempts == 2, "an empty queue makes no native call");
	return Passed;
}

static bool CheckCapacityAndReuse()
{
	constexpr u8 Small[] = {0xa6};
	constexpr u8 TooManyBytes[] = {0x55, 0x00, 0x86};
	xr_array<u8, Wire::MaxMessageSize + 1> Oversized{};
	xr_array<ReliableSendQueue::Slot, 2> Slots;
	ReliableSendQueue Queue(Slots, Wire::MaxMessageSize + 1, 20);
	const auto MaxFrame = xr_span<u8>(Oversized).first(Wire::MaxMessageSize);
	Oversized[0] = 0x77;
	Oversized[Wire::MaxMessageSize - 1] = 0xcc;

	bool Passed = Expect(Queue.Enqueue({}, 10) == ReliableEnqueueResult::InvalidSize &&
		Queue.Enqueue(Oversized, 10) == ReliableEnqueueResult::InvalidSize, "empty and oversized frames fail without using capacity");
	Passed &= Expect(Queue.Enqueue(Small, 11) == ReliableEnqueueResult::Queued &&
		Queue.Enqueue(MaxFrame, 12) == ReliableEnqueueResult::Queued, "the maximum frame size and exact queue limits are accepted");
	const auto AcceptFirst = [&](xr_span<const u8> Bytes) noexcept
	{
		Passed &= Expect(std::ranges::equal(Bytes, Small), "the oldest frame leaves first");
		return ReliableSendAttempt::Accepted;
	};
	Passed &= Expect(Queue.SubmitNext(13, AcceptFirst) == ReliableSendResult::Submitted, "submission frees both a slot and its bytes");
	Passed &= Expect(Queue.Enqueue(Small, 14) == ReliableEnqueueResult::Queued, "a freed slot can wrap while an older frame remains");

	int Attempts = 0;
	const auto Accept = [&](xr_span<const u8> Bytes) noexcept
	{
		const xr_span<const u8> Expected = Attempts++ == 0 ? MaxFrame : xr_span<const u8>(Small);
		Passed &= Expect(std::ranges::equal(Bytes, Expected), "wrapping preserves both frame order and exact maximum-sized bytes");
		return ReliableSendAttempt::Accepted;
	};
	Passed &= Expect(Queue.SubmitNext(15, Accept) == ReliableSendResult::Submitted &&
		Queue.SubmitNext(16, Accept) == ReliableSendResult::Submitted && Attempts == 2, "both frames drain after a wrap");
	Passed &= Expect(Queue.Inspect(17) == ReliableSendQueue::Status{0, 0, 0, ReliableSendFailure::None}, "draining clears the pending metrics");

	xr_array<ReliableSendQueue::Slot, 1> CountSlots;
	ReliableSendQueue CountLimited(CountSlots, 20, 20);
	Passed &= Expect(CountLimited.Enqueue(Small, 20) == ReliableEnqueueResult::Queued &&
		CountLimited.Enqueue({}, 21) == ReliableEnqueueResult::InvalidSize &&
		CountLimited.Enqueue(Small, 21) == ReliableEnqueueResult::CloseRequired, "only a valid frame triggers slot exhaustion");
	Passed &= Expect(CountLimited.Inspect(21) == ReliableSendQueue::Status{1, 1, 1, ReliableSendFailure::Capacity},
		"slot exhaustion leaves the original frame and diagnostics intact");

	xr_array<ReliableSendQueue::Slot, 2> ByteSlots;
	ReliableSendQueue ByteLimited(ByteSlots, 3, 20);
	Passed &= Expect(ByteLimited.Enqueue(Small, 20) == ReliableEnqueueResult::Queued &&
		ByteLimited.Enqueue(TooManyBytes, 21) == ReliableEnqueueResult::CloseRequired, "a full byte limit requires closure with slots still available");
	Passed &= Expect(ByteLimited.Inspect(21) == ReliableSendQueue::Status{1, 1, 1, ReliableSendFailure::Capacity},
		"byte exhaustion publishes no partial frame");
	Passed &= Expect(ByteLimited.Enqueue(Small, 22) == ReliableEnqueueResult::CloseRequired &&
		ByteLimited.SubmitNext(22, Accept) == ReliableSendResult::CloseRequired && Attempts == 2, "a capacity close stops later queueing and submission");

	ReliableSendQueue NoSlots({}, 20, 20);
	Passed &= Expect(NoSlots.Enqueue(Small, 20) == ReliableEnqueueResult::CloseRequired, "zero slots cannot accept a frame");
	return Passed;
}

static bool CheckDeadlineAndTransportFailure()
{
	constexpr u8 Frame[] = {0x9e};
	constexpr auto End = std::numeric_limits<u64>::max();
	xr_array<ReliableSendQueue::Slot, 2> DeadlineSlots;
	xr_array<ReliableSendQueue::Slot, 1> FailureSlots;
	xr_array<ReliableSendQueue::Slot, 1> ReadySlots;
	ReliableSendQueue Deadline(DeadlineSlots, 3, 5);
	ReliableSendQueue Failure(FailureSlots, 3, 5);
	ReliableSendQueue Ready(ReadySlots, 3, 5);
	bool Passed = Expect(Deadline.Enqueue(Frame, End - 5) == ReliableEnqueueResult::Queued &&
		Deadline.Enqueue(Frame, End - 2) == ReliableEnqueueResult::Queued, "time near the clock maximum can retain both frames");

	int Attempts = 0;
	const auto Refuse = [&](xr_span<const u8>) noexcept
	{
		++Attempts;
		return ReliableSendAttempt::Backpressured;
	};
	Passed &= Expect(Deadline.SubmitNext(End - 1, Refuse) == ReliableSendResult::Backpressured && Attempts == 1,
		"pressure is retryable until the deadline");

	int Accepted = 0;
	const auto Accept = [&](xr_span<const u8>) noexcept
	{
		++Accepted;
		return ReliableSendAttempt::Accepted;
	};
	Passed &= Expect(Deadline.SubmitNext(End, Accept) == ReliableSendResult::CloseRequired && Accepted == 0,
		"the oldest frame expires at its exact deadline even when the next attempt would succeed");
	Passed &= Expect(Deadline.Inspect(End) == ReliableSendQueue::Status{2, 2, 5, ReliableSendFailure::Deadline},
		"deadline expiry keeps the original age and pending bytes for diagnostics");

	const auto Fail = [](xr_span<const u8>) noexcept { return ReliableSendAttempt::Failed; };
	Passed &= Expect(Failure.Enqueue(Frame, 1) == ReliableEnqueueResult::Queued &&
		Failure.SubmitNext(2, Fail) == ReliableSendResult::CloseRequired, "a terminal native error requires immediate closure");
	Passed &= Expect(Failure.SubmitNext(20, Accept) == ReliableSendResult::CloseRequired && Accepted == 0 &&
		Failure.Inspect(20) == ReliableSendQueue::Status{1, 1, 19, ReliableSendFailure::Transport},
		"a terminal error is neither submitted nor overwritten by later expiry");
	Passed &= Expect(Ready.Enqueue(Frame, End - 3) == ReliableEnqueueResult::Queued &&
		Ready.SubmitNext(End, Accept) == ReliableSendResult::Submitted && Accepted == 1,
		"another peer can submit when its deadline would extend past the clock range");
	return Passed;
}

bool Run()
{
	bool Passed = CheckRetryAndOrder();
	Passed &= CheckCapacityAndReuse();
	Passed &= CheckDeadlineAndTransportFailure();
	if (Passed)
	{
		std::puts("Reliable send queue checks passed");
	}
	return Passed;
}
}
