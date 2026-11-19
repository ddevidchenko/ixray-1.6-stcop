#pragma once

#include <chrono>

namespace Multiplayer
{
enum class MapSyncStatus
{
	Waiting,
	Received,
	TimedOut,
};

class MapSyncWait
{
public:
	using Clock = std::chrono::steady_clock;

	explicit MapSyncWait(Clock::time_point SentAt) noexcept
		: Deadline(SentAt + std::chrono::seconds(5))
	{
	}

	// A reply processed on this update takes precedence over expiry.
	[[nodiscard]] MapSyncStatus Poll(Clock::time_point Now, bool ReplyReceived, bool DemoPlayback) const noexcept
	{
		if (ReplyReceived)
		{
			return MapSyncStatus::Received;
		}
		if (!DemoPlayback && Now >= Deadline)
		{
			return MapSyncStatus::TimedOut;
		}
		return MapSyncStatus::Waiting;
	}

private:
	Clock::time_point Deadline;
};
}
