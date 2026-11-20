#include "WireEnvelope.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace WireEnvelopeTests
{
using namespace Multiplayer::Wire;

constexpr u8 GoldenWelcome[] = {
	0x49, 0x58, 0x4d, 0x50, 0x01, 0x00, 0x02, 0x00, 0x03, 0x00,
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
	0xd4, 0xc3, 0xb2, 0xa1, 0x04, 0x03, 0x02, 0x01, 0xef, 0xcd, 0xab, 0x89,
	0x00, 0xa5, 0xff,
};
constexpr EnvelopeHeader Welcome{MessageType::ServerWelcome, 0x8877665544332211ull, 0xa1b2c3d4u, 0x01020304u, 0x89abcdefu};
constexpr u8 WelcomePayload[] = {0x00, 0xa5, 0xff};

constexpr std::size_t FixtureHeaderBytes = 30;
constexpr std::size_t FixtureMessageCap = 1100;
constexpr u8 Canary = 0x7b;

constexpr u8 GoldenHello[] = {
	0x49, 0x58, 0x4d, 0x50, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
};
constexpr EnvelopeHeader Hello{MessageType::ClientHello, 0, 0, 0, 0xffffffffu};

template <class Value>
static bool ExpectError(const char* Case, const std::expected<Value, EnvelopeError>& Result, EnvelopeError Expected)
{
	if (!Result && Result.error() == Expected)
	{
		return true;
	}
	const int Actual = Result ? -1 : static_cast<int>(Result.error());
	std::fprintf(stderr, "%s: expected error %d, got %d (-1 means success)\n", Case, static_cast<int>(Expected), Actual);
	return false;
}

static bool Expect(const char* Case, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Case);
	}
	return Passed;
}

static bool CheckGolden(std::span<const u8> Golden, EnvelopeHeader Header, std::span<const u8> Payload)
{
	const auto Decoded = DecodeEnvelope(Golden);
	bool Passed = Expect("hand-authored envelope did not decode to its expected fields", Decoded && Decoded->Header == Header &&
		std::ranges::equal(Decoded->Payload, Payload));
	if (Decoded)
	{
		Passed &= Expect("decoded payload must borrow the original message", Decoded->Payload.data() == Golden.data() + FixtureHeaderBytes);
	}

	std::array<u8, sizeof(GoldenWelcome) + 2> Storage;
	Storage.fill(Canary);
	const auto Output = std::span(Storage).subspan(1);
	const auto Written = EncodeEnvelope(Header, Payload, Output);
	Passed &= Expect("encoded envelope did not match independent bytes", Written && *Written == Golden.size() &&
		std::ranges::equal(Output.first(Golden.size()), Golden));
	Passed &= Expect("encode touched bytes outside the message", Storage.front() == Canary &&
		std::ranges::all_of(Output.subspan(Golden.size()), [](u8 Byte) { return Byte == Canary; }));
	return Passed;
}

static bool CheckLengths()
{
	bool Passed = ExpectError("default empty input", DecodeEnvelope({}), EnvelopeError::ShortHeader);
	const auto Packet = std::span(GoldenWelcome);
	for (std::size_t Length = 0; Length < Packet.size(); ++Length)
	{
		const auto Expected = Length < FixtureHeaderBytes ? EnvelopeError::ShortHeader : EnvelopeError::PayloadLengthMismatch;
		Passed &= ExpectError("truncated packet prefix", DecodeEnvelope(Packet.first(Length)), Expected);
	}

	std::array<u8, sizeof(GoldenWelcome) + 1> Trailing{};
	std::ranges::copy(GoldenWelcome, Trailing.begin());
	Passed &= ExpectError("trailing byte", DecodeEnvelope(Trailing), EnvelopeError::PayloadLengthMismatch);

	for (const u16 Length : {u16{0}, u16{1}, u16{4}, u16{0x0300}, u16{0xffff}})
	{
		auto Bytes = std::to_array(GoldenWelcome);
		Bytes[8] = static_cast<u8>(Length);
		Bytes[9] = static_cast<u8>(Length >> 8);
		Passed &= ExpectError("incorrect payload length", DecodeEnvelope(Bytes), EnvelopeError::PayloadLengthMismatch);
	}
	return Passed;
}

static bool CheckHeaderRejection()
{
	bool Passed = true;
	for (std::size_t Offset = 0; Offset < 4; ++Offset)
	{
		auto Bytes = std::to_array(GoldenWelcome);
		Bytes[Offset] ^= 0xff;
		Passed &= ExpectError("wrong magic byte", DecodeEnvelope(Bytes), EnvelopeError::InvalidMagic);
	}
	for (const u16 Version : {u16{0}, u16{2}, u16{0x0100}, u16{0xffff}})
	{
		auto Bytes = std::to_array(GoldenWelcome);
		Bytes[4] = static_cast<u8>(Version);
		Bytes[5] = static_cast<u8>(Version >> 8);
		Passed &= ExpectError("unsupported version", DecodeEnvelope(Bytes), EnvelopeError::UnsupportedVersion);
	}
	for (u16 Tag = 0; Tag <= 0xff; ++Tag)
	{
		if (Tag >= 1 && Tag <= 3)
		{
			continue;
		}
		auto Bytes = std::to_array(GoldenWelcome);
		Bytes[6] = static_cast<u8>(Tag);
		if (!ExpectError("unassigned message type", DecodeEnvelope(Bytes), EnvelopeError::UnknownMessageType))
		{
			Passed = false;
			break;
		}
	}
	for (u16 Flags = 1; Flags <= 0xff; ++Flags)
	{
		auto Bytes = std::to_array(GoldenWelcome);
		Bytes[7] = static_cast<u8>(Flags);
		if (!ExpectError("reserved flags set", DecodeEnvelope(Bytes), EnvelopeError::UnsupportedFlags))
		{
			Passed = false;
			break;
		}
	}
	return Passed;
}

static bool CheckCaps()
{
	std::array<u8, FixtureMessageCap - FixtureHeaderBytes> Payload;
	for (std::size_t Index = 0; Index < Payload.size(); ++Index)
	{
		Payload[Index] = static_cast<u8>(Index * 37u);
	}
	std::array<u8, FixtureMessageCap> Expected{};
	std::ranges::copy(std::span(GoldenWelcome).first(FixtureHeaderBytes), Expected.begin());
	Expected[8] = 0x2e;
	Expected[9] = 0x04;
	std::ranges::copy(Payload, Expected.begin() + FixtureHeaderBytes);
	const auto Decoded = DecodeEnvelope(Expected);
	bool Passed = Expect("exactly 1100 bytes must decode", Decoded && Decoded->Header == Welcome && std::ranges::equal(Decoded->Payload, Payload));

	std::array<u8, FixtureMessageCap> Output{};
	const auto Written = EncodeEnvelope(Welcome, Payload, Output);
	Passed &= Expect("exactly 1100 bytes must encode as the golden frame", Written && *Written == FixtureMessageCap && Output == Expected);

	std::array<u8, FixtureMessageCap + 1> Oversize{};
	std::ranges::copy(Expected, Oversize.begin());
	Oversize[8] = 0x2f;
	Passed &= ExpectError("over cap with otherwise consistent length", DecodeEnvelope(Oversize), EnvelopeError::MessageTooLarge);

	std::array<u8, FixtureMessageCap + 1> LargeOutput;
	LargeOutput.fill(Canary);
	const auto Saved = LargeOutput;
	Passed &= ExpectError("oversized payload", EncodeEnvelope(Welcome, std::span(Oversize).subspan(FixtureHeaderBytes), LargeOutput), EnvelopeError::MessageTooLarge);
	Passed &= Expect("oversized payload changed output", LargeOutput == Saved);
	return Passed;
}

static bool CheckWriteFailures()
{
	std::array<u8, sizeof(GoldenWelcome)> Storage;
	Storage.fill(Canary);
	const auto Saved = Storage;
	bool Passed = ExpectError("default empty output", EncodeEnvelope(Welcome, WelcomePayload, {}), EnvelopeError::OutputTooSmall);
	for (std::size_t Length = 0; Length < Storage.size(); ++Length)
	{
		Passed &= ExpectError("short encode output", EncodeEnvelope(Welcome, WelcomePayload, std::span(Storage).first(Length)), EnvelopeError::OutputTooSmall);
		Passed &= Expect("short encode changed output", Storage == Saved);
	}
	auto Invalid = Welcome;
	Invalid.Type = static_cast<MessageType>(0xff);
	Passed &= ExpectError("unassigned encode tag", EncodeEnvelope(Invalid, WelcomePayload, Storage), EnvelopeError::UnknownMessageType);
	Passed &= Expect("unassigned tag changed output", Storage == Saved);
	return Passed;
}

static bool CheckInPlaceEncoding()
{
	bool Passed = true;
	for (const std::size_t SourceOffset : {std::size_t{0}, FixtureHeaderBytes - 1, FixtureHeaderBytes, FixtureHeaderBytes + 1})
	{
		std::array<u8, sizeof(GoldenWelcome) + 2> Storage;
		Storage.fill(Canary);
		const auto Source = std::span(Storage).subspan(SourceOffset, sizeof(WelcomePayload));
		std::ranges::copy(WelcomePayload, Source.begin());
		const auto Saved = Storage;
		const auto Written = EncodeEnvelope(Welcome, Source, Storage);
		Passed &= Expect("overlapping payload encoded incorrectly", Written && *Written == sizeof(GoldenWelcome) &&
			std::ranges::equal(std::span(Storage).first(sizeof(GoldenWelcome)), GoldenWelcome));
		Passed &= Expect("overlapping encode changed bytes after the frame", std::ranges::equal(
			std::span(Storage).subspan(sizeof(GoldenWelcome)), std::span(Saved).subspan(sizeof(GoldenWelcome))));
	}
	return Passed;
}

bool Run()
{
	bool Passed = CheckGolden(GoldenWelcome, Welcome, WelcomePayload);
	Passed &= CheckGolden(GoldenHello, Hello, {});
	auto RejectBytes = std::to_array(GoldenHello);
	RejectBytes[6] = 0x03;
	auto Reject = Hello;
	Reject.Type = MessageType::AdmissionReject;
	Passed &= CheckGolden(RejectBytes, Reject, {});
	Passed &= CheckLengths();
	Passed &= CheckHeaderRejection();
	Passed &= CheckCaps();
	Passed &= CheckWriteFailures();
	Passed &= CheckInPlaceEncoding();
	if (Passed)
	{
		std::puts("Wire envelope checks passed");
	}
	return Passed;
}
}
