#include "ClientJoin.h"
#include "PeerJoin.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace ClientJoinTests
{
using namespace Multiplayer;
using namespace Multiplayer::Wire;

constexpr u8 GoldenLoaded[] = {
	0x49, 0x58, 0x4d, 0x50, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00,
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
	0xd4, 0xc3, 0xb2, 0xa1, 0x04, 0x03, 0x02, 0x01, 0x98, 0xba, 0xdc, 0xfe,
};
constexpr u8 GoldenReady[] = {
	0x49, 0x58, 0x4d, 0x50, 0x01, 0x00, 0x05, 0x00, 0x10, 0x00,
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
	0xd4, 0xc3, 0xb2, 0xa1, 0x04, 0x03, 0x02, 0x01, 0xef, 0xcd, 0xab, 0x89,
	0x78, 0x56, 0x34, 0x12, 0xe0, 0xac, 0x68, 0x24, 0xfe, 0xff, 0xff, 0xff, 0x12, 0x34, 0x56, 0x78,
};
constexpr JoinIdentity Identity{0x8877665544332211ull, 0xa1b2c3d4u, 0x01020304u};
constexpr ClientMapLoaded Loaded{Identity, 0xfedcba98u};
constexpr ReadyAcknowledgement Ready{Identity, 0x89abcdefu, {0x12345678u, 0x2468ace0u, 0xfffffffeu, 0x78563412u}};
constexpr std::size_t HeaderBytes = 30;
constexpr std::size_t TypeOffset = 6;
constexpr std::size_t SizeOffset = 8;
constexpr std::size_t SessionOffset = 10;
constexpr std::size_t WorldOffset = 18;
constexpr std::size_t ConnectionOffset = 22;

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

template <class Value>
static bool ExpectError(const char* Step, const std::expected<Value, ClientJoinError>& Result, ClientJoinError Expected)
{
	if (!Result && Result.error() == Expected)
	{
		return true;
	}
	std::fprintf(stderr, "%s: expected %d, got %d\n", Step, static_cast<int>(Expected), Result ? -1 : static_cast<int>(Result.error()));
	return false;
}

static bool CheckGolden()
{
	const auto DecodedLoaded = DecodeClientMapLoaded(GoldenLoaded);
	const auto DecodedReady = DecodeReadyAcknowledgement(GoldenReady);
	bool Passed = Expect("golden loading confirmation decodes identity and sequence", DecodedLoaded &&
		DecodedLoaded->Identity == Identity && DecodedLoaded->MessageSequence == Loaded.MessageSequence);
	Passed &= Expect("golden ready acknowledgement decodes identity, sequence and exact barrier", DecodedReady &&
		DecodedReady->Identity == Identity && DecodedReady->MessageSequence == Ready.MessageSequence && DecodedReady->Barrier == Ready.Barrier);

	std::array<u8, sizeof(GoldenLoaded)> LoadedBytes{};
	const auto LoadedSize = EncodeClientMapLoaded(Loaded, LoadedBytes);
	Passed &= Expect("loading confirmation encodes literal header bytes", LoadedSize == sizeof(GoldenLoaded) && std::ranges::equal(LoadedBytes, GoldenLoaded));
	std::array<u8, sizeof(GoldenReady)> ReadyBytes{};
	const auto ReadySize = EncodeReadyAcknowledgement(Ready, ReadyBytes);
	Passed &= Expect("ready acknowledgement encodes literal header and payload bytes", ReadySize == sizeof(GoldenReady) && std::ranges::equal(ReadyBytes, GoldenReady));
	return Passed;
}

template <std::size_t Size, class Decoder>
static bool CheckDecoding(const u8 (&Golden)[Size], Decoder Decode, u8 OtherTag)
{
	bool Passed = true;
	for (std::size_t Length = 0; Length < Size; ++Length)
	{
		Passed &= ExpectError("truncated datagram cannot decode", Decode(xr_span(Golden).first(Length)), ClientJoinError::InvalidEnvelope);
	}
	for (std::size_t Length = HeaderBytes; Length < Size; ++Length)
	{
		auto Bytes = std::to_array(Golden);
		Bytes[SizeOffset] = static_cast<u8>(Length - HeaderBytes);
		Passed &= ExpectError("an intact envelope can still have a truncated typed payload", Decode(xr_span(Bytes).first(Length)), ClientJoinError::TruncatedPayload);
	}

	std::array<u8, Size + 1> Extra{};
	std::ranges::copy(Golden, Extra.begin());
	Passed &= ExpectError("unframed trailing byte is an envelope error", Decode(Extra), ClientJoinError::InvalidEnvelope);
	++Extra[SizeOffset];
	Passed &= ExpectError("a framed extra byte is still forbidden", Decode(Extra), ClientJoinError::TrailingPayload);

	auto Bytes = std::to_array(Golden);
	Bytes[0] = 0xff;
	Passed &= ExpectError("join message requires protocol magic", Decode(Bytes), ClientJoinError::InvalidEnvelope);
	Bytes = std::to_array(Golden);
	Bytes[4] = 2;
	Passed &= ExpectError("join message rejects unknown version", Decode(Bytes), ClientJoinError::InvalidEnvelope);
	Bytes = std::to_array(Golden);
	Bytes[7] = 1;
	Passed &= ExpectError("join message rejects reserved flags", Decode(Bytes), ClientJoinError::InvalidEnvelope);
	Bytes = std::to_array(Golden);
	Bytes[TypeOffset] = 0xff;
	Passed &= ExpectError("join message rejects unassigned tag", Decode(Bytes), ClientJoinError::InvalidEnvelope);
	Bytes[TypeOffset] = OtherTag;
	Passed &= ExpectError("other join tag cannot select a different typed decoder", Decode(Bytes), ClientJoinError::WrongMessageType);
	Bytes[TypeOffset] = 0x02;
	Passed &= ExpectError("server welcome cannot decode as a client join message", Decode(Bytes), ClientJoinError::WrongMessageType);

	Bytes = std::to_array(Golden);
	std::fill_n(Bytes.begin() + SessionOffset, 8, 0);
	Passed &= ExpectError("zero server session is unbound even when connection is assigned", Decode(Bytes), ClientJoinError::UnboundJoin);
	Bytes = std::to_array(Golden);
	std::fill_n(Bytes.begin() + ConnectionOffset, 4, 0);
	Passed &= ExpectError("zero connection epoch is unbound even when session is assigned", Decode(Bytes), ClientJoinError::UnboundJoin);
	Bytes = std::to_array(Golden);
	std::fill_n(Bytes.begin() + WorldOffset, 4, 0);
	const auto Initial = Decode(Bytes);
	Passed &= Expect("the initial world may be zero in a bound message", Initial &&
		Initial->Identity == JoinIdentity{Identity.ServerSession, 0, Identity.ConnectionEpoch});
	std::fill_n(Bytes.begin() + SessionOffset, 16, 0);
	Passed &= ExpectError("unbound join frame cannot decode", Decode(Bytes), ClientJoinError::UnboundJoin);
	return Passed;
}

template <std::size_t Size, class Message, class Encoder>
static bool CheckEncoding(const u8 (&Golden)[Size], Message Original, Encoder Encode)
{
	std::array<u8, Size + 4> Storage;
	Storage.fill(0x77);
	const auto Untouched = Storage;
	bool Passed = true;
	for (std::size_t Length = 0; Length < Size; ++Length)
	{
		Passed &= ExpectError("undersized output fails", Encode(Original, xr_span(Storage).first(Length)), ClientJoinError::OutputTooSmall);
		Passed &= Expect("undersized output leaves the entire buffer intact", Storage == Untouched);
	}
	constexpr JoinIdentity Unbound[] = {{}, {0, Identity.WorldEpoch, Identity.ConnectionEpoch}, {Identity.ServerSession, Identity.WorldEpoch, 0}};
	for (const auto& Invalid : Unbound)
	{
		auto InvalidMessage = Original;
		InvalidMessage.Identity = Invalid;
		Passed &= ExpectError("unbound outgoing join message fails", Encode(InvalidMessage, Storage), ClientJoinError::UnboundJoin);
		Passed &= Expect("invalid identity leaves the entire output intact", Storage == Untouched);
	}

	Passed &= Expect("oversized output receives exactly the documented datagram", Encode(Original, Storage) == Size &&
		std::ranges::equal(xr_span(Storage).first(Size), Golden));
	Passed &= Expect("successful encode preserves extra output capacity", std::ranges::equal(xr_span(Storage).subspan(Size), xr_span(Untouched).subspan(Size)));
	return Passed;
}

static bool CheckZeroAndFullWidthFields()
{
	constexpr ReadyAcknowledgement Boundary{{Identity.ServerSession, 0, Identity.ConnectionEpoch}, 0, {0, 0xffffffffu, 0xffffffffu, 0}};
	auto Expected = std::to_array(GoldenReady);
	std::fill_n(Expected.begin() + WorldOffset, 4, 0);
	std::fill_n(Expected.begin() + 26, 4, 0);
	std::fill_n(Expected.begin() + HeaderBytes, 4, 0);
	std::fill_n(Expected.begin() + HeaderBytes + 4, 8, 0xff);
	std::fill_n(Expected.begin() + HeaderBytes + 12, 4, 0);
	auto Incoming = Expected;
	const auto Decoded = DecodeReadyAcknowledgement(Incoming);
	Incoming.fill(0xa5);
	bool Passed = Expect("zero and full-width fields decode into owned values", Decoded && Decoded->Identity == Boundary.Identity &&
		Decoded->MessageSequence == 0 && Decoded->Barrier == Boundary.Barrier);
	Passed &= Expect("initial world and full-width counters encode exactly", EncodeReadyAcknowledgement(Boundary, Incoming) == Expected.size() &&
		Incoming == Expected);
	return Passed;
}

static bool CheckReadinessHandoff()
{
	using namespace std::chrono_literals;
	constexpr auto Now = PeerJoin::Clock::time_point{} + 10s;
	const auto Map = DecodeClientMapLoaded(GoldenLoaded);
	const auto Old = DecodeReadyAcknowledgement(GoldenReady);
	if (!Expect("readiness fixtures decode", Map && Old))
	{
		return false;
	}
	PeerJoin Peer(Identity, Now);
	bool Passed = Expect("decoded map-loaded matches the connection owner's identity", Peer.ConfirmLoaded(Map->Identity, Now).has_value());
	Passed &= Expect("decoded acknowledgement cannot declare its own barrier", !Peer.AcknowledgeBarrier(Old->Identity, Old->Barrier, Now) &&
		Peer.GetState() == PeerJoinState::Synchronizing);
	constexpr BaselineBoundary Baseline{Ready.Barrier.BaselineId, Ready.Barrier.SnapshotTick, Ready.Barrier.EventWatermark};
	Passed &= Expect("owner stages the barrier for the admitted peer", Peer.SelectBaseline(Identity, Baseline, Now).has_value() &&
		Peer.DeclareBarrier(Identity, Ready.Barrier, Now).has_value());

	auto WrongConnection = std::to_array(GoldenReady);
	++WrongConnection[ConnectionOffset];
	const auto Foreign = DecodeReadyAcknowledgement(WrongConnection);
	Passed &= Expect("structurally valid acknowledgement from another connection decodes", Foreign && Foreign->Identity != Identity);
	if (Foreign)
	{
		const auto Rejected = Peer.AcknowledgeBarrier(Foreign->Identity, Foreign->Barrier, Now + 1s);
		Passed &= Expect("readiness gate rejects the decoded foreign connection", !Rejected && Rejected.error() == PeerJoinError::IdentityMismatch);
	}

	auto NewIdentity = Identity;
	++NewIdentity.WorldEpoch;
	Passed &= Expect("owner can declare the same numeric barrier for the next world", Peer.AdvanceWorld(NewIdentity.WorldEpoch, Now + 2s).has_value() &&
		Peer.SelectBaseline(NewIdentity, Baseline, Now + 2s).has_value() && Peer.DeclareBarrier(NewIdentity, Ready.Barrier, Now + 2s).has_value());
	const auto Stale = Peer.AcknowledgeBarrier(Old->Identity, Old->Barrier, Now + 3s);
	Passed &= Expect("readiness gate rejects a correctly encoded acknowledgement for the old world", !Stale && Stale.error() == PeerJoinError::IdentityMismatch &&
		Peer.GetState() == PeerJoinState::Synchronizing);
	auto NewFrame = std::to_array(GoldenReady);
	++NewFrame[WorldOffset];
	const auto Current = DecodeReadyAcknowledgement(NewFrame);
	Passed &= Expect("current decoded acknowledgement advances to spectator readiness", Current &&
		Peer.AcknowledgeBarrier(Current->Identity, Current->Barrier, Now + 3s).has_value() && Peer.GetState() == PeerJoinState::ReadySpectator);
	return Passed;
}

bool Run()
{
	bool Passed = CheckGolden();
	Passed &= CheckDecoding(GoldenLoaded, DecodeClientMapLoaded, 0x05);
	Passed &= CheckDecoding(GoldenReady, DecodeReadyAcknowledgement, 0x04);
	Passed &= CheckEncoding(GoldenLoaded, Loaded, EncodeClientMapLoaded);
	Passed &= CheckEncoding(GoldenReady, Ready, EncodeReadyAcknowledgement);
	Passed &= CheckZeroAndFullWidthFields();
	Passed &= CheckReadinessHandoff();
	if (!Passed)
	{
		return false;
	}
	std::puts("Client join codec checks passed");
	return true;
}
}
