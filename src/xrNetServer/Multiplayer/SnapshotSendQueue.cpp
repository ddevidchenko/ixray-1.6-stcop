#include "SnapshotSendQueue.h"
#include "SerialSequence.h"

namespace Multiplayer
{
std::expected<std::size_t, SnapshotStageError> SnapshotSendQueue::Stage(Wire::SnapshotBatchHeader Header,
	xr_span<const Wire::CombatantSnapshot> Records, u64 NowMs) noexcept
{
	if (TransportFailed)
	{
		return std::unexpected(SnapshotStageError::TransportFailed);
	}
	if (!Identity.IsBound() || Header.Identity != Identity)
	{
		return std::unexpected(SnapshotStageError::ScopeMismatch);
	}
	if (NewestTick && !IsSequenceNewer(Header.ServerTick, *NewestTick))
	{
		return std::unexpected(SnapshotStageError::StaleCapture);
	}
	if (Records.size() > MaxCombatants)
	{
		return std::unexpected(SnapshotStageError::InvalidCapture);
	}

	xr_array<xr_array<u8, Wire::StalkerCombatantRecordSize>, MaxCombatants> Bodies;
	xr_array<Wire::SnapshotRecordView, MaxCombatants> Views;
	for (std::size_t Index = 0; Index < Records.size(); ++Index)
	{
		const auto& Record = Records[Index];
		if (Wire::HasCombatantIdentityConflict(Wire::GetPublicCombatantState(Record), Records.first(Index)))
		{
			return std::unexpected(SnapshotStageError::InvalidCapture);
		}
		const auto Encoded = std::visit([&Body = Bodies[Index]](const auto& Snapshot) noexcept
		{
			return Wire::EncodeCombatantSnapshot(Snapshot, Body);
		}, Record);
		if (!Encoded)
		{
			return std::unexpected(SnapshotStageError::InvalidCapture);
		}
		Views[Index] = *Encoded;
	}

	Capture Candidate;
	for (std::size_t First = 0; First < Records.size();)
	{
		auto End = First;
		auto Remaining = Wire::SnapshotBatchRecordBudget;
		for (; End < Records.size(); ++End)
		{
			const auto RecordBytes = Wire::PayloadRecordHeaderSize + Views[End].Payload.size();
			if (RecordBytes > Remaining)
			{
				break;
			}
			Remaining -= RecordBytes;
		}
		if (Candidate.Count == Candidate.Datagrams.size())
		{
			return std::unexpected(SnapshotStageError::InvalidCapture);
		}
		auto& Frame = Candidate.Datagrams[Candidate.Count];
		const auto Encoded = Wire::EncodeSnapshotBatch(Header, xr_span(Views).subspan(First, End - First), Frame.Bytes);
		if (!Encoded)
		{
			return std::unexpected(SnapshotStageError::InvalidCapture);
		}
		Frame.Size = *Encoded;
		Candidate.Bytes += Frame.Size;
		++Candidate.Count;
		++Header.MessageSequence;
		First = End;
	}

	Candidate.StagedAtMs = NowMs;
	Pending = Candidate;
	NewestTick = Header.ServerTick;
	return Pending.Count;
}
}
