#pragma once

#include "ReliableSendQueue.h"

#include <steam/steamnetworkingsockets.h>

namespace Multiplayer
{
// Adapts a checked ReliableSendQueue frame to the single ordered reliable GNS stream.
[[nodiscard]] inline ReliableSendAttempt SubmitGnsReliable(ISteamNetworkingSockets& Sockets, HSteamNetConnection Connection,
	xr_span<const u8> Bytes) noexcept
{
	const auto Result = Sockets.SendMessageToConnection(Connection, Bytes.data(), static_cast<u32>(Bytes.size()),
		k_nSteamNetworkingSend_ReliableNoNagle, nullptr);
	switch (Result)
	{
		case k_EResultOK:
			return ReliableSendAttempt::Accepted;
		case k_EResultLimitExceeded:
			return ReliableSendAttempt::Backpressured;
		default:
			return ReliableSendAttempt::Failed;
	}
}
}
