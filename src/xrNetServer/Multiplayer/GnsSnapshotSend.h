#pragma once

#include "SnapshotSendQueue.h"
#include "GnsOutgoingSend.h"

namespace Multiplayer
{
[[nodiscard]] inline SnapshotSendAttempt SubmitGnsSnapshot(ISteamNetworkingSockets& Sockets, HSteamNetConnection Connection,
	xr_span<const u8> Bytes) noexcept
{
	switch (SubmitGnsOutgoing(Sockets, Connection, OutgoingTraffic::Snapshot, Bytes))
	{
		case GnsSendAttempt::Accepted:
			return SnapshotSendAttempt::Accepted;
		case GnsSendAttempt::Deferred:
			return SnapshotSendAttempt::Dropped;
		case GnsSendAttempt::Failed:
			return SnapshotSendAttempt::Failed;
	}
	return SnapshotSendAttempt::Failed;
}
}
