#pragma once

#include "BaselineTransfer.h"
#include "ReliableSendQueue.h"

#include <optional>

namespace Multiplayer
{
enum class BaselineSendResult
{
	Submitted,
	Deferred,
	Complete,
	InvalidSource,
	TimedOut,
	ControlFailed,
	TransportFailed,
};

// The peer owner keeps Source stable until the transfer completes or is retired, and
// destroys the transfer when the selected baseline, world or connection changes.
class BaselineSend
{
public:
	BaselineSend(JoinIdentity Identity, Wire::BaselineManifest Manifest, xr_span<const u8> Source, u64 DeadlineMs) noexcept;
	BaselineSend(const BaselineSend&) = delete;
	BaselineSend& operator=(const BaselineSend&) = delete;

	[[nodiscard]] bool IsComplete() const noexcept { return SubmittedCount > u32{Manifest.ChunkCount} + 1; }

	// Control belongs to this peer. Send borrows the frame for one native attempt and
	// cannot mutate the transfer, control queue, or the shared outgoing sequence.
	template <class Sender>
		requires std::is_nothrow_invocable_r_v<ReliableSendAttempt, Sender&, xr_span<const u8>>
	[[nodiscard]] BaselineSendResult SubmitNext(const ReliableSendQueue& Control, u32& NextMessageSequence, u64 NowMs, Sender&& Send) noexcept
	{
		if (Failure)
		{
			return *Failure;
		}
		if (IsComplete())
		{
			return BaselineSendResult::Complete;
		}

		const auto ControlStatus = Control.Inspect(NowMs);
		if (ControlStatus.Failure != ReliableSendFailure::None)
		{
			return Stop(BaselineSendResult::ControlFailed);
		}
		if (NowMs >= DeadlineMs)
		{
			return Stop(BaselineSendResult::TimedOut);
		}
		if (ControlStatus.PendingCount)
		{
			return BaselineSendResult::Deferred;
		}

		xr_array<u8, Wire::MaxMessageSize> Frame;
		const auto Encoded = EncodeNext(NextMessageSequence, Frame);
		if (!Encoded)
		{
			return Stop(BaselineSendResult::InvalidSource);
		}
		const ReliableSendAttempt Attempt = Send(xr_span<const u8>(Frame).first(*Encoded));
		if (Attempt == ReliableSendAttempt::Backpressured)
		{
			return BaselineSendResult::Deferred;
		}
		if (Attempt != ReliableSendAttempt::Accepted)
		{
			return Stop(BaselineSendResult::TransportFailed);
		}
		++SubmittedCount;
		++NextMessageSequence;
		return BaselineSendResult::Submitted;
	}

private:
	[[nodiscard]] std::expected<std::size_t, Wire::BoundMessageError> EncodeNext(u32 Sequence, xr_span<u8> Output) const noexcept;
	BaselineSendResult Stop(BaselineSendResult Reason) noexcept
	{
		Failure = Reason;
		return Reason;
	}

	const JoinIdentity Identity;
	const Wire::BaselineManifest Manifest;
	const xr_span<const u8> Source;
	const u64 DeadlineMs;
	u32 SubmittedCount = 0;
	std::optional<BaselineSendResult> Failure;
};
}
