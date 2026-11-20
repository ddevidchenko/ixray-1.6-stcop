#pragma once

#include "JoinTypes.h"
#include "WireEnvelope.h"
#include "../../xrCore/_stl_extensions_nonalloc.h"

#include <expected>

namespace Multiplayer::Wire
{
enum class BoundMessageError
{
	InvalidEnvelope,
	WrongMessageType,
	UnboundJoin,
	TruncatedPayload,
	TrailingPayload,
	OutputTooSmall,
	InvalidPayload,
};

namespace Detail
{
struct BoundMessageView
{
	JoinIdentity Identity;
	u32 MessageSequence;
	xr_span<const u8> Payload;
};

[[nodiscard]] std::expected<BoundMessageView, BoundMessageError> DecodeBoundMessage(xr_span<const u8> Bytes, MessageType Type,
	std::size_t MinPayload, std::size_t MaxPayload) noexcept;
[[nodiscard]] std::expected<std::size_t, BoundMessageError> EncodeBoundMessage(MessageType Type, JoinIdentity Identity, u32 Sequence,
	xr_span<const u8> Payload, xr_span<u8> Output) noexcept;
}
}
