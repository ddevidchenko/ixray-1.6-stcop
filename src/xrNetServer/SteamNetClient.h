#pragma once
#include "BaseClient.h"
#include "LegacyGnsTransport.h"

#pragma warning(push)
#pragma warning(disable:4995)
#include <steam/steamnetworkingsockets.h>
#pragma warning(pop)

class XRNETSERVER_API SteamNetClient : public BaseClient
{
	friend void LegacyGnsTransport::Poll(SteamNetClient& Client, SteamNetServer* Server);

private:
	xrCriticalSection			csConnection;
	ISteamNetworkingSockets*	m_pInterface = nullptr;
	HSteamNetConnection			m_hConnection = k_HSteamNetConnection_Invalid;
	s64 ConnectionGeneration = 0;

	xr_string m_user_name = "";
	xr_string m_user_pass = "";

	bool m_bWasConnected = false;
	bool m_bServerClient = false;
	bool m_bGameDescriptionRecieved = false;

public:
	SteamNetClient(CTimer* tm);
	virtual ~SteamNetClient();

private:
	IC bool			GameDescriptionReceived() const { return m_bGameDescriptionRecieved; }

	void			SendClientData();

	void ApplyConnectionStatus(const SteamNetConnectionStatusChangedCallback_t& Event);

	void			GetIpAddress(SteamNetConnectionInfo_t& info, ip_address& out);

	void PollIncomingMessages(u32 Budget);

protected:
	virtual bool	IsConnectionInit() override { return m_pInterface != nullptr; }

	virtual bool	CreateConnection(ClientConnectionOptions& opt) override;
	virtual void	DestroyConnection() override;
	virtual	void	SendTo_LL(void* data, u32 size, u32 dwFlags = DPNSEND_GUARANTEED, u32 dwTimeout = 0) override;

	virtual bool	GetPendingMessagesCount(DWORD& dwPending) override;
	Multiplayer::ClockProbeResult TrySendClockProbe(MSYS_PING& Ping) override;

public:
	virtual	bool	GetServerAddress(ip_address& pAddress, DWORD* pPort)  override;

	virtual bool	HasSessionName() { return GameDescriptionReceived(); }

	// statistic
	virtual	void	UpdateStatistic()  override;
};

