#pragma once

#include "OutgoingTraffic.h"
#include "../../xrCore/_stl_extensions_nonalloc.h"

#include <steam/steamnetworkingsockets.h>

namespace Multiplayer
{
enum class GnsSendAttempt
{
	Accepted,
	Deferred,
	Failed,
};

// The connection owner serializes submissions. The native worker may drain or reschedule bytes
// between the status sample and the send. Rescheduled reliable bytes remain in the budget sum.
[[nodiscard]] inline GnsSendAttempt SubmitGnsOutgoing(ISteamNetworkingSockets& Sockets, HSteamNetConnection Connection,
	OutgoingTraffic Traffic, xr_span<const u8> Bytes) noexcept
{
	SteamNetConnectionRealTimeStatus_t Status{};
	if (Sockets.GetConnectionRealTimeStatus(Connection, &Status, 0, nullptr) != k_EResultOK ||
		Status.m_cbPendingReliable < 0 || Status.m_cbPendingUnreliable < 0 || Status.m_cbSentUnackedReliable < 0)
	{
		return GnsSendAttempt::Failed;
	}
	const NativeSendBacklog Backlog{
		static_cast<u32>(Status.m_cbPendingReliable),
		static_cast<u32>(Status.m_cbPendingUnreliable),
		static_cast<u32>(Status.m_cbSentUnackedReliable),
	};
	const auto Allowance = CheckSendAllowance(Traffic, Bytes.size(), Backlog);
	if (Allowance == SendAllowance::Invalid)
	{
		return GnsSendAttempt::Failed;
	}
	if (Status.m_eState == k_ESteamNetworkingConnectionState_Connecting ||
		Status.m_eState == k_ESteamNetworkingConnectionState_FindingRoute)
	{
		return GnsSendAttempt::Deferred;
	}
	if (Status.m_eState != k_ESteamNetworkingConnectionState_Connected)
	{
		return GnsSendAttempt::Failed;
	}
	if (Allowance == SendAllowance::Deferred)
	{
		return GnsSendAttempt::Deferred;
	}

	const auto Flags = IsReliableTraffic(Traffic) ? k_nSteamNetworkingSend_ReliableNoNagle : k_nSteamNetworkingSend_UnreliableNoDelay;
	const auto Result = Sockets.SendMessageToConnection(Connection, Bytes.data(), static_cast<u32>(Bytes.size()), Flags, nullptr);
	switch (Result)
	{
		case k_EResultOK:
			return GnsSendAttempt::Accepted;
		case k_EResultIgnored:
		case k_EResultLimitExceeded:
			return GnsSendAttempt::Deferred;
		default:
			return GnsSendAttempt::Failed;
	}
}
}
