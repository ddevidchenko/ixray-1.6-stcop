#pragma once

#include "JoinTypes.h"
#include "../../xrCore/_stl_extensions_nonalloc.h"

#include <expected>

namespace Multiplayer::Wire
{
enum class ClientJoinError
{
	InvalidEnvelope,
	WrongMessageType,
	UnboundJoin,
	TruncatedPayload,
	TrailingPayload,
	OutputTooSmall,
};

struct ClientMapLoaded
{
	JoinIdentity Identity;
	u32 MessageSequence = 0;
};

struct ReadyAcknowledgement
{
	JoinIdentity Identity;
	u32 MessageSequence = 0;
	JoinBarrier Barrier{};
};

// Decoded messages own all of their fields.
[[nodiscard]] std::expected<ClientMapLoaded, ClientJoinError> DecodeClientMapLoaded(xr_span<const u8> Bytes) noexcept;
[[nodiscard]] std::expected<ReadyAcknowledgement, ClientJoinError> DecodeReadyAcknowledgement(xr_span<const u8> Bytes) noexcept;
// Failed encodes leave the output unchanged.
[[nodiscard]] std::expected<std::size_t, ClientJoinError> EncodeClientMapLoaded(ClientMapLoaded Loaded, xr_span<u8> Output) noexcept;
[[nodiscard]] std::expected<std::size_t, ClientJoinError> EncodeReadyAcknowledgement(ReadyAcknowledgement Ready, xr_span<u8> Output) noexcept;
}
