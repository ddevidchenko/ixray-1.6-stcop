#include "StdAfx.h"
#include "movement_manager.h"
#include "Multiplayer/ArenaLadderNavigation.h"
#include "CharacterPhysicsSupport.h"
#include "ClimableObject.h"
#include "Creature.h"
#include "Level.h"
#include "PHMovementControl.h"
#include "ai_object_location.h"
#include "detail_path_manager.h"
#include "level_path_manager.h"
#include "../xrPhysics/IElevatorState.h"
#include "../xrPhysics/PHCharacter.h"
#include "../xrPhysics/IPHWorld.h"

namespace
{
constexpr u32 ArenaPathNodes = 8192;
constexpr float ClimbSpeed = 1.7f;
constexpr float EndTolerance = 0.12f;

bool IsTraversing(Multiplayer::ArenaLadders::Phase Phase)
{
	using enum Multiplayer::ArenaLadders::Phase;
	return Phase == Mount || Phase == Climb || Phase == Dismount || Phase == Settle;
}
}

using namespace Multiplayer::ArenaLadders;
using Multiplayer::ArenaMovement::Segment;

bool CMovementManager::UsesArenaMovement() const
{
	const auto* Links = ai().ArenaLadderLinks();
	return g_dedicated_server && Links && ai().ArenaNavigationReady() && m_object->cast_stalker() && m_object->Local() && OnServer();
}

u32 CMovementManager::WalkDestinationVertex() const
{
	const auto* Links = ai().ArenaLadderLinks();
	if (Links && Links->Generation() == ArenaLadder.MapGeneration && ArenaLadder.NextLink != NoLink)
	{
		return Links->Link(ArenaLadder.NextLink).Entry;
	}
	return level_dest_vertex_id();
}

void CMovementManager::ProcessArenaLevelPath()
{
	if (IsTraversing(ArenaLadder.Motor.State()) || ArenaLadder.Recovering)
	{
		return;
	}

	auto& Links = *ai().ArenaLadderLinks();
	if (ArenaLadder.MapGeneration != Links.Generation())
	{
		CancelArenaLadder(nullptr);
		ArenaLadder.MapGeneration = Links.Generation();
		InvalidateArenaRoute();
	}
	const u32 Goal = level_dest_vertex_id();
	const u32 Now = Device.dwTimeGlobal;
	ArenaRecovery.SetRoute(Links.Generation(), Goal);

	if (level_path().actual() && detail().actual())
	{
		if (m_path_state == ePathStatePathCompleted)
		{
			return;
		}
		if (m_path_state == ePathStatePathVerification)
		{
			if (ArenaLadder.NextLink == NoLink || Links.Resolve(ArenaLadder.NextLink, object().ID()))
			{
				return;
			}
			ReplanArenaRoute(Segment::Ladder(ArenaLadder.NextLink));
		}
	}
	if (!ArenaRecovery.CanPlan(Now))
	{
		return;
	}
	InvalidateArenaRoute();

	auto* Support = object().character_physics_support();
	auto* Physics = Support ? Support->movement() : nullptr;
	if (!Physics || !Physics->CanDriveArenaCharacter() || object().animation_movement_controlled() ||
		!Physics->PHCharacter()->HasGroundSupport())
	{
		return;
	}
	Fvector Feet;
	Physics->PHCharacter()->GetPosition(Feet);
	const u32 Start = Links.ProjectWalkVertex(Feet);
	xr_vector<Step> Route;
	if (!Links.Plan(restrictions(), object().ID(), Start, Goal, ArenaPathNodes, ArenaRecovery, Now, Route))
	{
		ArenaRecovery.Reject(Now, {});
		return;
	}

	auto& Walk = level_path_path();
	Walk.clear();
	for (const auto& Step : Route)
	{
		if (Step.Link != NoLink)
		{
			ArenaLadder.NextLink = Step.Link;
			break;
		}
		Walk.push_back(Step.Vertex);
	}
	const Fvector End = ArenaLadder.NextLink != NoLink ? ai().level_graph().vertex_position(Walk.back()) : detail().dest_position();
	detail().set_start_direction(Fvector().setHP(-m_body.current.yaw, 0.f));
	if (!detail().BuildArenaPath(Walk, Feet, End))
	{
		ReplanArenaRoute();
		return;
	}
	level_path().m_failed = false;
	level_path().m_actuality = true;
	level_path().m_current_index = u32(-1);
	level_path().select_intermediate_vertex();

	const u64 PhysicsStep = physics_world()->StepsNum();
	ArenaNextWaypoint = detail().path().size() > 1 ? 1 : 0;
	ArenaWalker.BeginWaypoint(PhysicsStep);
	LastArenaWalkSample = PhysicsStep;
	LastArenaWalkFeet = Feet;
	on_build_path();
	m_path_state = ePathStatePathVerification;
}

void CMovementManager::InvalidateArenaRoute()
{
	ArenaLadder.NextLink = NoLink;
	ArenaWalkFinished = false;
	level_path().make_inactual();
	detail().make_inactual();
	clear_path();
	if (m_path_type == ePathTypeLevelPath)
	{
		m_path_state = ePathStateBuildLevelPath;
	}
}

void CMovementManager::ReplanArenaRoute(Segment Blocked)
{
	ArenaRecovery.Reject(Device.dwTimeGlobal, Blocked);
	InvalidateArenaRoute();
}

void CMovementManager::CancelArenaLadder(CPHMovementControl* Physics)
{
	if (IsTraversing(ArenaLadder.Motor.State()))
	{
		if (Physics && Physics->CharacterExist())
		{
			Physics->ElevatorState()->EndDirectedClimb();
		}
		if (auto* Links = ai().ArenaLadderLinks(); Links && Links->Generation() == ArenaLadder.MapGeneration)
		{
			Links->Release(ArenaLadder.NextLink, object().ID());
		}
	}
	ArenaLadder = {};
	ArenaRecovery = {};
	ArenaWalkFinished = false;
}

bool CMovementManager::MoveArenaLadder(CPHMovementControl& Physics, Fvector& Position)
{
	const bool Traversing = IsTraversing(ArenaLadder.Motor.State());
	if (!Traversing && !ArenaLadder.Recovering &&
		(ArenaLadder.NextLink == NoLink || !ArenaWalkFinished || m_path_type != ePathTypeLevelPath ||
		m_path_state != ePathStatePathVerification || !level_path().actual() || !enabled()))
	{
		return false;
	}
	auto* Links = ai().ArenaLadderLinks();
	if (!Links || Links->Generation() != ArenaLadder.MapGeneration || !Physics.CanDriveArenaCharacter() ||
		object().animation_movement_controlled() || !enabled())
	{
		CancelArenaLadder(&Physics);
		ReplanArenaRoute();
		return false;
	}

	auto* Character = Physics.PHCharacter();
	auto* Elevator = Physics.ElevatorState();
	Character->GetPosition(Position);
	const u32 Now = Device.dwTimeGlobal;
	const u32 Link = ArenaLadder.NextLink;
	const u16 Owner = object().ID();
	const auto Generation = static_cast<unsigned long long>(ArenaLadder.MapGeneration);

	const auto Drive = [&](const Fvector& Velocity)
	{
		Physics.DriveLadder(Velocity);
		Physics.GetPosition(Position);
		m_speed = Velocity.magnitude();
		apply_collision_hit(&Physics);
	};
	const Fvector Stop = Fvector().set(0.f, 0.f, 0.f);

	if (ArenaLadder.Recovering)
	{
		if (physics_world()->StepsNum() > ArenaLadder.ReleasedOnStep && Character->HasGroundSupport())
		{
			ArenaLadder.Recovering = false;
		}
		Drive(Stop);
		return true;
	}

	const auto& Edge = Links->Link(Link);
	auto* Ladder = Links->Resolve(Link, Owner);
	if (!Traversing)
	{
		if (!Character->HasGroundSupport() || !Links->AtLanding(Edge.Entry, Position))
		{
			ReplanArenaRoute();
			return false;
		}
		const bool Up = Edge.Travel == Direction::Up;
		if (!Ladder || !Links->Acquire(Link, Owner))
		{
			ReplanArenaRoute(Segment::Ladder(Link));
			return false;
		}
		if (!Elevator->BeginDirectedClimb(*Ladder, Up))
		{
			Links->Release(Link, Owner);
			ReplanArenaRoute(Segment::Ladder(Link));
			return false;
		}

		Fvector Lower, Upper, Normal;
		Ladder->LowerPoint(Lower);
		Ladder->UpperPoint(Upper);
		Ladder->DDNorm(Normal);
		Normal.y = 0.f;
		Normal.normalize();
		ArenaLadder.Facing.set(Normal).invert();
		const float Offset = Character->FootRadius() + 0.025f;
		Lower.mad(Normal, Offset);
		Upper.mad(Normal, Offset);

		const auto& Grid = ai().level_graph();
		const auto LowerLanding = Grid.vertex_position(Up ? Edge.Entry : Edge.Exit);
		const auto UpperLanding = Grid.vertex_position(Up ? Edge.Exit : Edge.Entry);
		Lower.y = std::max(Lower.y, LowerLanding.y) + 0.10f;
		Upper.y = std::max(Upper.y, UpperLanding.y) + 0.20f;
		ArenaLadder.Mount = Up ? Lower : Upper;
		ArenaLadder.End = Up ? Upper : Lower;
		ArenaLadder.Exit = Up ? UpperLanding : LowerLanding;
		ArenaLadder.Exit.y = ArenaLadder.End.y;
		const float Distance = Position.distance_to(ArenaLadder.Mount) + Lower.distance_to(Upper) +
			ArenaLadder.End.distance_to(ArenaLadder.Exit);
		ArenaLadder.LadderRevision = Links->BindingRevision(Link);
		ArenaLadder.Motor.Begin(Now, 2500 + u32(2000.f * Distance / ClimbSpeed));
		Physics.EnableCharacter();
		Character->SetVelocity(Stop);
		clear_path();
		Msg("Arena ladder: generation=%llu stalker=%u link=%u %u->%u mount", Generation, u32(Owner), Link, Edge.Entry, Edge.Exit);
	}

	const auto Previous = ArenaLadder.Motor.State();
	const bool Settling = Previous == Phase::Settle;
	const auto Target = [&]() -> const Fvector&
	{
		switch (ArenaLadder.Motor.State())
		{
		case Phase::Mount: return ArenaLadder.Mount;
		case Phase::Climb: return ArenaLadder.End;
		default: return ArenaLadder.Exit;
		}
	};
	const float Remaining = Position.distance_to(Target());
	const bool AtTarget = Settling ? Links->AtLanding(Edge.Exit, Position) : Remaining <= EndTolerance;
	const bool Grounded = Settling && physics_world()->StepsNum() > ArenaLadder.ReleasedOnStep && Character->HasGroundSupport();
	const auto Current = ArenaLadder.Motor.Update(Now,
		Ladder && Links->BindingRevision(Link) == ArenaLadder.LadderRevision &&
			(Settling || Elevator->DirectedClimbActive()) && restrictions().accessible(Edge.Exit),
		AtTarget, Grounded, Remaining);

	if (Current == Phase::Complete || Current == Phase::Failed)
	{
		Elevator->EndDirectedClimb();
		Links->Release(Link, Owner);
		ArenaLadder.Motor = {};
		ArenaLadder.Recovering = Current == Phase::Failed;
		ArenaLadder.ReleasedOnStep = physics_world()->StepsNum();
		if (Current == Phase::Complete)
		{
			object().ai_location().level_vertex(Edge.Exit);
		}
		Msg("Arena ladder: generation=%llu stalker=%u link=%u %s", Generation, u32(Owner), Link,
			Current == Phase::Complete ? "grounded exit" : "failed");
		if (Current == Phase::Failed)
		{
			ReplanArenaRoute(Segment::Ladder(Link));
		}
		else
		{
			InvalidateArenaRoute();
		}
		Drive(Stop);
		return true;
	}

	if (Previous != Current)
	{
		if (Current == Phase::Climb)
		{
			Msg("Arena ladder: generation=%llu stalker=%u link=%u climb", Generation, u32(Owner), Link);
		}
		else if (Current == Phase::Dismount)
		{
			Msg("Arena ladder: generation=%llu stalker=%u link=%u dismount", Generation, u32(Owner), Link);
		}
		else if (Current == Phase::Settle)
		{
			Elevator->EndDirectedClimb();
			Character->SetVelocity(Stop);
			ArenaLadder.ReleasedOnStep = physics_world()->StepsNum();
		}
	}
	m_body.target.yaw = -ArenaLadder.Facing.getH();
	if (Current == Phase::Settle)
	{
		Drive(Stop);
	}
	else
	{
		Fvector Velocity;
		Velocity.sub(Target(), Position);
		const float Speed = std::min(ClimbSpeed, 5.f * Velocity.magnitude());
		Drive(Velocity.normalize_safe().mul(Speed));
	}
	return true;
}
