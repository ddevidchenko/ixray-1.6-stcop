#include "CatchUpDespawn.h"
#include "WireEndian.h"

namespace Multiplayer::Wire
{
constexpr std::size_t DespawnBaselineOffset = 0;
constexpr std::size_t DespawnSequenceOffset = DespawnBaselineOffset + sizeof(u32);
constexpr std::size_t DespawnEventIdOffset = DespawnSequenceOffset + sizeof(u32);
constexpr std::size_t DespawnTickOffset = DespawnEventIdOffset + sizeof(u64);
constexpr std::size_t DespawnKindOffset = DespawnTickOffset + sizeof(u32);
constexpr std::size_t DespawnParticipantOffset = DespawnKindOffset + sizeof(u8);
constexpr std::size_t DespawnObjectOffset = DespawnParticipantOffset + sizeof(u32);
constexpr std::size_t DespawnGenerationOffset = DespawnObjectOffset + sizeof(u16);
constexpr std::size_t DespawnLifeOffset = DespawnGenerationOffset + sizeof(u32);
constexpr std::size_t DespawnPayloadSize = DespawnLifeOffset + sizeof(u32);
constexpr u8 DespawnKind = 0x01;

static_assert(DespawnPayloadSize == 35 && DespawnPayloadSize <= MaxPayloadSize);

static bool HasAssignedDespawnFields(const CatchUpDespawn& Event) noexcept
{
	return Event.EventId != 0 && Event.Participant.IsAssigned() && Event.Body.IsAssigned();
}

std::expected<DecodedCatchUpDespawn, BoundMessageError> DecodeCatchUpDespawn(xr_span<const u8> Bytes) noexcept
{
	const auto Frame = Detail::DecodeBoundMessage(Bytes, MessageType::CatchUpStateEvent, DespawnPayloadSize, DespawnPayloadSize);
	if (!Frame)
	{
		return std::unexpected(Frame.error());
	}
	const auto Payload = Frame->Payload.first<DespawnPayloadSize>();
	if (Payload[DespawnKindOffset] != DespawnKind)
	{
		return std::unexpected(BoundMessageError::InvalidPayload);
	}

	const CatchUpDespawn Message{
		Frame->Identity,
		Frame->MessageSequence,
		Detail::ReadLittleEndian<u32>(Payload.subspan<DespawnBaselineOffset, sizeof(u32)>()),
		Detail::ReadLittleEndian<u32>(Payload.subspan<DespawnSequenceOffset, sizeof(u32)>()),
		Detail::ReadLittleEndian<u64>(Payload.subspan<DespawnEventIdOffset, sizeof(u64)>()),
		Detail::ReadLittleEndian<u32>(Payload.subspan<DespawnTickOffset, sizeof(u32)>()),
		{Detail::ReadLittleEndian<u32>(Payload.subspan<DespawnParticipantOffset, sizeof(u32)>())},
		{
			Detail::ReadLittleEndian<u16>(Payload.subspan<DespawnObjectOffset, sizeof(u16)>()),
			Detail::ReadLittleEndian<u32>(Payload.subspan<DespawnGenerationOffset, sizeof(u32)>()),
			Detail::ReadLittleEndian<u32>(Payload.subspan<DespawnLifeOffset, sizeof(u32)>()),
		},
	};
	if (!HasAssignedDespawnFields(Message))
	{
		return std::unexpected(BoundMessageError::InvalidPayload);
	}
	return DecodedCatchUpDespawn{Message, Bytes};
}

std::expected<std::size_t, BoundMessageError> EncodeCatchUpDespawn(const CatchUpDespawn& Event, xr_span<u8> Output) noexcept
{
	if (!HasAssignedDespawnFields(Event))
	{
		return std::unexpected(BoundMessageError::InvalidPayload);
	}
	xr_array<u8, DespawnPayloadSize> Bytes;
	const auto Payload = xr_span(Bytes);
	Detail::WriteLittleEndian(Payload.subspan<DespawnBaselineOffset, sizeof(u32)>(), Event.BaselineId);
	Detail::WriteLittleEndian(Payload.subspan<DespawnSequenceOffset, sizeof(u32)>(), Event.StateEventSequence);
	Detail::WriteLittleEndian(Payload.subspan<DespawnEventIdOffset, sizeof(u64)>(), Event.EventId);
	Detail::WriteLittleEndian(Payload.subspan<DespawnTickOffset, sizeof(u32)>(), Event.ServerTick);
	Payload[DespawnKindOffset] = DespawnKind;
	Detail::WriteLittleEndian(Payload.subspan<DespawnParticipantOffset, sizeof(u32)>(), Event.Participant.Value);
	Detail::WriteLittleEndian(Payload.subspan<DespawnObjectOffset, sizeof(u16)>(), Event.Body.ObjectId);
	Detail::WriteLittleEndian(Payload.subspan<DespawnGenerationOffset, sizeof(u32)>(), Event.Body.Generation);
	Detail::WriteLittleEndian(Payload.subspan<DespawnLifeOffset, sizeof(u32)>(), Event.Body.LifeId);
	return Detail::EncodeBoundMessage(MessageType::CatchUpStateEvent, Event.Identity, Event.MessageSequence, Payload, Output);
}
}
