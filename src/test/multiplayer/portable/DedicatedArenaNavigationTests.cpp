#include "../../../xrGame/Multiplayer/DedicatedArenaNavigation.h"

#include <cstdio>
#include <string>
#include <vector>

namespace DedicatedArenaNavigationTests
{
using namespace Multiplayer::DedicatedArena;

static bool Expect(const char* Case, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Case);
	}
	return Passed;
}

static bool CheckMode()
{
	bool Ok = Expect("ordinary dedicated DM remains human only",
		ParseMode(true, "arena/deathmatch/portsv=2302") == Mode::HumanOnly);
	Ok &= Expect("dedicated DM accepts explicit bots and explicit human-only mode",
		ParseMode(true, "arena/deathmatch/bots=1/public=0") == Mode::Bots &&
		ParseMode(true, "arena/deathmatch/bots=0") == Mode::HumanOnly);
	Ok &= Expect("existing W1 scripts still request required navigation",
		ParseMode(true, "arena/deathmatch/w1_stalker_navigation=1") == Mode::Bots &&
		ParseMode(true, "arena/deathmatch/w1_stalker_navigation=0") == Mode::HumanOnly);
	Ok &= Expect("malformed or ambiguous bot options cannot silently switch to human-only",
		ParseMode(true, "arena/deathmatch/bots=10") == Mode::Invalid &&
		ParseMode(true, "arena/deathmatch/bots") == Mode::Invalid &&
		ParseMode(true, "arena/deathmatch/bots=0/w1_stalker_navigation=1") == Mode::Invalid &&
		ParseMode(true, "arena/deathmatch/bots=1/bots=1") == Mode::Invalid);
	Ok &= Expect("a non-dedicated or non-DM server cannot enable arena bots",
		ParseMode(false, "arena/deathmatch/bots=1") == Mode::Invalid &&
		ParseMode(false, "arena/artefacthunt/w1_stalker_navigation=1") == Mode::Invalid);
	Ok &= Expect("map names and unrelated options never select bot mode",
		ParseMode(true, "bots=1/deathmatch/ignorebots=1/note=w1_stalker_navigation=1") == Mode::HumanOnly);
	return Ok;
}

static bool CheckStartup()
{
	std::string Calls;
	const auto Start = [&](Mode Requested, bool Valid, bool Listener)
	{
		Calls.clear();
		return StartServer(Requested, [&]
		{
			Calls += 'N';
			return Valid;
		}, [&]
		{
			Calls += 'L';
			return Listener;
		});
	};

	bool Ok = Expect("authoritative arena validation runs before the network can accept peers",
		Start(Mode::Bots, true, true) == Startup::Started && Calls == "NL");
	Ok &= Expect("missing or corrupt nav cannot start the listener",
		Start(Mode::Bots, false, true) == Startup::NavigationRejected && Calls == "N");
	Ok &= Expect("human-only DM never consults its optional arena data",
		Start(Mode::HumanOnly, false, true) == Startup::Started && Calls == "L");
	Ok &= Expect("malformed options leave both navigation and listener untouched",
		Start(Mode::Invalid, true, true) == Startup::InvalidOptions && Calls.empty());
	Ok &= Expect("listener failure remains distinguishable after valid navigation",
		Start(Mode::Bots, true, false) == Startup::TransportRejected && Calls == "NL");
	return Ok;
}

static bool CheckLifetime()
{
	Lifetime Arena;
	bool Ok = Expect("legacy work is permitted when there is no arena",
		!Arena.Loaded() && !Arena.Ready() && Arena.Generation() == 0 && Arena.CanRun(0));
	const auto First = Arena.NextGeneration();
	Arena.Publish();
	Ok &= Expect("publication opens only the new generation",
		First != 0 && Arena.Loaded() && Arena.Ready() && Arena.Generation() == First && Arena.CanRun(First) && !Arena.CanRun(0));

	std::vector<u64> Reads;
	const auto RunDeferred = [&](u64 Ticket)
	{
		if (Arena.CanRun(Ticket))
		{
			Reads.push_back(Ticket);
		}
	};

	Ok &= Expect("stop closes the queue while existing consumers retain their teardown context",
		Arena.Stop() && !Arena.Stop() && Arena.Loaded() && !Arena.Ready());
	RunDeferred(First);
	RunDeferred(0);
	Ok &= Expect("neither arena nor legacy builders can read while an arena closes", Reads.empty());

	Arena.Release();
	RunDeferred(First);
	Ok &= Expect("a freed generation stays rejected in the gap between maps",
		!Arena.Loaded() && !Arena.Ready() && Arena.Generation() == 0 && Reads.empty());
	const auto Replacement = Arena.NextGeneration();
	Arena.Publish();
	RunDeferred(First);
	RunDeferred(Replacement);
	Ok &= Expect("reloading the same map cannot run old callbacks against reused node IDs",
		Replacement != First && Reads == std::vector<u64>{Replacement});

	Arena.Release();
	RunDeferred(Replacement);
	Ok &= Expect("direct teardown invalidates pending work even without a graceful stop",
		Reads == std::vector<u64>{Replacement} && Arena.CanRun(0));
	return Ok;
}

bool Run()
{
	const bool Modes = CheckMode();
	const bool Startup = CheckStartup();
	const bool Reload = CheckLifetime();
	if (Modes && Startup && Reload)
	{
		std::puts("Dedicated arena navigation checks passed");
		return true;
	}
	return false;
}
}
