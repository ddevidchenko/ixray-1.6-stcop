#include "StdAfx.h"
#include "StalkerNavigation.h"
#include "../ai_space.h"
#include "../game_graph.h"
#include "../level_graph.h"
#include "xrServer_Objects_ALife_Monsters.h"

namespace StalkerNavigation
{
static const char* Describe(Status Navigation)
{
	switch (Navigation)
	{
	case Status::MissingGameGraph:
		return "missing game graph";
	case Status::MissingLevelGraph:
		return "missing level graph";
	case Status::InactiveLevel:
		return "no active level in the game graph";
	case Status::MissingCrossTable:
		return "missing cross table";
	case Status::LevelMismatch:
		return "level graph does not match the game graph";
	case Status::EmptyLevelGraph:
		return "level graph has no vertices";
	case Status::CrossTableMismatch:
		return "cross table does not match the active navigation graphs";
	case Status::Ready:
		return "navigation is ready";
	}
	return "unrecognized navigation status";
}

bool CanSpawn(const CSE_Abstract& Entity)
{
	if (!smart_cast<const CSE_ALifeHumanStalker*>(&Entity))
	{
		return true;
	}
	const auto Navigation = Check(ai().get_game_graph(), ai().get_level_graph());
	if (Navigation == Status::Ready)
	{
		return true;
	}
	Msg("! Rejected stalker spawn '%s' (id %u): %s", *Entity.s_name, static_cast<u32>(Entity.ID), Describe(Navigation));
	return false;
}
}
