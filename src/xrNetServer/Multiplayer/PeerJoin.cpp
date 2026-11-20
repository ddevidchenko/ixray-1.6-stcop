#include "PeerJoin.h"
#include "SerialSequence.h"

#include <algorithm>

namespace Multiplayer
{
constexpr auto LoadingTimeout = std::chrono::seconds(120);
constexpr auto SynchronizationTimeout = std::chrono::seconds(30);
constexpr auto RebaseWindow = std::chrono::seconds(60);

static PeerJoin::Clock::time_point DeadlineAfter(PeerJoin::Clock::time_point Now, PeerJoin::Clock::duration Timeout) noexcept
{
	// Clamp the start before addition to keep the deadline representable.
	return std::min(Now, PeerJoin::Clock::time_point::max() - Timeout) + Timeout;
}

PeerJoin::PeerJoin(JoinIdentity Identity, Clock::time_point AdmittedAt) noexcept
	: Identity(Identity), Deadline(DeadlineAfter(AdmittedAt, LoadingTimeout)),
	  State(Identity.IsBound() ? PeerJoinState::Loading : PeerJoinState::Closing)
{
}

std::optional<PeerJoinError> PeerJoin::Expire(Clock::time_point Now) noexcept
{
	if ((State != PeerJoinState::Loading && State != PeerJoinState::Synchronizing) || Now < Deadline)
	{
		return std::nullopt;
	}
	const auto Error = State == PeerJoinState::Loading ? PeerJoinError::LoadingTimedOut : PeerJoinError::SynchronizationTimedOut;
	State = PeerJoinState::Closing;
	ClearBaseline();
	return Error;
}

std::optional<PeerJoinError> PeerJoin::CheckPhase(PeerJoinState Required, JoinIdentity Subject, Clock::time_point Now) noexcept
{
	if (const auto Expired = Expire(Now))
	{
		return Expired;
	}
	if (State != Required)
	{
		return PeerJoinError::InvalidState;
	}
	if (Subject != Identity)
	{
		return PeerJoinError::IdentityMismatch;
	}
	return std::nullopt;
}

std::expected<void, PeerJoinError> PeerJoin::ConfirmLoaded(JoinIdentity Peer, Clock::time_point Now) noexcept
{
	if (const auto Error = CheckPhase(PeerJoinState::Loading, Peer, Now))
	{
		return std::unexpected(*Error);
	}
	State = PeerJoinState::Synchronizing;
	Deadline = DeadlineAfter(Now, SynchronizationTimeout);
	return {};
}

std::expected<void, PeerJoinError> PeerJoin::SelectBaseline(JoinIdentity Peer, BaselineBoundary Boundary, Clock::time_point Now) noexcept
{
	if (const auto Error = CheckPhase(PeerJoinState::Synchronizing, Peer, Now))
	{
		return std::unexpected(*Error);
	}
	if (SelectedBaseline)
	{
		return std::unexpected(PeerJoinError::InvalidState);
	}
	SelectedBaseline = Boundary;
	return {};
}

std::expected<void, PeerJoinError> PeerJoin::Rebase(JoinIdentity Peer, BaselineBoundary Boundary, Clock::time_point Now) noexcept
{
	if (const auto Error = CheckPhase(PeerJoinState::Synchronizing, Peer, Now))
	{
		return std::unexpected(*Error);
	}
	if (!SelectedBaseline)
	{
		return std::unexpected(PeerJoinError::InvalidState);
	}
	if (!IsNewerBaseline(Boundary, *SelectedBaseline))
	{
		return std::unexpected(PeerJoinError::StaleBaseline);
	}

	// On a full ring, the next write would replace the oldest accepted attempt.
	auto& Oldest = RebaseTimes[NextRebaseSlot];
	if (Oldest && Now < DeadlineAfter(*Oldest, RebaseWindow))
	{
		State = PeerJoinState::Closing;
		ClearBaseline();
		return std::unexpected(PeerJoinError::RebaseLimitExceeded);
	}
	Oldest = Now;
	NextRebaseSlot = (NextRebaseSlot + 1) % RebaseTimes.size();
	SelectedBaseline = Boundary;
	DeclaredBarrier.reset();
	return {};
}

std::expected<void, PeerJoinError> PeerJoin::DeclareBarrier(JoinIdentity World, JoinBarrier Barrier, Clock::time_point Now) noexcept
{
	if (const auto Error = CheckPhase(PeerJoinState::Synchronizing, World, Now))
	{
		return std::unexpected(*Error);
	}
	if (!SelectedBaseline || DeclaredBarrier)
	{
		return std::unexpected(PeerJoinError::InvalidState);
	}
	if (Barrier.BaselineId != SelectedBaseline->BaselineId)
	{
		return std::unexpected(PeerJoinError::BaselineMismatch);
	}
	if (!IsSequenceCurrentOrNewer(Barrier.SnapshotTick, SelectedBaseline->CaptureTick) ||
		!IsSequenceCurrentOrNewer(Barrier.EventWatermark, SelectedBaseline->EventWatermark))
	{
		return std::unexpected(PeerJoinError::BarrierMismatch);
	}
	DeclaredBarrier = Barrier;
	return {};
}

std::expected<void, PeerJoinError> PeerJoin::AcknowledgeBarrier(JoinIdentity Peer, JoinBarrier Barrier, Clock::time_point Now) noexcept
{
	if (const auto Error = CheckPhase(PeerJoinState::Synchronizing, Peer, Now))
	{
		return std::unexpected(*Error);
	}
	if (!DeclaredBarrier)
	{
		return std::unexpected(PeerJoinError::InvalidState);
	}
	if (Barrier != *DeclaredBarrier)
	{
		return std::unexpected(PeerJoinError::BarrierMismatch);
	}
	DeclaredBarrier.reset();
	State = PeerJoinState::ReadySpectator;
	return {};
}

std::expected<void, PeerJoinError> PeerJoin::AdvanceWorld(u32 WorldEpoch, Clock::time_point Now) noexcept
{
	if (const auto Expired = Expire(Now))
	{
		return std::unexpected(*Expired);
	}
	if (State == PeerJoinState::Closing)
	{
		return std::unexpected(PeerJoinError::InvalidState);
	}
	if (WorldEpoch <= Identity.WorldEpoch)
	{
		return std::unexpected(PeerJoinError::IdentityMismatch);
	}

	Identity.WorldEpoch = WorldEpoch;
	ClearBaseline();
	if (State == PeerJoinState::ReadySpectator)
	{
		State = PeerJoinState::Synchronizing;
		Deadline = DeadlineAfter(Now, SynchronizationTimeout);
	}
	return {};
}

void PeerJoin::ClearBaseline() noexcept
{
	SelectedBaseline.reset();
	DeclaredBarrier.reset();
}
}
