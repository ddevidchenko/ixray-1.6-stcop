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

SteamNetServer::~SteamNetServer()
{

}
// -----------------------------------------------------------------------------

void SteamNetServer::_SendTo_LL(ClientID ID, void * data, u32 size, u32 dwFlags, u32 dwTimeout)
{
	EResult result = m_pInterface->SendMessageToConnection(ID.value(), data, size, convert_flags_for_steam(dwFlags), nullptr);
	if (result != k_EResultOK)
	{
		Msg("! [SteamNetClient] ERROR: Failed to send net-packet, reason: %d", result);
	}
}

// -----------------------------------------------------------------------------

#pragma region create / destroy listener

bool SteamNetServer::CreateConnection(GameDescriptionData & game_descr, ServerConnectionOptions & connectOpt)
{
	SteamDatagramErrMsg errMsg;
	SteamNetworkingIdentity identity;
	identity.Clear();
	identity.SetLocalHost();

	if (!GameNetworkingSockets_Init(&identity, errMsg))
	{
		Msg("! [SteamNetServer] GameNetworkingSockets_Init failed.  %s", errMsg);
		return false;
	}

	m_pInterface = SteamNetworkingSockets();

	if (m_pInterface == NULL)
	{
		Msg("! [SteamNetServer] Server interface is NULL");
		GameNetworkingSockets_Kill();
		return false;
	}

	// SETUP SERVER PORT
	SteamNetworkingIPAddr bindServerAddress;
	bindServerAddress.Clear();
	bindServerAddress.m_port = (uint16)connectOpt.dwServerPort;

	// SET UPPER LIMIT OF BUFFERED PENDING BYTES TO BE SENT
	SteamNetworkingUtils()->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendBufferSize, 1024 * 1024);	

	// SETUP CALLBACK
	const auto opt = Multiplayer::GnsCallbackDispatch::CallbackOption();

	// CREATE LISTENER
	m_hListenSock = m_pInterface->CreateListenSocketIP(bindServerAddress, 1, &opt);

	if (m_hListenSock == k_HSteamListenSocket_Invalid)
	{
		Msg("! [SteamNetServer] Failed to listen on port %d", bindServerAddress.m_port);
		DestroyConnection();
		return false;
	}

	m_hPollGroup = m_pInterface->CreatePollGroup();
	if (m_hPollGroup == k_HSteamNetPollGroup_Invalid)
	{
		Msg("! [SteamNetServer] Failed to create poll group");
		DestroyConnection();
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

	if (m_pInterface == nullptr)
	{
		return;
	}

	DisconnectAll();

	if (m_hListenSock != k_HSteamListenSocket_Invalid)
	{
		m_pInterface->CloseListenSocket(m_hListenSock);
		m_hListenSock = k_HSteamListenSocket_Invalid;
	}

	if (m_hPollGroup != k_HSteamNetPollGroup_Invalid)
	{
		m_pInterface->DestroyPollGroup(m_hPollGroup);
		m_hPollGroup = k_HSteamNetPollGroup_Invalid;
	}

	m_pInterface = nullptr;

	GameNetworkingSockets_Kill();
}

#pragma endregion

// -----------------------------------------------------------------------------

void SteamNetServer::PollIncomingMessages(u32 Budget)
{
	for (u32 Index = 0; Index < Budget; ++Index)
	{
		if (m_pInterface == nullptr)
			break;

		ISteamNetworkingMessage *pIncomingMsg = nullptr;

		int numMsgs = m_pInterface->ReceiveMessagesOnPollGroup(m_hPollGroup, &pIncomingMsg, 1);
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
	using Multiplayer::LegacyAdmissionDecision;

	SteamNetConnectionInfo_t Info{};
	if (!m_pInterface->GetConnectionInfo(Connection, &Info) || Info.m_hListenSocket != m_hListenSock)
	{
		return;
	}
	if (Info.m_eState != k_ESteamNetworkingConnectionState_Connecting)
	{
		if (Multiplayer::IsGnsPeerClosed(Info.m_eState))
		{
			m_pInterface->CloseConnection(Connection, EUnknownReason, nullptr, false);
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
		m_pInterface->CloseConnection(Connection, Rejection, nullptr, false);
		return;
	}

	if (m_pInterface->AcceptConnection(Connection) != k_EResultOK ||
		!m_pInterface->SetConnectionPollGroup(Connection, m_hPollGroup))
	{
		m_pInterface->CloseConnection(Connection, EUnknownReason, nullptr, false);
		return;
	}
	m_players.push_back(Connection);
}

void SteamNetServer::RejectPendingConnection(HSteamNetConnection Connection)
{
	SteamNetConnectionInfo_t Info{};
	if (m_pInterface->GetConnectionInfo(Connection, &Info) && Info.m_hListenSocket == m_hListenSock &&
		(Info.m_eState == k_ESteamNetworkingConnectionState_Connecting || Multiplayer::IsGnsPeerClosed(Info.m_eState)))
	{
		m_pInterface->CloseConnection(Connection, ESessionFull, nullptr, false);
	}
}

void SteamNetServer::RetireClosedConnections()
{
	xr_vector<HSteamNetConnection> Closed;
	for (const auto Connection : m_players)
	{
		SteamNetConnectionInfo_t Info{};
		if (!m_pInterface->GetConnectionInfo(Connection, &Info) || Multiplayer::IsGnsPeerClosed(Info.m_eState))
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
		m_pInterface->CloseConnection(Connection, EServerShutdown, nullptr, false);
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
	if (std::erase(m_players, connection) != 0)
	{
		m_pInterface->CloseConnection(connection, nReason, sReason, false);
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
	SteamNetConnectionInfo_t info;
	if (!m_pInterface->GetConnectionInfo(ID.value(), &info))
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
	R_ASSERT(m_pInterface);

	SteamNetConnectionRealTimeStatus_t status;
	if (m_pInterface->GetConnectionRealTimeStatus(ID.value(), &status, 0, nullptr))
	{
		dwPending = status.m_cbPendingReliable + status.m_cbPendingUnreliable;
		return true;
	}

	return false;
}

// -----------------------------------------------------------------------------

void SteamNetServer::UpdateClientStatistic(IClient* C)
{
	SteamNetConnectionRealTimeStatus_t status;
	if (!m_pInterface->GetConnectionRealTimeStatus(C->ID.value(), &status, 0, nullptr))
	{
		return;
	}
	C->stats.Update(status);
}
