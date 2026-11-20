#include "CatchUpDespawn.h"
#include "CatchUpEventQueue.h"
#include "ClientCatchUp.h"
#include "PeerJoin.h"
#include "../../../xrGame/Multiplayer/CombatantRegistry.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>

namespace CatchUpDespawnTests
{
using namespace Multiplayer;
using namespace Multiplayer::Wire;

constexpr u8 Golden[] = {
	0x49, 0x58, 0x4d, 0x50, 0x01, 0x00, 0x0a, 0x00, 0x23, 0x00,
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
	0xd4, 0xc3, 0xb2, 0xa1, 0x04, 0x03, 0x02, 0x01, 0x98, 0xba, 0xdc, 0xfe,
	0x04, 0x03, 0x02, 0x01, 0xfe, 0xff, 0xff, 0xff,
	0xef, 0xcd, 0xab, 0x89, 0x67, 0x45, 0x23, 0x01, 0x05, 0x04, 0xd0, 0xfe, 0x01,
	0x40, 0x30, 0x20, 0x10, 0x36, 0xb2, 0x88, 0x77, 0x66, 0x55, 0xdc, 0xcb, 0xba, 0xa9,
};
constexpr JoinIdentity Receiver{0x8877665544332211ull, 0xa1b2c3d4u, 0x01020304u};
constexpr CatchUpDespawn Expected{
	Receiver, 0xfedcba98u, 0x01020304u, 0xfffffffeu, 0x0123456789abcdefull, 0xfed00405u,
	{0x10203040u}, {0xb236u, 0x55667788u, 0xa9bacbdcu},
};

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

template <class Value, class Error>
static bool ExpectError(const char* Step, const std::expected<Value, Error>& Result, Error ExpectedError)
{
	return Expect(Step, !Result && Result.error() == ExpectedError);
}

static bool CheckGolden()
{
	const auto Decoded = DecodeCatchUpDespawn(Golden);
	bool Passed = Expect("independent despawn frame decodes all fields", Decoded && Decoded->GetMessage() == Expected);

	std::array<u8, sizeof(Golden)> Output{};
	const auto Written = EncodeCatchUpDespawn(Expected, Output);
	Passed &= Expect("despawn encoder matches the independent frame", Written == sizeof(Golden) && std::ranges::equal(Output, Golden));
	return Passed;
}

static bool CheckMalformedFrames()
{
	constexpr std::size_t HeaderBytes = 30;
	constexpr std::size_t LengthOffset = 8;
	bool Passed = true;
	for (std::size_t Length = 0; Length < sizeof(Golden); ++Length)
	{
		Passed &= ExpectError("every truncated datagram is rejected",
			DecodeCatchUpDespawn(xr_span(Golden).first(Length)), BoundMessageError::InvalidEnvelope);
		if (Length >= HeaderBytes)
		{
			auto Bytes = std::to_array(Golden);
			Bytes[LengthOffset] = static_cast<u8>(Length - HeaderBytes);
			Passed &= ExpectError("a complete envelope with a truncated event is rejected",
				DecodeCatchUpDespawn(xr_span(Bytes).first(Length)), BoundMessageError::TruncatedPayload);
		}
	}
	std::array<u8, sizeof(Golden) + 1> Extra{};
	std::ranges::copy(Golden, Extra.begin());
	++Extra[LengthOffset];
	Passed &= ExpectError("extra framed event data is rejected", DecodeCatchUpDespawn(Extra), BoundMessageError::TrailingPayload);

	auto Bytes = std::to_array(Golden);
	Bytes[6] = 0x09;
	Passed &= ExpectError("the join barrier tag cannot carry a despawn", DecodeCatchUpDespawn(Bytes), BoundMessageError::WrongMessageType);
	Bytes = std::to_array(Golden);
	Bytes[7] = 1;
	Passed &= ExpectError("reserved flags are rejected", DecodeCatchUpDespawn(Bytes), BoundMessageError::InvalidEnvelope);
	Bytes = std::to_array(Golden);
	std::fill_n(Bytes.begin() + 10, 8, 0);
	Passed &= ExpectError("a missing session is unbound", DecodeCatchUpDespawn(Bytes), BoundMessageError::UnboundJoin);
	Bytes = std::to_array(Golden);
	std::fill_n(Bytes.begin() + 22, 4, 0);
	Passed &= ExpectError("a missing connection is unbound", DecodeCatchUpDespawn(Bytes), BoundMessageError::UnboundJoin);

	struct InvalidField
	{
		std::size_t Offset;
		std::size_t Size;
		u8 Fill;
	};
	constexpr InvalidField Fields[] = {{38, 8, 0}, {51, 4, 0}, {55, 2, 0xff}, {57, 4, 0}, {61, 4, 0}};
	for (const auto Field : Fields)
	{
		Bytes = std::to_array(Golden);
		std::fill_n(Bytes.begin() + Field.Offset, Field.Size, Field.Fill);
		Passed &= ExpectError("an unassigned event or combatant field is rejected", DecodeCatchUpDespawn(Bytes), BoundMessageError::InvalidPayload);
	}

	for (u16 Kind = 0; Kind <= 0xff; ++Kind)
	{
		if (Kind == 1)
		{
			continue;
		}
		Bytes = std::to_array(Golden);
		Bytes[50] = static_cast<u8>(Kind);
		Passed &= ExpectError("only the assigned event kind is accepted", DecodeCatchUpDespawn(Bytes), BoundMessageError::InvalidPayload);
	}
	return Passed;
}

static bool CheckEncodingAndOwnership()
{
	std::array<u8, sizeof(Golden) + 2> Output;
	Output.fill(0x5a);
	const auto Canary = Output;
	bool Passed = true;
	for (std::size_t Size = 0; Size < sizeof(Golden); ++Size)
	{
		Passed &= ExpectError("an undersized output rejects the full event", EncodeCatchUpDespawn(Expected, xr_span(Output).first(Size)),
			BoundMessageError::OutputTooSmall);
		Passed &= Expect("a failed encode leaves the full output unchanged", Output == Canary);
	}

	auto CheckInvalid = [&](const CatchUpDespawn& Invalid, BoundMessageError Error)
	{
		return ExpectError("an unassigned outgoing field is rejected", EncodeCatchUpDespawn(Invalid, Output), Error) &&
			Expect("invalid outgoing fields leave the output unchanged", Output == Canary);
	};
	auto Invalid = Expected;
	Invalid.Identity.ConnectionEpoch = 0;
	Passed &= CheckInvalid(Invalid, BoundMessageError::UnboundJoin);
	Invalid = Expected;
	Invalid.Identity.ServerSession = 0;
	Passed &= CheckInvalid(Invalid, BoundMessageError::UnboundJoin);
	Invalid = Expected;
	Invalid.EventId = 0;
	Passed &= CheckInvalid(Invalid, BoundMessageError::InvalidPayload);
	Invalid = Expected;
	Invalid.Participant = {};
	Passed &= CheckInvalid(Invalid, BoundMessageError::InvalidPayload);
	Invalid = Expected;
	Invalid.Body.ObjectId = 0xffff;
	Passed &= CheckInvalid(Invalid, BoundMessageError::InvalidPayload);
	Invalid = Expected;
	Invalid.Body.Generation = 0;
	Passed &= CheckInvalid(Invalid, BoundMessageError::InvalidPayload);
	Invalid = Expected;
	Invalid.Body.LifeId = 0;
	Passed &= CheckInvalid(Invalid, BoundMessageError::InvalidPayload);

	auto Buffer = xr_span(Output).subspan(1);
	const auto Written = EncodeCatchUpDespawn(Expected, Buffer);
	Passed &= Expect("a successful write preserves surrounding storage", Written == sizeof(Golden) &&
		std::ranges::equal(Buffer.first(sizeof(Golden)), Golden) && Output.front() == Canary.front() && Output.back() == Canary.back());

	auto Receive = std::to_array(Golden);
	const auto Decoded = DecodeCatchUpDespawn(Receive);
	if (!Expect("the event wrapper decodes from reused receive storage", Decoded.has_value()))
	{
		return false;
	}
	const auto Event = Decoded->AsEvent();
	Passed &= Expect("the catch-up view carries the checked frame and derived scope", Event.Identity == Expected.Identity &&
		Event.BaselineId == Expected.BaselineId && Event.Sequence == Expected.StateEventSequence &&
		Event.Bytes.data() == Receive.data() && Event.Bytes.size() == Receive.size());
	Receive.fill(0x5a);
	Passed &= Expect("the decoded identities and event fields are owned values", Decoded->GetMessage() == Expected);

	auto AtLimit = Expected;
	constexpr u32 MaxId = std::numeric_limits<u32>::max();
	AtLimit.Participant = {MaxId};
	AtLimit.Body = {65534, MaxId, MaxId};
	const auto LimitSize = EncodeCatchUpDespawn(AtLimit, Receive);
	const auto LimitMessage = LimitSize ? DecodeCatchUpDespawn(xr_span(Receive).first(*LimitSize)) : std::unexpected(LimitSize.error());
	Passed &= Expect("the highest engine slot and full-width combatant IDs are valid", LimitMessage && LimitMessage->GetMessage() == AtLimit);
	return Passed;
}

class FixtureApplier final : public IClientCatchUpApplier
{
public:
	explicit FixtureApplier(CombatantRegistry& Bodies) : Bodies(Bodies) {}

	bool ApplyEvent(const CatchUpEvent& Event) noexcept override
	{
		const auto Decoded = DecodeCatchUpDespawn(Event.Bytes);
		if (!Decoded)
		{
			return false;
		}
		const auto& Despawn = Decoded->GetMessage();
		const WorldIdentity World{Despawn.Identity.ServerSession, Despawn.Identity.WorldEpoch};
		return Bodies.Resolve(World, Despawn.Body) == Despawn.Participant && Bodies.RetireBody(World, Despawn.Body);
	}

	bool ApplyBarrier(JoinIdentity, JoinBarrier) noexcept override
	{
		return true;
	}

private:
	CombatantRegistry& Bodies;
};

static bool CheckCatchUpHandoff()
{
	using namespace std::chrono_literals;
	constexpr auto Now = PeerJoin::Clock::time_point{} + 1s;
	constexpr JoinIdentity Peer{Receiver.ServerSession, 0, Receiver.ConnectionEpoch};
	constexpr WorldIdentity World{Peer.ServerSession, Peer.WorldEpoch};
	constexpr BaselineBoundary Baseline{0, 0, std::numeric_limits<u32>::max()};
	constexpr JoinBarrier Barrier{Baseline.BaselineId, 5, 0, 0};

	CombatantRegistry Bodies(World);
	FixtureApplier Applier(Bodies);
	const auto Participant = Bodies.AddParticipant();
	const auto OtherParticipant = Bodies.AddParticipant();
	const auto Previous = Participant ? Bodies.BindBody(World, *Participant, 0) : std::nullopt;
	if (!Expect("the fixture can retire the previous life", Previous && Bodies.RetireBody(World, *Previous) && OtherParticipant))
	{
		return false;
	}
	const auto Current = Bodies.BindBody(World, *Participant, 0);
	if (!Expect("the current body reuses the engine slot", Current && Current != Previous))
	{
		return false;
	}

	const CatchUpDespawn Message{Peer, 0, Baseline.BaselineId, 0, std::numeric_limits<u64>::max(), 0, *Participant, *Current};
	std::array<u8, sizeof(Golden)> Send{};
	const auto Written = EncodeCatchUpDespawn(Message, Send);
	const auto Encoded = Written ? DecodeCatchUpDespawn(xr_span(Send).first(*Written)) : std::unexpected(Written.error());
	if (!Expect("zero counters and slot plus a full-width event ID are valid", Encoded && Encoded->GetMessage() == Message))
	{
		return false;
	}

	PeerJoin Server(Peer, Now);
	ClientCatchUp Client(Peer, Baseline);
	std::array<u16, 1> Lengths{};
	std::array<u8, sizeof(Golden)> Storage{};
	std::array<u8, sizeof(Golden)> Receive{};
	CatchUpEventQueue Queue(Peer, Baseline, Lengths, Storage);
	bool Passed = Expect("server and client select the same capture", Server.ConfirmLoaded(Peer, Now).has_value() &&
		Server.SelectBaseline(Peer, Baseline, Now).has_value() && Client.ConfirmBaselineApplied(Peer, Baseline).has_value());
	Passed &= Expect("the server retains the checked wraparound event", Queue.Append(Encoded->AsEvent()).has_value());
	Send.fill(0xa5);
	const auto Peek = Queue.Peek(Receive);
	const auto Delivered = Peek ? DecodeCatchUpDespawn(Peek->Bytes) : std::unexpected(BoundMessageError::InvalidEnvelope);
	if (!Expect("the retained frame survives reuse of its source bytes", Delivered && Delivered->GetMessage() == Message &&
		Queue.CommitSend(*Peek).has_value() && Queue.GetSubmittedWatermark() == 0u))
	{
		return false;
	}
	Passed &= Expect("the barrier covers the submitted despawn", Server.DeclareBarrier(Peer, Barrier, Now).has_value() &&
		Client.DeclareBarrier(Peer, Barrier).has_value());

	auto Reject = [&](const CatchUpDespawn& Wrong, ClientCatchUpError Reason)
	{
		const auto Size = EncodeCatchUpDespawn(Wrong, Send);
		const auto Checked = Size ? DecodeCatchUpDespawn(xr_span(Send).first(*Size)) : std::unexpected(Size.error());
		return Expect("the stale event is still structurally valid", Checked && Checked->GetMessage() == Wrong) &&
			ExpectError("the client rejects the stale event", Client.ApplyEvent(Checked->AsEvent(), Applier), Reason);
	};
	auto Wrong = Message;
	++Wrong.Identity.ConnectionEpoch;
	Passed &= Reject(Wrong, ClientCatchUpError::IdentityMismatch);
	Wrong = Message;
	Wrong.BaselineId = std::numeric_limits<u32>::max();
	Passed &= Reject(Wrong, ClientCatchUpError::BaselineMismatch);
	Wrong = Message;
	Wrong.Body = *Previous;
	Passed &= Reject(Wrong, ClientCatchUpError::ApplicationFailed);
	Wrong = Message;
	Wrong.Participant = *OtherParticipant;
	Passed &= Reject(Wrong, ClientCatchUpError::ApplicationFailed);
	Passed &= Expect("rejected events preserve the replacement body and applied watermark",
		Bodies.Resolve(World, *Current) == Participant && Client.GetAppliedWatermark() == Baseline.EventWatermark);
	Passed &= ExpectError("a submitted but unapplied despawn keeps the barrier pending",
		Client.CompleteBarrier(Peer, Barrier, 1, Applier), ClientCatchUpError::BarrierPending);

	Passed &= Expect("the exact current body retires and advances the applied watermark",
		Client.ApplyEvent(Delivered->AsEvent(), Applier).has_value() && !Bodies.Resolve(World, *Current) && Client.GetAppliedWatermark() == 0u);
	Passed &= ExpectError("duplicate delivery cannot apply again", Client.ApplyEvent(Delivered->AsEvent(), Applier),
		ClientCatchUpError::UnexpectedSequence);
	const auto Ack = Client.CompleteBarrier(Peer, Barrier, 1, Applier);
	Passed &= Expect("the server accepts readiness after the despawn applies", Ack &&
		Server.AcknowledgeBarrier(Ack->Identity, Ack->Barrier, Now).has_value() && Server.GetState() == PeerJoinState::ReadySpectator);
	return Passed;
}

bool Run()
{
	bool Passed = CheckGolden();
	Passed &= CheckMalformedFrames();
	Passed &= CheckEncodingAndOwnership();
	Passed &= CheckCatchUpHandoff();
	if (!Passed)
	{
		return false;
	}
	std::puts("Catch-up despawn codec checks passed");
	return true;
}
}
