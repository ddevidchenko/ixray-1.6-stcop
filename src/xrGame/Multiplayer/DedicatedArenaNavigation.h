#pragma once

#include "../../xrCore/ScalarTypes.h"
#include "../../xrCore/_stl_extensions_nonalloc.h"

namespace Multiplayer::DedicatedArena
{
enum class Mode { HumanOnly, Bots, Invalid };
enum class Startup { Started, InvalidOptions, NavigationRejected, TransportRejected };

constexpr Mode ParseMode(bool DedicatedDeathmatch, xr_string_view Options)
{
	Mode Requested = Mode::HumanOnly;
	bool Seen = false;
	auto Separator = Options.find('/');
	while (Separator != xr_string_view::npos)
	{
		Options.remove_prefix(Separator + 1);
		Separator = Options.find('/');
		const auto Item = Options.substr(0, Separator);
		const auto Equals = Item.find('=');
		const auto Name = Item.substr(0, Equals);
		if (Name != "bots" && Name != "w1_stalker_navigation")
		{
			continue;
		}

		const auto Value = Equals == xr_string_view::npos ? xr_string_view{} : Item.substr(Equals + 1);
		if (Seen || (Value != "0" && Value != "1") || (Value == "1" && !DedicatedDeathmatch))
		{
			return Mode::Invalid;
		}
		Seen = true;
		Requested = Value == "1" ? Mode::Bots : Mode::HumanOnly;
	}
	return Requested;
}

template <class Prepare, class Connect>
Startup StartServer(Mode Requested, Prepare PrepareArena, Connect OpenListener)
{
	if (Requested == Mode::Invalid)
	{
		return Startup::InvalidOptions;
	}
	if (Requested == Mode::Bots && !PrepareArena())
	{
		return Startup::NavigationRejected;
	}
	return OpenListener() ? Startup::Started : Startup::TransportRejected;
}

// The engine drains its game-thread tasks between frames. Stopping closes
// admission immediately and keeps the graph alive for consumer destruction.
class Lifetime
{
	enum class State { Unloaded, Ready, Stopping };

	State Current = State::Unloaded;
	u64 LastGeneration = 0;

public:
	u64 NextGeneration() const { return LastGeneration + 1; }
	u64 Generation() const { return Loaded() ? LastGeneration : 0; }
	bool Loaded() const { return Current != State::Unloaded; }
	bool Ready() const { return Current == State::Ready; }

	void Publish()
	{
		LastGeneration = NextGeneration();
		Current = State::Ready;
	}

	bool Stop()
	{
		if (!Ready())
		{
			return false;
		}
		Current = State::Stopping;
		return true;
	}

	void Release() { Current = State::Unloaded; }

	// A zero stamp denotes the legacy queue, which has no arena graph.
	bool CanRun(u64 ScheduledGeneration) const
	{
		return Loaded() ? Ready() && LastGeneration == ScheduledGeneration : ScheduledGeneration == 0;
	}
};
}
