#include "PendingAdmission.h"
#include "ClientHello.h"
#include "WireEnvelope.h"

namespace Multiplayer
{
using Wire::AdmissionRejectReason;

PendingAdmission::PendingAdmission(Clock::time_point Expiration) noexcept : Deadline(Expiration)
{
}

std::optional<AdmissionRejectReason> PendingAdmission::Expire(Clock::time_point Now) noexcept
{
	if ((State != PendingAdmissionState::AwaitingHello && State != PendingAdmissionState::AwaitingSlot) || Now < Deadline)
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

	VerifiedContent = Hello->Content;
	VerifiedRules = Hello->Rules;
	State = PendingAdmissionState::AwaitingSlot;
	return VerifiedHello{Hello->MessageSequence, Hello->DisplayName};
}

std::expected<void, AdmissionCompletionError> PendingAdmission::Complete(const Wire::ServerWelcome& Welcome,
	IAdmissionSlotReservation& Slot, IAdmissionWelcomeQueue& Queue, Clock::time_point Now) noexcept
{
	if (Expire(Now))
	{
		return std::unexpected(AdmissionCompletionError::AdmissionTimedOut);
	}
	if (State != PendingAdmissionState::AwaitingSlot)
	{
		return std::unexpected(AdmissionCompletionError::InvalidState);
	}

	const auto Close = [this](AdmissionCompletionError Error)
	{
		State = PendingAdmissionState::Closing;
		return std::unexpected(Error);
	};
	if (Welcome.ContentId != VerifiedContent || Welcome.RulesId != VerifiedRules)
	{
		return Close(AdmissionCompletionError::InvalidWelcome);
	}
	xr_array<u8, Wire::MaxMessageSize> Packet;
	const auto Size = Wire::EncodeServerWelcome(Welcome, Packet);
	if (!Size)
	{
		return Close(AdmissionCompletionError::InvalidWelcome);
	}
	if (!Slot.TryReserve())
	{
		return Close(AdmissionCompletionError::ServerFull);
	}
	if (!Queue.TryQueue(xr_span(Packet).first(*Size)))
	{
		Slot.Cancel();
		return Close(AdmissionCompletionError::WelcomeQueueFailed);
	}

	Slot.Commit();
	State = PendingAdmissionState::Admitted;
	return {};
}
}
