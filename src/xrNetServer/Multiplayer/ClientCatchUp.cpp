#include "ClientCatchUp.h"
#include "SerialSequence.h"

namespace Multiplayer
{
ClientCatchUp::ClientCatchUp(JoinIdentity Identity, BaselineBoundary Baseline) noexcept : Identity(Identity), Baseline(Baseline)
{
}

bool ClientCatchUp::MatchesIdentity(JoinIdentity Peer) const noexcept
{
	return Identity.IsBound() && Identity == Peer;
}

std::optional<ClientCatchUpError> ClientCatchUp::CheckScope(JoinIdentity Peer, u32 BaselineId) const noexcept
{
	if (!MatchesIdentity(Peer))
	{
		return ClientCatchUpError::IdentityMismatch;
	}
	if (BaselineId != Baseline.BaselineId)
	{
		return ClientCatchUpError::BaselineMismatch;
	}
	return std::nullopt;
}

std::expected<void, ClientCatchUpError> ClientCatchUp::ConfirmBaselineApplied(JoinIdentity Peer, BaselineBoundary Applied) noexcept
{
	if (!MatchesIdentity(Peer))
	{
		return std::unexpected(ClientCatchUpError::IdentityMismatch);
	}
	if (Applied != Baseline)
	{
		return std::unexpected(ClientCatchUpError::BaselineMismatch);
	}
	if (!AppliedWatermark)
	{
		AppliedWatermark = Baseline.EventWatermark;
	}
	return {};
}

std::expected<void, ClientCatchUpError> ClientCatchUp::ApplyEvent(const CatchUpEvent& Event, IClientCatchUpApplier& Applier) noexcept
{
	if (const auto Error = CheckScope(Event.Identity, Event.BaselineId))
	{
		return std::unexpected(*Error);
	}
	if (!AppliedWatermark)
	{
		return std::unexpected(ClientCatchUpError::BaselineNotApplied);
	}
	if (Event.Sequence != *AppliedWatermark + 1u)
	{
		return std::unexpected(ClientCatchUpError::UnexpectedSequence);
	}
	if (DeclaredBarrier && !BarrierApplied && *AppliedWatermark == DeclaredBarrier->EventWatermark)
	{
		return std::unexpected(ClientCatchUpError::BarrierPending);
	}
	if (!Applier.ApplyEvent(Event))
	{
		return std::unexpected(ClientCatchUpError::ApplicationFailed);
	}
	AppliedWatermark = Event.Sequence;
	return {};
}

std::expected<void, ClientCatchUpError> ClientCatchUp::DeclareBarrier(JoinIdentity Peer, JoinBarrier Barrier) noexcept
{
	if (const auto Error = CheckScope(Peer, Barrier.BaselineId))
	{
		return std::unexpected(*Error);
	}
	if (DeclaredBarrier)
	{
		if (*DeclaredBarrier != Barrier)
		{
			return std::unexpected(ClientCatchUpError::BarrierMismatch);
		}
		return {};
	}
	if (!IsSequenceCurrentOrNewer(Barrier.SnapshotTick, Baseline.CaptureTick) ||
		!IsSequenceCurrentOrNewer(Barrier.EventWatermark, AppliedWatermark.value_or(Baseline.EventWatermark)))
	{
		return std::unexpected(ClientCatchUpError::BarrierMismatch);
	}
	DeclaredBarrier = Barrier;
	return {};
}

std::expected<Wire::ReadyAcknowledgement, ClientCatchUpError> ClientCatchUp::CompleteBarrier(JoinIdentity Peer, JoinBarrier Barrier,
	u32 MessageSequence, IClientCatchUpApplier& Applier) noexcept
{
	if (const auto Error = CheckScope(Peer, Barrier.BaselineId))
	{
		return std::unexpected(*Error);
	}
	if (!DeclaredBarrier)
	{
		return std::unexpected(ClientCatchUpError::InvalidState);
	}
	if (*DeclaredBarrier != Barrier)
	{
		return std::unexpected(ClientCatchUpError::BarrierMismatch);
	}
	if (!AppliedWatermark)
	{
		return std::unexpected(ClientCatchUpError::BaselineNotApplied);
	}
	if (!BarrierApplied)
	{
		if (*AppliedWatermark != Barrier.EventWatermark)
		{
			return std::unexpected(ClientCatchUpError::BarrierPending);
		}
		if (!Applier.ApplyBarrier(Peer, Barrier))
		{
			return std::unexpected(ClientCatchUpError::ApplicationFailed);
		}
		BarrierApplied = true;
	}
	return Wire::ReadyAcknowledgement{Identity, MessageSequence, Barrier};
}

std::expected<void, ClientCatchUpError> ClientCatchUp::Rebase(JoinIdentity Peer, BaselineBoundary Replacement) noexcept
{
	if (!MatchesIdentity(Peer))
	{
		return std::unexpected(ClientCatchUpError::IdentityMismatch);
	}
	const bool CoversAppliedBarrier = !BarrierApplied || IsSequenceCurrentOrNewer(Replacement.CaptureTick, DeclaredBarrier->SnapshotTick);
	if (!IsNewerBaseline(Replacement, Baseline) || !CoversAppliedBarrier ||
		(AppliedWatermark && !IsSequenceCurrentOrNewer(Replacement.EventWatermark, *AppliedWatermark)))
	{
		return std::unexpected(ClientCatchUpError::StaleBaseline);
	}
	Baseline = Replacement;
	AppliedWatermark.reset();
	DeclaredBarrier.reset();
	BarrierApplied = false;
	return {};
}
}
