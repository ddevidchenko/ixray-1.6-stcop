#include "../../../xrGame/Multiplayer/ArenaMovement.h"
#include "../../../xrGame/Multiplayer/ArenaLadderRoute.h"

#include <cstdio>
#include <vector>

namespace ArenaMovementTests
{
using namespace Multiplayer::ArenaMovement;
using namespace Multiplayer::ArenaLadders;

static bool Expect(bool Passed, const char* Description)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Description);
	}
	return Passed;
}

static bool ConfirmedWalking()
{
	WalkController Walk;
	const Point Target{1.f, 0.2f, 0.f};
	const BodySample Start{10, {0.f, 0.f, 0.f}, true};
	Walk.BeginWaypoint(Start.Step);
	bool Ok = Expect(Walk.Observe(100, Start, Target, true, true) == WalkState::Walking,
		"Movement demand alone cannot reach a waypoint.");
	Ok &= Expect(Walk.Observe(101, {10, Target, true}, Target, true, true) == WalkState::Walking,
		"An interpolated or predicted result from the same physics step cannot reach a waypoint.");
	Ok &= Expect(Walk.Observe(110, {11, {1.f, 3.f, 0.f}, true}, Target, true, true) == WalkState::Walking,
		"The floor above the destination cannot complete a path at the same X and Z.");
	Ok &= Expect(Walk.Observe(120, {12, Target, false}, Target, true, true) == WalkState::Walking,
		"An airborne body crossing the projected target cannot reach it.");
	Ok &= Expect(Walk.Observe(130, {13, {1.f, 0.2f, 0.4f}, true}, Target, true, true) == WalkState::Walking,
		"Passing a waypoint along one axis cannot skip the doorway clearance check.");
	Ok &= Expect(Walk.Observe(140, {14, {0.92f, 0.15f, 0.02f}, true}, Target, true, true) == WalkState::Reached,
		"A grounded physics sample on the correct stair tread advances the waypoint.");
	Walk.BeginWaypoint(14);
	Ok &= Expect(Walk.Observe(141, {14, Target, true}, Target, true, true) == WalkState::Walking &&
		Walk.Observe(150, {15, Target, true}, Target, false, true) == WalkState::Reached,
		"Each waypoint needs its own physics sample, including an already occupied destination.");
	return Ok;
}

static bool BlockedWalking()
{
	WalkController Walk;
	const Point Target{1.f, 0.f, 0.f};
	const Point Partial{0.4f, 0.f, 0.f};
	Walk.BeginWaypoint(20);
	Walk.Observe(100, {20, {}, true}, Target, true, true);
	bool Ok = Expect(Walk.Observe(900, {21, Partial, true}, Target, true, true) == WalkState::Walking,
		"Partial movement toward a static obstruction refreshes the progress deadline.");
	Ok &= Expect(Walk.Observe(1800, {22, Partial, true}, Target, true, true) == WalkState::Walking &&
		Walk.Observe(2900, {23, Partial, true}, Target, true, true) == WalkState::Blocked,
		"Repeated contact at the same feet position eventually blocks the edge.");
	Walk.BeginWaypoint(23);
	Ok &= Expect(Walk.Observe(3000, {23, Partial, true}, Target, true, false) == WalkState::Stopped &&
		Walk.Observe(3400, {24, Partial, true}, Target, true, false) == WalkState::Stopped &&
		Walk.Observe(3500, {25, Partial, true}, Target, true, true) == WalkState::Walking &&
		Walk.Observe(3600, {26, Target, true}, Target, true, true) == WalkState::Reached,
		"A door that opens during the grace period can resume on the same edge.");
	Walk.BeginWaypoint(26);
	Ok &= Expect(Walk.Observe(3700, {26, Partial, true}, Target, true, false) == WalkState::Stopped &&
		Walk.Observe(5700, {27, Partial, true}, Target, true, false) == WalkState::Blocked,
		"A persistently closed door uses the same blocked-edge outcome as a crowd or wall.");
	Walk.BeginWaypoint(27);
	Walk.Observe(6000, {27, Partial, true}, Target, true, true);
	Ok &= Expect(Walk.Observe(9000, {27, Partial, true}, Target, true, true) == WalkState::Walking &&
		Walk.Observe(9100, {28, Partial, true}, Target, false, true) == WalkState::Stopped &&
		Walk.Observe(12000, {29, Partial, true}, Target, true, true) == WalkState::Walking,
		"A frozen physics world and an intentional stop do not blacklist a walk edge.");
	Walk.BeginWaypoint(29);
	Walk.Observe(13000, {29, Partial, true}, Target, true, false);
	Ok &= Expect(Walk.Observe(13600, {30, Partial, true}, Target, true, true) == WalkState::Walking &&
		Walk.Observe(14000, {31, Partial, true}, Target, true, false) == WalkState::Stopped &&
		Walk.Observe(14500, {32, Partial, true}, Target, true, true) == WalkState::Blocked,
		"Door state changes cannot extend the grace period without physical progress.");
	return Ok;
}

static bool Replanning()
{
	Recovery Policy;
	Policy.SetRoute(7, 4);
	const auto Plan = [&](u32 Now, bool Bypass, std::vector<Step>& Out)
	{
		return FindRoute(0, 4, 12, Out, [&](u32 From, auto Visit)
		{
			const auto Walk = [&](u32 To)
			{
				if (!Policy.AvoidsWalk(Now, From, To))
				{
					Visit({To}, From == 2 ? 4.f : 1.f);
				}
			};
			if (From == 0)
			{
				Walk(1);
				if (Bypass)
				{
					Walk(2);
				}
			}
			if (From == 1 && !Policy.AvoidsLadder(Now, 5, [](u32 A, u32 B) { return (A / 2) == (B / 2); }))
			{
				Visit({4, 5}, 2.f);
			}
			if (From == 2 && Bypass)
			{
				Walk(4);
			}
		}, [](u32) { return 0.f; });
	};
	std::vector<Step> Path;
	bool Ok = Expect(Plan(100, true, Path) == RouteResult::Found && Path[1].Vertex == 1,
		"The same planner starts with its preferred route.");
	Policy.Reject(100, Segment::Walk(0, 1));
	Ok &= Expect(!Policy.CanPlan(599) && Policy.CanPlan(600) &&
		Plan(600, true, Path) == RouteResult::Found && Path[1].Vertex == 2 &&
		!Policy.AvoidsWalk(600, 1, 0),
		"The active directed walk edge is bypassed after the shared retry delay.");
	Ok &= Expect(Plan(600, false, Path) == RouteResult::Unreachable && Path.empty(),
		"With no bypass the planner cannot route across the blocked edge.");
	Policy.Reject(700, Segment::Ladder(4));
	Ok &= Expect(Policy.AvoidsWalk(1200, 0, 1) &&
		Policy.AvoidsLadder(1200, 5, [](u32 A, u32 B) { return A / 2 == B / 2; }) &&
		!Policy.AvoidsWalk(2700, 0, 1) && Plan(2700, false, Path) == RouteResult::Unreachable &&
		Plan(3200, false, Path) == RouteResult::Found,
		"Ladder failure excludes both directions of that ladder and preserves previous walk exclusions.");
	Policy.SetRoute(7, 4);
	Policy.Reject(3250, {});
	Ok &= Expect(!Policy.CanPlan(3500), "An unreachable query shares the recovery delay without inventing a blocker.");
	Policy.SetRoute(7, 4);
	Ok &= Expect(!Policy.CanPlan(3500), "Repeated movement demand cannot bypass the retry delay.");
	Policy.SetRoute(8, 4);
	Ok &= Expect(Policy.CanPlan(3500) && !Policy.AvoidsWalk(3500, 0, 1),
		"New map generations never reuse old exclusions or retry clocks.");
	Policy.Reject(u32(-200), Segment::Walk(1, 2));
	Ok &= Expect(!Policy.CanPlan(150) && Policy.CanPlan(300) && Policy.AvoidsWalk(300, 1, 2),
		"Temporary recovery survives the simulation clock wrapping.");
	Policy.SetRoute(8, 6);
	Ok &= Expect(Policy.CanPlan(300) && !Policy.AvoidsWalk(300, 1, 2),
		"Changing the goal clears its old recovery state.");
	return Ok;
}

static bool BoundedSteering()
{
	bool Ok = Expect(std::abs(SteeringSpeed(20.f, 2.f, 0.24f, 0.02f) - 6.f) < 0.001f,
		"A faster demand still travels at most half a foot radius per fixed step.");
	Ok &= Expect(SteeringSpeed(2.f, 1.f, 0.24f, 0.02f) == 2.f &&
		SteeringSpeed(3.f, 0.1f, 0.24f, 0.02f) < 1.f,
		"Normal movement uses the requested speed and slows for the waypoint.");
	Ok &= Expect(SteeringSpeed(2.f, 1.f, 0.24f, 0.f) == 0.f,
		"An invalid fixed step cannot issue a movement demand.");
	return Ok;
}

bool Run()
{
	bool Ok = ConfirmedWalking();
	Ok &= BlockedWalking();
	Ok &= Replanning();
	Ok &= BoundedSteering();
	if (Ok)
	{
		std::puts("Arena movement checks passed");
	}
	return Ok;
}
}
