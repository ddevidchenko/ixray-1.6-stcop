#pragma once

#include "AdmissionReject.h"
#include "CompatibilityId.h"
#include "ServerWelcome.h"

#include <chrono>
#include <expected>
#include <optional>

namespace Multiplayer
{
struct AdmissionRequirements
{
	Wire::CompatibilityId Build{};
	Wire::CompatibilityId Content{};
	Wire::CompatibilityId Rules{};
	bool RequireCredentials = false;
};

class IAdmissionCredentialVerifier
{
public:
	virtual ~IAdmissionCredentialVerifier() = default;
	// The credential view is valid for this call.
	virtual bool Verify(xr_span<const u8> Credentials) const noexcept = 0;
};

class IAdmissionSlotReservation
{
public:
	virtual ~IAdmissionSlotReservation() = default;
	// Provisionally holds one eligible slot for this peer. False leaves capacity unchanged.
	virtual bool TryReserve() noexcept = 0;
	// A successful hold is committed or canceled on the same owner turn.
	virtual void Commit() noexcept = 0;
	virtual void Cancel() noexcept = 0;
};

class IAdmissionWelcomeQueue
{
public:
	virtual ~IAdmissionWelcomeQueue() = default;
	// Copies the packet into the peer's reliable queue. False means nothing was queued.
	virtual bool TryQueue(xr_span<const u8> Packet) noexcept = 0;
};

enum class AdmissionCompletionError
{
	InvalidState,
	AdmissionTimedOut,
	ServerFull,
	InvalidWelcome,
	WelcomeQueueFailed,
};

enum class PendingAdmissionState
{
	AwaitingHello,
	AwaitingSlot,
	Admitted,
	Closing,
};

struct PendingAdmissionFailure
{
	Wire::AdmissionRejectReason Reason;
	bool ShouldReject;
};

struct VerifiedHello
{
	u32 MessageSequence;
	xr_string_view DisplayName;
};

class PendingAdmission
{
public:
	using Clock = std::chrono::steady_clock;

	explicit PendingAdmission(Clock::time_point Expiration) noexcept;
	PendingAdmission(const PendingAdmission&) = delete;
	PendingAdmission& operator=(const PendingAdmission&) = delete;

	[[nodiscard]] PendingAdmissionState GetState() const noexcept { return State; }
	// Returns a reason only when this call moves the peer to Closing.
	[[nodiscard]] std::optional<Wire::AdmissionRejectReason> Expire(Clock::time_point Now) noexcept;
	// The verified display name borrows Bytes. ShouldReject is true only on entry to Closing.
	[[nodiscard]] std::expected<VerifiedHello, PendingAdmissionFailure> ReceiveHello(xr_span<const u8> Bytes,
		const AdmissionRequirements& Requirements, const IAdmissionCredentialVerifier& Verifier, Clock::time_point Now) noexcept;

	// The owner completes admission and its reservation on one serialized turn.
	// The reservation and queue must belong to this peer and must not reenter admission.
	// ServerFull and AdmissionTimedOut are terminal and can be sent as wire rejects.
	[[nodiscard]] std::expected<void, AdmissionCompletionError> Complete(const Wire::ServerWelcome& Welcome,
		IAdmissionSlotReservation& Slot, IAdmissionWelcomeQueue& Queue, Clock::time_point Now) noexcept;

private:
	const Clock::time_point Deadline;
	PendingAdmissionState State = PendingAdmissionState::AwaitingHello;
	Wire::CompatibilityId VerifiedContent{};
	Wire::CompatibilityId VerifiedRules{};
};
}
