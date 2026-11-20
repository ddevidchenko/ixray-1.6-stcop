#include "StdAfx.h"
#include "movement_manager.h"
#include "Creature.h"
#include "PHMovementControl.h"
#include "detail_path_manager.h"
#include "level_path_manager.h"
#include "../xrPhysics/IPHWorld.h"
#include "../xrPhysics/PHCharacter.h"
#include "../xrPhysics/PhysicsCommon.h"

void CMovementManager::MoveArenaWalk(CPHMovementControl& Physics, Fvector& Position)
{
	using namespace Multiplayer::ArenaMovement;

	m_speed = 0.f;
	Position = object().Position();
	if (!Physics.CanDriveArenaCharacter() || object().animation_movement_controlled())
	{
		return;
	}

	auto* Character = Physics.PHCharacter();
	Fvector Feet;
	Character->GetPosition(Feet);
	const u64 Step = physics_world()->StepsNum();
	if (Physics.PhysicsOnlyMode())
	{
		ArenaWalker.BeginWaypoint(Step);
		Position = Feet;
		return;
	}

	const auto& Path = detail().path();
	Fvector Velocity = Fvector().set(0.f, 0.f, 0.f);
	if (enabled() && actual() && m_path_type == ePathTypeLevelPath && m_path_state == ePathStatePathVerification &&
		level_path().actual() && detail().actual() && !ArenaWalkFinished && ArenaNextWaypoint < Path.size())
	{
		const float Speed = std::abs(old_desirable_speed());
		const bool WantsMovement = Speed > EPS_L;
		const bool DoorReady = !WantsMovement || ArenaDoorReady(Speed);
		const auto& Target = Path[ArenaNextWaypoint].position;
		const BodySample Sample{Step, {Feet.x, Feet.y, Feet.z}, Character->HasGroundSupport()};
		const auto State = ArenaWalker.Observe(Device.dwTimeGlobal, Sample, {Target.x, Target.y, Target.z}, WantsMovement, DoorReady);

		if (Step != LastArenaWalkSample)
		{
			if (Sample.Grounded && Feet.distance_to_xz_sqr(LastArenaWalkFeet) > _sqr(EPS_L))
			{
				TraceArenaWalk(false);
			}
			LastArenaWalkFeet = Feet;
			LastArenaWalkSample = Step;
		}

		if (State == WalkState::Blocked)
		{
			const u32 From = Path[detail().curr_travel_point_index()].vertex_id;
			ReplanArenaRoute(Segment::Walk(From, Path[ArenaNextWaypoint].vertex_id));
		}
		else if (State == WalkState::Reached)
		{
			const u32 Previous = detail().curr_travel_point_index();
			detail().m_current_travel_point = ArenaNextWaypoint++;
			if (Previous != detail().m_current_travel_point)
			{
				on_travel_point_change(Previous);
			}
			ArenaWalker.BeginWaypoint(Step);
			ArenaWalkFinished = ArenaNextWaypoint == Path.size();
			if (ArenaWalkFinished && ArenaLadder.NextLink == Multiplayer::ArenaLadders::NoLink)
			{
				m_path_state = ePathStatePathCompleted;
			}
		}
		const bool Continue = State == WalkState::Reached && !ArenaWalkFinished && WantsMovement &&
			ArenaDoorReady(Speed);
		if (State == WalkState::Walking || Continue)
		{
			Velocity.sub(Path[ArenaNextWaypoint].position, Feet);
			Velocity.y = 0.f;
			const float Distance = Velocity.magnitude();
			if (Distance > EPS_S)
			{
				const float ApproachSpeed = SteeringSpeed(Speed, Distance, Character->FootRadius(), fixed_step);
				Velocity.mul(ApproachSpeed / Distance);
				m_speed = ApproachSpeed;
			}
		}
	}
	else
	{
		ArenaWalker.BeginWaypoint(Step);
	}

	// The world step applies gravity and static and dynamic collision to this steering request.
	Physics.DriveArenaCharacter(Velocity);
	Physics.GetPosition(Position);
	apply_collision_hit(&Physics);
}
