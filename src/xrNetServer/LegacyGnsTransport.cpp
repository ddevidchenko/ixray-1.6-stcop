#include "stdafx.h"
#include "LegacyGnsTransport.h"
#include "SteamNetClient.h"
#include "SteamNetServer.h"
#include "Multiplayer/GnsCallbackDispatch.h"
#include "Multiplayer/LegacyAdmission.h"

void LegacyGnsTransport::Poll(SteamNetClient& Client, SteamNetServer* Server)
{
	using Multiplayer::GnsCallbackDispatch;
	using Multiplayer::IsGnsPeerClosed;

	const auto ClientView = Client.Endpoint.Get();
	const auto ServerView = Server ? Server->Endpoint.Get() : Multiplayer::GnsEndpointView{};
	auto* Sockets = ClientView.Sockets ? ClientView.Sockets : ServerView.Sockets;
	if (!Sockets || GnsCallbackDispatch::IsPolling())
	{
		return;
	}

	constexpr u32 PacketBudget = 64;
	if (ServerView.Sockets)
	{
		Server->PollIncomingMessages(PacketBudget);
	}
	if (ClientView.Sockets)
	{
		Client.PollIncomingMessages(PacketBudget);
	}

	struct CallbackBatch
	{
		SteamNetServer* Server;
		HSteamListenSocket Listener;
		HSteamNetConnection Client;
		s64 ClientGeneration;
		Multiplayer::LegacyAdmissionBatch Arrivals;
		std::optional<SteamNetConnectionStatusChangedCallback_t> Connected{};
		std::optional<SteamNetConnectionStatusChangedCallback_t> Closed{};
	};
	xr_array<u32, 64> Requests;
	CallbackBatch Batch{ServerView.Sockets ? Server : nullptr, ServerView.Listener, ClientView.Connection, Client.ConnectionGeneration,
		Multiplayer::LegacyAdmissionBatch(Requests)};
	GnsCallbackDispatch::Poll(*Sockets, &Batch, [](void* Target, const SteamNetConnectionStatusChangedCallback_t& Event) noexcept
	{
		auto& Batch = *static_cast<CallbackBatch*>(Target);
		const auto& Info = Event.m_info;
		if (Info.m_hListenSocket != k_HSteamListenSocket_Invalid)
		{
			if (Batch.Server && Info.m_hListenSocket == Batch.Listener &&
				Info.m_eState == k_ESteamNetworkingConnectionState_Connecting && !Batch.Arrivals.TryRecord(Event.m_hConn))
			{
				Batch.Server->RejectPendingConnection(Event.m_hConn);
			}
			return;
		}
		if (Event.m_hConn != Batch.Client || Info.m_nUserData != Batch.ClientGeneration)
		{
			return;
		}
		if (Info.m_eState == k_ESteamNetworkingConnectionState_Connected)
		{
			Batch.Connected = Event;
		}
		else if (IsGnsPeerClosed(Info.m_eState))
		{
			Batch.Closed = Event;
		}
	});

	if (Server && Server->Endpoint.Get().Sockets)
	{
		Server->RetireClosedConnections();
		for (const auto Handle : Batch.Arrivals.GetRequests())
		{
			Server->ProcessConnection(Handle);
		}
	}

	const auto CurrentClient = Client.Endpoint.Get();
	if (CurrentClient.Sockets && Client.ConnectionGeneration == Batch.ClientGeneration && !Client.net_isDisconnected() &&
		(Batch.Closed || Batch.Connected))
	{
		const auto& Event = Batch.Closed ? *Batch.Closed : *Batch.Connected;
		SteamNetConnectionInfo_t Current{};
		if (Event.m_hConn == CurrentClient.Connection && CurrentClient.Sockets->GetConnectionInfo(CurrentClient.Connection, &Current) &&
			Current.m_hListenSocket == k_HSteamListenSocket_Invalid && Current.m_nUserData == Batch.ClientGeneration &&
			Current.m_eState == Event.m_info.m_eState)
		{
			Client.ApplyConnectionStatus(Event);
		}
	}
}
