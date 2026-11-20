#include "ClientHello.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace ClientHelloTests
{
using namespace Multiplayer::Wire;
using namespace std::string_view_literals;

constexpr u8 Golden[] = {
	0x49, 0x58, 0x4d, 0x50, 0x01, 0x00, 0x01, 0x00, 0x6a, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x98, 0xba, 0xdc, 0xfe,
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
	0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
	0x03, 0x00, 0x41, 0xc2, 0xa2, 0x03, 0x00, 0x00, 0xca, 0xff,
};
constexpr u8 CredentialFixture[] = {0x00, 0xca, 0xff};
constexpr CompatibilityId MakeId(u8 First)
{
	CompatibilityId Id{};
	for (std::size_t Index = 0; Index < Id.size(); ++Index)
	{
		Id[Index] = static_cast<u8>(First + Index);
	}
	return Id;
}
constexpr ClientHello Expected{0xfedcba98u, MakeId(0), MakeId(0x40), MakeId(0x80), "A\xc2\xa2"sv, CredentialFixture};

constexpr std::size_t HeaderBytes = 30;
constexpr std::size_t NameLengthOffset = HeaderBytes + 3 * 32;
constexpr std::size_t NameOffset = NameLengthOffset + 2;
constexpr std::size_t CredentialLengthOffset = NameOffset + 3;
constexpr std::size_t CredentialOffset = CredentialLengthOffset + 2;
constexpr u8 Canary = 0x5a;

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

template <class Value>
static bool ExpectError(const char* Step, const std::expected<Value, ClientHelloError>& Result, ClientHelloError Error)
{
	if (!Result && Result.error() == Error)
	{
		return true;
	}
	std::fprintf(stderr, "%s: expected error %d, got %d (-1 means success)\n", Step, static_cast<int>(Error), Result ? -1 : static_cast<int>(Result.error()));
	return false;
}

static bool Matches(const ClientHello& Actual, const ClientHello& Wanted)
{
	return Actual.MessageSequence == Wanted.MessageSequence && Actual.Build == Wanted.Build && Actual.Content == Wanted.Content && Actual.Rules == Wanted.Rules &&
		Actual.DisplayName == Wanted.DisplayName && std::ranges::equal(Actual.Credentials, Wanted.Credentials);
}

static void SetPayloadLength(xr_span<u8> Bytes)
{
	const std::size_t Length = Bytes.size() - HeaderBytes;
	Bytes[8] = static_cast<u8>(Length);
	Bytes[9] = static_cast<u8>(Length >> 8);
}

static bool CheckGolden()
{
	const auto Read = DecodeClientHello(Golden);
	bool Passed = Expect("hand-authored client hello did not decode to the expected fields", Read && Matches(*Read, Expected));
	if (Read)
	{
		Passed &= Expect("hello name and credentials borrow the original message", Read->DisplayName.data() == reinterpret_cast<const char*>(Golden + NameOffset) &&
			Read->Credentials.data() == Golden + CredentialOffset);
	}
	std::array<u8, sizeof(Golden) + 2> Bytes;
	Bytes.fill(Canary);
	const auto Output = xr_span(Bytes).subspan(1);
	const auto Written = EncodeClientHello(Expected, Output);
	Passed &= Expect("hello encoding must match independent golden bytes", Written && *Written == sizeof(Golden) && std::ranges::equal(Output.first(sizeof(Golden)), Golden));
	Passed &= Expect("hello encoding touched bytes outside the message", Bytes.front() == Canary && Bytes.back() == Canary);
	return Passed;
}

static bool CheckEnvelopeAndTruncation()
{
	bool Passed = true;
	for (std::size_t Size = 0; Size < sizeof(Golden); ++Size)
	{
		if (!ExpectError("truncated datagram", DecodeClientHello(xr_span(Golden).first(Size)), ClientHelloError::InvalidEnvelope))
		{
			Passed = false;
			break;
		}
	}
	for (std::size_t Size = HeaderBytes; Size < sizeof(Golden); ++Size)
	{
		auto Bytes = std::to_array(Golden);
		const auto Prefix = xr_span(Bytes).first(Size);
		SetPayloadLength(Prefix);
		if (!ExpectError("valid envelope with a truncated typed payload", DecodeClientHello(Prefix), ClientHelloError::TruncatedPayload))
		{
			Passed = false;
			break;
		}
	}
	for (const std::size_t Offset : {std::size_t{17}, std::size_t{21}, std::size_t{25}})
	{
		auto Bytes = std::to_array(Golden);
		Bytes[Offset] = 1;
		Passed &= ExpectError("hello must have zero session, world, and connection fields", DecodeClientHello(Bytes), ClientHelloError::BoundHello);
	}
	auto WrongType = std::to_array(Golden);
	WrongType[6] = 0x02;
	Passed &= ExpectError("welcome tag cannot decode as hello", DecodeClientHello(WrongType), ClientHelloError::WrongMessageType);
	auto WrongVersion = std::to_array(Golden);
	WrongVersion[4] = 2;
	Passed &= ExpectError("unsupported envelope version cannot decode as hello", DecodeClientHello(WrongVersion), ClientHelloError::UnsupportedVersion);

	std::array<u8, sizeof(Golden) + 1> Trailing{};
	std::ranges::copy(Golden, Trailing.begin());
	SetPayloadLength(Trailing);
	Passed &= ExpectError("valid frame with trailing payload", DecodeClientHello(Trailing), ClientHelloError::TrailingPayload);
	return Passed;
}

static bool CheckRejectedFields()
{
	auto InvalidUtf8 = std::to_array(Golden);
	InvalidUtf8[NameOffset + 1] = 0xc0;
	bool Passed = ExpectError("invalid UTF-8 display name", DecodeClientHello(InvalidUtf8), ClientHelloError::InvalidDisplayName);
	auto EmbeddedZero = std::to_array(Golden);
	EmbeddedZero[NameOffset] = 0;
	Passed &= ExpectError("embedded zero in display name", DecodeClientHello(EmbeddedZero), ClientHelloError::InvalidDisplayName);

	std::array<u8, NameOffset + 33 + 2> LongName{};
	std::ranges::copy(xr_span(Golden).first(NameLengthOffset), LongName.begin());
	SetPayloadLength(LongName);
	LongName[NameLengthOffset] = 33;
	std::ranges::fill(xr_span(LongName).subspan(NameOffset, 33), u8{0x41});
	Passed &= ExpectError("complete display name beyond its byte cap", DecodeClientHello(LongName), ClientHelloError::InvalidDisplayName);
	LongName[NameLengthOffset] = 0;
	LongName[NameLengthOffset + 1] = 1;
	Passed &= ExpectError("display name cap uses both count bytes", DecodeClientHello(LongName), ClientHelloError::InvalidDisplayName);

	std::array<u8, CredentialOffset + 129> LongCredentials{};
	std::ranges::copy(xr_span(Golden).first(CredentialLengthOffset), LongCredentials.begin());
	SetPayloadLength(LongCredentials);
	LongCredentials[CredentialLengthOffset] = 129;
	Passed &= ExpectError("complete credentials beyond their byte cap", DecodeClientHello(LongCredentials), ClientHelloError::CredentialsTooLong);
	LongCredentials[CredentialLengthOffset] = 0;
	LongCredentials[CredentialLengthOffset + 1] = 1;
	Passed &= ExpectError("credential cap uses both count bytes", DecodeClientHello(LongCredentials), ClientHelloError::CredentialsTooLong);
	return Passed;
}

static bool CheckAllowedBounds()
{
	auto Hello = Expected;
	Hello.DisplayName = {};
	Hello.Credentials = {};
	std::array<u8, 130> Minimum{};
	const auto MinSize = EncodeClientHello(Hello, Minimum);
	bool Passed = Expect("empty display preference and credentials may encode", MinSize == Minimum.size() && Minimum[8] == 100 && Minimum[9] == 0);
	const auto MinRead = DecodeClientHello(Minimum);
	Passed &= Expect("empty display preference and credentials may decode", MinRead && Matches(*MinRead, Hello));

	std::array<char, 32> Name;
	Name.fill('A');
	Name[30] = static_cast<char>(0xc2);
	Name[31] = static_cast<char>(0xa2);
	std::array<u8, 128> Opaque;
	Opaque.fill(0xff);
	Opaque.front() = 0;
	Hello.DisplayName = {Name.data(), Name.size()};
	Hello.Credentials = Opaque;
	std::array<u8, 290> Maximum{};
	const auto MaxSize = EncodeClientHello(Hello, Maximum);
	Passed &= Expect("maximum field lengths fit their exact wire size", MaxSize == Maximum.size() && Maximum[8] == 4 && Maximum[9] == 1 &&
		Maximum[NameLengthOffset] == 32 && Maximum[NameOffset + Name.size()] == 128);
	const auto MaxRead = DecodeClientHello(Maximum);
	Passed &= Expect("maximum byte lengths preserve the entire requested name and opaque field", MaxRead && Matches(*MaxRead, Hello));
	return Passed;
}

static bool CheckAtomicEncoding()
{
	std::array<u8, 290> Bytes;
	Bytes.fill(Canary);
	const auto Saved = Bytes;
	bool Passed = ExpectError("default empty hello output", EncodeClientHello(Expected, {}), ClientHelloError::OutputTooSmall);
	for (std::size_t Size = 0; Size < sizeof(Golden); ++Size)
	{
		if (!ExpectError("short hello encode output", EncodeClientHello(Expected, xr_span(Bytes).first(Size)), ClientHelloError::OutputTooSmall) ||
			!Expect("short output remains untouched", Bytes == Saved))
		{
			Passed = false;
			break;
		}
	}
	auto Invalid = Expected;
	for (const xr_string_view Name : {"A\0B"sv, "\xc0\x80"sv, "012345678901234567890123456789012"sv})
	{
		Invalid.DisplayName = Name;
		Passed &= ExpectError("invalid encode display name", EncodeClientHello(Invalid, Bytes), ClientHelloError::InvalidDisplayName);
		Passed &= Expect("invalid display name leaves output unchanged", Bytes == Saved);
	}
	Invalid = Expected;
	const std::array<u8, 129> TooMany{};
	Invalid.Credentials = TooMany;
	Passed &= ExpectError("oversized credentials cannot encode", EncodeClientHello(Invalid, Bytes), ClientHelloError::CredentialsTooLong);
	Passed &= Expect("oversized credentials leave output unchanged", Bytes == Saved);

	std::ranges::copy(Expected.DisplayName, reinterpret_cast<char*>(Bytes.data() + 3));
	std::ranges::copy(CredentialFixture, Bytes.begin() + 25);
	auto Overlapping = Expected;
	Overlapping.DisplayName = {reinterpret_cast<const char*>(Bytes.data() + 3), 3};
	Overlapping.Credentials = xr_span(Bytes).subspan(25, 3);
	const auto Size = EncodeClientHello(Overlapping, Bytes);
	Passed &= Expect("overlapping input fields preserve independent golden bytes", Size == sizeof(Golden) && std::ranges::equal(xr_span(Bytes).first(sizeof(Golden)), Golden));
	return Passed;
}

bool Run()
{
	bool Passed = CheckGolden();
	Passed &= CheckEnvelopeAndTruncation();
	Passed &= CheckRejectedFields();
	Passed &= CheckAllowedBounds();
	Passed &= CheckAtomicEncoding();
	if (Passed)
	{
		std::puts("Client hello checks passed");
	}
	return Passed;
}
}
