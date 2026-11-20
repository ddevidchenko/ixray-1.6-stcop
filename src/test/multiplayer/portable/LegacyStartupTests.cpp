#include "../../../xrGame/Multiplayer/LegacyClientStartup.h"
#include "LegacyAdmission.h"

#include <cstdio>

namespace LegacyStartupTests
{
using namespace Multiplayer;
using namespace std::chrono_literals;

static bool Expect(const char* Failure, bool Condition)
{
	if (!Condition)
	{
		std::fprintf(stderr, "%s\n", Failure);
	}
	return Condition;
}

static bool CheckRemoteStartup()
{
	constexpr LegacyClientStartup::Clock::time_point Start{};
	LegacyClientStartup Startup;
	Startup.Begin(Start, false);
	bool Passed = Expect("a remote start waits for the game description and does not consume authentication early",
		Startup.Poll(Start + 1s, {.Authentication = LegacyAuthentication::Accepted}) == LegacyStartupStatus::AwaitingDescription);
	Passed &= Expect("description near its deadline grants a fresh authentication window and yields",
		Startup.Poll(Start + 59s, {.HasDescription = true, .Authentication = LegacyAuthentication::Accepted}) ==
		LegacyStartupStatus::AwaitingAuthentication);
	Passed &= Expect("authentication still waits just before its own deadline",
		Startup.Poll(Start + 118s, {}) == LegacyStartupStatus::AwaitingAuthentication);
	Passed &= Expect("an authentication result processed on the deadline completes startup",
		Startup.Poll(Start + 119s, {.Authentication = LegacyAuthentication::Accepted}) == LegacyStartupStatus::Ready);
	Passed &= Expect("completed startup stays terminal without reapplying a late failure",
		Startup.Poll(Start + 120s, {.TransportFailed = true}) == LegacyStartupStatus::Ready);
	return Passed;
}

static bool CheckFailureAndRestart()
{
	constexpr LegacyClientStartup::Clock::time_point Start{};
	LegacyClientStartup Startup;
	Startup.Begin(Start, false);
	bool Passed = Expect("missing description expires after one minute",
		Startup.Poll(Start + 60s, {}) == LegacyStartupStatus::DescriptionExpired);

	Startup.Begin(Start + 100s, false);
	Passed &= Expect("transport loss wins over a queued game description",
		Startup.Poll(Start + 101s, {.TransportFailed = true, .HasDescription = true}) == LegacyStartupStatus::TransportFailed);

	Startup.Begin(Start + 200s, true);
	Passed &= Expect("direct startup waits for local registration without needing a game description",
		Startup.GetStatus() == LegacyStartupStatus::AwaitingAuthentication &&
		Startup.Poll(Start + 201s, {}) == LegacyStartupStatus::AwaitingAuthentication);
	Passed &= Expect("a local or remote authentication rejection stops startup",
		Startup.Poll(Start + 202s, {.Authentication = LegacyAuthentication::Rejected}) == LegacyStartupStatus::AuthenticationRejected);

	Startup.Begin(Start + 300s, true);
	Passed &= Expect("authentication expires distinctly from the description",
		Startup.Poll(Start + 360s, {}) == LegacyStartupStatus::AuthenticationExpired);

	Startup.Begin(Start + 400s, false);
	Startup.Cancel();
	Passed &= Expect("cancellation suppresses late results before a fresh start",
		Startup.Poll(Start + 401s, {.HasDescription = true, .Authentication = LegacyAuthentication::Accepted}) == LegacyStartupStatus::Idle);
	Startup.Begin(Start + 402s, true);
	Passed &= Expect("a fresh direct start can complete after cancellation",
		Startup.Poll(Start + 403s, {.Authentication = LegacyAuthentication::Accepted}) == LegacyStartupStatus::Ready);

	Startup.Begin(Start + 500s, false);
	Passed &= Expect("a queued description takes precedence over description expiry on the same turn",
		Startup.Poll(Start + 560s, {.HasDescription = true}) == LegacyStartupStatus::AwaitingAuthentication);
	Passed &= Expect("disconnect prevents a queued authentication success from starting the clock",
		Startup.Poll(Start + 561s, {.TransportFailed = true, .Authentication = LegacyAuthentication::Accepted}) ==
		LegacyStartupStatus::TransportFailed);
	return Passed;
}

static bool CheckAdmissionOrder()
{
	LegacyAdmissionChecks Checks{};
	Checks.MaxConnections = 2;
	Checks.CurrentConnections = 2;
	Checks.Duplicate = true;
	Checks.Banned = true;
	Checks.PresentedPassword = "wrong";
	Checks.ServerPassword = "original";
	bool Passed = Expect("duplicate requests remain no-ops when the server is full",
		CheckLegacyAdmission(Checks) == LegacyAdmissionDecision::Duplicate);
	Checks.Duplicate = false;
	Passed &= Expect("capacity rejection takes precedence over access policy",
		CheckLegacyAdmission(Checks) == LegacyAdmissionDecision::SessionFull);
	Checks.CurrentConnections = 1;
	Checks.LocalHostCandidate = true;
	Passed &= Expect("a hosted candidate is still subject to the ban list",
		CheckLegacyAdmission(Checks) == LegacyAdmissionDecision::Banned);
	Checks.Banned = false;
	Passed &= Expect("the hosted candidate bypasses IP filter and password checks",
		CheckLegacyAdmission(Checks) == LegacyAdmissionDecision::Accept);
	Checks.LocalHostCandidate = false;
	Passed &= Expect("the IP filter precedes password verification for remote candidates",
		CheckLegacyAdmission(Checks) == LegacyAdmissionDecision::AddressDenied);
	Checks.AddressAllowed = true;
	Passed &= Expect("an incorrect supplied password has a distinct rejection",
		CheckLegacyAdmission(Checks) == LegacyAdmissionDecision::PasswordRejected);
	Checks.PresentedPassword = "original";
	Passed &= Expect("a remote peer with matching credentials is accepted", CheckLegacyAdmission(Checks) == LegacyAdmissionDecision::Accept);
	Checks.PresentedPassword.reset();
	Passed &= Expect("the legacy path permits identities without a generic-string password",
		CheckLegacyAdmission(Checks) == LegacyAdmissionDecision::Accept);
	Checks.PresentedPassword = "";
	Passed &= Expect("a supplied empty password still participates in verification",
		CheckLegacyAdmission(Checks) == LegacyAdmissionDecision::PasswordRejected);
	return Passed;
}

static bool CheckAdmissionBatch()
{
	xr_array<u32, 2> Storage{};
	LegacyAdmissionBatch Batch(Storage);
	bool Passed = Expect("a native batch starts empty and skips the invalid handle", Batch.GetRequests().empty() && !Batch.TryRecord(0));
	Passed &= Expect("recording a duplicate does not consume an admission place",
		Batch.TryRecord(7) && Batch.TryRecord(7) && Batch.TryRecord(11));
	Passed &= Expect("an overflowing request leaves the bounded ordered batch intact", !Batch.TryRecord(13));
	Passed &= Expect("a recorded handle is still deduplicated at capacity", Batch.TryRecord(7));
	const auto Requests = Batch.GetRequests();
	Passed &= Expect("the owner later sees recorded native handles in arrival order",
		Requests.size() == 2 && Requests[0] == 7 && Requests[1] == 11);
	return Passed;
}

bool Run()
{
	bool Passed = CheckRemoteStartup();
	Passed &= CheckFailureAndRestart();
	Passed &= CheckAdmissionOrder();
	Passed &= CheckAdmissionBatch();
	return Passed;
}
}
