#pragma once

#include "ReliableSendQueue.h"
#include "GnsOutgoingSend.h"

namespace Multiplayer
{
// Adapts a checked ReliableSendQueue frame to the single ordered reliable GNS stream.
[[nodiscard]] inline ReliableSendAttempt SubmitGnsReliable(ISteamNetworkingSockets& Sockets, HSteamNetConnection Connection,
	xr_span<const u8> Bytes, OutgoingTraffic Traffic = OutgoingTraffic::Control) noexcept
{
	if (!IsReliableTraffic(Traffic))
	{
		return ReliableSendAttempt::Failed;
	}
	switch (SubmitGnsOutgoing(Sockets, Connection, Traffic, Bytes))
	{
		case GnsSendAttempt::Accepted:
			return ReliableSendAttempt::Accepted;
		case GnsSendAttempt::Deferred:
			return ReliableSendAttempt::Backpressured;
		case GnsSendAttempt::Failed:
			return ReliableSendAttempt::Failed;
	}
	return ReliableSendAttempt::Failed;
}

// The queue or transfer owner calls this for a complete version-one reliable frame.
[[nodiscard]] inline ReliableSendAttempt SubmitGnsReliableFrame(ISteamNetworkingSockets& Sockets, HSteamNetConnection Connection,
	xr_span<const u8> Bytes) noexcept
{
	const auto Frame = Wire::DecodeEnvelope(Bytes);
	if (!Frame)
	{
		return ReliableSendAttempt::Failed;
	}
	const auto Traffic = GetOutgoingTraffic(Frame->Header.Type);
	return Traffic ? SubmitGnsReliable(Sockets, Connection, Bytes, *Traffic) : ReliableSendAttempt::Failed;
}
}
