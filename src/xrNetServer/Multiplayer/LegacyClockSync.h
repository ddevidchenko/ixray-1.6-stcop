#pragma once

#include "../../xrCore/ScalarTypes.h"

#include <chrono>
#include <optional>

namespace Multiplayer
{
enum class LegacyClockSyncStatus
{
	Idle,
	Pending,
	Ready,
	Failed,
};

enum class ClockProbeResult
{
	Deferred,
	Sent,
	Failed,
};

// The loading or frame owner advances the attempt. Callers serialize reply delivery with owner operations.
class LegacyClockSync
{
public:
	using Clock = std::chrono::steady_clock;

	void Start(Clock::time_point Now) noexcept
	{
		State = LegacyClockSyncStatus::Pending;
		SendDeadline = Now + Window;
		Probe.reset();
		ProbeSent = false;
		Sample.reset();
	}

	void Cancel() noexcept { State = LegacyClockSyncStatus::Idle; }

	[[nodiscard]] std::optional<u32> Poll(Clock::time_point Now, u32 ClientTime) noexcept
	{
		if (State != LegacyClockSyncStatus::Pending)
		{
			return {};
		}
		if (ProbeSent)
		{
			if (Sample || Now >= ReplyDeadline)
			{
				State = LegacyClockSyncStatus::Ready;
			}
			return {};
		}
		if (Probe)
		{
			return {};
		}
		if (Now >= SendDeadline)
		{
			State = LegacyClockSyncStatus::Failed;
			return {};
		}
		if (LastSent == ClientTime)
		{
			return {};
		}

		Probe = ClientTime;
		ReplyDeadline = Now + Window;
		return Probe;
	}

	void CompleteProbe(ClockProbeResult Result) noexcept
	{
		if (State != LegacyClockSyncStatus::Pending || !Probe || ProbeSent)
		{
			return;
		}
		switch (Result)
		{
		case ClockProbeResult::Deferred:
			Probe.reset();
			Sample.reset();
			break;
		case ClockProbeResult::Sent:
			LastSent = Probe;
			ProbeSent = true;
			break;
		case ClockProbeResult::Failed:
			State = LegacyClockSyncStatus::Failed;
			break;
		}
	}

	void RecordReply(Clock::time_point Now, u32 Echo, u32 ServerTime, u32 ClientTime) noexcept
	{
		if (State == LegacyClockSyncStatus::Pending && Probe == Echo && !Sample && Now < ReplyDeadline)
		{
			const u32 RoundTrip = ClientTime - Echo;
			Sample = static_cast<s32>(ServerTime + RoundTrip / 2 - ClientTime);
		}
	}

	[[nodiscard]] LegacyClockSyncStatus GetStatus() const noexcept { return State; }

	[[nodiscard]] std::optional<s32> GetOffset() const noexcept
	{
		return State == LegacyClockSyncStatus::Ready ? Sample : std::nullopt;
	}

private:
	static constexpr auto Window = std::chrono::seconds(5);

	LegacyClockSyncStatus State = LegacyClockSyncStatus::Idle;
	Clock::time_point SendDeadline{};
	Clock::time_point ReplyDeadline{};
	std::optional<u32> Probe;
	std::optional<u32> LastSent;
	std::optional<s32> Sample;
	bool ProbeSent = false;
};
}
