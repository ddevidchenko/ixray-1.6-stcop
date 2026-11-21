#include "StdAfx.h"
#include "ai_stalker.h"
#include "../../CharacterPhysicsSupport.h"
#include "../../Level.h"
#include "../../PHMovementControl.h"
#include "../../stalker_movement_manager_smart_cover.h"
#include "../../../Include/xrRender/Kinematics.h"
#include "../../../Include/xrRender/KinematicsAnimated.h"
#include "../../../Include/xrRender/RenderVisual.h"

namespace
{
constexpr u8 ArenaClimbChannel = 1;
constexpr u8 ArenaClimbChannelMask = 1 << ArenaClimbChannel;
constexpr u32 ArenaPoseInterpolationDelay = 100;
constexpr xr_array<const char*, 9> ArenaClimbStageNames{
	"ground", "mount_up", "up", "exit_up", "settle_up",
	"mount_down", "down", "exit_down", "settle_down"
};
}

void CAI_Stalker::PublishArenaPose()
{
	const xrSRWLockGuard Guard(ArenaPoseLock);
	if (ArenaPublishedPose.Life)
	{
		using namespace Multiplayer::ArenaStalkerPose;
		const Snapshot State{ArenaPublishedPose.Life, Level().timeServer(), Position().x, Position().y, Position().z,
			angle_normalize_signed(movement().m_body.current.yaw), movement().ArenaClimbPose()};
		if (Valid(State))
		{
			ArenaPublishedPose = State;
		}
	}
}

void CAI_Stalker::SetArenaFacing(float Yaw)
{
	auto& Move = movement();
	Move.m_body.current.yaw = Move.m_body.target.yaw = Move.m_head.current.yaw = Move.m_head.target.yaw = Yaw;
	Move.m_body.current.pitch = Move.m_body.target.pitch = Move.m_head.current.pitch = Move.m_head.target.pitch = 0.f;
	const Fvector Feet = Position();
	XFORM().setHPB(-Yaw, 0.f, 0.f);
	Position().set(Feet);
}

void CAI_Stalker::UpdateArenaReplica()
{
	if (animation_movement())
	{
		destroy_anim_mov_ctrl();
	}
	const auto State = ArenaReplica.Sample(Level().timeServer() - ArenaPoseInterpolationDelay);
	if (!State)
	{
		ClearArenaClimbPose();
		return;
	}

	Position().set(State->X, State->Y, State->Z);
	SetArenaFacing(State->Yaw);
	auto* Physics = character_physics_support()->movement();
	if (Physics->CharacterExist())
	{
		Physics->SetNonInteractive(true);
		Physics->SetPosition(Position());
	}
	ApplyArenaClimbPose(State->Ladder);
}

void CAI_Stalker::ClearArenaClimbPose()
{
	auto* CurrentVisual = Visual();
	auto* Animated = CurrentVisual && CurrentVisual == ArenaClimbVisual ? CurrentVisual->dcast_PKinematicsAnimated() : nullptr;
	if (Animated)
	{
		for (u16 Part = 0; Part < MAX_PARTS; ++Part)
		{
			Animated->LL_CloseCycle(Part, ArenaClimbChannelMask);
		}
		Animated->dcast_PKinematics()->CalculateBones_Invalidate();
	}
	if (ArenaClimbStage != Multiplayer::ArenaStalkerPose::Stage::Ground && Core.CheckParams("-mp_route_trace"))
	{
		Msg("Arena stalker pose: stalker=%u %s stage=ground", u32(ID()), OnServer() ? "authority" : "replica");
	}
	ArenaClimbVisual = nullptr;
	ArenaClimbStage = Multiplayer::ArenaStalkerPose::Stage::Ground;
	ArenaClimbMotion = {};
}

void CAI_Stalker::ApplyArenaClimbPose(Multiplayer::ArenaStalkerPose::Pose Pose)
{
	using namespace Multiplayer::ArenaStalkerPose;
	if (Pose.Action == Stage::Ground)
	{
		ClearArenaClimbPose();
		return;
	}
	if (animation_movement())
	{
		destroy_anim_mov_ctrl();
	}

	auto* CurrentVisual = Visual();
	auto* Animated = CurrentVisual ? CurrentVisual->dcast_PKinematicsAnimated() : nullptr;
	if (ArenaClimbVisual != CurrentVisual)
	{
		ArenaClimbVisual = nullptr;
		ArenaClimbMotion = {};
	}
	const Clip Selected = SelectClip(Pose, [&](xr_string_view Name)
	{
		return Animated && Animated->ID_Cycle_Safe(Name.data()).valid();
	});
	const MotionID Motion = Selected.Name.empty() ? MotionID{} : Animated->ID_Cycle_Safe(Selected.Name.data());
	if ((ArenaClimbStage != Pose.Action || Motion != ArenaClimbMotion) && Core.CheckParams("-mp_route_trace"))
	{
		Msg("Arena stalker pose: stalker=%u %s stage=%s clip=%s", u32(ID()), OnServer() ? "authority" : "replica",
			ArenaClimbStageNames[u32(Pose.Action)], Selected.Name.empty() ? "missing" : Selected.Name.data());
	}
	ArenaClimbStage = Pose.Action;
	if (!Animated)
	{
		return;
	}

	// Channel 1 supplies the full-body pose over the regular locomotion tracks.
	bool Changed = false;
	for (u16 Part = 0; Part < MAX_PARTS; ++Part)
	{
		if (!Animated->partitions().part(Part).Name.size())
		{
			continue;
		}
		if (Motion != ArenaClimbMotion)
		{
			Animated->LL_CloseCycle(Part, ArenaClimbChannelMask);
			Changed = true;
		}
		if (!Motion.valid())
		{
			continue;
		}
		CBlend* Cycle = nullptr;
		for (u32 Index = 0; Index < Animated->LL_PartBlendsCount(Part); ++Index)
		{
			auto* Blend = Animated->LL_PartBlend(Part, Index);
			if (Blend && Blend->channel == ArenaClimbChannel && Blend->motionID == Motion)
			{
				Cycle = Blend;
				break;
			}
		}
		if (!Cycle)
		{
			Cycle = Animated->LL_PlayCycle(Part, Motion, false, 0.f, 0.f, 0.f, false, nullptr, nullptr, ArenaClimbChannel);
			if (!Cycle)
			{
				continue;
			}
			Cycle->notifies.clear();
			Changed = true;
		}

		const float Time = SampleTime(Pose, Selected, Cycle->timeTotal, SAMPLE_SPF);
		if (Cycle->timeCurrent != Time)
		{
			Cycle->timeCurrent = Time;
			Changed = true;
		}
	}
	ArenaClimbVisual = Motion.valid() ? CurrentVisual : nullptr;
	ArenaClimbMotion = Motion;
	if (Changed)
	{
		Animated->dcast_PKinematics()->CalculateBones_Invalidate();
	}
}
