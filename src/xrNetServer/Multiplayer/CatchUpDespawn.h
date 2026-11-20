#pragma once

#include "BoundMessage.h"
#include "CatchUpEvent.h"
#include "CombatantIdentity.h"

namespace Multiplayer::Wire
{
struct CatchUpDespawn
{
	JoinIdentity Identity;
	u32 MessageSequence;
	u32 BaselineId;
	u32 StateEventSequence;
	u64 EventId;
	u32 ServerTick;
	ParticipantId Participant;
	EntityRef Body;

	bool operator==(const CatchUpDespawn&) const = default;
};

class DecodedCatchUpDespawn
{
public:
	[[nodiscard]] const CatchUpDespawn& GetMessage() const noexcept { return Message; }

	// Keep the checked datagram alive and unchanged until retention or application completes.
	[[nodiscard]] CatchUpEvent AsEvent() const noexcept
	{
		return {Message.Identity, Message.BaselineId, Message.StateEventSequence, Datagram};
	}

private:
	friend std::expected<DecodedCatchUpDespawn, BoundMessageError> DecodeCatchUpDespawn(xr_span<const u8> Bytes) noexcept;
	DecodedCatchUpDespawn(CatchUpDespawn Message, xr_span<const u8> Datagram) noexcept : Message(Message), Datagram(Datagram) {}

	CatchUpDespawn Message;
	xr_span<const u8> Datagram;
};

[[nodiscard]] std::expected<DecodedCatchUpDespawn, BoundMessageError> DecodeCatchUpDespawn(xr_span<const u8> Bytes) noexcept;
[[nodiscard]] std::expected<std::size_t, BoundMessageError> EncodeCatchUpDespawn(const CatchUpDespawn& Event, xr_span<u8> Output) noexcept;
}
