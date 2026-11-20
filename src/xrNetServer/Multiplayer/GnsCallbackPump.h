#pragma once

#include "GnsEndpoint.h"
#include "TransportInbox.h"

#include <expected>
#include <limits>
#include <optional>
#include <thread>

namespace Multiplayer
{
struct GnsCallbackApi
{
	void (*Run)(ISteamNetworkingSockets&) noexcept = [](ISteamNetworkingSockets& Sockets) noexcept { Sockets.RunCallbacks(); };
	bool (*Read)(ISteamNetworkingSockets&, HSteamNetConnection, SteamNetConnectionInfo_t&) noexcept =
		[](ISteamNetworkingSockets& Sockets, HSteamNetConnection Connection, SteamNetConnectionInfo_t& Info) noexcept
	{
		return Sockets.GetConnectionInfo(Connection, &Info);
	};
	bool (*TagAndAccept)(ISteamNetworkingSockets&, HSteamNetConnection, s64) noexcept =
		[](ISteamNetworkingSockets& Sockets, HSteamNetConnection Connection, s64 Generation) noexcept
	{
		return Sockets.SetConnectionUserData(Connection, Generation) && Sockets.AcceptConnection(Connection) == k_EResultOK;
	};
	bool (*SetGroup)(ISteamNetworkingSockets&, HSteamNetConnection, HSteamNetPollGroup) noexcept =
		[](ISteamNetworkingSockets& Sockets, HSteamNetConnection Connection, HSteamNetPollGroup Group) noexcept
	{
		return Sockets.SetConnectionPollGroup(Connection, Group);
	};
	void (*ClosePeer)(ISteamNetworkingSockets&, HSteamNetConnection) noexcept = [](ISteamNetworkingSockets& Sockets, HSteamNetConnection Connection) noexcept
	{
		Sockets.CloseConnection(Connection, 0, nullptr, false);
	};
};

enum class GnsCallbackPollError
{
	WrongThread,
	AlreadyPolling,
	InvalidRoute,
};

enum class GnsEndpointEventStatus
{
	Ok,
	InboxOverflow,
	ReceiveGroupFailed,
	GenerationExhausted,
};

struct GnsCallbackPollResult
{
	GnsEndpointEventStatus Server = GnsEndpointEventStatus::Ok;
	GnsEndpointEventStatus Client = GnsEndpointEventStatus::Ok;
};

// The process owner supplies live endpoint views and keeps their inboxes fixed while open.
// A failed endpoint result requires cleanup before polling or applying more events for that endpoint.
template <class Inbox>
class GnsCallbackPump
{
public:
	struct ServerRoute
	{
		GnsEndpointView Endpoint;
		Inbox& Events;
	};

	struct ClientRoute
	{
		GnsEndpointView Endpoint;
		Inbox& Events;
		s64 Generation;
	};

	struct ClientOptions
	{
		s64 Generation;
		xr_array<SteamNetworkingConfigValue_t, 2> Native;
	};

	explicit GnsCallbackPump(GnsCallbackApi Api = {}) noexcept : Api(Api) {}
	GnsCallbackPump(const GnsCallbackPump&) = delete;
	GnsCallbackPump& operator=(const GnsCallbackPump&) = delete;

	[[nodiscard]] static SteamNetworkingConfigValue_t CallbackOption() noexcept
	{
		SteamNetworkingConfigValue_t Option{};
		Option.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, reinterpret_cast<void*>(&Dispatch));
		return Option;
	}

	[[nodiscard]] std::optional<ClientOptions> PrepareClient() noexcept
	{
		if (const auto Generation = ReserveGeneration())
		{
			ClientOptions Options{*Generation, {CallbackOption(), {}}};
			Options.Native[1].SetInt64(k_ESteamNetworkingConfig_ConnectionUserData, *Generation);
			return Options;
		}
		return {};
	}

	[[nodiscard]] std::expected<GnsCallbackPollResult, GnsCallbackPollError> Poll(const ServerRoute* Server, const ClientRoute* Client) noexcept
	{
		if (std::this_thread::get_id() != Owner)
		{
			return std::unexpected(GnsCallbackPollError::WrongThread);
		}
		if (g_Context)
		{
			return std::unexpected(GnsCallbackPollError::AlreadyPolling);
		}
		if ((Server && !IsServerView(Server->Endpoint)) ||
			(Client && (!IsClientView(Client->Endpoint) || Client->Generation <= 0 || Client->Generation > LastGeneration)) ||
			(Server && Client && (Server->Endpoint.Sockets != Client->Endpoint.Sockets || &Server->Events == &Client->Events)))
		{
			return std::unexpected(GnsCallbackPollError::InvalidRoute);
		}

		const TransportPeer CurrentClient = Client ?
			TransportPeer{Client->Endpoint.Connection, static_cast<u64>(Client->Generation)} : TransportPeer{};
		if (TrackedClient != CurrentClient)
		{
			TrackedClient = CurrentClient;
			ClientState = OutboundState::Connecting;
		}
		if (!Server && !Client)
		{
			return GnsCallbackPollResult{};
		}

		PollContext Context{*this, Server, Client};
		g_Context = &Context;
		Api.Run(*(Server ? Server->Endpoint.Sockets : Client->Endpoint.Sockets));
		g_Context = nullptr;
		return Context.Result;
	}

private:
	enum class OutboundState
	{
		Connecting,
		Connected,
		Closed,
	};

	struct PollContext
	{
		GnsCallbackPump& Pump;
		const ServerRoute* Server;
		const ClientRoute* Client;
		GnsCallbackPollResult Result{};
	};

	[[nodiscard]] std::optional<s64> ReserveGeneration() noexcept
	{
		if (std::this_thread::get_id() != Owner || LastGeneration == std::numeric_limits<s64>::max())
		{
			return {};
		}
		return ++LastGeneration;
	}

	[[nodiscard]] static bool IsServerView(GnsEndpointView Endpoint) noexcept
	{
		return Endpoint.Sockets && Endpoint.Listener != k_HSteamListenSocket_Invalid &&
			Endpoint.PollGroup != k_HSteamNetPollGroup_Invalid && Endpoint.Connection == k_HSteamNetConnection_Invalid;
	}

	[[nodiscard]] static bool IsClientView(GnsEndpointView Endpoint) noexcept
	{
		return Endpoint.Sockets && Endpoint.Listener == k_HSteamListenSocket_Invalid &&
			Endpoint.PollGroup != k_HSteamNetPollGroup_Invalid && Endpoint.Connection != k_HSteamNetConnection_Invalid;
	}

	[[nodiscard]] static bool IsTerminal(ESteamNetworkingConnectionState State) noexcept
	{
		return State == k_ESteamNetworkingConnectionState_ClosedByPeer || State == k_ESteamNetworkingConnectionState_ProblemDetectedLocally;
	}

	static void Dispatch(SteamNetConnectionStatusChangedCallback_t* Event) noexcept
	{
		if (!g_Context || !Event || Event->m_hConn == k_HSteamNetConnection_Invalid)
		{
			return;
		}
		auto& Context = *g_Context;
		if (Event->m_info.m_hListenSocket != k_HSteamListenSocket_Invalid)
		{
			if (Context.Server && Context.Result.Server == GnsEndpointEventStatus::Ok &&
				Context.Server->Endpoint.Listener == Event->m_info.m_hListenSocket)
			{
				Context.Result.Server = Context.Pump.HandleInbound(*Context.Server, *Event);
			}
		}
		else if (Context.Client && Context.Result.Client == GnsEndpointEventStatus::Ok &&
			Context.Client->Endpoint.Connection == Event->m_hConn)
		{
			Context.Result.Client = Context.Pump.HandleOutbound(*Context.Client, *Event);
		}
	}

	[[nodiscard]] GnsEndpointEventStatus HandleInbound(const ServerRoute& Route, const SteamNetConnectionStatusChangedCallback_t& Event) noexcept
	{
		auto& Sockets = *Route.Endpoint.Sockets;
		SteamNetConnectionInfo_t Current{};
		if (!Api.Read(Sockets, Event.m_hConn, Current) || Current.m_hListenSocket != Route.Endpoint.Listener)
		{
			return GnsEndpointEventStatus::Ok;
		}

		const auto& Snapshot = Event.m_info;
		if (IsTerminal(Snapshot.m_eState) && IsTerminal(Current.m_eState) && Snapshot.m_nUserData == Current.m_nUserData)
		{
			const bool Posted = Current.m_nUserData <= 0 || Route.Events.PushDisconnected(
				{Event.m_hConn, static_cast<u64>(Current.m_nUserData)}, static_cast<u32>(Snapshot.m_eEndReason)).has_value();
			Api.ClosePeer(Sockets, Event.m_hConn);
			return Posted ? GnsEndpointEventStatus::Ok : GnsEndpointEventStatus::InboxOverflow;
		}
		if (Snapshot.m_eState != k_ESteamNetworkingConnectionState_Connecting || Snapshot.m_nUserData > 0 ||
			Current.m_eState != k_ESteamNetworkingConnectionState_Connecting || Current.m_nUserData > 0)
		{
			return GnsEndpointEventStatus::Ok;
		}

		const auto Generation = ReserveGeneration();
		if (!Generation || !Api.TagAndAccept(Sockets, Event.m_hConn, *Generation))
		{
			Api.ClosePeer(Sockets, Event.m_hConn);
			return Generation ? GnsEndpointEventStatus::Ok : GnsEndpointEventStatus::GenerationExhausted;
		}
		const auto Status = PublishConnected(Route.Endpoint, Route.Events, {Event.m_hConn, static_cast<u64>(*Generation)});
		if (Status != GnsEndpointEventStatus::Ok)
		{
			Api.ClosePeer(Sockets, Event.m_hConn);
		}
		return Status;
	}

	[[nodiscard]] GnsEndpointEventStatus HandleOutbound(const ClientRoute& Route, const SteamNetConnectionStatusChangedCallback_t& Event) noexcept
	{
		const auto& Snapshot = Event.m_info;
		if (ClientState == OutboundState::Closed || Snapshot.m_nUserData != Route.Generation)
		{
			return GnsEndpointEventStatus::Ok;
		}
		SteamNetConnectionInfo_t Current{};
		if (!Api.Read(*Route.Endpoint.Sockets, Event.m_hConn, Current) || Current.m_hListenSocket != k_HSteamListenSocket_Invalid ||
			Current.m_nUserData != Route.Generation)
		{
			return GnsEndpointEventStatus::Ok;
		}

		if (IsTerminal(Snapshot.m_eState) && IsTerminal(Current.m_eState))
		{
			ClientState = OutboundState::Closed;
			return Route.Events.PushDisconnected(TrackedClient, static_cast<u32>(Snapshot.m_eEndReason)) ?
				GnsEndpointEventStatus::Ok : GnsEndpointEventStatus::InboxOverflow;
		}
		if (ClientState == OutboundState::Connecting && Snapshot.m_eState == k_ESteamNetworkingConnectionState_Connected &&
			Current.m_eState == k_ESteamNetworkingConnectionState_Connected)
		{
			ClientState = OutboundState::Connected;
			return PublishConnected(Route.Endpoint, Route.Events, TrackedClient);
		}
		return GnsEndpointEventStatus::Ok;
	}

	[[nodiscard]] GnsEndpointEventStatus PublishConnected(GnsEndpointView Endpoint, Inbox& Events, TransportPeer Peer) noexcept
	{
		if (!Events.PushConnected(Peer))
		{
			return GnsEndpointEventStatus::InboxOverflow;
		}
		return Api.SetGroup(*Endpoint.Sockets, Peer.Handle, Endpoint.PollGroup) ?
			GnsEndpointEventStatus::Ok : GnsEndpointEventStatus::ReceiveGroupFailed;
	}

	inline static thread_local PollContext* g_Context = nullptr;
	const std::thread::id Owner = std::this_thread::get_id();
	const GnsCallbackApi Api;
	s64 LastGeneration = 0;
	TransportPeer TrackedClient;
	OutboundState ClientState = OutboundState::Connecting;
};
}
