#pragma once

#include "../../xrCore/ScalarTypes.h"
#include "../../xrCore/_stl_extensions_nonalloc.h"

#include <limits>

namespace Multiplayer::ArenaLadders
{
constexpr u32 NoLink = u32(-1);
enum class Direction { Up, Down };

struct Record
{
	xr_string_view Name;
	Direction Travel;
	u32 Entry;
	u32 Exit;
	f32 Cost;
};

struct Step
{
	u32 Vertex;
	u32 Link = NoLink;
	bool operator==(const Step&) const = default;
};

enum class RouteResult { Found, Unreachable, BudgetExceeded };

class Reservation
{
	u32 Current = NoLink;

public:
	bool TryAcquire(u32 Owner)
	{
		if (Owner == NoLink || !Available(Owner))
		{
			return false;
		}
		Current = Owner;
		return true;
	}

	void Release(u32 Owner)
	{
		if (Current == Owner)
		{
			Current = NoLink;
		}
	}

	bool Available(u32 Owner) const { return Current == NoLink || Current == Owner; }
};

enum class Phase { Idle, Mount, Climb, Dismount, Settle, Complete, Failed };

class Traversal
{
	Phase Current = Phase::Idle;
	u32 Start = 0;
	u32 Duration = 0;
	u32 LastProgress = 0;
	f32 BestRemaining = std::numeric_limits<f32>::infinity();

public:
	void Begin(u32 Now, u32 Timeout)
	{
		Current = Phase::Mount;
		Start = Now;
		Duration = Timeout;
		LastProgress = Now;
		BestRemaining = std::numeric_limits<f32>::infinity();
	}

	Phase Update(u32 Now, bool Available, bool Reached, bool Grounded, f32 Remaining = 0.f)
	{
		if (Current == Phase::Idle || Current == Phase::Complete || Current == Phase::Failed)
		{
			return Current;
		}
		if (Remaining + 0.08f < BestRemaining)
		{
			BestRemaining = Remaining;
			LastProgress = Now;
		}
		const auto Previous = Current;
		if (!Available || Now - Start >= Duration || Now - LastProgress >= 2000)
		{
			Current = Phase::Failed;
		}
		else if (Reached)
		{
			switch (Current)
			{
			case Phase::Mount: Current = Phase::Climb; break;
			case Phase::Climb: Current = Phase::Dismount; break;
			case Phase::Dismount: Current = Phase::Settle; break;
			case Phase::Settle:
				if (Grounded)
				{
					Current = Phase::Complete;
				}
				break;
			default: break;
			}
		}
		if (Current != Previous)
		{
			LastProgress = Now;
			BestRemaining = std::numeric_limits<f32>::infinity();
		}
		return Current;
	}

	Phase State() const { return Current; }
};
}
