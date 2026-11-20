#pragma once

#include "AdmissionReject.h"
#include "CompatibilityId.h"

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

enum class PendingAdmissionState
{
	AwaitingHello,
	AwaitingSlot,
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

	[[nodiscard]] PendingAdmissionState GetState() const noexcept { return State; }
	// Returns a reason only when this call moves the peer to Closing.
	[[nodiscard]] std::optional<Wire::AdmissionRejectReason> Expire(Clock::time_point Now) noexcept;
	// The verified display name borrows Bytes. ShouldReject is true only on entry to Closing.
	[[nodiscard]] std::expected<VerifiedHello, PendingAdmissionFailure> ReceiveHello(xr_span<const u8> Bytes,
		const AdmissionRequirements& Requirements, const IAdmissionCredentialVerifier& Verifier, Clock::time_point Now) noexcept;

private:
	const Clock::time_point Deadline;
	PendingAdmissionState State = PendingAdmissionState::AwaitingHello;
};
}
