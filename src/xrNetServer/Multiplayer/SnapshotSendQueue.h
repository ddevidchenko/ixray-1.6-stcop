#pragma once

#include "CombatantSnapshot.h"

#include <optional>
#include <type_traits>

namespace Multiplayer
{
enum class SnapshotStageError
{
	ScopeMismatch,
	StaleCapture,
	InvalidCapture,
	TransportFailed,
};

enum class SnapshotSendAttempt
{
	Accepted,
	Dropped,
	Failed,
};

enum class SnapshotSendResult
{
	Empty,
	Submitted,
	Dropped,
	Expired,
	CloseRequired,
};

// The peer owner serializes operations and supplies timestamps from one monotonic clock.
class SnapshotSendQueue
{
public:
	struct Status
	{
		std::size_t PendingCount;
		std::size_t PendingBytes;
		u64 AgeMs;

		bool operator==(const Status&) const = default;
	};

	SnapshotSendQueue(JoinIdentity Identity, u64 MaxAgeMs) noexcept : Identity(Identity), MaxAgeMs(MaxAgeMs) {}
	SnapshotSendQueue(const SnapshotSendQueue&) = delete;
	SnapshotSendQueue& operator=(const SnapshotSendQueue&) = delete;

	// Stage all relevant combatants for this peer at one tick. Success consumes the returned
	// number of consecutive message sequences, starting at Header.MessageSequence.
	[[nodiscard]] std::expected<std::size_t, SnapshotStageError> Stage(Wire::SnapshotBatchHeader Header,
		xr_span<const Wire::CombatantSnapshot> Records, u64 NowMs) noexcept;

	// Send borrows bytes for a single attempt on the owning peer and does not mutate the queue.
	template <class Sender>
		requires std::is_nothrow_invocable_r_v<SnapshotSendAttempt, Sender&, xr_span<const u8>>
	[[nodiscard]] SnapshotSendResult SubmitNext(u64 NowMs, Sender&& Send) noexcept
	{
		if (TransportFailed)
		{
			return SnapshotSendResult::CloseRequired;
		}
		if (Pending.Next == Pending.Count)
		{
			return SnapshotSendResult::Empty;
		}
		if (NowMs - Pending.StagedAtMs >= MaxAgeMs)
		{
			Discard();
			return SnapshotSendResult::Expired;
		}
		const auto& Frame = Pending.Datagrams[Pending.Next];
		const SnapshotSendAttempt Attempt = Send(xr_span(Frame.Bytes).first(Frame.Size));
		if (Attempt != SnapshotSendAttempt::Accepted && Attempt != SnapshotSendAttempt::Dropped)
		{
			TransportFailed = true;
			Discard();
			return SnapshotSendResult::CloseRequired;
		}
		++Pending.Next;
		Pending.Bytes -= Frame.Size;
		return Attempt == SnapshotSendAttempt::Accepted ? SnapshotSendResult::Submitted : SnapshotSendResult::Dropped;
	}

	[[nodiscard]] Status Inspect(u64 NowMs) const noexcept
	{
		const auto Remaining = Pending.Count - Pending.Next;
		return {Remaining, Pending.Bytes, Remaining ? NowMs - Pending.StagedAtMs : 0};
	}

private:
	static_assert(Wire::HumanCombatantRecordSize <= Wire::StalkerCombatantRecordSize && MaxCombatants <= Wire::MaxSnapshotRecords);
	static constexpr std::size_t MaxSizedRecordsPerDatagram = Wire::SnapshotBatchRecordBudget /
		(Wire::PayloadRecordHeaderSize + Wire::StalkerCombatantRecordSize);
	static_assert(MaxSizedRecordsPerDatagram > 0);
	static constexpr std::size_t MaxDatagrams = (MaxCombatants + MaxSizedRecordsPerDatagram - 1) / MaxSizedRecordsPerDatagram;

	struct Datagram
	{
		xr_array<u8, Wire::MaxMessageSize> Bytes{};
		std::size_t Size = 0;
	};

	struct Capture
	{
		xr_array<Datagram, MaxDatagrams> Datagrams{};
		std::size_t Count = 0;
		std::size_t Next = 0;
		std::size_t Bytes = 0;
		u64 StagedAtMs = 0;
	};

	void Discard() noexcept
	{
		Pending.Next = Pending.Count;
		Pending.Bytes = 0;
	}

	const JoinIdentity Identity;
	const u64 MaxAgeMs;
	std::optional<u32> NewestTick;
	Capture Pending;
	bool TransportFailed = false;
};
}
