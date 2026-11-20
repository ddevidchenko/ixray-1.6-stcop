#include "BoundMessage.h"

namespace Multiplayer::Wire::Detail
{
std::expected<BoundMessageView, BoundMessageError> DecodeBoundMessage(xr_span<const u8> Bytes, MessageType Type,
	std::size_t MinPayload, std::size_t MaxPayload) noexcept
{
	const auto Frame = DecodeEnvelope(Bytes);
	if (!Frame)
	{
		return std::unexpected(BoundMessageError::InvalidEnvelope);
	}
	const auto& Header = Frame->Header;
	if (Header.Type != Type)
	{
		return std::unexpected(BoundMessageError::WrongMessageType);
	}
	const JoinIdentity Identity{Header.ServerSession, Header.WorldEpoch, Header.ConnectionEpoch};
	if (!Identity.IsBound())
	{
		return std::unexpected(BoundMessageError::UnboundJoin);
	}
	if (Frame->Payload.size() < MinPayload)
	{
		return std::unexpected(BoundMessageError::TruncatedPayload);
	}
	if (Frame->Payload.size() > MaxPayload)
	{
		return std::unexpected(BoundMessageError::TrailingPayload);
	}
	return BoundMessageView{Identity, Header.MessageSequence, Frame->Payload};
}

std::expected<std::size_t, BoundMessageError> EncodeBoundMessage(MessageType Type, JoinIdentity Identity, u32 Sequence,
	xr_span<const u8> Payload, xr_span<u8> Output) noexcept
{
	if (!Identity.IsBound())
	{
		return std::unexpected(BoundMessageError::UnboundJoin);
	}
	const auto Size = EncodeEnvelope({Type, Identity.ServerSession, Identity.WorldEpoch, Identity.ConnectionEpoch, Sequence}, Payload, Output);
	if (!Size)
	{
		const auto Error = Size.error() == EnvelopeError::OutputTooSmall ? BoundMessageError::OutputTooSmall : BoundMessageError::InvalidEnvelope;
		return std::unexpected(Error);
	}
	return *Size;
}
}
