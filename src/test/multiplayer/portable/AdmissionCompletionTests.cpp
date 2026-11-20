#include "ClientHello.h"
#include "PendingAdmission.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace AdmissionCompletionTests
{
using namespace Multiplayer;
using namespace std::chrono_literals;

constexpr auto Deadline = PendingAdmission::Clock::time_point{} + 17s;
constexpr auto OnTime = Deadline - 1ms;
constexpr AdmissionRequirements Requirements{Wire::CompatibilityId{0x11}, Wire::CompatibilityId{0x22}, Wire::CompatibilityId{0x33}, false};

class OpenServerVerifier final : public IAdmissionCredentialVerifier
{
public:
	bool Verify(xr_span<const u8>) const noexcept override { return false; }
};

struct Capacity
{
	int Limit = 1;
	int Occupied = 0;
};

class SlotReservation final : public IAdmissionSlotReservation
{
public:
	explicit SlotReservation(Capacity& Slots) : Slots(Slots) {}

	bool TryReserve() noexcept override
	{
		++Attempts;
		if (Slots.Occupied == Slots.Limit || Held || Committed)
		{
			return false;
		}
		Held = true;
		++Slots.Occupied;
		return true;
	}

	void Commit() noexcept override
	{
		++Commits;
		Committed = Held;
		Held = false;
	}

	void Cancel() noexcept override
	{
		++Cancellations;
		if (Held)
		{
			--Slots.Occupied;
			Held = false;
		}
	}

	bool ReleaseCommitted() noexcept
	{
		if (!Committed)
		{
			return false;
		}
		--Slots.Occupied;
		Committed = false;
		return true;
	}

	int Attempts = 0;
	int Commits = 0;
	int Cancellations = 0;
	bool Held = false;
	bool Committed = false;

private:
	Capacity& Slots;
};

class WelcomeQueue final : public IAdmissionWelcomeQueue
{
public:
	explicit WelcomeQueue(const SlotReservation& Slot) : Slot(Slot) {}

	bool TryQueue(xr_span<const u8> Packet) noexcept override
	{
		++Attempts;
		SawProvisionalSlot = Slot.Held && !Slot.Committed;
		if (!Accepts || Packet.size() > Bytes.size())
		{
			return false;
		}
		std::ranges::copy(Packet, Bytes.begin());
		Size = Packet.size();
		return true;
	}

	[[nodiscard]] xr_span<const u8> View() const noexcept { return xr_span(Bytes).first(Size); }

	bool Accepts = true;
	int Attempts = 0;
	bool SawProvisionalSlot = false;

private:
	const SlotReservation& Slot;
	std::array<u8, 1100> Bytes{};
	std::size_t Size = 0;
};

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

static bool ExpectError(const char* Step, const std::expected<void, AdmissionCompletionError>& Result, AdmissionCompletionError Error)
{
	if (!Result && Result.error() == Error)
	{
		return true;
	}
	std::fprintf(stderr, "%s: expected %d, got %d\n", Step, static_cast<int>(Error), Result ? -1 : static_cast<int>(Result.error()));
	return false;
}

static bool VerifyHello(PendingAdmission& Peer)
{
	const Wire::ClientHello Hello{0xfedcba98, Requirements.Build, Requirements.Content, Requirements.Rules, "Player", {}};
	std::array<u8, 290> Packet;
	const auto Size = Wire::EncodeClientHello(Hello, Packet);
	const OpenServerVerifier Verifier;
	return Expect("fixture hello is compatible and requests a slot", Size && Peer.ReceiveHello(xr_span(Packet).first(*Size), Requirements, Verifier, OnTime));
}

static Wire::ServerWelcome MakeWelcome(u32 ConnectionEpoch)
{
	return {0x8877665544332211, 3, ConnectionEpoch, 9, Requirements.Content, Requirements.Rules,
		"test_arena", {50000, 150000, 20000, 50000}, {5000, 2000, 1200000, 50}};
}

static bool CheckWelcome()
{
	PendingAdmission Peer(Deadline);
	if (!VerifyHello(Peer))
	{
		return false;
	}
	Capacity Slots;
	SlotReservation Slot(Slots);
	WelcomeQueue Queue(Slot);
	const auto Welcome = MakeWelcome(27);
	bool Passed = Expect("accepted welcome commits the slot and admits the peer", Peer.Complete(Welcome, Slot, Queue, OnTime).has_value() &&
		Peer.GetState() == PendingAdmissionState::Admitted && Slots.Occupied == 1 && Slot.Committed);
	const auto Decoded = Wire::DecodeServerWelcome(Queue.View());
	Passed &= Expect("queued welcome uses the trusted session, round and connection identities", Decoded &&
		Decoded->ServerSession == 0x8877665544332211 && Decoded->WorldEpoch == 3 && Decoded->ConnectionEpoch == 27 && Decoded->MessageSequence == 9);
	Passed &= Expect("queued welcome carries the validated IDs and effective settings", Decoded &&
		Decoded->ContentId == Requirements.Content && Decoded->RulesId == Requirements.Rules && Decoded->MapId == "test_arena" &&
		Decoded->Rates == Wire::WelcomeRates{50000, 150000, 20000, 50000} && Decoded->Rules == Wire::WelcomeRules{5000, 2000, 1200000, 50});
	Passed &= Expect("reserve, queue and commit run once in order", Slot.Attempts == 1 && Queue.Attempts == 1 && Queue.SawProvisionalSlot &&
		Slot.Commits == 1 && Slot.Cancellations == 0);
	Passed &= Expect("admitted peer is outside the initial deadline", !Peer.Expire(Deadline) && Peer.GetState() == PendingAdmissionState::Admitted);
	Passed &= ExpectError("admission cannot be committed a second time", Peer.Complete(MakeWelcome(28), Slot, Queue, Deadline), AdmissionCompletionError::InvalidState);
	const OpenServerVerifier Verifier;
	const auto LaterHello = Peer.ReceiveHello({}, Requirements, Verifier, Deadline);
	Passed &= Expect("admitted peer ignores further hello input without rejection", !LaterHello &&
		LaterHello.error().Reason == Wire::AdmissionRejectReason::InvalidState && !LaterHello.error().ShouldReject &&
		Peer.GetState() == PendingAdmissionState::Admitted);
	Passed &= Expect("later calls leave the existing welcome and slot intact", Slot.Attempts == 1 && Slot.Commits == 1 && Slot.Cancellations == 0 &&
		Queue.Attempts == 1 && Slots.Occupied == 1);
	return Passed;
}

static bool CheckFullThenDisconnect()
{
	PendingAdmission First(Deadline);
	PendingAdmission Second(Deadline);
	PendingAdmission Third(Deadline);
	if (!VerifyHello(First) || !VerifyHello(Second) || !VerifyHello(Third))
	{
		return false;
	}
	Capacity Slots;
	SlotReservation FirstSlot(Slots);
	SlotReservation SecondSlot(Slots);
	SlotReservation ThirdSlot(Slots);
	WelcomeQueue FirstQueue(FirstSlot);
	WelcomeQueue SecondQueue(SecondSlot);
	WelcomeQueue ThirdQueue(ThirdSlot);
	bool Passed = Expect("first peer takes the last slot", First.Complete(MakeWelcome(40), FirstSlot, FirstQueue, OnTime).has_value());
	Passed &= ExpectError("second peer with a valid hello is rejected when the roster is full",
		Second.Complete(MakeWelcome(41), SecondSlot, SecondQueue, OnTime), AdmissionCompletionError::ServerFull);
	Passed &= Expect("full roster never queues a welcome or releases another peer's slot", Second.GetState() == PendingAdmissionState::Closing &&
		SecondSlot.Attempts == 1 && SecondQueue.Attempts == 0 && SecondSlot.Commits == 0 && SecondSlot.Cancellations == 0 &&
		Slots.Occupied == 1 && FirstSlot.Committed && First.GetState() == PendingAdmissionState::Admitted);

	Passed &= Expect("fixture roster releases the first peer's committed slot", FirstSlot.ReleaseCommitted());
	Passed &= ExpectError("a rejected peer cannot take newly available capacity",
		Second.Complete(MakeWelcome(41), SecondSlot, SecondQueue, OnTime), AdmissionCompletionError::InvalidState);
	Passed &= Expect("new peer can reserve the freed slot", Third.Complete(MakeWelcome(42), ThirdSlot, ThirdQueue, OnTime).has_value() &&
		Third.GetState() == PendingAdmissionState::Admitted && ThirdSlot.Committed && Slots.Occupied == 1 && SecondSlot.Attempts == 1);
	const auto FirstWelcome = Wire::DecodeServerWelcome(FirstQueue.View());
	const auto ThirdWelcome = Wire::DecodeServerWelcome(ThirdQueue.View());
	Passed &= Expect("reusing capacity retains each connection's assigned epoch in its own welcome", FirstWelcome && ThirdWelcome &&
		FirstWelcome->ConnectionEpoch == 40 && ThirdWelcome->ConnectionEpoch == 42);
	return Passed;
}

static bool CheckQueueFailure()
{
	PendingAdmission First(Deadline);
	PendingAdmission Second(Deadline);
	if (!VerifyHello(First) || !VerifyHello(Second))
	{
		return false;
	}
	Capacity Slots;
	SlotReservation FirstSlot(Slots);
	SlotReservation SecondSlot(Slots);
	WelcomeQueue FirstQueue(FirstSlot);
	WelcomeQueue SecondQueue(SecondSlot);
	FirstQueue.Accepts = false;
	bool Passed = ExpectError("failed welcome queue reports the local failure", First.Complete(MakeWelcome(70), FirstSlot, FirstQueue, OnTime),
		AdmissionCompletionError::WelcomeQueueFailed);
	Passed &= Expect("failed queue closes the peer and cancels the provisional slot exactly once", First.GetState() == PendingAdmissionState::Closing &&
		FirstSlot.Attempts == 1 && FirstQueue.Attempts == 1 && FirstQueue.SawProvisionalSlot && FirstQueue.View().empty() &&
		FirstSlot.Cancellations == 1 && FirstSlot.Commits == 0 && !FirstSlot.Held && !FirstSlot.Committed && Slots.Occupied == 0);
	FirstQueue.Accepts = true;
	Passed &= ExpectError("a closed peer cannot retry a failed handoff", First.Complete(MakeWelcome(70), FirstSlot, FirstQueue, OnTime),
		AdmissionCompletionError::InvalidState);
	auto Welcome = MakeWelcome(71);
	Welcome.WorldEpoch = 0;
	Passed &= Expect("another peer can use capacity released by a failed handoff", Second.Complete(Welcome, SecondSlot, SecondQueue, OnTime).has_value() &&
		SecondSlot.Committed && Slots.Occupied == 1 && FirstQueue.Attempts == 1 && FirstSlot.Cancellations == 1);
	const auto Decoded = Wire::DecodeServerWelcome(SecondQueue.View());
	Passed &= Expect("initial world epoch zero is valid in a bound welcome", Decoded && Decoded->WorldEpoch == 0 && Decoded->ConnectionEpoch == 71);
	return Passed;
}

static bool CheckStateAndDeadline()
{
	Capacity Slots;
	SlotReservation Slot(Slots);
	WelcomeQueue Queue(Slot);
	const auto Welcome = MakeWelcome(13);
	PendingAdmission Peer(Deadline);
	bool Passed = ExpectError("welcome requires a verified hello", Peer.Complete(Welcome, Slot, Queue, OnTime), AdmissionCompletionError::InvalidState);
	Passed &= Expect("premature completion leaves the peer awaiting a hello", Peer.GetState() == PendingAdmissionState::AwaitingHello);
	if (!VerifyHello(Peer))
	{
		return false;
	}
	Passed &= ExpectError("the exact deadline expires a pending slot before any reservation or send", Peer.Complete(Welcome, Slot, Queue, Deadline),
		AdmissionCompletionError::AdmissionTimedOut);
	Passed &= ExpectError("completion timeout is reported once", Peer.Complete(Welcome, Slot, Queue, Deadline + 1ms), AdmissionCompletionError::InvalidState);
	Passed &= Expect("expired reservation stays closed and unchanged", Peer.GetState() == PendingAdmissionState::Closing && !Peer.Expire(Deadline + 1ms));

	PendingAdmission AlreadyExpired(Deadline);
	if (!VerifyHello(AlreadyExpired))
	{
		return false;
	}
	Passed &= Expect("timer owns the first timeout if it runs before completion", AlreadyExpired.Expire(Deadline) == Wire::AdmissionRejectReason::AdmissionTimedOut);
	Passed &= ExpectError("completion after timer expiry cannot report a second timeout", AlreadyExpired.Complete(Welcome, Slot, Queue, Deadline),
		AdmissionCompletionError::InvalidState);
	Passed &= Expect("state and deadline failures have no slot or queue side effects", Slot.Attempts == 0 && Slot.Commits == 0 && Slot.Cancellations == 0 &&
		Queue.Attempts == 0 && Slots.Occupied == 0);
	return Passed;
}

static bool CheckInvalidWelcome()
{
	const auto NoSideEffects = [](const Wire::ServerWelcome& Welcome, const char* Step)
	{
		PendingAdmission Peer(Deadline);
		if (!VerifyHello(Peer))
		{
			return false;
		}
		Capacity Slots;
		SlotReservation Slot(Slots);
		WelcomeQueue Queue(Slot);
		bool Passed = ExpectError(Step, Peer.Complete(Welcome, Slot, Queue, OnTime), AdmissionCompletionError::InvalidWelcome);
		Passed &= ExpectError("invalid welcome closes that admission even if the configuration is corrected",
			Peer.Complete(MakeWelcome(11), Slot, Queue, OnTime), AdmissionCompletionError::InvalidState);
		Passed &= Expect("invalid welcome closes before changing capacity or queuing data", Peer.GetState() == PendingAdmissionState::Closing &&
			Slot.Attempts == 0 && Slot.Commits == 0 && Slot.Cancellations == 0 && Queue.Attempts == 0 && Slots.Occupied == 0);
		return Passed;
	};
	auto Welcome = MakeWelcome(11);
	Welcome.ContentId.back() = 1;
	bool Passed = NoSideEffects(Welcome, "welcome cannot advertise content different from the verified hello");
	Welcome = MakeWelcome(11);
	Welcome.RulesId.back() = 1;
	Passed &= NoSideEffects(Welcome, "welcome cannot advertise rules different from the verified hello");
	Welcome = MakeWelcome(0);
	Passed &= NoSideEffects(Welcome, "unassigned connection epoch cannot be published");
	Welcome = MakeWelcome(11);
	Welcome.ServerSession = 0;
	Passed &= NoSideEffects(Welcome, "unassigned server session cannot be published");
	Welcome = MakeWelcome(11);
	Welcome.MapId = {};
	Passed &= NoSideEffects(Welcome, "invalid map ID cannot be published");
	Welcome = MakeWelcome(11);
	Welcome.Rates.InputMilliHz = 0;
	Passed &= NoSideEffects(Welcome, "invalid input rate cannot be published");
	return Passed;
}

bool Run()
{
	bool Passed = CheckWelcome();
	Passed &= CheckFullThenDisconnect();
	Passed &= CheckQueueFailure();
	Passed &= CheckStateAndDeadline();
	Passed &= CheckInvalidWelcome();
	if (!Passed)
	{
		return false;
	}
	std::puts("Admission completion checks passed");
	return true;
}
}
