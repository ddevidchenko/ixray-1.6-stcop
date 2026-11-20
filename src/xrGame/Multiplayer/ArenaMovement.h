#pragma once

#include "../../xrCore/ScalarTypes.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Multiplayer::ArenaMovement
{
struct Point
{
	f32 X, Y, Z;
};

struct BodySample
{
	u64 Step;
	Point Feet;
	bool Grounded;
};

constexpr f32 WalkVerticalTolerance = 0.23f;

inline f32 SteeringSpeed(f32 Requested, f32 Distance, f32 FootRadius, f32 FixedStep)
{
	if (!(FixedStep > 0.f))
	{
		return 0.f;
	}
	return std::min({Requested, Distance * 6.f, FootRadius * 0.5f / FixedStep});
}

enum class WalkState { Stopped, Walking, Reached, Blocked };

class WalkController
{
	static constexpr f32 HorizontalTolerance = 0.14f;
	static constexpr f32 MinimumProgress = 0.03f;
	static constexpr u32 StallTime = 1500;

	u64 LastStep = 0;
	u32 LastProgress = 0;
	f32 BestRemaining = 0;
	bool Tracking = false;

public:
	void BeginWaypoint(u64 Step)
	{
		LastStep = Step;
		Tracking = false;
	}

	WalkState Observe(u32 Now, const BodySample& Body, Point Target, bool WantsMovement, bool DoorReady)
	{
		const bool Fresh = Body.Step > LastStep;
		if (Fresh)
		{
			LastStep = Body.Step;
		}

		const f32 Dx = Target.X - Body.Feet.X;
		const f32 Dy = Target.Y - Body.Feet.Y;
		const f32 Dz = Target.Z - Body.Feet.Z;
		const f32 Horizontal = Dx * Dx + Dz * Dz;
		if (Fresh && Body.Grounded && Horizontal <= HorizontalTolerance * HorizontalTolerance &&
			std::abs(Dy) <= WalkVerticalTolerance)
		{
			return WalkState::Reached;
		}
		if (!WantsMovement)
		{
			Tracking = false;
			return WalkState::Stopped;
		}

		const f32 Remaining = std::sqrt(Horizontal + Dy * Dy);
		if (!Tracking || (Fresh && Body.Grounded && Remaining + MinimumProgress < BestRemaining))
		{
			Tracking = true;
			LastProgress = Now;
			BestRemaining = Remaining;
		}
		if (Fresh && Now - LastProgress >= StallTime)
		{
			return WalkState::Blocked;
		}
		return DoorReady ? WalkState::Walking : WalkState::Stopped;
	}
};

struct Segment
{
	static constexpr u32 Invalid = u32(-1);

	u32 From = Invalid;
	u32 To = Invalid;
	u32 Link = Invalid;

	static Segment Walk(u32 From, u32 To) { return From != To ? Segment{From, To} : Segment{}; }
	static Segment Ladder(u32 Link) { return {Invalid, Invalid, Link}; }
	bool operator==(const Segment&) const = default;
};

class Recovery
{
	static constexpr u32 RetryDelay = 500;
	static constexpr u32 BlockDuration = 2500;

	struct Exclusion
	{
		Segment Blocked;
		u32 Since = 0;

		bool Active(u32 Now) const { return Blocked != Segment{} && Now - Since < BlockDuration; }
	};

	std::array<Exclusion, 4> Exclusions{};
	u64 Generation = 0;
	u32 Goal = Segment::Invalid;
	u32 LastFailure = 0;
	bool Delayed = false;

public:
	void SetRoute(u64 MapGeneration, u32 Destination)
	{
		if (MapGeneration != Generation || Destination != Goal)
		{
			*this = {};
			Generation = MapGeneration;
			Goal = Destination;
		}
	}

	void Reject(u32 Now, Segment Blocked)
	{
		Delayed = true;
		LastFailure = Now;
		if (Blocked == Segment{})
		{
			return;
		}
		auto* Oldest = &Exclusions.front();
		for (auto& Entry : Exclusions)
		{
			if (Entry.Blocked == Blocked)
			{
				Entry.Since = Now;
				return;
			}
			if (!Entry.Active(Now) || (Oldest->Active(Now) && Now - Entry.Since > Now - Oldest->Since))
			{
				Oldest = &Entry;
			}
		}
		*Oldest = {Blocked, Now};
	}

	bool CanPlan(u32 Now)
	{
		if (Delayed && Now - LastFailure < RetryDelay)
		{
			return false;
		}
		Delayed = false;
		for (auto& Entry : Exclusions)
		{
			if (!Entry.Active(Now))
			{
				Entry = {};
			}
		}
		return true;
	}

	bool AvoidsWalk(u32 Now, u32 From, u32 To) const
	{
		for (const auto& Entry : Exclusions)
		{
			if (Entry.Active(Now) && Entry.Blocked.From == From && Entry.Blocked.To == To)
			{
				return true;
			}
		}
		return false;
	}

	template <class SameLadder>
	bool AvoidsLadder(u32 Now, u32 Link, SameLadder Same) const
	{
		for (const auto& Entry : Exclusions)
		{
			if (Entry.Active(Now) && Entry.Blocked.Link != Segment::Invalid && Same(Entry.Blocked.Link, Link))
			{
				return true;
			}
		}
		return false;
	}
};
}
