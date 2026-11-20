#include "../../../xrGame/Multiplayer/ArenaLadderRoute.h"

#include <cstdio>
#include <vector>

namespace ArenaLadderTests
{
using namespace Multiplayer::ArenaLadders;

static bool Expect(const char* Case, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Case);
	}
	return Passed;
}

static bool Attachment()
{
	std::vector<Record> Records;
	const auto Read = [&](xr_string_view Text, u64 Guid = 0x42)
	{
		return ReadAttachment(Text, {{Guid, 0xab}}, 6, Records);
	};

	bool Ok = Expect("matching grid preserves each direction and native node ID",
		Read("arena_ladders 1 42 ab\r\nshaft up 1 4 3.5\r\nshaft down 4 1 4") &&
		Records.size() == 2 && Records[0].Name == "shaft" && Records[0].Travel == Direction::Up &&
		Records[0].Entry == 1 && Records[0].Exit == 4 && Records[0].Cost == 3.5f &&
		Records[1].Travel == Direction::Down && Records[1].Entry == 4 && Records[1].Exit == 1);
	Ok &= Expect("identical node IDs from a regenerated grid are stale",
		!Read("arena_ladders 1 42 ab\nshaft up 1 4 3.5", 0x43) && Records.empty());
	Ok &= Expect("duplicate direction rejects the entire attachment",
		!Read("arena_ladders 1 42 ab\nshaft up 1 4 3\nshaft up 2 5 3") && Records.empty());
	Ok &= Expect("invalid exit cannot leave an earlier record published",
		!Read("arena_ladders 1 42 ab\nshaft up 1 4 3\nother down 5 6 3") && Records.empty());
	Ok &= Expect("self-links and nonfinite costs are invalid",
		!Read("arena_ladders 1 42 ab\nshaft up 4 4 3") &&
		!Read("arena_ladders 1 42 ab\nshaft up 1 4 nan") &&
		!Read("arena_ladders 1 42 ab\nshaft up 1 4 0"));
	Ok &= Expect("unknown schema, surplus fields, and malformed nodes fail closed",
		!Read("arena_ladders 2 42 ab") &&
		!Read("arena_ladders 1 42 ab\nshaft up 1 4 2 surplus") &&
		!Read("arena_ladders 1 42 ab\nshaft up -1 4 2"));
	Ok &= Expect("walk-only attachment and whitespace are valid",
		Read("arena_ladders 1 42 ab\r\n  \r\n") && Records.empty());
	return Ok;
}

static bool Routing()
{
	struct Edge
	{
		u32 From;
		Step To;
		f32 Cost = 1.f;
	};
	// Nodes 0–1, 2–4 and 5–6 are three separate walkable floors.
	constexpr Edge Edges[]{
		{0, {1}}, {1, {0}}, {1, {2, 0}, 5}, {2, {6, 4}, 40}, {2, {3}}, {3, {2}}, {3, {4}}, {4, {3}},
		{4, {5, 1}, 3}, {5, {4, 2}, 3}, {5, {6}}, {6, {5}}, {3, {6, 3}, 8},
	};
	std::vector<Step> Path;
	u32 UnavailableLink = NoLink;
	u32 BlockedNode = NoLink;
	const auto Find = [&](u32 Start, u32 Goal, u32 Budget = 16)
	{
		return FindRoute(Start, Goal, Budget, Path, [&](u32 From, auto Visit)
		{
			for (const auto& Edge : Edges)
			{
				if (Edge.From == From && Edge.To.Vertex != BlockedNode &&
					(Edge.To.Link == NoLink || Edge.To.Link != UnavailableLink))
				{
					Visit(Edge.To, Edge.Cost);
				}
			}
		}, [](u32) { return 0.f; });
	};

	bool Ok = Expect("cheaper mixed route replaces an already discovered expensive shortcut",
		Find(0, 6) == RouteResult::Found &&
		Path == std::vector<Step>{{0}, {1}, {2, 0}, {3}, {4}, {5, 1}, {6}});
	Ok &= Expect("authored downward link is independently usable",
		Find(6, 2) == RouteResult::Found && Path == std::vector<Step>{{6}, {5}, {4, 2}, {3}, {2}});
	Ok &= Expect("one-way lower ladder cannot be reversed",
		Find(6, 0) == RouteResult::Unreachable && Path.empty());
	UnavailableLink = 1;
	Ok &= Expect("an occupied ladder is routed around when an alternative exists",
		Find(0, 6) == RouteResult::Found && Path == std::vector<Step>{{0}, {1}, {2, 0}, {3}, {6, 3}});
	BlockedNode = 2;
	Ok &= Expect("a disconnected landing cannot be reached by climbing",
		Find(0, 6) == RouteResult::Unreachable && Path.empty());
	BlockedNode = NoLink;
	Ok &= Expect("budget exhaustion clears previously found output",
		Find(0, 1) == RouteResult::Found && Find(0, 6, 2) == RouteResult::BudgetExceeded && Path.empty());
	Ok &= Expect("already at the target preserves a one-node walking path",
		Find(4, 4) == RouteResult::Found && Path == std::vector<Step>{{4}});
	return Ok;
}

static bool Motor()
{
	Reservation Ladder;
	bool Ok = Expect("opposite direction cannot take the same physical ladder",
		Ladder.TryAcquire(10) && Ladder.TryAcquire(10) && !Ladder.TryAcquire(11) && !Ladder.Available(11));
	Ladder.Release(11);
	Ok &= Expect("only the owner releases its reservation", !Ladder.Available(11));
	Ladder.Release(10);
	Ok &= Expect("next bot can reserve once released", Ladder.TryAcquire(11));

	Traversal Motor;
	Motor.Begin(100, 500);
	Ok &= Expect("mount needs physical arrival", Motor.Update(110, true, false, true) == Phase::Mount);
	Ok &= Expect("mount reaches the ladder before climbing",
		Motor.Update(120, true, true, false) == Phase::Climb);
	Ok &= Expect("climb reaches the far end before dismounting",
		Motor.Update(130, true, true, false) == Phase::Dismount);
	Ok &= Expect("reaching the exit releases climbing support before completion",
		Motor.Update(140, true, true, true) == Phase::Settle);
	Ok &= Expect("exit is not complete in midair", Motor.Update(150, true, true, false) == Phase::Settle);
	Ok &= Expect("ground support on the wrong node cannot complete the route",
		Motor.Update(160, true, false, true) == Phase::Settle);
	Ok &= Expect("only the grounded exit completes traversal",
		Motor.Update(170, true, true, true) == Phase::Complete);

	Motor.Begin(0xfffffff0, 100);
	Ok &= Expect("blocked exits time out across the global clock wrap",
		Motor.Update(0xfffffff1, true, true, false) == Phase::Climb &&
		Motor.Update(0xfffffff2, true, true, false) == Phase::Dismount &&
		Motor.Update(0x53, true, false, false) == Phase::Dismount &&
		Motor.Update(0x54, true, true, true) == Phase::Failed);
	Ok &= Expect("a failed attempt cannot resume from late physics observations",
		Motor.Update(0x55, true, true, true) == Phase::Failed);
	Motor.Begin(1000, 12000);
	Motor.Update(1010, true, true, false);
	Motor.Update(1020, true, true, false);
	Motor.Update(1030, true, false, false, 1.f);
	Ok &= Expect("an obstacle at the exit triggers a replan before the long climb deadline",
		Motor.Update(2200, true, false, false, 0.96f) == Phase::Dismount &&
		Motor.Update(3030, true, false, false, 0.97f) == Phase::Failed);
	Motor.Begin(1000, 12000);
	Motor.Update(1010, true, false, false, 1.f);
	Ok &= Expect("continued progress extends the stall deadline",
		Motor.Update(2800, true, false, false, 0.7f) == Phase::Mount &&
		Motor.Update(4100, true, true, false, 0.08f) == Phase::Climb);
	Motor.Begin(500, 100);
	Ok &= Expect("destroying a ladder between physics frames fails the current climb",
		Motor.Update(501, true, true, false) == Phase::Climb &&
		Motor.Update(502, false, false, false) == Phase::Failed);
	return Ok;
}

bool Run()
{
	const bool Files = Attachment();
	const bool Routes = Routing();
	const bool Traversal = Motor();
	if (!Files || !Routes || !Traversal)
	{
		return false;
	}
	std::puts("Arena ladder checks passed");
	return true;
}
}
