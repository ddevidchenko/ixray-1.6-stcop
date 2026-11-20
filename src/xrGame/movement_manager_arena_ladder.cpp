#include "StdAfx.h"
#include "movement_manager.h"
#include "Multiplayer/ArenaLadderNavigation.h"
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
constexpr u32 RetryDelay = 500;
constexpr u32 FailedLadderDelay = 2500;
constexpr float ClimbSpeed = 1.7f;
constexpr float EndTolerance = 0.12f;

bool IsTraversing(Multiplayer::ArenaLadders::Phase Phase)
{
	using enum Multiplayer::ArenaLadders::Phase;
	return Phase == Mount || Phase == Climb || Phase == Dismount || Phase == Settle;
}
}

using namespace Multiplayer::ArenaLadders;

bool CMovementManager::UsesArenaLadderRouting() const
{
	const auto* Links = ai().ArenaLadderLinks();
	return Links && Links->HasLinks() && m_object->cast_stalker() && m_object->Local() && OnServer();
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
		ArenaLadder = {};
		ArenaLadder.MapGeneration = Links.Generation();
	}
	const auto& Grid = ai().level_graph();
	const u32 Start = object().ai_location().level_vertex_id();
	const u32 Goal = level_dest_vertex_id();
	const u32 Now = Device.dwTimeGlobal;

	if (level_path().actual() && m_path_state == ePathStatePathVerification)
	{
		if (ArenaLadder.NextLink != NoLink)
		{
			if (!Links.Resolve(ArenaLadder.NextLink, object().ID()))
			{
				ReplanArenaLadder(NoLink);
			}
			else if (detail().actual() || Links.AtLanding(WalkDestinationVertex(), object().Position()))
			{
				return;
			}
		}
		else if (detail().actual())
		{
			if (detail().completed(object().Position(), !detail().state_patrol_path()))
			{
				m_path_state = ePathStatePathCompleted;
			}
			return;
		}
	}
	if (level_path().actual() && detail().actual() && m_path_state == ePathStatePathCompleted)
	{
		return;
	}

	m_path_state = ePathStateBuildLevelPath;
	if (ArenaLadder.PlanDelayed && Now - ArenaLadder.LastPlan < RetryDelay)
	{
		return;
	}
	if (Now - ArenaLadder.BlockedAt >= FailedLadderDelay)
	{
		ArenaLadder.BlockedLink = NoLink;
	}
	ArenaLadder.NextLink = NoLink;
	level_path().make_inactual();
	detail().make_inactual();

	xr_vector<Step> Route;
	if (!Links.Plan(restrictions(), object().ID(), Start, Goal, ArenaPathNodes, ArenaLadder.BlockedLink, Route))
	{
		ArenaLadder.PlanDelayed = true;
		ArenaLadder.LastPlan = Now;
		return;
	}

	// Give the detail path builder only the walking prefix. The next search
	// starts at the actual grounded exit and sees the current tactical goal.
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
	level_path().m_failed = false;
	level_path().m_actuality = true;
	level_path().m_current_index = u32(-1);
	level_path().select_intermediate_vertex();
	const bool HasLadder = ArenaLadder.NextLink != NoLink;
	if (HasLadder && Links.AtLanding(Walk.back(), object().Position()))
	{
		clear_path();
	}
	else
	{
		detail().set_state_patrol_path(!HasLadder && extrapolate_path());
		detail().set_start_position(object().Position());
		detail().set_start_direction(Fvector().setHP(-m_body.current.yaw, 0.f));
		const auto End = Grid.vertex_position(Walk.back());
		detail().build_path(Walk, level_path().intermediate_index(), HasLadder ? &End : nullptr);
		if (detail().failed() || !detail().actual())
		{
			ReplanArenaLadder(ArenaLadder.NextLink);
			return;
		}
		on_build_path();
	}
	ArenaLadder.PlanDelayed = false;
	m_path_state = ePathStatePathVerification;
}

void CMovementManager::ReplanArenaLadder(u32 BlockedLink)
{
	if (BlockedLink != NoLink)
	{
		ArenaLadder.BlockedLink = BlockedLink;
		ArenaLadder.BlockedAt = Device.dwTimeGlobal;
	}
	ArenaLadder.NextLink = NoLink;
	ArenaLadder.PlanDelayed = true;
	ArenaLadder.LastPlan = Device.dwTimeGlobal;
	level_path().make_inactual();
	detail().make_inactual();
	clear_path();
	if (m_path_type == ePathTypeLevelPath)
	{
		m_path_state = ePathStateBuildLevelPath;
	}
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
}

bool CMovementManager::MoveArenaLadder(CPHMovementControl& Physics, Fvector& Position)
{
	const bool Traversing = IsTraversing(ArenaLadder.Motor.State());
	if (!Traversing && !ArenaLadder.Recovering &&
		(ArenaLadder.NextLink == NoLink || !UsesArenaLadderRouting() || m_path_type != ePathTypeLevelPath ||
		m_path_state != ePathStatePathVerification || !level_path().actual() || !enabled()))
	{
		return false;
	}
	auto* Links = ai().ArenaLadderLinks();
	if (!Links || Links->Generation() != ArenaLadder.MapGeneration || !Physics.CharacterExist() ||
		object().animation_movement_controlled() || !enabled())
	{
		CancelArenaLadder(&Physics);
		ReplanArenaLadder(NoLink);
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
		if (!Links->AtLanding(Edge.Entry, Position))
		{
			if (detail().completed(Position))
			{
				ReplanArenaLadder(Link);
			}
			return false;
		}
		const bool Up = Edge.Travel == Direction::Up;
		if (!Ladder || !Links->Acquire(Link, Owner))
		{
			ReplanArenaLadder(NoLink);
			return false;
		}
		if (!Elevator->BeginDirectedClimb(*Ladder, Up))
		{
			Links->Release(Link, Owner);
			ReplanArenaLadder(Link);
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
		ReplanArenaLadder(Current == Phase::Failed ? Link : NoLink);
		if (Current == Phase::Complete)
		{
			ArenaLadder.PlanDelayed = false;
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
