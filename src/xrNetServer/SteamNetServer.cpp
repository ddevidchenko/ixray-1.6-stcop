#include "stdafx.h"
#include "SteamNetServer.h"
#include "Multiplayer/GnsCallbackDispatch.h"
#include "Multiplayer/LegacyAdmission.h"
#include <steam/isteamnetworkingutils.h>

// -----------------------------------------------------------------------------

SteamNetServer::SteamNetServer(CTimer* timer, bool dedicated)
	: BaseServer(timer, dedicated)
{
	m_players.reserve((dedicated) ? GetMaxPlayers() + 1 : GetMaxPlayers()); // 
	m_server_password.reserve(64);
	m_pending_clients.clear();
}

// -----------------------------------------------------------------------------

SteamNetServer::~SteamNetServer() = default;
// -----------------------------------------------------------------------------

void SteamNetServer::_SendTo_LL(ClientID ID, void * data, u32 size, u32 dwFlags, u32 dwTimeout)
{
	auto* Sockets = Endpoint.Get().Sockets;
	if (!Sockets)
	{
		return;
	}
	EResult result = Sockets->SendMessageToConnection(ID.value(), data, size, convert_flags_for_steam(dwFlags), nullptr);
	if (result != k_EResultOK)
	{
		Msg("! [SteamNetServer] ERROR: Failed to send net-packet, reason: %d", result);
	}
}

// -----------------------------------------------------------------------------

#pragma region create / destroy listener

bool SteamNetServer::CreateConnection(GameDescriptionData & game_descr, ServerConnectionOptions & connectOpt)
{
	SteamNetworkingErrMsg Error{};
	auto Lease = Multiplayer::GetGnsProcessRuntime().AcquireServer(Error);
	if (!Lease.Get())
	{
		Msg("! [SteamNetServer] Could not acquire the GNS runtime. %s", Error);
		return false;
	}

	SteamNetworkingIPAddr bindServerAddress{};
	bindServerAddress.m_port = static_cast<u16>(connectOpt.dwServerPort);

	SteamNetworkingUtils()->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendBufferSize, 1024 * 1024);
	const auto Callback = Multiplayer::GnsCallbackDispatch::CallbackOption();
	const auto Opened = Endpoint.OpenServer(std::move(Lease), bindServerAddress, xr_span(&Callback, 1));
	if (!Opened)
	{
		Msg("! [SteamNetServer] Failed to open listener on port %u (%d)", static_cast<unsigned>(bindServerAddress.m_port),
			static_cast<int>(Opened.error()));
		return false;
	}

	// set m_game_description
	CopyMemory(&m_game_description, &game_descr, sizeof(game_descr));

	m_server_password = connectOpt.server_pass;
	m_max_players = (m_bDedicated) ? (connectOpt.dwMaxPlayers + 1) : (connectOpt.dwMaxPlayers);
	psNET_Port = connectOpt.dwServerPort;

	Msg("- [SteamNetServer] created on port %d", bindServerAddress.m_port);

	return true;
}

void SteamNetServer::DestroyConnection()
{
	m_server_password.clear();
	m_bServerClientConnected = false;

	if (Endpoint.Get().Sockets)
	{
		DisconnectAll();
		Endpoint.Close();
	}
}

#pragma endregion

// -----------------------------------------------------------------------------

void SteamNetServer::PollIncomingMessages(u32 Budget)
{
	const auto View = Endpoint.Get();
	if (!View.Sockets)
	{
		return;
	}

	for (u32 Index = 0; Index < Budget; ++Index)
	{
		ISteamNetworkingMessage *pIncomingMsg = nullptr;
		int numMsgs = View.Sockets->ReceiveMessagesOnPollGroup(View.PollGroup, &pIncomingMsg, 1);
		if (numMsgs <= 0)
		{
			break;
		}
		const xr_scope_exit Release([pIncomingMsg] { pIncomingMsg->Release(); });

		void*	m_data = pIncomingMsg->m_pData;
		u32	m_size = pIncomingMsg->m_cbSize;
		HSteamNetConnection m_sender = pIncomingMsg->m_conn;

		MSYS_PING*	m_ping = (MSYS_PING*)m_data;

		if (m_size == sizeof(MSYS_PING) && m_ping->sign1 == 0x12071980 && m_ping->sign2 == 0x26111975)
		{
			// ping - save server time and reply
			m_ping->dwTime_Server = TimerAsync(device_timer);
			ClientID ID; ID.set(m_sender);
			BaseServer::SendTo_Buf(ID, m_data, m_size, net_flags(false, false, true, true));
		}
		else if (m_size == sizeof(MSYS_CLIENT_DATA) && m_ping->sign1 == 0x02281488 && m_ping->sign2 == 0x01488228)
		{ // client data message
			OnClientDataReceived(pIncomingMsg->m_conn, pIncomingMsg->m_identityPeer, (MSYS_CLIENT_DATA*)m_data);
		}
		else
		{
			MultipacketReciever::RecievePacket(m_data, m_size, m_sender);
		}
	}
}

void SteamNetServer::ProcessConnection(HSteamNetConnection Connection)
{
	const auto View = Endpoint.Get();
	using Multiplayer::LegacyAdmissionDecision;

	SteamNetConnectionInfo_t Info{};
	if (!View.Sockets || !View.Sockets->GetConnectionInfo(Connection, &Info) || Info.View.Listeneret != View.Listener)
	{
		return;
	}
	if (Info.m_eState != k_ESteamNetworkingConnectionState_Connecting)
	{
		if (Multiplayer::IsGnsPeerClosed(Info.m_eState))
		{
			View.Sockets->CloseConnection(Connection, EUnknownReason, nullptr, false);
		}
		return;
	}

	ip_address Address;
	GetIpAddress(Info, Address);
	const bool LocalHostCandidate = !SV_Client && Info.m_identityRemote.IsLocalHost();
	const char* Password = Info.m_identityRemote.GetGenericString();
	const Multiplayer::LegacyAdmissionChecks Checks{
		m_players.size(),
		m_max_players,
		std::find(m_players.begin(), m_players.end(), Connection) != m_players.end(),
		GetBannedClient(Address) != nullptr,
		LocalHostCandidate,
		LocalHostCandidate || m_ip_filter.is_ip_present(Address.m_data.data),
		Password ? std::optional<xr_string_view>(Password) : std::nullopt,
		m_server_password.c_str(),
	};

	enmDisconnectReason Rejection = EUnknownReason;
	const char* Detail = nullptr;
	switch (Multiplayer::CheckLegacyAdmission(Checks))
	{
	case LegacyAdmissionDecision::Duplicate:
		return;
	case LegacyAdmissionDecision::SessionFull:
		Rejection = ESessionFull;
		Detail = "Session full";
		break;
	case LegacyAdmissionDecision::Banned:
		Rejection = EPlayerBanned;
		Detail = "Player is banned";
		break;
	case LegacyAdmissionDecision::AddressDenied:
		Detail = "IP address is not present";
		break;
	case LegacyAdmissionDecision::PasswordRejected:
		Rejection = EInvalidPassword;
		Detail = "Incorrect server password";
		break;
	case LegacyAdmissionDecision::Accept:
		break;
	}
	if (Detail)
	{
		Msg("[SteamNetServer] Close connection. %s", Detail);
		View.Sockets->CloseConnection(Connection, Rejection, nullptr, false);
		return;
	}

	if (View.Sockets->AcceptConnection(Connection) != k_EResultOK ||
		!View.Sockets->SetConnectionPollGroup(Connection, View.PollGroup))
	{
		View.Sockets->CloseConnection(Connection, EUnknownReason, nullptr, false);
		return;
	}
	m_players.push_back(Connection);
}

void SteamNetServer::RejectPendingConnection(HSteamNetConnection Connection)
{
	const auto View = Endpoint.Get();
	SteamNetConnectionInfo_t Info{};
	if (View.Sockets && View.Sockets->GetConnectionInfo(Connection, &Info) && Info.View.Listeneret == View.Listener &&
		(Info.m_eState == k_ESteamNetworkingConnectionState_Connecting || Multiplayer::IsGnsPeerClosed(Info.m_eState)))
	{
		View.Sockets->CloseConnection(Connection, ESessionFull, nullptr, false);
	}
}

void SteamNetServer::RetireClosedConnections()
{
	auto* Sockets = Endpoint.Get().Sockets;
	if (!Sockets)
	{
		return;
	}
	xr_vector<HSteamNetConnection> Closed;
	for (const auto Connection : m_players)
	{
		SteamNetConnectionInfo_t Info{};
		if (!Sockets->GetConnectionInfo(Connection, &Info) || Multiplayer::IsGnsPeerClosed(Info.m_eState))
		{
			Closed.push_back(Connection);
		}
	}
	for (const auto Connection : Closed)
	{
		CloseConnection(Connection, EUnknownReason);
		DestroyCleint(Connection);
	}
}

void SteamNetServer::AddPendingClient(SClientConnectData & cl_data)
{
	auto pending_client_it = std::find_if(m_pending_clients.cbegin(), m_pending_clients.cend(),
		[&](const auto &data) {
			return data.clientID == cl_data.clientID;
		}
	);
	if (pending_client_it == m_pending_clients.cend())
	{
		m_pending_clients.push_back(cl_data);
	}
}

void SteamNetServer::HandlePendingClients()
{
	if (m_pending_clients.empty())
		return;

	for (auto &cl_data : m_pending_clients)
		FinishConnection(cl_data);

	m_pending_clients.clear();
}

void SteamNetServer::OnClientDataReceived(HSteamNetConnection connection, SteamNetworkingIdentity &identity, MSYS_CLIENT_DATA* data)
{
	SClientConnectData cl_data;
	cl_data.clientID.set(connection); // set clientId
	cl_data.process_id = data->process_id;

	if (identity.IsLocalHost() && data->process_id == Platform::GetCurrentProcessId())
	{ // if server client
		xr_strcpy(cl_data.name, data->name);
		xr_strcpy(cl_data.pass, data->pass);

		FinishConnection(cl_data);
		m_bServerClientConnected = true;
		Msg("- [SteamNetServer] server client connected");

		HandlePendingClients(); // Finish connection for all pending clients
	}
	else
	{
		xr_strcpy(cl_data.name, data->name);
		xr_strcpy(cl_data.pass, data->pass);

		if (m_bServerClientConnected)
			FinishConnection(cl_data);
		else
			AddPendingClient(cl_data);
	}
}

void SteamNetServer::FinishConnection(SClientConnectData &cl_data)
{
	// register client
	new_client(&cl_data);

	// send game description to client
	MSYS_GAME_DESCRIPTION sys_gd;
	sys_gd.sign1 = 0x02281488;
	sys_gd.sign2 = 0x01488228;
	CopyMemory(&sys_gd.data, &m_game_description, sizeof(m_game_description));

	_SendTo_LL(cl_data.clientID, &sys_gd, sizeof(sys_gd), net_flags(true, true, false, false));
}


#pragma region client disconnect

void SteamNetServer::DisconnectAll()
{
	xr_vector<HSteamNetConnection> Connections;
	Connections.swap(m_players);
	m_pending_clients.clear();
	for (const auto Connection : Connections)
	{
		if (auto* Sockets = Endpoint.Get().Sockets)
		{
			Sockets->CloseConnection(Connection, EServerShutdown, nullptr, false);
		}
		DestroyCleint(Connection);
	}
}
 
bool SteamNetServer::DisconnectClient(IClient * C, LPCSTR Reason)
{
	if (!C) return false;

	CloseConnection(C->ID.value(), EDetailedReason, Reason);
	DestroyCleint(C->ID.value());
	return true;
}

void SteamNetServer::CloseConnection(HSteamNetConnection connection, enmDisconnectReason nReason, LPCSTR sReason)
{
	auto* Sockets = Endpoint.Get().Sockets;
	if (std::erase(m_players, connection) != 0 && Sockets)
	{
		Sockets->CloseConnection(connection, nReason, sReason, false);
	}
	std::erase_if(m_pending_clients, [connection](const SClientConnectData& Client)
	{
		return Client.clientID.value() == connection;
	});
}

void SteamNetServer::DestroyCleint(ClientID clientId)
{
	IClient* tmp_client = net_players.GetFoundClient(
		ClientIdSearchPredicate(clientId)
	);

	if (tmp_client)
	{
		tmp_client->flags.bConnected = false;
		tmp_client->flags.bReconnect = false;
		OnCL_Disconnected(tmp_client);
		// real destroy
		client_Destroy(tmp_client);
	}
}

#pragma endregion

// -----------------------------------------------------------------------------

#pragma region ip address

void SteamNetServer::GetIpAddress(SteamNetConnectionInfo_t & info, ip_address & out)
{
	string64 ip_str;
	info.m_addrRemote.ToString(ip_str, sizeof(string64), false);
	out.set(ip_str);
}

bool SteamNetServer::GetClientAddress(ClientID ID, ip_address& address, DWORD* pPort)
{
	auto* Sockets = Endpoint.Get().Sockets;
	SteamNetConnectionInfo_t info;
	if (!Sockets || !Sockets->GetConnectionInfo(ID.value(), &info))
	{
		return false;
	}

	if (pPort != NULL)
	{
		*pPort = info.m_addrRemote.m_port;
	}

	GetIpAddress(info, address);

	return true;
}

#pragma endregion

// -----------------------------------------------------------------------------

bool SteamNetServer::GetClientPendingMessagesCount(ClientID ID, DWORD & dwPending)
{
	auto* Sockets = Endpoint.Get().Sockets;
	SteamNetConnectionRealTimeStatus_t status;
	if (Sockets && Sockets->GetConnectionRealTimeStatus(ID.value(), &status, 0, nullptr))
	{
		dwPending = status.m_cbPendingReliable + status.m_cbPendingUnreliable;
		return true;
	}

	return false;
}

// -----------------------------------------------------------------------------

void SteamNetServer::UpdateClientStatistic(IClient* C)
{
	auto* Sockets = Endpoint.Get().Sockets;
	SteamNetConnectionRealTimeStatus_t status;
	if (!Sockets || !Sockets->GetConnectionRealTimeStatus(C->ID.value(), &status, 0, nullptr))
	{
		return;
	}
	C->stats.Update(status);
}
