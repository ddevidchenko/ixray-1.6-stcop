#include "LegacyClockSync.h"

#include <cstdio>
#include <limits>

namespace LegacyClockSyncTests
{
using Multiplayer::ClockProbeResult;
using Multiplayer::LegacyClockSync;
using Multiplayer::LegacyClockSyncStatus;
using namespace std::chrono_literals;

static bool Expect(const char* Failure, bool Condition)
{
	if (!Condition)
	{
		std::fprintf(stderr, "%s\n", Failure);
	}
	return Condition;
}

static bool CheckMatchingReply()
{
	LegacyClockSync Sync;
	constexpr LegacyClockSync::Clock::time_point Start{};
	Sync.Start(Start);

	bool Passed = Expect("starting an attempt waits for a probe", Sync.GetStatus() == LegacyClockSyncStatus::Pending);
	Sync.RecordReply(Start, 1000, 5000, 1001);
	Passed &= Expect("an unsolicited reply cannot suppress the first probe", Sync.Poll(Start, 1000) == 1000u);
	Sync.CompleteProbe(ClockProbeResult::Sent);
	Sync.RecordReply(Start + 10ms, 999, 2000, 1010);
	Passed &= Expect("an unrelated echo does not satisfy or retransmit an outstanding probe",
		!Sync.Poll(Start + 10ms, 1010) && Sync.GetStatus() == LegacyClockSyncStatus::Pending && !Sync.GetOffset());

	Sync.RecordReply(Start + 19ms, 1000, 1025, 1019);
	Sync.RecordReply(Start + 20ms, 1000, 4000, 1020);
	Passed &= Expect("the receive path records the reply while the owner publishes readiness",
		Sync.GetStatus() == LegacyClockSyncStatus::Pending && !Sync.GetOffset());
	Passed &= Expect("the first matching reply sets an offset using half the elapsed ping",
		!Sync.Poll(Start + 30ms, 1030) && Sync.GetStatus() == LegacyClockSyncStatus::Ready && Sync.GetOffset() == 15);
	return Passed;
}

static bool CheckDeadlines()
{
	LegacyClockSync Sync;
	constexpr LegacyClockSync::Clock::time_point Start{};
	Sync.Start(Start);
	bool Passed = Expect("a backlogged transport gets one probe opportunity in a turn", Sync.Poll(Start, 1000) == 1000u);
	Sync.CompleteProbe(ClockProbeResult::Deferred);
	Passed &= Expect("a backlogged probe retries later without resetting the send deadline",
		Sync.Poll(Start + 4999ms, 5999) == 5999u);
	Sync.RecordReply(Start + 4999ms, 5999, 6100, 5999);
	Sync.CompleteProbe(ClockProbeResult::Deferred);
	Passed &= Expect("backlog through the send deadline fails and cannot publish a reply to an unsent probe",
		!Sync.Poll(Start + 5s, 6000) && Sync.GetStatus() == LegacyClockSyncStatus::Failed && !Sync.GetOffset());

	Sync.Start(Start);
	Passed &= Expect("a probe sent just before the send deadline gets the full reply window", Sync.Poll(Start + 4999ms, 5999) == 5999u);
	Sync.CompleteProbe(ClockProbeResult::Sent);
	Passed &= Expect("a lost reply holds startup until the reply deadline",
		!Sync.Poll(Start + 9998ms, 10998) && Sync.GetStatus() == LegacyClockSyncStatus::Pending);
	Sync.RecordReply(Start + 9999ms, 5999, 14000, 10999);
	Passed &= Expect("a reply at the deadline falls back to the existing offset",
		!Sync.Poll(Start + 9999ms, 10999) && Sync.GetStatus() == LegacyClockSyncStatus::Ready && !Sync.GetOffset());
	return Passed;
}

static bool CheckCancellationAndRestart()
{
	LegacyClockSync Sync;
	constexpr LegacyClockSync::Clock::time_point Start{};
	Sync.Start(Start);
	bool Passed = Expect("starting makes a probe available", Sync.Poll(Start, 1000) == 1000u);
	Sync.CompleteProbe(ClockProbeResult::Sent);
	Sync.RecordReply(Start + 1ms, 1000, 1040, 1001);
	Sync.Cancel();
	Sync.RecordReply(Start + 1ms, 1000, 1050, 1001);
	Passed &= Expect("cancel drops a queued reply and future polls do no work",
		!Sync.Poll(Start + 1ms, 1001) && Sync.GetStatus() == LegacyClockSyncStatus::Idle && !Sync.GetOffset());

	Sync.Start(Start + 1ms);
	Passed &= Expect("a restart in the same wire millisecond cannot reuse the previous probe",
		!Sync.Poll(Start + 1ms, 1000) && Sync.GetStatus() == LegacyClockSyncStatus::Pending);
	Passed &= Expect("the restart can use the next wire millisecond", Sync.Poll(Start + 2ms, 1001) == 1001u);
	Sync.CompleteProbe(ClockProbeResult::Sent);
	Sync.RecordReply(Start + 3ms, 1000, 1400, 1002);
	Passed &= Expect("a late reply from the previous attempt does not affect the current one",
		!Sync.Poll(Start + 3ms, 1002) && Sync.GetStatus() == LegacyClockSyncStatus::Pending);

	Sync.Start(Start + 4ms);
	Passed &= Expect("a new attempt may send after an outstanding probe was superseded", Sync.Poll(Start + 4ms, 1003) == 1003u);
	Sync.CompleteProbe(ClockProbeResult::Failed);
	Passed &= Expect("a rejected send ends the attempt immediately",
		Sync.GetStatus() == LegacyClockSyncStatus::Failed && !Sync.Poll(Start + 5ms, 1004) && !Sync.GetOffset());
	return Passed;
}

static bool CheckReplyDuringSendAcrossWrap()
{
	LegacyClockSync Sync;
	constexpr LegacyClockSync::Clock::time_point Start{};
	constexpr u32 SendTime = std::numeric_limits<u32>::max() - 3;
	Sync.Start(Start);
	bool Passed = Expect("wire time near wrap is a valid correlation value", Sync.Poll(Start, SendTime) == SendTime);
	Sync.RecordReply(Start + 8ms, SendTime, std::numeric_limits<u32>::max() - 4, 4);
	Sync.CompleteProbe(ClockProbeResult::Sent);
	Passed &= Expect("a reply during send is retained and the offset is signed across wrap",
		!Sync.Poll(Start + 8ms, 4) && Sync.GetStatus() == LegacyClockSyncStatus::Ready && Sync.GetOffset() == -5);
	return Passed;
}

static bool CheckArrivalBeforeDeadline()
{
	LegacyClockSync Sync;
	constexpr LegacyClockSync::Clock::time_point Start{};
	Sync.Start(Start);
	bool Passed = Expect("zero is a valid wire timestamp", Sync.Poll(Start, 0) == 0u);
	Passed &= Expect("the owner cannot issue two probes before the first send returns",
		!Sync.Poll(Start + 1ms, 1) && Sync.GetStatus() == LegacyClockSyncStatus::Pending);
	Sync.CompleteProbe(ClockProbeResult::Sent);
	Sync.RecordReply(Start + 4999ms, 0, 2500, 4999);
	Passed &= Expect("a timely reply survives a stalled owner poll past the deadline",
		!Sync.Poll(Start + 6s, 6000) && Sync.GetStatus() == LegacyClockSyncStatus::Ready && Sync.GetOffset() == 0);
	Sync.RecordReply(Start + 6s, 0, 4000, 6000);
	Passed &= Expect("a completed attempt stays ready and keeps its accepted offset",
		!Sync.Poll(Start + 7s, 7000) && Sync.GetStatus() == LegacyClockSyncStatus::Ready && Sync.GetOffset() == 0);
	return Passed;
}

bool Run()
{
	bool Passed = CheckMatchingReply();
	Passed &= CheckDeadlines();
	Passed &= CheckCancellationAndRestart();
	Passed &= CheckReplyDuringSendAcrossWrap();
	Passed &= CheckArrivalBeforeDeadline();
	return Passed;
}
}
