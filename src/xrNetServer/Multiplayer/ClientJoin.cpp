#include "ClientJoin.h"
#include "WireEndian.h"

namespace Multiplayer::Wire
{
constexpr std::size_t BaselineOffset = 0;
constexpr std::size_t BarrierOffset = BaselineOffset + sizeof(u32);
constexpr std::size_t TickOffset = BarrierOffset + sizeof(u32);
constexpr std::size_t WatermarkOffset = TickOffset + sizeof(u32);
constexpr std::size_t ReadyPayloadSize = WatermarkOffset + sizeof(u32);
static_assert(ReadyPayloadSize <= MaxPayloadSize);

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
	const auto Frame = Detail::DecodeBoundMessage(Bytes, MessageType::ReadyAcknowledgement, ReadyPayloadSize, ReadyPayloadSize);
	if (!Frame)
	{
		return std::unexpected(Frame.error());
	}
	const auto Payload = Frame->Payload.first<ReadyPayloadSize>();
	const JoinBarrier Barrier{
		Detail::ReadLittleEndian<u32>(Payload.subspan<BaselineOffset, sizeof(u32)>()),
		Detail::ReadLittleEndian<u32>(Payload.subspan<BarrierOffset, sizeof(u32)>()),
		Detail::ReadLittleEndian<u32>(Payload.subspan<TickOffset, sizeof(u32)>()),
		Detail::ReadLittleEndian<u32>(Payload.subspan<WatermarkOffset, sizeof(u32)>()),
	};
	return ReadyAcknowledgement{Frame->Identity, Frame->MessageSequence, Barrier};
}

std::expected<std::size_t, ClientJoinError> EncodeClientMapLoaded(ClientMapLoaded Loaded, xr_span<u8> Output) noexcept
{
	return Detail::EncodeBoundMessage(MessageType::ClientMapLoaded, Loaded.Identity, Loaded.MessageSequence, {}, Output);
}

std::expected<std::size_t, ClientJoinError> EncodeReadyAcknowledgement(ReadyAcknowledgement Ready, xr_span<u8> Output) noexcept
{
	xr_array<u8, ReadyPayloadSize> Bytes;
	const auto Payload = xr_span(Bytes);
	Detail::WriteLittleEndian(Payload.subspan<BaselineOffset, sizeof(u32)>(), Ready.Barrier.BaselineId);
	Detail::WriteLittleEndian(Payload.subspan<BarrierOffset, sizeof(u32)>(), Ready.Barrier.BarrierId);
	Detail::WriteLittleEndian(Payload.subspan<TickOffset, sizeof(u32)>(), Ready.Barrier.SnapshotTick);
	Detail::WriteLittleEndian(Payload.subspan<WatermarkOffset, sizeof(u32)>(), Ready.Barrier.EventWatermark);
	return Detail::EncodeBoundMessage(MessageType::ReadyAcknowledgement, Ready.Identity, Ready.MessageSequence, Payload, Output);
}
}
