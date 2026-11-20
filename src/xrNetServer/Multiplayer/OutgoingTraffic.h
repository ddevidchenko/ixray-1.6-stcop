#pragma once

#include "WireEnvelope.h"

#include <optional>

namespace Multiplayer
{
enum class OutgoingTraffic
{
	Control,
	Bulk,
	Command,
	Snapshot,
	Cosmetic,
};

struct NativeSendBacklog
{
	u32 PendingReliable;
	u32 PendingUnreliable;
	u32 UnacknowledgedReliable;

	bool operator==(const NativeSendBacklog&) const = default;
};

enum class SendAllowance
{
	Allowed,
	Deferred,
	Invalid,
};

[[nodiscard]] std::optional<OutgoingTraffic> GetOutgoingTraffic(Wire::MessageType Type) noexcept;

[[nodiscard]] constexpr bool IsReliableTraffic(OutgoingTraffic Traffic) noexcept
{
	return Traffic == OutgoingTraffic::Control || Traffic == OutgoingTraffic::Bulk;
}

[[nodiscard]] SendAllowance CheckSendAllowance(OutgoingTraffic Traffic, std::size_t FrameBytes, NativeSendBacklog Backlog) noexcept;
}
