#include "StdAfx.h"
#include "Level.h"
#include "../xrCore/stream_reader.h"
#include "MainMenu.h"
#include "../xrEngine/string_table.h"
#include "../xrEngine/XR_IOConsole.h"

static constexpr u32 r_buffer_size = 131072;	//128 Kb
void CLevel::CalculateLevelCrc32()
{
	void* read_buffer	= _alloca(r_buffer_size);
	Msg("* calculating checksum of level.geom");
	CStreamReader*		geom = FS.rs_open	("$level$","level.geom");
	R_ASSERT2			(geom, "failed to open level.geom file");
	u32 remaind			= geom->elapsed();
	map_data.m_level_geom_crc32	= 0;
	while (remaind)
	{
		u32 to_read = remaind;
		if (remaind > r_buffer_size)
		{
			to_read = r_buffer_size;
		}
		geom->r(read_buffer, to_read);
		map_data.m_level_geom_crc32 ^= crc32(read_buffer, to_read);
		remaind = geom->elapsed();
	}
	FS.r_close					(geom);
}

bool CLevel::IsChecksumsEqual(u32 check_sum) const
{
	return check_sum == map_data.m_level_geom_crc32;
}

bool CLevel::synchronize_map_data()
{
	PollNetworkTransport();
	if (!psNET_direct_connect && !IsDemoPlay() && net_isDisconnected())
	{
		connected_to_server = false;
		net_Stop();
		return false;
	}
	if (!OnClient() && !IsDemoSave())
	{
		deny_m_spawn		= false;
		map_data.m_map_sync_received	= true;
		return synchronize_client();
	}

#ifndef MASTER_GOLD
	Msg					("* synchronizing map data...");
#endif // #ifndef MASTER_GOLD

	const auto& ReplyWait = map_data.CheckToSendMapSync();

#ifdef DEBUG
	Msg("--- Waiting for server map name...");
#endif // #ifdef DEBUG
	ClientReceive(); 

	const auto Status = ReplyWait.Poll(Multiplayer::MapSyncWait::Clock::now(), map_data.m_map_sync_received, IsDemoPlay());
	if (Status == Multiplayer::MapSyncStatus::TimedOut)
	{
		Msg("Wait map data time out: reconnecting...");
		MakeReconnect();
		g_loading_events.erase(++g_loading_events.begin(), g_loading_events.end());
		return true;
	}
	
	if (Status == Multiplayer::MapSyncStatus::Waiting)
	{
		return false;
	}
			
	if (map_data.IsInvalidMapOrVersion())
	{
		Msg("! Incorect map or version, reconnecting...");
		MakeReconnect();
		g_loading_events.erase(++g_loading_events.begin(), g_loading_events.end());
		return true;
	}
	if (map_data.IsInvalidClientChecksum())
	{
		connected_to_server	= false;
		return false;	//!!!
	}
	return synchronize_client();
}

bool	CLevel::synchronize_client()
{
//---------------------------------------------------------------------------
	if (!sended_request_connection_data)
	{
		NET_Packet	P;
		P.w_begin	(M_CLIENT_REQUEST_CONNECTION_DATA);
		
		Send		(P, net_flags(true, true, true, true));
		sended_request_connection_data = true;
	}
//---------------------------------------------------------------------------
	if (game_configured)
	{
		deny_m_spawn = false;
		return true;
	}
#ifdef DEBUG
	Msg("--- Waiting for server configuration...");
#endif // #ifdef DEBUG
	if(Server)
	{
		ClientReceive();
		Server->Update();
	}	// if OnClient ClientReceive method called in upper invokation
	//Sleep(5); 
	return !!game_configured;
}


const Multiplayer::MapSyncWait& LevelMapSyncData::CheckToSendMapSync()
{
	if (!MapReplyWait)
	{
		NET_Packet P;
		P.w_begin	(M_SV_MAP_NAME);
		P.w_stringZ	(m_name);
		P.w_stringZ	(m_map_version);
		P.w_u32		(m_level_geom_crc32);
		Level().Send(P, net_flags(true, true, true, true));
		invalid_geom_checksum = false;
		m_map_sync_received = false;
		MapReplyWait.emplace(Multiplayer::MapSyncWait::Clock::now());
	}
	return *MapReplyWait;
}

void LevelMapSyncData::ReceiveServerMapSync(NET_Packet& P)
{
	m_map_sync_received	= true;
	MapSyncResponse	server_resp = static_cast<MapSyncResponse>(P.r_u8());
	if (server_resp == InvalidChecksum)
	{
		invalid_geom_checksum = true;
	} else if (server_resp == YouHaveOtherMap)
	{
		invalid_map_or_version = true;
	}
}
