#include "StdAfx.h"
#include "LevelGameDef.h"
#include "ai_space.h"
#include "game_graph.h"
#include "game_sv_mp.h"
#include "../xrScripts/script_process.h"
#include "../xrScripts/script_engine.h"
#include "../xrScripts/script_engine_space.h"
#include "Level.h"
#include "game_cl_base.h"
#include "../xrEngine/x_ray.h"
#include "../xrPhysics/PhysicsCommon.h"
#include "level_sounds.h"
#include "GamePersistent.h"
#include "../xrEngine/Rain.h"

#include "inventory_upgrade_manager.h"
#include "ElectronicsProblemsManager.h"
#include "ParticlesObject.h"

const char* CLevel::LoadMultiplayerGraph()
{
	if (ai().get_game_graph())
	{
		return "game graph already has an owner";
	}

	xr_unique_ptr<IReader> SpawnFile(FS.r_open("$level$", "alife.spawn"));
	if (!SpawnFile)
	{
		return "missing alife.spawn";
	}

	xr_unique_ptr<IReader> PatrolChunk(SpawnFile->open_chunk(3));
	xr_unique_ptr<IReader> GraphChunk(SpawnFile->open_chunk(4));
	if (!PatrolChunk || !GraphChunk)
	{
		return "alife.spawn needs patrol and game graph chunks";
	}

	xr_unique_ptr<CGameGraph> GameGraph(new CGameGraph(*GraphChunk));
	ai().game_graph(GameGraph.release());

	MultiplayerSpawnFile = std::move(SpawnFile);
	MultiplayerGraphChunk = std::move(GraphChunk);
	ai().patrol_path_storage(*PatrolChunk);
	return nullptr;
}

bool CLevel::Load_GameSpecific_Before()
{
	// AI space
	g_pGamePersistent->SetLoadStageTitle("st_loading_ai_objects");
	g_pGamePersistent->LoadTitle();
	string_path fn_game;

	const auto* ServerGame = Server && Server->game ? Server->game->cast_game_sv_mp() : nullptr;
	const bool W1Navigation = ServerGame && ServerGame->UsesW1StalkerNavigation();

	const auto GameType = GamePersistent().GameType();
	// Remote clients can reach map load before the game mode arrives.
	const bool ArenaClient = (GameType == eGameIDDeathmatch || GameType == eGameIDNoGame) && OnClient() &&
		!FS.exist(fn_game, "$level$", "alife.spawn") && FS.exist(fn_game, "$level$", "level.ai");
	if (W1Navigation || ArenaClient)
	{
		const bool Loaded = ai().LoadArenaNavigation();
		if (W1Navigation)
		{
			if (Loaded)
			{
				Msg("W1 navigation: status=ready map=%s", map_data.m_name.c_str());
			}
			else
			{
				Msg("W1 navigation: status=failed map=%s reason=level.ai unavailable or invalid", map_data.m_name.c_str());
			}
		}
		else if (!Loaded)
		{
			Msg("! Unable to load arena navigation from level.ai");
		}
	}
	else if (GameType != eGameIDSingle && OnClient() && FS.exist(fn_game, "$level$", "alife.spawn"))
	{
		if (const char* Failure = LoadMultiplayerGraph())
		{
			Msg("! Unable to load multiplayer navigation: %s", Failure);
		}
	}

	if (!W1Navigation && !ai().get_alife())
	{
		const auto* GameGraph = ai().get_game_graph();
		if (GameGraph && FS.exist(fn_game, "$level$", "level.ai") && HasSessionName() &&
			GameGraph->header().level_exist(net_SessionName()))
		{
			ai().load(net_SessionName());
		}

		if (!g_dedicated_server && GameGraph && (!ai().get_level_graph() || ai().get_cross_table()) &&
			FS.exist(fn_game, "$level$", "level.game"))
		{
			IReader* stream = FS.r_open(fn_game);
			ai().patrol_path_storage_raw(*stream);
			FS.r_close(stream);
		}
	}

	if (!IsGameTypeSingle() && OnClient())
	{
		R_ASSERT(m_upgrade_manager == nullptr);
		m_upgrade_manager = new inventory::upgrade::Manager();

		R_ASSERT(m_electronics_problems_manager == nullptr);
		m_electronics_problems_manager = new CElectronicsProblemsManager();
	}

	return (true);
}

bool CLevel::Load_GameSpecific_After()
{
	R_ASSERT(m_StaticParticles.empty());
	// loading static particles
	string_path		fn_game;
	if (FS.exist(fn_game, "$level$", "level.ps_static")) 
	{
		IReader *F = FS.r_open	(fn_game);

		u32				chunk = 0;
		string256		ref_name;
		Fmatrix			transform;
		Fvector			zero_vel_={0.f,0.f,0.f};
		u32 ver			= 0;
		for (IReader *OBJ = F->open_chunk_iterator(chunk); OBJ; OBJ = F->open_chunk_iterator(chunk,OBJ)) 
		{
			if(chunk==0)
			{
				if(OBJ->length()==sizeof(u32))
				{
					ver		= OBJ->r_u32();
#ifndef MASTER_GOLD
					Msg		("PS new version, %d", ver);
#endif // #ifndef MASTER_GOLD
					continue;
				}
			}
			u16 gametype_usage				= 0;
			if(ver>0)
			{
				gametype_usage				= OBJ->r_u16();
			}
			OBJ->r_stringZ					(ref_name,sizeof(ref_name));
			OBJ->r							(&transform,sizeof(Fmatrix));transform.c.y+=0.01f;
			
						
			if ((g_pGamePersistent->m_game_params.m_e_game_type & EGameIDs(gametype_usage)) || (ver == 0))
			{
				auto pStaticParticles = Particles::Details::Create(ref_name,false,false);
				pStaticParticles->UpdateParent	(transform,zero_vel_);
				pStaticParticles->Play			(false);
				m_StaticParticles.push_back		(pStaticParticles);
			}
		}
		FS.r_close		(F);
	}
	
	if	(!g_dedicated_server)
	{
		// loading static sounds
		VERIFY								(m_level_sound_manager);
		m_level_sound_manager->Load			();

		// loading sound environment
		if ( FS.exist(fn_game, "$level$", "level.snd_env")) {
			IReader *F				= FS.r_open	(fn_game);
			::Sound->set_geometry_env(F);
			FS.r_close				(F);
		}
		// loading SOM
		if (FS.exist(fn_game, "$level$", "level.som")) {
			IReader *F				= FS.r_open	(fn_game);
			::Sound->set_geometry_som(F);
			FS.r_close				(F);
		}

		// loading random (around player) sounds
		if (pSettings->section_exist("sounds_random")){ 
			CInifile::Sect& S		= pSettings->r_section("sounds_random");
			Sounds_Random.reserve	(S.Data.size());
			for (CInifile::SectCIt I=S.Data.begin(); S.Data.end()!=I; ++I) 
			{
				Sounds_Random.push_back	(ref_sound());
				Sound->create			(Sounds_Random.back(),*I->first,st_Effect,sg_SourceType);
			}
			Sounds_Random_dwNextTime= Device.TimerAsync	()	+ 50000;
			Sounds_Random_Enabled	= false;
		}

		if (g_pGamePersistent->pEnvironment) {
			if (CEffect_Rain* rain = g_pGamePersistent->pEnvironment->eff_Rain) {
				rain->InvalidateState();
			}
		}

		if ( FS.exist(fn_game, "$level$", "level.fog_vol")) 
		{
			IReader *F				= FS.r_open	(fn_game);
			u16 version				= F->r_u16();
			if(version == 2)
			{
				u32 cnt					= F->r_u32();

				Fmatrix					volume_matrix;
				for(u32 i=0; i<cnt; ++i)
				{
					F->r				(&volume_matrix, sizeof(volume_matrix));
					u32 sub_cnt			= F->r_u32();
					for(u32 is=0; is<sub_cnt; ++is)
					{
						F->r			(&volume_matrix, sizeof(volume_matrix));
					}

				}
			}
			FS.r_close				(F);
		}
	}	

	// loading scripts
	ai().script_engine().remove_script_process(ScriptEngine::eScriptProcessorLevel);

	if (pLevel->section_exist("level_scripts") && pLevel->line_exist("level_scripts", "script"))
		ai().script_engine().add_script_process(ScriptEngine::eScriptProcessorLevel, new CScriptProcess("level", pLevel->r_string("level_scripts", "script")));
	else
		ai().script_engine().add_script_process(ScriptEngine::eScriptProcessorLevel, new CScriptProcess("level", ""));
		
	BlockCheatLoad();

	g_pGamePersistent->Environment().SetGameTime	(GetEnvironmentGameDayTimeSec(),game->GetEnvironmentGameTimeFactor());

	return true;
}

void CLevel::BlockCheatLoad()
{
#ifndef	DEBUG
	if( game && (!IsGameTypeSingle()) ) phTimefactor=1.f;
#endif
}
