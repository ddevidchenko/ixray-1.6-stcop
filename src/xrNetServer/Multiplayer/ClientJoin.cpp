#include "ClientJoin.h"
#include "WireEndian.h"

namespace Multiplayer::Wire
{
constexpr std::size_t BaselineOffset = 0;
constexpr std::size_t BarrierOffset = BaselineOffset + sizeof(u32);
constexpr std::size_t TickOffset = BarrierOffset + sizeof(u32);
constexpr std::size_t WatermarkOffset = TickOffset + sizeof(u32);
constexpr std::size_t BarrierPayloadSize = WatermarkOffset + sizeof(u32);
static_assert(BarrierPayloadSize == 16 && BarrierPayloadSize <= MaxPayloadSize);

template <class Message>
static std::expected<Message, BoundMessageError> DecodeBarrierMessage(xr_span<const u8> Bytes, MessageType Type) noexcept
{
	const auto Frame = Detail::DecodeBoundMessage(Bytes, Type, BarrierPayloadSize, BarrierPayloadSize);
	if (!Frame)
	{
		return std::unexpected(Frame.error());
	}
	const auto Payload = Frame->Payload.first<BarrierPayloadSize>();
	const JoinBarrier Barrier{
		Detail::ReadLittleEndian<u32>(Payload.subspan<BaselineOffset, sizeof(u32)>()),
		Detail::ReadLittleEndian<u32>(Payload.subspan<BarrierOffset, sizeof(u32)>()),
		Detail::ReadLittleEndian<u32>(Payload.subspan<TickOffset, sizeof(u32)>()),
		Detail::ReadLittleEndian<u32>(Payload.subspan<WatermarkOffset, sizeof(u32)>()),
	};
	return Message{Frame->Identity, Frame->MessageSequence, Barrier};
}

static std::expected<std::size_t, BoundMessageError> EncodeBarrierMessage(MessageType Type, JoinIdentity Identity, u32 Sequence,
	JoinBarrier Barrier, xr_span<u8> Output) noexcept
{
	xr_array<u8, BarrierPayloadSize> Bytes;
	const auto Payload = xr_span(Bytes);
	Detail::WriteLittleEndian(Payload.subspan<BaselineOffset, sizeof(u32)>(), Barrier.BaselineId);
	Detail::WriteLittleEndian(Payload.subspan<BarrierOffset, sizeof(u32)>(), Barrier.BarrierId);
	Detail::WriteLittleEndian(Payload.subspan<TickOffset, sizeof(u32)>(), Barrier.SnapshotTick);
	Detail::WriteLittleEndian(Payload.subspan<WatermarkOffset, sizeof(u32)>(), Barrier.EventWatermark);
	return Detail::EncodeBoundMessage(Type, Identity, Sequence, Payload, Output);
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
