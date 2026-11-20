#include "ClientJoin.h"
#include "JoinBarrierFields.h"

namespace Multiplayer::Wire
{
template <class Message>
static std::expected<Message, BoundMessageError> DecodeBarrierMessage(xr_span<const u8> Bytes, MessageType Type) noexcept
{
	const auto Frame = Detail::DecodeBoundMessage(Bytes, Type, Detail::BarrierPayloadSize, Detail::BarrierPayloadSize);
	if (!Frame)
	{
		return std::unexpected(Frame.error());
	}
	const auto Barrier = Detail::ReadJoinBarrier(Frame->Payload.first<Detail::BarrierPayloadSize>());
	return Message{Frame->Identity, Frame->MessageSequence, Barrier};
}

static std::expected<std::size_t, BoundMessageError> EncodeBarrierMessage(MessageType Type, JoinIdentity Identity, u32 Sequence,
	JoinBarrier Barrier, xr_span<u8> Output) noexcept
{
	xr_array<u8, Detail::BarrierPayloadSize> Bytes;
	Detail::WriteJoinBarrier(Bytes, Barrier);
	return Detail::EncodeBoundMessage(Type, Identity, Sequence, Bytes, Output);
}

std::expected<ClientMapLoaded, ClientJoinError> DecodeClientMapLoaded(xr_span<const u8> Bytes) noexcept
{
	const auto Frame = Detail::DecodeBoundMessage(Bytes, MessageType::ClientMapLoaded, 0, 0);
	if (!Frame)
	{
		return std::unexpected(Frame.error());
	}
	return ClientMapLoaded{Frame->Identity, Frame->MessageSequence};
}

std::expected<ReadyAcknowledgement, ClientJoinError> DecodeReadyAcknowledgement(xr_span<const u8> Bytes) noexcept
{
	return DecodeBarrierMessage<ReadyAcknowledgement>(Bytes, MessageType::ReadyAcknowledgement);
}

std::expected<ServerJoinBarrier, BoundMessageError> DecodeServerJoinBarrier(xr_span<const u8> Bytes) noexcept
{
	return DecodeBarrierMessage<ServerJoinBarrier>(Bytes, MessageType::ServerJoinBarrier);
}

std::expected<std::size_t, ClientJoinError> EncodeClientMapLoaded(ClientMapLoaded Loaded, xr_span<u8> Output) noexcept
{
	return Detail::EncodeBoundMessage(MessageType::ClientMapLoaded, Loaded.Identity, Loaded.MessageSequence, {}, Output);
}

std::expected<std::size_t, ClientJoinError> EncodeReadyAcknowledgement(ReadyAcknowledgement Ready, xr_span<u8> Output) noexcept
{
	return EncodeBarrierMessage(MessageType::ReadyAcknowledgement, Ready.Identity, Ready.MessageSequence, Ready.Barrier, Output);
}

std::expected<std::size_t, BoundMessageError> EncodeServerJoinBarrier(ServerJoinBarrier Declaration, xr_span<u8> Output) noexcept
{
	return EncodeBarrierMessage(MessageType::ServerJoinBarrier, Declaration.Identity, Declaration.MessageSequence, Declaration.Barrier, Output);
}
}
