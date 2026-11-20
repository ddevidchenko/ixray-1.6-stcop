#pragma once

#include <chrono>

namespace Multiplayer
{
enum class LegacyStartupStatus
{
	Idle,
	AwaitingDescription,
	AwaitingAuthentication,
	Ready,
	TransportFailed,
	DescriptionExpired,
	AuthenticationExpired,
	AuthenticationRejected,
};

enum class LegacyAuthentication
{
	Pending,
	Accepted,
	Rejected,
};

struct LegacyStartupObservation
{
	bool TransportFailed = false;
	bool HasDescription = false;
	LegacyAuthentication Authentication = LegacyAuthentication::Pending;
};

class LegacyClientStartup
{
public:
	using Clock = std::chrono::steady_clock;

	void Begin(Clock::time_point Now, bool Direct) noexcept
	{
		Status = Direct ? LegacyStartupStatus::AwaitingAuthentication : LegacyStartupStatus::AwaitingDescription;
		Deadline = Now + PhaseTimeout;
	}

	void Cancel() noexcept { Status = LegacyStartupStatus::Idle; }

	[[nodiscard]] LegacyStartupStatus Poll(Clock::time_point Now, LegacyStartupObservation Observation) noexcept
	{
		if (Status != LegacyStartupStatus::AwaitingDescription && Status != LegacyStartupStatus::AwaitingAuthentication)
		{
			return Status;
		}
		if (Observation.TransportFailed)
		{
			Status = LegacyStartupStatus::TransportFailed;
			return Status;
		}
		if (Status == LegacyStartupStatus::AwaitingDescription)
		{
			if (Observation.HasDescription)
			{
				Deadline = Now + PhaseTimeout;
				Status = LegacyStartupStatus::AwaitingAuthentication;
			}
			else if (Now >= Deadline)
			{
				Status = LegacyStartupStatus::DescriptionExpired;
			}
			return Status;
		}

		if (Observation.Authentication == LegacyAuthentication::Accepted)
		{
			Status = LegacyStartupStatus::Ready;
		}
		else if (Observation.Authentication == LegacyAuthentication::Rejected)
		{
			Status = LegacyStartupStatus::AuthenticationRejected;
		}
		else if (Now >= Deadline)
		{
			Status = LegacyStartupStatus::AuthenticationExpired;
		}
		return Status;
	}

	[[nodiscard]] LegacyStartupStatus GetStatus() const noexcept { return Status; }

private:
	static constexpr auto PhaseTimeout = std::chrono::seconds(60);

	LegacyStartupStatus Status = LegacyStartupStatus::Idle;
	Clock::time_point Deadline{};
};
}
