#pragma once

#include "SnapshotSendQueue.h"

#include <steam/steamnetworkingsockets.h>

namespace Multiplayer
{
[[nodiscard]] inline SnapshotSendAttempt SubmitGnsSnapshot(ISteamNetworkingSockets& Sockets, HSteamNetConnection Connection,
	xr_span<const u8> Bytes) noexcept
{
	const auto Result = Sockets.SendMessageToConnection(Connection, Bytes.data(), static_cast<u32>(Bytes.size()),
		k_nSteamNetworkingSend_UnreliableNoDelay, nullptr);
	switch (Result)
	{
		case k_EResultOK:
			return SnapshotSendAttempt::Accepted;
		case k_EResultIgnored:
		case k_EResultLimitExceeded:
			return SnapshotSendAttempt::Dropped;
		default:
			return SnapshotSendAttempt::Failed;
	}
}
}
