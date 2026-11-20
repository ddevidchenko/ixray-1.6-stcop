#pragma once

#include "../../xrCore/ScalarTypes.h"
#include "../../xrCore/_stl_extensions_nonalloc.h"

#include <expected>

namespace Multiplayer::Wire
{
enum class AdmissionRejectReason : u8
{
	MalformedHello = 0x01,
	UnsupportedProtocol = 0x02,
	IncompatibleBuild = 0x03,
	IncompatibleContent = 0x04,
	IncompatibleRules = 0x05,
	CredentialsRejected = 0x06,
	ServerFull = 0x07,
	ServerBusy = 0x08,
	AdmissionTimedOut = 0x09,
	InvalidState = 0x0a,
};

enum class AdmissionRejectError
{
	InvalidEnvelope,
	WrongMessageType,
	BoundReject,
	MissingReason,
	UnknownReason,
	TrailingPayload,
	OutputTooSmall,
};

struct AdmissionReject
{
	u32 MessageSequence = 0;
	AdmissionRejectReason Reason{};

	bool operator==(const AdmissionReject&) const = default;
};

[[nodiscard]] std::expected<AdmissionReject, AdmissionRejectError> DecodeAdmissionReject(xr_span<const u8> Bytes) noexcept;

// Encodes a complete unbound message. On failure the output is unchanged.
[[nodiscard]] std::expected<std::size_t, AdmissionRejectError> EncodeAdmissionReject(AdmissionReject Rejection, xr_span<u8> Output) noexcept;
}
