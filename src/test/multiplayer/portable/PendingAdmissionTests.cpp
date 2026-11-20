#include "ClientHello.h"
#include "PendingAdmission.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

namespace PendingAdmissionTests
{
using namespace Multiplayer;
using namespace std::chrono_literals;
using namespace std::string_view_literals;
using Wire::AdmissionRejectReason;
using Wire::ClientHello;

constexpr u8 ValidCredential[] = {0x06, 0x00, 0xff};
constexpr auto Connected = PendingAdmission::Clock::time_point{} + 37s;
constexpr auto Deadline = Connected + 10s;

class FixtureVerifier final : public IAdmissionCredentialVerifier
{
public:
	bool Verify(xr_span<const u8> Credentials) const noexcept override
	{
		++Calls;
		return std::ranges::equal(Credentials, ValidCredential);
	}

	mutable int Calls = 0;
};

static ClientHello MakeHello()
{
	ClientHello Hello;
	Hello.MessageSequence = 9;
	Hello.Build[0] = 0x24;
	Hello.Content[0] = 0x41;
	Hello.Rules[0] = 0x73;
	Hello.DisplayName = "Player"sv;
	Hello.Credentials = ValidCredential;
	return Hello;
}

struct EncodedHello
{
	std::array<u8, 290> Bytes{};
	std::size_t Size = 0;

	[[nodiscard]] xr_span<const u8> View() const noexcept { return xr_span(Bytes).first(Size); }
};

static EncodedHello EncodeFixture(const ClientHello& Hello)
{
	EncodedHello Result;
	const auto Size = Wire::EncodeClientHello(Hello, Result.Bytes);
	if (!Size)
	{
		std::fputs("Unable to encode hello fixture\n", stderr);
		std::exit(EXIT_FAILURE);
	}
	Result.Size = *Size;
	return Result;
}

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

static bool ExpectError(const char* Step, const std::expected<VerifiedHello, PendingAdmissionFailure>& Result, AdmissionRejectReason Reason, bool ShouldReject = true)
{
	if (!Result && Result.error().Reason == Reason && Result.error().ShouldReject == ShouldReject)
	{
		return true;
	}
	std::fprintf(stderr, "%s: expected reason %u and rejection %d, got reason %d and rejection %d\n",
		Step, static_cast<unsigned>(Reason), ShouldReject, Result ? -1 : static_cast<int>(Result.error().Reason), !Result && Result.error().ShouldReject);
	return false;
}

static bool CheckCompatiblePeers(const ClientHello& Hello, const EncodedHello& Packet, const AdmissionRequirements& Requirements)
{
	PendingAdmissionLimit Limit(2);
	auto First = Limit.TryBegin(Deadline).value();
	auto Second = Limit.TryBegin(Deadline).value();
	const FixtureVerifier Verifier;
	bool Passed = Expect("new peer waits for hello", First.GetState() == PendingAdmissionState::AwaitingHello && Second.GetState() == PendingAdmissionState::AwaitingHello);
	const auto FirstResult = First.ReceiveHello(Packet.View(), Requirements, Verifier, Connected);
	Passed &= Expect("compatible hello with valid credentials reaches slot reservation", FirstResult && FirstResult->MessageSequence == Hello.MessageSequence &&
		FirstResult->DisplayName == Hello.DisplayName && First.GetState() == PendingAdmissionState::AwaitingSlot && Verifier.Calls == 1);
	Passed &= Expect("valid hello does not move another peer", Second.GetState() == PendingAdmissionState::AwaitingHello);

	auto Other = Hello;
	Other.MessageSequence = 0xfffffffeu;
	Other.DisplayName = "Other player"sv;
	const auto OtherPacket = EncodeFixture(Other);
	const auto SecondResult = Second.ReceiveHello(OtherPacket.View(), Requirements, Verifier, Connected + 1ms);
	Passed &= Expect("second compatible peer independently requests a slot", SecondResult && SecondResult->MessageSequence == Other.MessageSequence &&
		SecondResult->DisplayName == Other.DisplayName && Second.GetState() == PendingAdmissionState::AwaitingSlot && Verifier.Calls == 2);
	Passed &= ExpectError("duplicate hello cannot issue a second slot request", First.ReceiveHello(Packet.View(), Requirements, Verifier, Connected + 2ms), AdmissionRejectReason::InvalidState, false);
	Passed &= ExpectError("a different hello cannot replace a verified request", First.ReceiveHello(OtherPacket.View(), Requirements, Verifier, Connected + 3ms), AdmissionRejectReason::InvalidState, false);
	Passed &= Expect("duplicate leaves the original slot request intact and avoids reverification", First.GetState() == PendingAdmissionState::AwaitingSlot && Verifier.Calls == 2);
	return Passed;
}

static bool CheckWireFailures(const EncodedHello& Packet, const AdmissionRequirements& Requirements)
{
	bool Passed = true;
	const auto RejectPacket = [&Passed, &Requirements](xr_span<const u8> Bytes, AdmissionRejectReason Reason)
	{
		const FixtureVerifier Verifier;
		PendingAdmissionLimit Limit(1);
		auto Peer = Limit.TryBegin(Deadline).value();
		Passed &= ExpectError("invalid wire hello has the specified admission reason", Peer.ReceiveHello(Bytes, Requirements, Verifier, Connected), Reason);
		Passed &= Expect("wire rejection closes the peer before credentials", Peer.GetState() == PendingAdmissionState::Closing && Verifier.Calls == 0);
	};

	RejectPacket(Packet.View().first(Packet.Size - 1), AdmissionRejectReason::MalformedHello);
	auto Bytes = Packet;
	Bytes.Bytes[4] = 2;
	RejectPacket(Bytes.View(), AdmissionRejectReason::UnsupportedProtocol);
	Bytes = Packet;
	Bytes.Bytes[6] = 0xff;
	RejectPacket(Bytes.View(), AdmissionRejectReason::MalformedHello);
	Bytes = Packet;
	Bytes.Bytes[6] = 0x02;
	RejectPacket(Bytes.View(), AdmissionRejectReason::InvalidState);
	Bytes = Packet;
	Bytes.Bytes[17] = 1;
	RejectPacket(Bytes.View(), AdmissionRejectReason::MalformedHello);
	Bytes = Packet;
	constexpr std::size_t DisplayNameOffset = 30 + 3 * 32 + 2;
	Bytes.Bytes[DisplayNameOffset] = 0xff;
	RejectPacket(Bytes.View(), AdmissionRejectReason::MalformedHello);
	return Passed;
}

static bool CheckIdentityMismatches(const EncodedHello& Packet, const AdmissionRequirements& Requirements)
{
	struct MismatchCase
	{
		Wire::CompatibilityId AdmissionRequirements::* Field;
		AdmissionRejectReason Reason;
	};
	constexpr MismatchCase Cases[] = {
		{&AdmissionRequirements::Build, AdmissionRejectReason::IncompatibleBuild},
		{&AdmissionRequirements::Content, AdmissionRejectReason::IncompatibleContent},
		{&AdmissionRequirements::Rules, AdmissionRejectReason::IncompatibleRules},
	};
	bool Passed = true;
	for (const auto& Case : Cases)
	{
		auto Mismatch = Requirements;
		(Mismatch.*Case.Field).back() ^= 0x80;
		const FixtureVerifier Verifier;
		PendingAdmissionLimit Limit(1);
		auto Peer = Limit.TryBegin(Deadline).value();
		Passed &= ExpectError("compatibility check detects the final byte difference", Peer.ReceiveHello(Packet.View(), Mismatch, Verifier, Connected), Case.Reason);
		Passed &= Expect("incompatible peer closes without credential verification", Peer.GetState() == PendingAdmissionState::Closing && Verifier.Calls == 0);
	}
	return Passed;
}

static bool CheckCredentials(const ClientHello& Hello, const EncodedHello& Valid, const AdmissionRequirements& Requirements)
{
	static constexpr u8 WrongCredential[] = {0x06, 0x00, 0xfe};
	struct CredentialCase
	{
		xr_span<const u8> Credentials;
		bool Required;
		bool Accepted;
		int VerifyCalls;
	};
	constexpr CredentialCase Cases[] = {
		{{}, true, false, 0},
		{WrongCredential, true, false, 1},
		{ValidCredential, true, true, 1},
		{{}, false, true, 0},
		{WrongCredential, false, true, 0},
	};
	bool Passed = true;
	for (const auto& Case : Cases)
	{
		auto Presented = Hello;
		Presented.Credentials = Case.Credentials;
		const auto Packet = EncodeFixture(Presented);
		auto Policy = Requirements;
		Policy.RequireCredentials = Case.Required;
		const FixtureVerifier Verifier;
		PendingAdmissionLimit Limit(1);
		auto Peer = Limit.TryBegin(Deadline).value();
		const auto Result = Peer.ReceiveHello(Packet.View(), Policy, Verifier, Connected);
		if (Case.Accepted)
		{
			Passed &= Expect("credential policy allows a slot request", Result && Peer.GetState() == PendingAdmissionState::AwaitingSlot);
		}
		else
		{
			Passed &= ExpectError("missing or wrong credentials use the same reason", Result, AdmissionRejectReason::CredentialsRejected);
			Passed &= Expect("credential rejection closes the pending handshake", Peer.GetState() == PendingAdmissionState::Closing);
			Passed &= ExpectError("valid hello cannot reopen a rejected connection", Peer.ReceiveHello(Valid.View(), Requirements, Verifier, Connected + 1ms), AdmissionRejectReason::InvalidState, false);
		}
		Passed &= Expect("credentials verified exactly when required and present", Verifier.Calls == Case.VerifyCalls);
	}
	return Passed;
}

static bool CheckDeadlines(const EncodedHello& Packet, const AdmissionRequirements& Requirements)
{
	const FixtureVerifier Verifier;
	PendingAdmissionLimit Limit(4);
	auto Awaiting = Limit.TryBegin(Deadline).value();
	bool Passed = Expect("hello wait starts unexpired", !Awaiting.Expire(Deadline - 1ms) && Awaiting.GetState() == PendingAdmissionState::AwaitingHello);
	Passed &= Expect("poll expires pending hello at equality", Awaiting.Expire(Deadline) == AdmissionRejectReason::AdmissionTimedOut &&
		Awaiting.GetState() == PendingAdmissionState::Closing);
	Passed &= Expect("expiry emits only once", !Awaiting.Expire(Deadline + 1ms));
	Passed &= ExpectError("expired peer cannot accept a later hello", Awaiting.ReceiveHello(Packet.View(), Requirements, Verifier, Deadline + 1ms), AdmissionRejectReason::InvalidState, false);

	auto Exact = Limit.TryBegin(Deadline).value();
	Passed &= ExpectError("hello at deadline loses to timeout", Exact.ReceiveHello(Packet.View(), Requirements, Verifier, Deadline), AdmissionRejectReason::AdmissionTimedOut);
	Passed &= Expect("timeout precedes verification", Exact.GetState() == PendingAdmissionState::Closing && Verifier.Calls == 0);
	auto Late = Limit.TryBegin(Deadline).value();
	Passed &= ExpectError("late malformed datagram loses to timeout", Late.ReceiveHello({}, Requirements, Verifier, Deadline + 1ms), AdmissionRejectReason::AdmissionTimedOut);

	auto AwaitingSlot = Limit.TryBegin(Deadline).value();
	const auto Timely = AwaitingSlot.ReceiveHello(Packet.View(), Requirements, Verifier, Deadline - 1ms);
	Passed &= Expect("hello one millisecond early is accepted for slot reservation", Timely && AwaitingSlot.GetState() == PendingAdmissionState::AwaitingSlot);
	Passed &= Expect("slot wait still shares the original deadline", AwaitingSlot.Expire(Deadline) == AdmissionRejectReason::AdmissionTimedOut &&
		AwaitingSlot.GetState() == PendingAdmissionState::Closing && Verifier.Calls == 1);
	return Passed;
}

bool Run()
{
	const auto Hello = MakeHello();
	const auto Packet = EncodeFixture(Hello);
	const AdmissionRequirements Requirements{Hello.Build, Hello.Content, Hello.Rules, true};
	bool Passed = CheckCompatiblePeers(Hello, Packet, Requirements);
	Passed &= CheckWireFailures(Packet, Requirements);
	Passed &= CheckIdentityMismatches(Packet, Requirements);
	Passed &= CheckCredentials(Hello, Packet, Requirements);
	Passed &= CheckDeadlines(Packet, Requirements);
	if (Passed)
	{
		std::puts("Pending admission checks passed");
	}
	return Passed;
}
}
