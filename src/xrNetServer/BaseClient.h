#pragma once
#include "NET_Shared.h"
#include "NET_Common.h"
#include "ClientConnectionOptions.h"
#include "IClientStatistic.h"
#include "INetQueue.h"
#include "Multiplayer/LegacyClockSync.h"

#include <atomic>

struct ip_address;

class XRNETSERVER_API BaseClient : public MultipacketReciever, private MultipacketSender
{
public:
	enum ConnectionState
	{
		EnmConnectionFails = 0,
		EnmConnectionWait = -1,
		EnmConnectionCompleted = 1
	};

private:
	ClientID            net_ClientID;
	xrCriticalSection ClockSyncSection;
	Multiplayer::LegacyClockSync ClockSync;

protected:
	u32                 net_Time_LastUpdate;
	std::atomic<s32>    net_TimeDelta{0};
	std::atomic<s32>    net_TimeDelta_User{0};

	GameDescriptionData m_game_description;
	CTimer*             device_timer;

	std::atomic<ConnectionState> net_Connected{EnmConnectionWait};
	bool                net_Syncronised = false;
	std::atomic<bool>   net_Disconnected{true};

	INetQueue           net_Queue;
	IClientStatistic    net_Statistic;

public:
	BaseClient(CTimer* tm);
	virtual ~BaseClient();

private:
	virtual void        _Recieve(const void* data, u32 data_size, u32 param);
	virtual void        _SendTo_LL(const void* data, u32 size, u32 flags, u32 timeout);

protected:
	virtual bool        IsConnectionInit() { return true; }

	void                SetClientID(ClientID const & local_client) { net_ClientID = local_client; };

	void                ParseConnectionOptions(const char* options, ClientConnectionOptions& out);
	virtual bool        CreateConnection(ClientConnectionOptions& opt) { return true; };
	virtual void        DestroyConnection() {};

	virtual bool        GetPendingMessagesCount(DWORD& dwPending) { return true; };
	virtual Multiplayer::ClockProbeResult TrySendClockProbe(MSYS_PING&) { return Multiplayer::ClockProbeResult::Failed; }

	virtual	void        SendTo_LL(void* data, u32 size, u32 dwFlags = DPNSEND_GUARANTEED, u32 dwTimeout = 0) {}

public:
	bool                Connect(const char* options);
	void                Disconnect();

	ClientID const &	GetClientID() { return net_ClientID; };

	bool                net_isCompleted_Connect() const { return net_Connected == EnmConnectionCompleted; }
	bool                net_isFails_Connect() const { return net_Connected == EnmConnectionFails; }
	bool                net_isCompleted_Sync() const { return net_Syncronised; }
	bool                net_isDisconnected() const { return net_Disconnected; }
	IC GameDescriptionData const & get_net_DescriptionData() const { return m_game_description; }

	virtual bool        HasSessionName() { return false; }
	virtual const char*      net_SessionName() const { return m_game_description.map_name; }

	bool                net_HasBandwidth();

	bool                net_IsSyncronised() const { return net_Syncronised; };
	void                net_Syncronize();
	[[nodiscard]] Multiplayer::LegacyClockSyncStatus PollClockSync();

	// receive
	IC void							StartProcessQueue() { net_Queue.Lock(); }; // WARNING ! after Start mast be End !!! <-
	IC NET_Packet*			net_msg_Retreive() { return net_Queue.Retreive(); };//							|
	IC void							net_msg_Release() { net_Queue.Release(); };//							|
	IC void							EndProcessQueue() { net_Queue.Unlock(); };//							<-

	// send
	virtual	void			  Send(NET_Packet& P, u32 dwFlags = DPNSEND_GUARANTEED, u32 dwTimeout = 0);
	virtual void			  Flush_Send_Buffer();

	virtual void        OnMessage(void* data, u32 size);
	virtual void        OnInvalidHost() {};
	virtual void        OnInvalidPassword() {};
	virtual void        OnSessionFull() {};
	virtual void        OnConnectRejected() {};


	virtual	const char*      GetMsgId2Name(u16 ID) { return ""; }
	virtual void        OnSessionTerminate(const char* reason) {};

	virtual	bool        GetServerAddress(ip_address& pAddress, DWORD* pPort) { return false; }

	// time management
	IC u32              timeServer() { return TimeGlobal(device_timer) + net_TimeDelta + net_TimeDelta_User; }
	IC u32              timeServer_Async() { return TimerAsync(device_timer) + net_TimeDelta + net_TimeDelta_User; }
	IC u32              timeServer_Delta() { return net_TimeDelta; }
	IC void             timeServer_UserDelta(s32 d) { net_TimeDelta_User = d; }

	// unused
	//IC void				    timeServer_Correct(u32 sv_time, u32 cl_time);

	// statistic
	void					      ClearStatistic() { net_Statistic.Clear(); };
	IClientStatistic&		GetStatistic() { return  net_Statistic; }
	virtual	void			  UpdateStatistic() {}
};

