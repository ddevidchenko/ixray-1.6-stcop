#include "ClientJoin.h"
#include "WireEnvelope.h"
#include "WireEndian.h"

namespace Multiplayer::Wire
{
constexpr std::size_t BaselineOffset = 0;
constexpr std::size_t BarrierOffset = BaselineOffset + sizeof(u32);
constexpr std::size_t TickOffset = BarrierOffset + sizeof(u32);
constexpr std::size_t WatermarkOffset = TickOffset + sizeof(u32);
constexpr std::size_t ReadyPayloadSize = WatermarkOffset + sizeof(u32);
static_assert(ReadyPayloadSize <= MaxPayloadSize);

static JoinIdentity ReadIdentity(const EnvelopeHeader& Header) noexcept
{
	return {Header.ServerSession, Header.WorldEpoch, Header.ConnectionEpoch};
}

static std::expected<EnvelopeView, ClientJoinError> DecodeJoinFrame(xr_span<const u8> Bytes, MessageType Type, std::size_t PayloadSize) noexcept
{
	const auto Frame = DecodeEnvelope(Bytes);
	if (!Frame)
	{
		return std::unexpected(ClientJoinError::InvalidEnvelope);
	}
	if (Frame->Header.Type != Type)
	{
		return std::unexpected(ClientJoinError::WrongMessageType);
	}
	if (!ReadIdentity(Frame->Header).IsBound())
	{
		return std::unexpected(ClientJoinError::UnboundJoin);
	}
	if (Frame->Payload.size() < PayloadSize)
	{
		return std::unexpected(ClientJoinError::TruncatedPayload);
	}
	if (Frame->Payload.size() > PayloadSize)
	{
		return std::unexpected(ClientJoinError::TrailingPayload);
	}
	return *Frame;
}

static std::expected<std::size_t, ClientJoinError> EncodeJoinFrame(MessageType Type, JoinIdentity Identity, u32 Sequence,
	xr_span<const u8> Payload, xr_span<u8> Output) noexcept
{
	if (!Identity.IsBound())
	{
		return std::unexpected(ClientJoinError::UnboundJoin);
	}
	const auto Size = EncodeEnvelope({Type, Identity.ServerSession, Identity.WorldEpoch, Identity.ConnectionEpoch, Sequence}, Payload, Output);
	if (!Size)
	{
		return std::unexpected(ClientJoinError::OutputTooSmall);
	}
	return *Size;
}

std::expected<ClientMapLoaded, ClientJoinError> DecodeClientMapLoaded(xr_span<const u8> Bytes) noexcept
{
	const auto Frame = DecodeJoinFrame(Bytes, MessageType::ClientMapLoaded, 0);
	if (!Frame)
	{
		return std::unexpected(Frame.error());
	}
	return ClientMapLoaded{ReadIdentity(Frame->Header), Frame->Header.MessageSequence};
}

std::expected<ReadyAcknowledgement, ClientJoinError> DecodeReadyAcknowledgement(xr_span<const u8> Bytes) noexcept
{
	const auto Frame = DecodeJoinFrame(Bytes, MessageType::ReadyAcknowledgement, ReadyPayloadSize);
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
	return ReadyAcknowledgement{ReadIdentity(Frame->Header), Frame->Header.MessageSequence, Barrier};
}

std::expected<std::size_t, ClientJoinError> EncodeClientMapLoaded(ClientMapLoaded Loaded, xr_span<u8> Output) noexcept
{
	return EncodeJoinFrame(MessageType::ClientMapLoaded, Loaded.Identity, Loaded.MessageSequence, {}, Output);
}

std::expected<std::size_t, ClientJoinError> EncodeReadyAcknowledgement(ReadyAcknowledgement Ready, xr_span<u8> Output) noexcept
{
	xr_array<u8, ReadyPayloadSize> Bytes;
	const auto Payload = xr_span(Bytes);
	Detail::WriteLittleEndian(Payload.subspan<BaselineOffset, sizeof(u32)>(), Ready.Barrier.BaselineId);
	Detail::WriteLittleEndian(Payload.subspan<BarrierOffset, sizeof(u32)>(), Ready.Barrier.BarrierId);
	Detail::WriteLittleEndian(Payload.subspan<TickOffset, sizeof(u32)>(), Ready.Barrier.SnapshotTick);
	Detail::WriteLittleEndian(Payload.subspan<WatermarkOffset, sizeof(u32)>(), Ready.Barrier.EventWatermark);
	return EncodeJoinFrame(MessageType::ReadyAcknowledgement, Ready.Identity, Ready.MessageSequence, Payload, Output);
}
}
