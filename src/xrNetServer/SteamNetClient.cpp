#include "stdafx.h"
#include "SteamNetClient.h"
#include "SteamNetServer.h"
#include "Multiplayer/GnsCallbackDispatch.h"
#include "ip_address.h"
#include "WinsocksHelper.h"
#include <steam/isteamnetworkingutils.h>

#include <limits>

static s64 g_LegacyOutboundGeneration = 0;

SteamNetClient::SteamNetClient(CTimer* tm)
	: BaseClient(tm)
{
}

// -----------------------------------------------------------------------------

SteamNetClient::~SteamNetClient()
{

}

// -----------------------------------------------------------------------------

#pragma region connect / disconnect

bool SteamNetClient::CreateConnection(ClientConnectionOptions & connectOpt)
{
	m_bWasConnected = false;
	m_bServerClient = false;
	m_bGameDescriptionRecieved = false;
	m_pInterface = SteamNetworkingSockets();

	if (m_pInterface != nullptr)
	{
		// server client
		SteamNetworkingIdentity identity;
		m_pInterface->GetIdentity(&identity);
		R_ASSERT2(identity.IsLocalHost(), "! [SteamNetClient] Interface is initialized and identity is not localhost!");
		m_bServerClient = true;
	}
	else
	{
		SteamNetworkingIdentity identity;
		identity.Clear();
		if (!identity.SetGenericString(connectOpt.server_pass))
		{
			Msg("! [SteamNetClient] server password is too long!");
			return false;
		}

		SteamDatagramErrMsg errMsg;
		if (!GameNetworkingSockets_Init(&identity, errMsg))
		{
			Msg("! [SteamNetClient] GameNetworkingSockets_Init failed.  %s", errMsg);
			return false;
		}

		m_pInterface = SteamNetworkingSockets();

		if (m_pInterface == nullptr)
		{
			Msg("! [SteamNetClient] Server interface is NULL");
			return false;
		}
	}

	SteamNetworkingIPAddr serverAddr;
	serverAddr.Clear();

	int sv_port = connectOpt.sv_port;
	//if (stricmp(connectOpt.server_name, "localhost") == 0) // 127.0.0.1 ?!
	//{
	//	serverAddr.SetIPv6LocalHost((uint16)sv_port);
	//}
	//else
	{
		serverAddr.ParseString(connectOpt.server_name);
		serverAddr.m_port = (uint16)sv_port;

		// TRY GET HOSTNAME
		if (!serverAddr.IsIPv4())
		{
			uint32 ip = WinsocksHelper::GetIpAddress(connectOpt.server_name);
			if (ip == 0)
			{
				Msg("[SteamNetClient] Incorrect server address %s", connectOpt.server_name);
				OnInvalidHost();
				return false;
			}
			serverAddr.Clear();
			serverAddr.SetIPv4(ip, (uint16)sv_port);
		}
	}

	char szAddr[SteamNetworkingIPAddr::k_cchMaxString];
	serverAddr.ToString(szAddr, sizeof(szAddr), true);
	Msg("[SteamNetClient] Connecting to server at %s", szAddr);

	if (g_LegacyOutboundGeneration == std::numeric_limits<s64>::max())
	{
		return false;
	}
	ConnectionGeneration = ++g_LegacyOutboundGeneration;
	xr_array Options{Multiplayer::GnsCallbackDispatch::CallbackOption(), SteamNetworkingConfigValue_t{}};
	Options[1].SetInt64(k_ESteamNetworkingConfig_ConnectionUserData, ConnectionGeneration);
	m_hConnection = m_pInterface->ConnectByIPAddress(serverAddr, static_cast<int>(Options.size()), Options.data());
	if (m_hConnection == k_HSteamNetConnection_Invalid)
	{
		Msg("! [SteamNetClient] Failed to create connection");
		return false;
	}

	m_user_name = connectOpt.user_name;
	m_user_pass = connectOpt.user_pass;

	Msg("- [SteamNetClient] connect created");

	return true;
}

void SteamNetClient::DestroyConnection()
{
	Msg("- [SteamNetClient] destroy connection");

	xrCriticalSectionGuard lock(&csConnection);

	net_Disconnected = true;
	ConnectionGeneration = 0;

	if (m_pInterface == nullptr)
	{
		return;
	}

	if (m_hConnection != k_HSteamNetConnection_Invalid)
	{
		m_pInterface->CloseConnection(m_hConnection, 0, nullptr, false);
		m_hConnection = k_HSteamNetConnection_Invalid;
	}
	m_pInterface = nullptr;

	m_user_name.clear();
	m_user_pass.clear();

	// if no server client
	if (!m_bServerClient)
	{
		GameNetworkingSockets_Kill();
	}
}

#pragma endregion

// -----------------------------------------------------------------------------

#pragma region receive

void SteamNetClient::PollIncomingMessages(u32 Budget)
{
	for (u32 Index = 0; Index < Budget; ++Index)
	{
		if (m_pInterface == nullptr)
			break;

		ISteamNetworkingMessage *pIncomingMsg = nullptr;
		int numMsgs = m_pInterface->ReceiveMessagesOnConnection(m_hConnection, &pIncomingMsg, 1);
		if (numMsgs <= 0)
		{
			break;
		}
		const xr_scope_exit Release([pIncomingMsg] { pIncomingMsg->Release(); });

		void* data = pIncomingMsg->m_pData;
		int size = pIncomingMsg->m_cbSize;

		if (!GameDescriptionReceived())
		{
			MSYS_GAME_DESCRIPTION*	sys_gd = (MSYS_GAME_DESCRIPTION*)data;
			if (
				size == sizeof(MSYS_GAME_DESCRIPTION) &&
				sys_gd->sign1 == 0x02281488 &&
				sys_gd->sign2 == 0x01488228
				)
			{
				CopyMemory(&m_game_description, &sys_gd->data, sizeof(m_game_description));
				m_bGameDescriptionRecieved = true;

				continue;
			}
		}

		MultipacketReciever::RecievePacket(pIncomingMsg->m_pData, pIncomingMsg->m_cbSize);
	}
}

void SteamNetClient::ApplyConnectionStatus(const SteamNetConnectionStatusChangedCallback_t& Event)
{
	switch (Event.m_info.m_eState)
	{
	case k_ESteamNetworkingConnectionState_ClosedByPeer:
	case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
	{
		if (Event.m_info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer)
		{
			Msg("[SteamNetClient] ClosedByPeer");
		}
		else if (Event.m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
		{
			Msg("[SteamNetClient] ProblemDetectedLocally");
			Msg("Connection closed locally. Reason: %d (%s)", Event.m_info.m_eEndReason, Event.m_info.m_szEndDebug);
		}

		net_Connected = EnmConnectionFails;
		net_Disconnected = true;

		switch (Event.m_info.m_eEndReason)
		{
		case k_ESteamNetConnectionEnd_Misc_Timeout:
			if (m_bWasConnected)
				OnSessionTerminate("st_lost_connection");
			else
				OnInvalidHost();
			break;
		case EUnknownReason:
			OnSessionTerminate("Unknown");
			break;
		case EPlayerBanned:
			OnSessionTerminate("Banned");
			break;
		case EServerShutdown:
			OnSessionTerminate("st_server_shutdown");
			break;
		case EDetailedReason:
			OnSessionTerminate(Event.m_info.m_szEndDebug);
			break;
		case EInvalidPassword:
			OnInvalidPassword();
			break;
		case ESessionFull:
			OnSessionFull();
			break;
		default:
			OnSessionTerminate(Event.m_info.m_szEndDebug);
			break;
		}
	}
	break;
	case k_ESteamNetworkingConnectionState_Connected:
		Msg("[SteamNetClient] Connected to server");
		m_bWasConnected = true;
		SendClientData();
		break;
	case k_ESteamNetworkingConnectionState_None:
		// NOTE: We will get callbacks here when we destroy connections.  You can ignore these.
		break;
	case k_ESteamNetworkingConnectionState_Connecting:
		// NOTE: We will get this callback when we start connecting. We can ignore this.
		break;
	default:
		Msg("! [SteamNetClient] unknown steam connection state %d", Event.m_info.m_eState);
		break;
	}
}

#pragma endregion

// -----------------------------------------------------------------------------

void SteamNetClient::SendClientData()
{
	MSYS_CLIENT_DATA client_data;
	client_data.sign1 = 0x02281488;
	client_data.sign2 = 0x01488228;
	client_data.process_id = Platform::GetCurrentProcessId();
	xr_strcpy(client_data.name, m_user_name.c_str());
	xr_strcpy(client_data.pass, m_user_pass.c_str());

	SendTo_LL(&client_data, sizeof(MSYS_CLIENT_DATA), net_flags(true, true, false, false));
}

// -----------------------------------------------------------------------------

#pragma region send

void SteamNetClient::SendTo_LL(void * data, u32 size, u32 dwFlags, u32 dwTimeout)
{
	net_Statistic.dwBytesSended += size;

	EResult result = m_pInterface->SendMessageToConnection(m_hConnection, data, size, convert_flags_for_steam(dwFlags), nullptr);
	if (result != k_EResultOK)
	{
		//Msg("! [SteamNetClient] ERROR: Failed to send net-packet, reason: %d", result);
	}
}

Multiplayer::ClockProbeResult SteamNetClient::TrySendClockProbe(MSYS_PING& Ping)
{
	using Multiplayer::ClockProbeResult;
	if (!csConnection.TryEnter())
	{
		return ClockProbeResult::Deferred;
	}
	const xr_scope_exit Unlock([this] { csConnection.Leave(); });

	if (net_isDisconnected() || !m_pInterface || m_hConnection == k_HSteamNetConnection_Invalid)
	{
		return ClockProbeResult::Failed;
	}
	DWORD Pending = 0;
	if (!GetPendingMessagesCount(Pending))
	{
		return ClockProbeResult::Failed;
	}
	if (Pending != 0)
	{
		return ClockProbeResult::Deferred;
	}

	const auto Sent = m_pInterface->SendMessageToConnection(m_hConnection, &Ping, sizeof(Ping), k_nSteamNetworkingSend_UnreliableNoNagle, nullptr);
	if (Sent == k_EResultLimitExceeded)
	{
		return ClockProbeResult::Deferred;
	}
	return Sent == k_EResultOK ? ClockProbeResult::Sent : ClockProbeResult::Failed;
}

#pragma endregion

// -----------------------------------------------------------------------------

#pragma region statistic

void SteamNetClient::UpdateStatistic()
{
	SteamNetConnectionRealTimeStatus_t status;
	if (!m_pInterface->GetConnectionRealTimeStatus(m_hConnection, &status, 0, nullptr))
	{
		return;
	}
	net_Statistic.Update(status);
}

#pragma endregion

// -----------------------------------------------------------------------------

#pragma region pending messages

bool SteamNetClient::GetPendingMessagesCount(DWORD& dwPending)
{
	R_ASSERT(m_pInterface);
	R_ASSERT(m_hConnection != k_HSteamNetConnection_Invalid);

	SteamNetConnectionRealTimeStatus_t pStatus;
	if (m_pInterface->GetConnectionRealTimeStatus(m_hConnection, &pStatus, 0, nullptr))
	{
		dwPending = pStatus.m_cbPendingReliable + pStatus.m_cbPendingUnreliable;
		return true;
	}

	return false;
}

#pragma endregion

// -----------------------------------------------------------------------------

#pragma region ip address


void SteamNetClient::GetIpAddress(SteamNetConnectionInfo_t & info, ip_address & out)
{
	string64 ip_str;
	info.m_addrRemote.ToString(ip_str, sizeof(string64), false);
	out.set(ip_str);
}

bool SteamNetClient::GetServerAddress(ip_address& pAddress, DWORD* pPort)
{
	*pPort = 0;

	SteamNetConnectionInfo_t pInfo;
	if (m_pInterface->GetConnectionInfo(m_hConnection, &pInfo))
	{
		GetIpAddress(pInfo, pAddress);
		*pPort = pInfo.m_addrRemote.m_port;
		return true;
	}

	return false;
};

#pragma endregion

// -----------------------------------------------------------------------------
