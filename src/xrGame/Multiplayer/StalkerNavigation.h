#pragma once

#include "../../xrCore/ScalarTypes.h"

class CSE_Abstract;

namespace StalkerNavigation
{
// Checks the live navigation set for the existing stalker body type.
[[nodiscard]] bool CanSpawn(const CSE_Abstract& Entity);

enum class Status
{
	Ready,
	MissingGameGraph,
	MissingLevelGraph,
	InactiveLevel,
	MissingCrossTable,
	LevelMismatch,
	EmptyLevelGraph,
	CrossTableMismatch,
};

template <class GameGraph, class LevelGraph>
[[nodiscard]] Status Check(const GameGraph* Game, const LevelGraph* Level) noexcept
{
	if (!Game)
	{
		return Status::MissingGameGraph;
	}
	if (!Level)
	{
		return Status::MissingLevelGraph;
	}

	constexpr u8 UnassignedLevel = 0xff;
	const auto& Levels = Game->header().levels();
	const auto Current = Levels.find(Level->level_id());
	if (Level->level_id() == UnassignedLevel || Current == Levels.end() || !Game->HasCurrentLevelVertex())
	{
		return Status::InactiveLevel;
	}

	const auto* CrossTable = Game->GetCrossTable();
	if (!CrossTable)
	{
		return Status::MissingCrossTable;
	}

	const auto& LevelHeader = Level->header();
	if (LevelHeader.vertex_count() == 0)
	{
		return Status::EmptyLevelGraph;
	}
	if (Current->second.guid() != LevelHeader.guid())
	{
		return Status::LevelMismatch;
	}
	const auto& CrossHeader = CrossTable->header();
	if (CrossHeader.level_guid() != LevelHeader.guid() || CrossHeader.game_guid() != Game->header().guid() ||
		CrossHeader.level_vertex_count() != LevelHeader.vertex_count())
	{
		return Status::CrossTableMismatch;
	}
	return Status::Ready;
}
}
