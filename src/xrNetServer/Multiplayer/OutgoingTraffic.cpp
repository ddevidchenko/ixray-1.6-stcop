#include "OutgoingTraffic.h"

namespace Multiplayer
{
std::optional<OutgoingTraffic> GetOutgoingTraffic(Wire::MessageType Type) noexcept
{
	switch (Type)
	{
		case Wire::MessageType::ClientHello:
		case Wire::MessageType::ServerWelcome:
		case Wire::MessageType::AdmissionReject:
		case Wire::MessageType::ClientMapLoaded:
		case Wire::MessageType::ReadyAcknowledgement:
		case Wire::MessageType::BaselineBegin:
		case Wire::MessageType::BaselineEnd:
		case Wire::MessageType::ServerJoinBarrier:
		case Wire::MessageType::CatchUpStateEvent:
			return OutgoingTraffic::Control;
		case Wire::MessageType::BaselineChunk:
		case Wire::MessageType::CombatantBarrierChunk:
			return OutgoingTraffic::Bulk;
		case Wire::MessageType::SnapshotBatch:
			return OutgoingTraffic::Snapshot;
	}
	return std::nullopt;
}

SendAllowance CheckSendAllowance(OutgoingTraffic Traffic, std::size_t FrameBytes, NativeSendBacklog Backlog) noexcept
{
	if (!FrameBytes || FrameBytes > Wire::MaxMessageSize)
	{
		return SendAllowance::Invalid;
	}

	// GNS counts reliable stream headers in its byte counters. Allow for the flags byte
	// and variable-width message-number gap and length on every reliable submission.
	constexpr u64 ReliableFramingAllowance = 16;
	const auto Outstanding = u64{Backlog.PendingReliable} + Backlog.PendingUnreliable + Backlog.UnacknowledgedReliable;
	const auto ChargedBytes = FrameBytes + (IsReliableTraffic(Traffic) ? ReliableFramingAllowance : 0);
	u64 Ceiling;
	switch (Traffic)
	{
		case OutgoingTraffic::Control:
			Ceiling = 8 * Wire::MaxMessageSize;
			break;
		case OutgoingTraffic::Command:
		case OutgoingTraffic::Snapshot:
			Ceiling = 4 * Wire::MaxMessageSize;
			break;
		case OutgoingTraffic::Bulk:
			Ceiling = Wire::MaxMessageSize + ReliableFramingAllowance;
			break;
		case OutgoingTraffic::Cosmetic:
			return Outstanding == 0 ? SendAllowance::Allowed : SendAllowance::Deferred;
		default:
			return SendAllowance::Invalid;
	}
	return Outstanding + ChargedBytes <= Ceiling ? SendAllowance::Allowed : SendAllowance::Deferred;
}
}
