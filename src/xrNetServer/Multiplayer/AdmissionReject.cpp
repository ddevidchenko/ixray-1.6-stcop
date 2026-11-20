#include "AdmissionReject.h"
#include "WireEnvelope.h"

namespace Multiplayer::Wire
{
static bool IsKnownReason(AdmissionRejectReason Reason) noexcept
{
	switch (Reason)
	{
	case AdmissionRejectReason::MalformedHello:
	case AdmissionRejectReason::UnsupportedProtocol:
	case AdmissionRejectReason::IncompatibleBuild:
	case AdmissionRejectReason::IncompatibleContent:
	case AdmissionRejectReason::IncompatibleRules:
	case AdmissionRejectReason::CredentialsRejected:
	case AdmissionRejectReason::ServerFull:
	case AdmissionRejectReason::ServerBusy:
	case AdmissionRejectReason::AdmissionTimedOut:
	case AdmissionRejectReason::InvalidState:
		return true;
	}
	return false;
}

std::expected<AdmissionReject, AdmissionRejectError> DecodeAdmissionReject(xr_span<const u8> Bytes) noexcept
{
	const auto Frame = DecodeEnvelope(Bytes);
	if (!Frame)
	{
		return std::unexpected(AdmissionRejectError::InvalidEnvelope);
	}
	const auto& Header = Frame->Header;
	if (Header.Type != MessageType::AdmissionReject)
	{
		return std::unexpected(AdmissionRejectError::WrongMessageType);
	}
	if (!Header.HasUnboundIdentity())
	{
		return std::unexpected(AdmissionRejectError::BoundReject);
	}
	if (Frame->Payload.empty())
	{
		return std::unexpected(AdmissionRejectError::MissingReason);
	}
	if (Frame->Payload.size() != 1)
	{
		return std::unexpected(AdmissionRejectError::TrailingPayload);
	}
	const auto Reason = static_cast<AdmissionRejectReason>(Frame->Payload.front());
	if (!IsKnownReason(Reason))
	{
		return std::unexpected(AdmissionRejectError::UnknownReason);
	}
	return AdmissionReject{Header.MessageSequence, Reason};
}

std::expected<std::size_t, AdmissionRejectError> EncodeAdmissionReject(AdmissionReject Rejection, xr_span<u8> Output) noexcept
{
	if (!IsKnownReason(Rejection.Reason))
	{
		return std::unexpected(AdmissionRejectError::UnknownReason);
	}
	const u8 Payload[] = {static_cast<u8>(Rejection.Reason)};
	const auto Size = EncodeEnvelope({MessageType::AdmissionReject, 0, 0, 0, Rejection.MessageSequence}, Payload, Output);
	if (!Size)
	{
		return std::unexpected(AdmissionRejectError::OutputTooSmall);
	}
	return *Size;
}
}
