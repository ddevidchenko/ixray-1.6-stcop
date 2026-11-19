#include "../../../xrGame/Multiplayer/MapSyncWait.h"

#include <cstdio>

namespace MapSyncWaitTests
{
using Multiplayer::MapSyncStatus;
using Multiplayer::MapSyncWait;
using namespace std::chrono_literals;

static const char* StatusName(MapSyncStatus Status)
{
	switch (Status)
	{
	case MapSyncStatus::Waiting:
		return "waiting";
	case MapSyncStatus::Received:
		return "received";
	case MapSyncStatus::TimedOut:
		return "timed out";
	}
	return "unknown";
}

static bool ExpectStatus(const char* Name, MapSyncStatus Actual, MapSyncStatus Expected)
{
	if (Actual == Expected)
	{
		return true;
	}
	std::fprintf(stderr, "%s: expected %s, got %s\n", Name, StatusName(Expected), StatusName(Actual));
	return false;
}

struct WaitCase
{
	const char* Name;
	std::chrono::milliseconds Offset;
	bool ReplyReceived;
	bool DemoPlayback;
	MapSyncStatus Expected;
};

constexpr WaitCase Cases[] = {
	{"at request start", 0ms, false, false, MapSyncStatus::Waiting},
	{"one millisecond before deadline", 4999ms, false, false, MapSyncStatus::Waiting},
	{"missing reply at deadline", 5s, false, false, MapSyncStatus::TimedOut},
	{"first poll after a loading stall", 7s, false, false, MapSyncStatus::TimedOut},
	{"reply before deadline", 4999ms, true, false, MapSyncStatus::Received},
	{"reply observed at deadline", 5s, true, false, MapSyncStatus::Received},
	{"reply handled by a late poll", 7s, true, false, MapSyncStatus::Received},
	{"demo playback without a reply", 1min, false, true, MapSyncStatus::Waiting},
	{"demo playback with a reply", 1min, true, true, MapSyncStatus::Received},
};

static bool CheckRepeatedPolls(MapSyncWait::Clock::time_point Start)
{
	const MapSyncWait Wait(Start);

	for (int Poll = 0; Poll < 1001; ++Poll)
	{
		if (!ExpectStatus("repeated polls before deadline", Wait.Poll(Start + 4999ms, false, false), MapSyncStatus::Waiting))
		{
			return false;
		}
	}
	return ExpectStatus("deadline after repeated polls", Wait.Poll(Start + 5s, false, false), MapSyncStatus::TimedOut);
}

static bool CheckSeparateRequests(MapSyncWait::Clock::time_point Start)
{
	const MapSyncWait First(Start);
	const auto Restart = Start + 7s;
	const MapSyncWait Second(Restart);

	bool Passed = ExpectStatus("first request retains its deadline", First.Poll(Restart, false, false), MapSyncStatus::TimedOut);
	Passed &= ExpectStatus("new request has a fresh deadline", Second.Poll(Restart + 4999ms, false, false), MapSyncStatus::Waiting);
	Passed &= ExpectStatus("new request expires on its own deadline", Second.Poll(Restart + 5s, false, false), MapSyncStatus::TimedOut);
	return Passed;
}

bool Run()
{
	const auto Start = MapSyncWait::Clock::time_point{} + 37s;
	bool Passed = true;
	for (const WaitCase& Case : Cases)
	{
		const MapSyncWait Wait(Start);
		Passed &= ExpectStatus(Case.Name, Wait.Poll(Start + Case.Offset, Case.ReplyReceived, Case.DemoPlayback), Case.Expected);
	}
	Passed &= CheckRepeatedPolls(Start);
	Passed &= CheckSeparateRequests(Start);

	if (Passed)
	{
		std::puts("Map sync deadline checks passed");
	}
	return Passed;
}
}
