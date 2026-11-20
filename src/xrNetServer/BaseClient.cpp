#include "stdafx.h"
#include "BaseClient.h"

// -----------------------------------------------------------------------------
// global

XRNETSERVER_API Flags32	psNET_Flags = { 0 };
XRNETSERVER_API int		psNET_ClientUpdate = 30; // FPS
XRNETSERVER_API int		psNET_ClientPending = 2;
XRNETSERVER_API char	psNET_Name[32] = "Player";

//------------------------------------------------------------------------------
BaseClient::BaseClient(CTimer * timer) : net_Statistic(timer)
{
	device_timer = timer;

	net_Time_LastUpdate = 0;
}

// -----------------------------------------------------------------------------
BaseClient::~BaseClient()
{
	psNET_direct_connect = false;
}

// -----------------------------------------------------------------------------
#pragma region connect / disconnect
void BaseClient::ParseConnectionOptions(const char* options, ClientConnectionOptions& out)
{
	// SERVER NAME
	if (strchr(options, '/'))
	{
		strncpy_s(out.server_name, options, strchr(options, '/') - options);
	}
	if (strchr(out.server_name, '/'))
	{
		*strchr(out.server_name, '/') = 0;
	}

	// SERVER PASSWORD
	if (strstr(options, "psw="))
	{
		const char* PSW = strstr(options, "psw=") + 4;
		if (strchr(PSW, '/'))
			strncpy_s(out.server_pass, PSW, strchr(PSW, '/') - PSW);
		else
			xr_strcpy(out.server_pass, PSW);
	}

	// USER NAME
	if (strstr(options, "name="))
	{
		const char* NM = strstr(options, "name=") + 5;
		if (strchr(NM, '/'))
			strncpy_s(out.user_name, NM, strchr(NM, '/') - NM);
		else
			xr_strcpy(out.user_name, NM);
	}

	// USER PASSWORD
	if (strstr(options, "pass="))
	{
		const char* UP = strstr(options, "pass=") + 5;
		if (strchr(UP, '/'))
			strncpy_s(out.user_pass, UP, strchr(UP, '/') - UP);
		else
			xr_strcpy(out.user_pass, UP);
	}

	// SERVER PORT
	out.sv_port = START_PORT_LAN_SV;
	if (strstr(options, "port="))
	{
		string64	portstr;
		xr_strcpy(portstr, strstr(options, "port=") + 5);
		if (strchr(portstr, '/'))	*strchr(portstr, '/') = 0;
		out.sv_port = atol(portstr);
		clamp(out.sv_port, int(START_PORT), int(END_PORT));
	};

	// CLIENT PORT
	out.bClPortWasSet = false;
	out.cl_port = START_PORT_LAN_CL;
	if (strstr(options, "portcl="))
	{
		string64	portstr;
		xr_strcpy(portstr, strstr(options, "portcl=") + 7);
		if (strchr(portstr, '/'))	*strchr(portstr, '/') = 0;
		out.cl_port = atol(portstr);
		clamp(out.cl_port, int(START_PORT), int(END_PORT));
		out.bClPortWasSet = true;
	};
}

bool BaseClient::Connect(const char* options)
{
	R_ASSERT(options);

	{
		xrCriticalSectionGuard Locked(ClockSyncSection);
		ClockSync.Cancel();
	}
	net_Syncronised = false;
	net_TimeDelta = 0;
	net_Disconnected = false;

	if (!psNET_direct_connect)
	{
		ClientConnectionOptions connectOpt;
		ParseConnectionOptions(options, connectOpt);

		net_Connected = EnmConnectionWait;

		bool success = CreateConnection(connectOpt);
		if (!success)
		{
			DestroyConnection();
			return false;
		}

	} //psNET_direct_connect

	return true;
}

void BaseClient::Disconnect()
{
	net_Connected = EnmConnectionWait;
	net_Syncronised = false;
	{
		xrCriticalSectionGuard Locked(ClockSyncSection);
		ClockSync.Cancel();
	}
	DestroyConnection();
}

#pragma endregion

// -----------------------------------------------------------------------------
#pragma region recieve
void BaseClient::OnMessage(void * data, u32 size)
{
	// One of the messages - decompress it
	net_Queue.Lock();
	NET_Packet* P = net_Queue.Create();

	P->construct(data, size);
	P->timeReceive = timeServer_Async();

	u16 m_type;
	P->r_begin(m_type);
	net_Queue.Unlock();
}
#pragma endregion

// -----------------------------------------------------------------------------
#pragma region send
void BaseClient::_SendTo_LL(const void* data, u32 size, u32 flags, u32 timeout)
{
	if (net_Disconnected)
		return;

	SendTo_LL(const_cast<void*>(data), size, flags, timeout);
}


void	BaseClient::Send(NET_Packet& packet, u32 dwFlags, u32 dwTimeout)
{
	MultipacketSender::SendPacket(packet.B.data, packet.B.count, dwFlags, dwTimeout);
}

void	BaseClient::Flush_Send_Buffer()
{
	MultipacketSender::FlushSendBuffer(0);
}

#pragma endregion

// -----------------------------------------------------------------------------

#pragma region time correct
void BaseClient::net_Syncronize()
{
	net_Syncronised = psNET_direct_connect;
	xrCriticalSectionGuard Locked(ClockSyncSection);
	if (psNET_direct_connect)
	{
		ClockSync.Cancel();
		return;
	}
	ClockSync.Start(Multiplayer::LegacyClockSync::Clock::now());
}

Multiplayer::LegacyClockSyncStatus BaseClient::PollClockSync()
{
	using Multiplayer::LegacyClockSync;
	using Multiplayer::LegacyClockSyncStatus;

	// Only the loading or frame owner changes the state. The receive worker can latch a reply.
	const auto Status = ClockSync.GetStatus();
	if (Status != LegacyClockSyncStatus::Pending)
	{
		return Status;
	}

	std::optional<u32> Probe;
	{
		xrCriticalSectionGuard Locked(ClockSyncSection);
		if (net_isDisconnected())
		{
			ClockSync.Cancel();
			return LegacyClockSyncStatus::Idle;
		}

		Probe = ClockSync.Poll(LegacyClockSync::Clock::now(), TimerAsync(device_timer));
		if (ClockSync.GetStatus() == LegacyClockSyncStatus::Ready)
		{
			if (const auto Offset = ClockSync.GetOffset())
			{
				net_TimeDelta = *Offset;
			}
			net_Syncronised = true;
		}
	}

	if (Probe)
	{
		MSYS_PING Ping{};
		Ping.sign1 = 0x12071980;
		Ping.sign2 = 0x26111975;
		Ping.dwTime_ClientSend = *Probe;
		// The transport can take its own lock while the receive worker delivers a clock reply.
		const auto Result = TrySendClockProbe(Ping);
		xrCriticalSectionGuard Locked(ClockSyncSection);
		ClockSync.CompleteProbe(Result);
	}
	return ClockSync.GetStatus();
}

void BaseClient::_Recieve(const void* data, u32 data_size, u32 /*param*/)
{
	net_Statistic.dwBytesReceived += data_size;

	MSYS_CONFIG Header{};
	if (data_size >= sizeof(Header))
	{
		CopyMemory(&Header, data, sizeof(Header));
	}
	if (Header.sign1 == 0x12071980 && Header.sign2 == 0x26111975)
	{
		if (data_size == sizeof(MSYS_PING))
		{
			MSYS_PING Reply;
			CopyMemory(&Reply, data, sizeof(Reply));
			const auto Arrived = Multiplayer::LegacyClockSync::Clock::now();
			const u32 Received = TimerAsync(device_timer);
			xrCriticalSectionGuard Locked(ClockSyncSection);
			ClockSync.RecordReply(Arrived, Reply.dwTime_ClientSend, Reply.dwTime_Server, Received);
			return;
		}

		if (data_size == sizeof(MSYS_CONFIG))
		{
			net_Connected = EnmConnectionCompleted;
			return;
		}
		Msg("! Unknown system message");
		return;
	}
	else if (net_Connected == EnmConnectionCompleted)
	{
		OnMessage(const_cast<void*>(data), data_size);
	}
}
#pragma endregion

// -----------------------------------------------------------------------------
#pragma region bandwidth
bool BaseClient::net_HasBandwidth()
{
	u32 dwTime = TimeGlobal(device_timer);
	u32 dwInterval = 0;
	if (net_Disconnected) 
		return false;

	if (psNET_ClientUpdate != 0) dwInterval = 1000 / psNET_ClientUpdate;
	if (psNET_Flags.test(NETFLAG_MINIMIZEUPDATES))	dwInterval = 1000;	// approx 3 times per second

	if (psNET_direct_connect)
	{
		if (0 != psNET_ClientUpdate && (dwTime - net_Time_LastUpdate) > dwInterval)
		{
			net_Time_LastUpdate = dwTime;
			return true;
		}
	}
	else if (0 != psNET_ClientUpdate && (dwTime - net_Time_LastUpdate) > dwInterval)
	{
		R_ASSERT(IsConnectionInit());

		// check queue for "empty" state
		DWORD dwPending = 0;
		if (!GetPendingMessagesCount(dwPending))
		{
			return false;
		}

		if (dwPending > u32(psNET_ClientPending))
		{
			net_Statistic.dwTimesBlocked++;
			return false;
		};

		UpdateStatistic();

		// ok
		net_Time_LastUpdate = dwTime;
		return true;
	}

	return false;
}
#pragma endregion
