#include "PendingAdmission.h"
#include "ClientHello.h"

#include <cstdio>
#include <optional>
#include <utility>

namespace PendingAdmissionLimitTests
{
using namespace Multiplayer;
using namespace std::chrono_literals;
using Wire::AdmissionRejectReason;

constexpr auto Now = PendingAdmission::Clock::time_point{} + 17s;
constexpr auto Deadline = Now + 10s;
constexpr AdmissionRequirements Requirements{Wire::CompatibilityId{0x01}, Wire::CompatibilityId{0x12}, Wire::CompatibilityId{0x23}, false};

class OpenServerVerifier final : public IAdmissionCredentialVerifier
{
public:
	bool Verify(xr_span<const u8>) const noexcept override { return false; }
};

class RosterSlot final : public IAdmissionSlotReservation
{
public:
	explicit RosterSlot(bool Available) noexcept : Available(Available) {}
	bool TryReserve() noexcept override { return Available; }
	void Commit() noexcept override {}
	void Cancel() noexcept override {}

private:
	const bool Available;
};

class WelcomeQueue final : public IAdmissionWelcomeQueue
{
public:
	explicit WelcomeQueue(bool Accepts) noexcept : Accepts(Accepts) {}
	bool TryQueue(xr_span<const u8>) noexcept override { return Accepts; }

private:
	const bool Accepts;
};

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

static bool ExpectBusy(PendingAdmissionLimit& Limit, const char* Step)
{
	const auto Result = Limit.TryBegin(Deadline);
	return Expect(Step, !Result && Result.error() == AdmissionRejectReason::ServerBusy);
}

static bool VerifyHello(PendingAdmission& Peer)
{
	const Wire::ClientHello Hello{9, Requirements.Build, Requirements.Content, Requirements.Rules, "Player", {}};
	xr_array<u8, 290> Packet;
	const auto Size = Wire::EncodeClientHello(Hello, Packet);
	const OpenServerVerifier Verifier;
	return Expect("fixture hello reaches roster wait", Size && Peer.ReceiveHello(xr_span(Packet).first(*Size), Requirements, Verifier, Now) &&
		Peer.GetState() == PendingAdmissionState::AwaitingSlot);
}

static Wire::ServerWelcome MakeWelcome(u32 ConnectionEpoch)
{
	return {17, 0, ConnectionEpoch, 11, Requirements.Content, Requirements.Rules,
		"test_arena", {50000, 150000, 20000, 50000}, {5000, 2000, 1200000, 50}};
}

static bool CheckLimit()
{
	PendingAdmissionLimit Limit(2);
	auto First = Limit.TryBegin(Deadline);
	const auto Second = Limit.TryBegin(Deadline + 1s);
	bool Passed = Expect("two pending connections fit a limit of two", First && Second &&
		First->GetState() == PendingAdmissionState::AwaitingHello && Second->GetState() == PendingAdmissionState::AwaitingHello);
	for (int Attempt = 0; Attempt < 64; ++Attempt)
	{
		if (!ExpectBusy(Limit, "excess connection repeatedly receives ServerBusy before hello"))
		{
			Passed = false;
			break;
		}
	}
	Passed &= Expect("one timeout leaves the other pending peer in place", First && First->Expire(Deadline) == AdmissionRejectReason::AdmissionTimedOut &&
		Second && Second->GetState() == PendingAdmissionState::AwaitingHello);
	const auto Replacement = Limit.TryBegin(Deadline + 10s);
	Passed &= Expect("one timeout makes exactly one place available", Replacement.has_value());
	Passed &= ExpectBusy(Limit, "a surviving peer and its new neighbor fill the limit");
	PendingAdmissionLimit SeparateLimit(1);
	Passed &= Expect("an independent connection owner still has capacity", SeparateLimit.TryBegin(Deadline).has_value());
	PendingAdmissionLimit Disabled(0);
	Passed &= ExpectBusy(Disabled, "zero capacity rejects all attempts");
	return Passed;
}

static bool CheckDisconnect()
{
	bool Passed = true;
	for (const bool AwaitingSlot : {false, true})
	{
		PendingAdmissionLimit Limit(1);
		{
			auto Peer = Limit.TryBegin(Deadline);
			if (!Expect("disconnect fixture starts a peer", Peer.has_value()) || (AwaitingSlot && !VerifyHello(*Peer)))
			{
				return false;
			}
			Passed &= ExpectBusy(Limit, "both pending phases consume capacity");
		}
		const auto Replacement = Limit.TryBegin(Deadline);
		Passed &= Expect("disconnect returns the pending slot in either phase", Replacement.has_value());
		Passed &= ExpectBusy(Limit, "disconnect returns exactly one pending slot");
	}
	return Passed;
}

static bool CheckExpiry()
{
	PendingAdmissionLimit Limit(1);
	std::optional<PendingAdmission> Replacement;
	bool Passed = true;
	{
		auto Peer = Limit.TryBegin(Deadline);
		if (!Expect("expiry fixture starts a peer", Peer.has_value()) || !VerifyHello(*Peer))
		{
			return false;
		}
		const OpenServerVerifier Verifier;
		const auto Duplicate = Peer->ReceiveHello({}, Requirements, Verifier, Now);
		Passed &= Expect("duplicate hello is nonterminal in roster wait", !Duplicate && !Duplicate.error().ShouldReject &&
			Duplicate.error().Reason == AdmissionRejectReason::InvalidState);
		Passed &= Expect("a tick before the deadline still leaves the peer pending", !Peer->Expire(Deadline - 1ms));
		Passed &= ExpectBusy(Limit, "a duplicate or pre-deadline tick keeps the pending slot");
		Passed &= Expect("expiry occurs at the exact deadline", Peer->Expire(Deadline) == AdmissionRejectReason::AdmissionTimedOut);
		auto Started = Limit.TryBegin(Deadline + 10s);
		if (!Expect("expiry returns capacity while the closing peer is still retained", Started.has_value()))
		{
			return false;
		}
		Replacement.emplace(std::move(*Started));
		Passed &= Expect("repeated expiry produces no second rejection", !Peer->Expire(Deadline));
	}
	Passed &= ExpectBusy(Limit, "repeated expiry and later destruction cannot release a replacement's slot");
	return Passed;
}

static bool CheckHelloRejection()
{
	bool Passed = true;
	for (const bool AtDeadline : {false, true})
	{
		PendingAdmissionLimit Limit(1);
		auto Peer = Limit.TryBegin(Deadline);
		if (!Expect("hello rejection fixture starts a peer", Peer.has_value()))
		{
			return false;
		}
		const OpenServerVerifier Verifier;
		const auto Failure = Peer->ReceiveHello({}, Requirements, Verifier, AtDeadline ? Deadline : Now);
		const auto Reason = AtDeadline ? AdmissionRejectReason::AdmissionTimedOut : AdmissionRejectReason::MalformedHello;
		Passed &= Expect("hello error closes the pending peer", !Failure && Failure.error().ShouldReject && Failure.error().Reason == Reason &&
			Peer->GetState() == PendingAdmissionState::Closing);
		const auto Replacement = Limit.TryBegin(Deadline + 10s);
		Passed &= Expect("hello rejection returns capacity before disconnect", Replacement.has_value());
		Passed &= ExpectBusy(Limit, "one hello rejection gives back exactly one slot");
	}
	return Passed;
}

static bool CheckCompletion()
{
	struct Case
	{
		const char* Name;
		bool HasRosterSlot;
		bool QueueAccepts;
		u32 ConnectionEpoch;
		PendingAdmission::Clock::time_point When;
		std::optional<AdmissionCompletionError> Error;
	};
	constexpr Case Cases[] = {
		{"committed admission returns capacity before loading", true, true, 1, Now, std::nullopt},
		{"full match returns pending capacity", false, true, 2, Now, AdmissionCompletionError::ServerFull},
		{"queue failure returns pending capacity", true, false, 3, Now, AdmissionCompletionError::WelcomeQueueFailed},
		{"invalid welcome returns pending capacity", true, true, 0, Now, AdmissionCompletionError::InvalidWelcome},
		{"timeout during completion returns pending capacity", true, true, 4, Deadline, AdmissionCompletionError::AdmissionTimedOut},
	};
	bool Passed = true;
	for (const auto& Scenario : Cases)
	{
		PendingAdmissionLimit Limit(1);
		auto Peer = Limit.TryBegin(Deadline);
		if (!Expect("completion fixture starts a peer", Peer.has_value()))
		{
			return false;
		}
		RosterSlot Slot(Scenario.HasRosterSlot);
		WelcomeQueue Queue(Scenario.QueueAccepts);
		const auto Welcome = MakeWelcome(Scenario.ConnectionEpoch);
		const auto Early = Peer->Complete(Welcome, Slot, Queue, Now);
		Passed &= Expect("completion before hello is nonterminal", !Early && Early.error() == AdmissionCompletionError::InvalidState);
		Passed &= ExpectBusy(Limit, "premature completion keeps the pending slot");
		if (!VerifyHello(*Peer))
		{
			return false;
		}
		const auto Done = Peer->Complete(Welcome, Slot, Queue, Scenario.When);
		Passed &= Expect("completion fixture reaches the expected outcome", Scenario.Error ? (!Done && Done.error() == *Scenario.Error) : Done.has_value());
		const auto Replacement = Limit.TryBegin(Deadline + 10s);
		Passed &= Expect(Scenario.Name, Replacement.has_value());
		Passed &= ExpectBusy(Limit, "completion can return only one pending slot");
	}
	return Passed;
}

static bool CheckMove()
{
	PendingAdmissionLimit Limit(1);
	std::optional<PendingAdmission> Retained;
	bool Passed = true;
	{
		auto Peer = Limit.TryBegin(Deadline);
		if (!Expect("move fixture starts a peer", Peer.has_value()) || !VerifyHello(*Peer))
		{
			return false;
		}
		Retained.emplace(std::move(*Peer));
		Passed &= Expect("moving transfers the verified state and leaves the source inert", Retained->GetState() == PendingAdmissionState::AwaitingSlot &&
			Peer->GetState() == PendingAdmissionState::Closing && !Peer->Expire(Now));
	}
	Passed &= ExpectBusy(Limit, "destroying a moved-from tracker keeps its transferred slot charged");
	RosterSlot Slot(true);
	WelcomeQueue Queue(true);
	Passed &= Expect("a moved tracker can admit using its verified metadata", Retained->Complete(MakeWelcome(1), Slot, Queue, Now).has_value());
	const auto Replacement = Limit.TryBegin(Deadline);
	Passed &= Expect("completion returns the moved slot", Replacement.has_value());
	Retained.reset();
	Passed &= ExpectBusy(Limit, "destroying an admitted tracker cannot release a replacement's slot");
	return Passed;
}

bool Run()
{
	bool Passed = CheckLimit();
	Passed &= CheckDisconnect();
	Passed &= CheckExpiry();
	Passed &= CheckHelloRejection();
	Passed &= CheckCompletion();
	Passed &= CheckMove();
	if (!Passed)
	{
		return false;
	}
	std::puts("Pending admission limit checks passed");
	return true;
}
}
