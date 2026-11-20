#pragma once

#include "CatchUpEvent.h"
#include "ClientJoin.h"

#include <optional>
#include <type_traits>

namespace Multiplayer
{
// The owner applies checked data atomically. A failed call leaves world state unchanged.
class IClientCatchUpApplier
{
public:
	virtual ~IClientCatchUpApplier() = default;
	virtual bool ApplyEvent(const CatchUpEvent& Event) noexcept = 0;
	virtual bool ApplyBarrier(JoinIdentity Peer, JoinBarrier Barrier) noexcept = 0;
};

enum class ClientCatchUpError
{
	IdentityMismatch,
	BaselineMismatch,
	BaselineNotApplied,
	UnexpectedSequence,
	BarrierMismatch,
	BarrierPending,
	InvalidState,
	ApplicationFailed,
	StaleBaseline,
};

// The owner serializes operations and application callbacks must not reenter the tracker.
class ClientCatchUp
{
public:
	ClientCatchUp(JoinIdentity Identity, BaselineBoundary Baseline) noexcept;
	ClientCatchUp(const ClientCatchUp&) = delete;
	ClientCatchUp& operator=(const ClientCatchUp&) = delete;

	[[nodiscard]] JoinIdentity GetIdentity() const noexcept { return Identity; }
	[[nodiscard]] std::optional<u32> GetAppliedWatermark() const noexcept { return AppliedWatermark; }
	[[nodiscard]] std::optional<JoinBarrier> GetAppliedBarrier() const noexcept { return BarrierApplied ? DeclaredBarrier : std::nullopt; }
	// Apply the selected capture once. Failure leaves it unconfirmed and retryable.
	template <class Applier>
		requires std::is_nothrow_invocable_r_v<bool, Applier&>
	[[nodiscard]] std::expected<void, ClientCatchUpError> ApplyBaseline(JoinIdentity Peer, BaselineBoundary Capture, Applier&& ApplyCapture) noexcept
	{
		if (const auto Error = CheckScope(Peer, Capture.BaselineId))
		{
			return std::unexpected(*Error);
		}
		if (Capture != Baseline)
		{
			return std::unexpected(ClientCatchUpError::BaselineMismatch);
		}
		if (AppliedWatermark)
		{
			return {};
		}

		const bool Succeeded = ApplyCapture();
		if (!Succeeded)
		{
			return std::unexpected(ClientCatchUpError::ApplicationFailed);
		}
		AppliedWatermark = Baseline.EventWatermark;
		return {};
	}

	// Called after the selected capture has been imported successfully.
	[[nodiscard]] std::expected<void, ClientCatchUpError> ConfirmBaselineApplied(JoinIdentity Peer, BaselineBoundary Applied) noexcept;
	[[nodiscard]] std::expected<void, ClientCatchUpError> ApplyEvent(const CatchUpEvent& Event, IClientCatchUpApplier& Applier) noexcept;
	[[nodiscard]] std::expected<void, ClientCatchUpError> DeclareBarrier(JoinIdentity Peer, JoinBarrier Barrier) noexcept;
	// Apply the selected snapshot before acknowledging. An already applied barrier can be acknowledged again.
	template <class Applier>
		requires std::is_nothrow_invocable_r_v<bool, Applier&>
	[[nodiscard]] std::expected<Wire::ReadyAcknowledgement, ClientCatchUpError> CompleteBarrier(JoinIdentity Peer, JoinBarrier Barrier,
		u32 MessageSequence, Applier&& ApplySnapshot) noexcept
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
			const bool Succeeded = ApplySnapshot();
			if (!Succeeded)
			{
				return std::unexpected(ClientCatchUpError::ApplicationFailed);
			}
			BarrierApplied = true;
		}
		return Wire::ReadyAcknowledgement{Identity, MessageSequence, Barrier};
	}

	[[nodiscard]] std::expected<Wire::ReadyAcknowledgement, ClientCatchUpError> CompleteBarrier(JoinIdentity Peer, JoinBarrier Barrier,
		u32 MessageSequence, IClientCatchUpApplier& Applier) noexcept;
	[[nodiscard]] std::expected<void, ClientCatchUpError> Rebase(JoinIdentity Peer, BaselineBoundary Replacement) noexcept;

private:
	[[nodiscard]] bool MatchesIdentity(JoinIdentity Peer) const noexcept;
	[[nodiscard]] std::optional<ClientCatchUpError> CheckScope(JoinIdentity Peer, u32 BaselineId) const noexcept;

	const JoinIdentity Identity;
	BaselineBoundary Baseline;
	std::optional<u32> AppliedWatermark;
	std::optional<JoinBarrier> DeclaredBarrier;
	bool BarrierApplied = false;
};
}
