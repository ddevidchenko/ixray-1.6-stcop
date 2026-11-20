#pragma once

#include "WireEnvelope.h"
#include "../../xrCore/_stl_extensions_nonalloc.h"

#include <cstring>
#include <type_traits>

namespace Multiplayer
{
enum class ReliableEnqueueResult
{
	Queued,
	InvalidSize,
	CloseRequired,
};

enum class ReliableSendAttempt
{
	Accepted,
	Backpressured,
	Failed,
};

enum class ReliableSendResult
{
	Empty,
	Submitted,
	Backpressured,
	CloseRequired,
};

enum class ReliableSendFailure
{
	None,
	Capacity,
	Deadline,
	Transport,
};

// One queue per peer generation. Its owner serializes operations and supplies exclusive storage
// and timestamps from the same nondecreasing millisecond clock.
class ReliableSendQueue
{
public:
	struct Slot
	{
		xr_array<u8, Wire::MaxMessageSize> Bytes;
		u64 QueuedAtMs;
		std::size_t Size;
	};

	struct Status
	{
		std::size_t PendingCount;
		std::size_t PendingBytes;
		u64 OldestAgeMs;
		ReliableSendFailure Failure;

		bool operator==(const Status&) const = default;
	};

	ReliableSendQueue(xr_span<Slot> Slots, std::size_t ByteLimit, u64 MaxAgeMs) noexcept
		: Slots(Slots), ByteLimit(ByteLimit), MaxAgeMs(MaxAgeMs)
	{
	}
	ReliableSendQueue(const ReliableSendQueue&) = delete;
	ReliableSendQueue& operator=(const ReliableSendQueue&) = delete;

	// Copies an encoded frame. Its input bytes must be disjoint from Slots.
	[[nodiscard]] ReliableEnqueueResult Enqueue(xr_span<const u8> Bytes, u64 NowMs) noexcept
	{
		if (Failure != ReliableSendFailure::None)
		{
			return ReliableEnqueueResult::CloseRequired;
		}
		if (Bytes.empty() || Bytes.size() > Wire::MaxMessageSize)
		{
			return ReliableEnqueueResult::InvalidSize;
		}
		if (Count == Slots.size() || Bytes.size() > ByteLimit - PendingBytes)
		{
			Failure = ReliableSendFailure::Capacity;
			return ReliableEnqueueResult::CloseRequired;
		}
		auto& Tail = Slots[(Head + Count) % Slots.size()];
		std::memcpy(Tail.Bytes.data(), Bytes.data(), Bytes.size());
		Tail.Size = Bytes.size();
		Tail.QueuedAtMs = NowMs;
		PendingBytes += Tail.Size;
		++Count;
		return ReliableEnqueueResult::Queued;
	}

	// Send borrows bytes for one synchronous, nonthrowing attempt on the owning peer.
	// The sender cannot mutate the queue. Submitted means the transport accepted the frame.
	template <class Sender>
		requires std::is_nothrow_invocable_r_v<ReliableSendAttempt, Sender&, xr_span<const u8>>
	[[nodiscard]] ReliableSendResult SubmitNext(u64 NowMs, Sender&& Send) noexcept
	{
		if (Failure != ReliableSendFailure::None)
		{
			return ReliableSendResult::CloseRequired;
		}
		if (Count == 0)
		{
			return ReliableSendResult::Empty;
		}
		const auto& Front = Slots[Head];
		if (NowMs - Front.QueuedAtMs >= MaxAgeMs)
		{
			Failure = ReliableSendFailure::Deadline;
			return ReliableSendResult::CloseRequired;
		}
		const ReliableSendAttempt Attempt = Send(xr_span<const u8>(Front.Bytes.data(), Front.Size));
		if (Attempt == ReliableSendAttempt::Backpressured)
		{
			return ReliableSendResult::Backpressured;
		}
		if (Attempt != ReliableSendAttempt::Accepted)
		{
			Failure = ReliableSendFailure::Transport;
			return ReliableSendResult::CloseRequired;
		}
		PendingBytes -= Front.Size;
		Head = (Head + 1) % Slots.size();
		--Count;
		return ReliableSendResult::Submitted;
	}

	[[nodiscard]] Status Inspect(u64 NowMs) const noexcept
	{
		return {Count, PendingBytes, Count ? NowMs - Slots[Head].QueuedAtMs : 0, Failure};
	}

private:
	const xr_span<Slot> Slots;
	const std::size_t ByteLimit;
	const u64 MaxAgeMs;
	std::size_t Head = 0;
	std::size_t Count = 0;
	std::size_t PendingBytes = 0;
	ReliableSendFailure Failure = ReliableSendFailure::None;
};
}
