#include "PendingAdmission.h"
#include "ClientHello.h"

namespace Multiplayer
{
using Wire::AdmissionRejectReason;

PendingAdmission::PendingAdmission(Clock::time_point Expiration) noexcept : Deadline(Expiration)
{
}

std::optional<AdmissionRejectReason> PendingAdmission::Expire(Clock::time_point Now) noexcept
{
	if (State == PendingAdmissionState::Closing || Now < Deadline)
	{
		return std::nullopt;
	}
	State = PendingAdmissionState::Closing;
	return AdmissionRejectReason::AdmissionTimedOut;
}

std::expected<VerifiedHello, PendingAdmissionFailure> PendingAdmission::ReceiveHello(xr_span<const u8> Bytes,
	const AdmissionRequirements& Requirements, const IAdmissionCredentialVerifier& Verifier, Clock::time_point Now) noexcept
{
	if (const auto Expired = Expire(Now))
	{
		return std::unexpected(PendingAdmissionFailure{*Expired, true});
	}
	if (State != PendingAdmissionState::AwaitingHello)
	{
		return std::unexpected(PendingAdmissionFailure{AdmissionRejectReason::InvalidState, false});
	}

	const auto Reject = [this](AdmissionRejectReason Reason)
	{
		State = PendingAdmissionState::Closing;
		return std::unexpected(PendingAdmissionFailure{Reason, true});
	};
	const auto Hello = Wire::DecodeClientHello(Bytes);
	if (!Hello)
	{
		switch (Hello.error())
		{
		case Wire::ClientHelloError::UnsupportedVersion:
			return Reject(AdmissionRejectReason::UnsupportedProtocol);
		case Wire::ClientHelloError::WrongMessageType:
			return Reject(AdmissionRejectReason::InvalidState);
		default:
			return Reject(AdmissionRejectReason::MalformedHello);
		}
	}
	if (Hello->Build != Requirements.Build)
	{
		return Reject(AdmissionRejectReason::IncompatibleBuild);
	}
	if (Hello->Content != Requirements.Content)
	{
		return Reject(AdmissionRejectReason::IncompatibleContent);
	}
	if (Hello->Rules != Requirements.Rules)
	{
		return Reject(AdmissionRejectReason::IncompatibleRules);
	}
	if (Requirements.RequireCredentials && (Hello->Credentials.empty() || !Verifier.Verify(Hello->Credentials)))
	{
		return Reject(AdmissionRejectReason::CredentialsRejected);
	}

	State = PendingAdmissionState::AwaitingSlot;
	return VerifiedHello{Hello->MessageSequence, Hello->DisplayName};
}
}
