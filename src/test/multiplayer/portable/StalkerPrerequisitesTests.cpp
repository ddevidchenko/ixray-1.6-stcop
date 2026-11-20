#include "../../../xrGame/Multiplayer/StalkerNavigation.h"
#include "../../../xrGame/Multiplayer/StalkerAuthority.h"

#include <cstdio>
#include <map>

namespace StalkerPrerequisitesTests
{
struct LevelHeader
{
	u32 Guid = 12;
	u32 VertexCount = 30;

	u32 guid() const { return Guid; }
	u32 vertex_count() const { return VertexCount; }
};

struct LevelGraph
{
	u8 Id = 0;
	LevelHeader Header;

	u8 level_id() const { return Id; }
	const LevelHeader& header() const { return Header; }
};

struct GameHeader
{
	std::map<u8, LevelHeader> Levels{{0, {}}};
	u32 Guid = 44;

	const auto& levels() const { return Levels; }
	u32 guid() const { return Guid; }
};

struct CrossTable
{
	u32 GameGuid = 44;
	u32 LevelGuid = 12;
	u32 LevelVertexCount = 30;

	const CrossTable& header() const { return *this; }
	u32 game_guid() const { return GameGuid; }
	u32 level_guid() const { return LevelGuid; }
	u32 level_vertex_count() const { return LevelVertexCount; }
};

struct GameGraph
{
	GameHeader Header;
	const CrossTable* Table = nullptr;
	bool HasActiveVertex = true;

	bool HasCurrentLevelVertex() const { return HasActiveVertex; }
	const GameHeader& header() const { return Header; }
	const CrossTable* GetCrossTable() const { return Table; }
};

static bool Expect(const char* Case, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Case);
	}
	return Passed;
}

bool RunNavigation()
{
	using StalkerNavigation::Check;
	using StalkerNavigation::Status;
	CrossTable Table;
	GameGraph Game;
	Game.Table = &Table;
	LevelGraph Level;

	bool Passed = Expect("loaded level zero with matching navigation can spawn a stalker", Check(&Game, &Level) == Status::Ready);
	Passed &= Expect("stalker spawn requires a game graph", Check<GameGraph, LevelGraph>(nullptr, &Level) == Status::MissingGameGraph);
	Passed &= Expect("stalker spawn requires a level graph", Check<GameGraph, LevelGraph>(&Game, nullptr) == Status::MissingLevelGraph);
	Game.Table = nullptr;
	Passed &= Expect("game graph without a selected cross table cannot spawn a stalker", Check(&Game, &Level) == Status::MissingCrossTable);
	Game.Table = &Table;

	Level.Id = 0xff;
	Game.Header.Levels.emplace(0xff, Level.Header);
	Passed &= Expect("the unassigned level ID remains reserved even if listed", Check(&Game, &Level) == Status::InactiveLevel);
	Level.Id = 1;
	Passed &= Expect("level missing from the game graph cannot spawn a stalker", Check(&Game, &Level) == Status::InactiveLevel);
	Level.Id = 0;
	Game.HasActiveVertex = false;
	Passed &= Expect("an active level needs a usable game graph vertex", Check(&Game, &Level) == Status::InactiveLevel);
	Game.HasActiveVertex = true;
	Level.Header.VertexCount = 0;
	Table.LevelVertexCount = 0;
	Passed &= Expect("matching metadata cannot make an empty level traversable", Check(&Game, &Level) == Status::EmptyLevelGraph);
	Level.Header.VertexCount = 30;
	Table.LevelVertexCount = 30;

	Game.Header.Levels.at(0).Guid = 99;
	Passed &= Expect("selected game graph level must match the loaded level graph", Check(&Game, &Level) == Status::LevelMismatch);
	Game.Header.Levels.at(0).Guid = 12;

	++Table.LevelGuid;
	Passed &= Expect("cross table must belong to the loaded level", Check(&Game, &Level) == Status::CrossTableMismatch);
	--Table.LevelGuid;
	++Table.GameGuid;
	Passed &= Expect("cross table must belong to the current game graph", Check(&Game, &Level) == Status::CrossTableMismatch);
	--Table.GameGuid;
	--Table.LevelVertexCount;
	Passed &= Expect("cross table must cover every level vertex", Check(&Game, &Level) == Status::CrossTableMismatch);
	Table.LevelVertexCount += 2;
	Passed &= Expect("cross table must not claim additional level vertices", Check(&Game, &Level) == Status::CrossTableMismatch);
	--Table.LevelVertexCount;
	Passed &= Expect("the matching navigation set is usable again", Check(&Game, &Level) == Status::Ready);
	if (Passed)
	{
		std::puts("Stalker navigation checks passed");
	}
	return Passed;
}

bool RunAuthority()
{
	using Multiplayer::ResolveStalkerRole;
	using Multiplayer::StalkerRole;
	bool Passed = Expect("local stalker on the server can run its brain", ResolveStalkerRole(true, true) == StalkerRole::Authority);
	Passed &= Expect("remote stalker on a client can apply replicas", ResolveStalkerRole(false, false) == StalkerRole::Replica);
	Passed &= Expect("client-local ownership does not grant stalker authority", ResolveStalkerRole(false, true) == StalkerRole::Inactive);
	Passed &= Expect("remote stalker on the server is not a client replica", ResolveStalkerRole(true, false) == StalkerRole::Inactive);
	if (Passed)
	{
		std::puts("Stalker authority checks passed");
	}
	return Passed;
}
}
