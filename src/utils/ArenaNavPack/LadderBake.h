#pragma once

#include "../../xrGame/Multiplayer/ArenaLadderGeometry.h"
#include "../../xrGame/Multiplayer/ArenaNavigationPackage.h"

#include <vector>

namespace Multiplayer::ArenaLadders::Bake
{
using ArenaNavigation::Bytes;

enum class SpawnStatus { Ready, Invalid, Unsupported };
struct Ladder
{
	xr_string_view Name, Material;
	std::optional<Geometry> Shape;
	bool Deathmatch;
};

struct WalkNode { Point Position; bool CanEnter, CanLeave; };
struct Triangle { xr_array<Point, 3> Points; };
enum class CandidateStatus
{
	Ready, Disabled, DuplicateName, InvalidShape, UnsupportedGeometry,
	MissingLower, MissingUpper, BlockedLower, BlockedUpper, Incompatible
};
struct Candidate
{
	CandidateStatus Status;
	u32 Lower = NoLink;
	u32 Upper = NoLink;
	f32 Cost = 0.f;
};

SpawnStatus ReadSpawn(Bytes Data, std::vector<Ladder>& Ladders);
bool ReadWalkGrid(Bytes Data, std::vector<WalkNode>& Nodes);
bool AppendCollision(Bytes Data, u32 ExpectedVersion, std::vector<Triangle>& Triangles);
std::vector<Candidate> FindCandidates(xr_span<const Ladder> Ladders, xr_span<const WalkNode> Nodes,
	xr_span<const Triangle> Triangles);
const char* Describe(CandidateStatus Status);
}
