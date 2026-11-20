#pragma once

#include "GnsEndpoint.h"
#include "TransportInbox.h"

#include <expected>
#include <memory>
#include <variant>

namespace Multiplayer
{
enum class GnsReceiveError
{
	InactiveEndpoint,
	ReceiveFailed,
	UntaggedPacket,
};

using GnsIngressError = std::variant<GnsReceiveError, TransportInboxError>;

// User data is the positive generation assigned locally when this transport connection is admitted to receipt.
template <class Lock, class Guard>
[[nodiscard]] std::expected<bool, GnsIngressError> ReceiveGnsPacket(GnsEndpointView View, TransportInbox<Lock, Guard>& Inbox) noexcept
{
	if (!View.Sockets || (View.PollGroup == k_HSteamNetPollGroup_Invalid && View.Connection == k_HSteamNetConnection_Invalid))
	{
		return std::unexpected(GnsIngressError{GnsReceiveError::InactiveEndpoint});
	}

	SteamNetworkingMessage_t* Raw = nullptr;
	const int Count = View.PollGroup != k_HSteamNetPollGroup_Invalid ?
		View.Sockets->ReceiveMessagesOnPollGroup(View.PollGroup, &Raw, 1) :
		View.Sockets->ReceiveMessagesOnConnection(View.Connection, &Raw, 1);
	const auto Release = [](SteamNetworkingMessage_t* Packet) noexcept { Packet->Release(); };
	const std::unique_ptr<SteamNetworkingMessage_t, decltype(Release)> Packet(Raw, Release);
	if (Count == 0)
	{
		return false;
	}
	if (Count != 1 || !Packet || Packet->m_cbSize < 0 || (!Packet->m_pData && Packet->m_cbSize != 0))
	{
		return std::unexpected(GnsIngressError{GnsReceiveError::ReceiveFailed});
	}
	if (Packet->m_nConnUserData <= 0)
	{
		return std::unexpected(GnsIngressError{GnsReceiveError::UntaggedPacket});
	}

	const TransportPeer Peer{Packet->m_conn, static_cast<u64>(Packet->m_nConnUserData)};
	const auto Bytes = xr_span(static_cast<const u8*>(Packet->m_pData), static_cast<std::size_t>(Packet->m_cbSize));
	const auto Posted = Inbox.PushPacket(Peer, Bytes);
	if (!Posted)
	{
		return std::unexpected(GnsIngressError{Posted.error()});
	}
	return true;
}
}
