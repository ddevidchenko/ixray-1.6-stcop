#pragma once

#include "../../xrCore/ScalarTypes.h"
#include "../../xrCore/_stl_extensions_nonalloc.h"

#include <expected>

namespace Multiplayer::Wire
{
using CompatibilityId = xr_array<u8, 32>;

enum class ClientHelloError
{
	InvalidEnvelope,
	WrongMessageType,
	BoundHello,
	TruncatedPayload,
	InvalidDisplayName,
	CredentialsTooLong,
	TrailingPayload,
	OutputTooSmall,
};

struct ClientHello
{
	u32 MessageSequence = 0;
	CompatibilityId Build{};
	CompatibilityId Content{};
	CompatibilityId Rules{};
	xr_string_view DisplayName;
	xr_span<const u8> Credentials;
};

// DisplayName and Credentials borrow the supplied complete message.
[[nodiscard]] std::expected<ClientHello, ClientHelloError> DecodeClientHello(xr_span<const u8> Bytes) noexcept;

// Encodes a complete unbound message. The borrowed fields may overlap the output.
// On failure the output is unchanged.
[[nodiscard]] std::expected<std::size_t, ClientHelloError> EncodeClientHello(const ClientHello& Hello, xr_span<u8> Output) noexcept;
}
