#include "ServerWelcome.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace ServerWelcomeTests
{
using namespace Multiplayer::Wire;
using namespace std::string_view_literals;

constexpr u8 Golden[] = {
	0x49, 0x58, 0x4d, 0x50, 0x01, 0x00, 0x02, 0x00, 0x68, 0x00,
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
	0xc5, 0xb4, 0xa3, 0x12, 0x04, 0x03, 0x02, 0x01, 0x98, 0xba, 0xdc, 0xfe,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
	0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
	0x06, 0x00, 0x6d, 0x70, 0x5f, 0x61, 0x74, 0x70,
	0xf8, 0x24, 0x01, 0x00, 0xf0, 0x49, 0x02, 0x00, 0x7c, 0x92, 0x00, 0x00, 0x50, 0xc3, 0x00, 0x00,
	0x88, 0x13, 0x00, 0x00, 0xd0, 0x07, 0x00, 0x00, 0x80, 0x4f, 0x12, 0x00, 0x32, 0x00, 0x00, 0x00,
};

constexpr CompatibilityId MakeId(u8 First)
{
	CompatibilityId Id{};
	for (std::size_t Index = 0; Index < Id.size(); ++Index)
	{
		Id[Index] = static_cast<u8>(First + Index);
	}
	return Id;
}

constexpr ServerWelcome Expected{
	0x8877665544332211ull, 0x12a3b4c5u, 0x01020304u, 0xfedcba98u,
	MakeId(0x20), MakeId(0x80), "mp_atp"sv,
	{75000, 150000, 37500, 50000}, {5000, 2000, 1200000, 50},
};

static bool Matches(const ServerWelcome& Actual, const ServerWelcome& Wanted)
{
	return Actual.ServerSession == Wanted.ServerSession && Actual.WorldEpoch == Wanted.WorldEpoch && Actual.ConnectionEpoch == Wanted.ConnectionEpoch &&
		Actual.MessageSequence == Wanted.MessageSequence && Actual.ContentId == Wanted.ContentId && Actual.RulesId == Wanted.RulesId &&
		Actual.MapId == Wanted.MapId && Actual.Rates == Wanted.Rates && Actual.Rules == Wanted.Rules;
}

constexpr std::size_t HeaderBytes = 30;
constexpr std::size_t MapLengthOffset = HeaderBytes + 2 * 32;
constexpr std::size_t MapOffset = MapLengthOffset + 2;
constexpr std::size_t RatesOffset = MapOffset + 6;
constexpr u8 Canary = 0x7b;

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

template <class Value>
static bool ExpectError(const char* Step, const std::expected<Value, ServerWelcomeError>& Result, ServerWelcomeError Error)
{
	if (!Result && Result.error() == Error)
	{
		return true;
	}
	std::fprintf(stderr, "%s: expected error %d, got %d (-1 means success)\n", Step, static_cast<int>(Error), Result ? -1 : static_cast<int>(Result.error()));
	return false;
}

static void SetPayloadLength(xr_span<u8> Bytes)
{
	const std::size_t Length = Bytes.size() - HeaderBytes;
	Bytes[8] = static_cast<u8>(Length);
	Bytes[9] = static_cast<u8>(Length >> 8);
}

static bool CheckGolden()
{
	const auto Read = DecodeServerWelcome(Golden);
	bool Passed = Expect("hand-authored server welcome did not decode to the expected fields", Read && Matches(*Read, Expected));
	if (Read)
	{
		Passed &= Expect("decoded map ID must borrow the message", Read->MapId.data() == reinterpret_cast<const char*>(Golden + MapOffset));
	}

	std::array<u8, sizeof(Golden) + 2> Storage;
	Storage.fill(Canary);
	const auto Output = xr_span(Storage).subspan(1);
	const auto Written = EncodeServerWelcome(Expected, Output);
	Passed &= Expect("server welcome encode differs from the independent golden bytes", Written == sizeof(Golden) &&
		std::ranges::equal(Output.first(sizeof(Golden)), Golden));
	Passed &= Expect("server welcome encode changed bytes outside the frame", Storage.front() == Canary && Storage.back() == Canary);
	return Passed;
}

static bool CheckFraming()
{
	bool Passed = true;
	for (std::size_t Size = 0; Size < sizeof(Golden); ++Size)
	{
		if (!ExpectError("truncated welcome frame", DecodeServerWelcome(xr_span(Golden).first(Size)), ServerWelcomeError::InvalidEnvelope))
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
		if (!ExpectError("valid envelope with truncated welcome fields", DecodeServerWelcome(Prefix), ServerWelcomeError::TruncatedPayload))
		{
			Passed = false;
			break;
		}
	}
	auto OtherTag = std::to_array(Golden);
	OtherTag[6] = 0x01;
	Passed &= ExpectError("wrong message type cannot decode as welcome", DecodeServerWelcome(OtherTag), ServerWelcomeError::WrongMessageType);
	auto OtherVersion = std::to_array(Golden);
	OtherVersion[4] = 2;
	Passed &= ExpectError("unsupported version cannot decode as welcome", DecodeServerWelcome(OtherVersion), ServerWelcomeError::InvalidEnvelope);

	std::array<u8, sizeof(Golden) + 1> Trailing{};
	std::ranges::copy(Golden, Trailing.begin());
	SetPayloadLength(Trailing);
	Passed &= ExpectError("valid envelope with trailing welcome payload", DecodeServerWelcome(Trailing), ServerWelcomeError::TrailingPayload);
	return Passed;
}

static bool CheckRejectedValues()
{
	auto Unbound = std::to_array(Golden);
	std::ranges::fill(xr_span(Unbound).subspan(10, 8), u8{0});
	bool Passed = ExpectError("zero session is unbound", DecodeServerWelcome(Unbound), ServerWelcomeError::UnboundWelcome);
	Unbound = std::to_array(Golden);
	std::ranges::fill(xr_span(Unbound).subspan(22, 4), u8{0});
	Passed &= ExpectError("zero connection epoch is unbound", DecodeServerWelcome(Unbound), ServerWelcomeError::UnboundWelcome);

	std::array<u8, MapOffset + 32> EmptyMap{};
	std::ranges::copy(xr_span(Golden).first(MapLengthOffset), EmptyMap.begin());
	std::ranges::copy(xr_span(Golden).subspan(RatesOffset), EmptyMap.begin() + MapOffset);
	SetPayloadLength(EmptyMap);
	Passed &= ExpectError("empty map ID is invalid", DecodeServerWelcome(EmptyMap), ServerWelcomeError::InvalidMapId);
	auto BadMap = std::to_array(Golden);
	BadMap[MapOffset + 1] = 0;
	Passed &= ExpectError("embedded zero in map ID", DecodeServerWelcome(BadMap), ServerWelcomeError::InvalidMapId);
	BadMap[MapOffset + 1] = 0xff;
	Passed &= ExpectError("invalid UTF-8 map ID", DecodeServerWelcome(BadMap), ServerWelcomeError::InvalidMapId);
	std::array<u8, MapOffset + 65 + 32> LongMap{};
	std::ranges::copy(xr_span(Golden).first(MapLengthOffset), LongMap.begin());
	LongMap[MapLengthOffset] = 65;
	std::ranges::fill(xr_span(LongMap).subspan(MapOffset, 65), u8{0x41});
	std::ranges::copy(xr_span(Golden).subspan(RatesOffset), LongMap.begin() + MapOffset + 65);
	SetPayloadLength(LongMap);
	Passed &= ExpectError("complete map ID beyond the byte cap", DecodeServerWelcome(LongMap), ServerWelcomeError::InvalidMapId);
	LongMap[MapLengthOffset] = 0;
	LongMap[MapLengthOffset + 1] = 1;
	Passed &= ExpectError("map byte cap checks both length bytes", DecodeServerWelcome(LongMap), ServerWelcomeError::InvalidMapId);

	for (std::size_t Index = 0; Index < 4; ++Index)
	{
		auto ZeroRate = std::to_array(Golden);
		std::ranges::fill(xr_span(ZeroRate).subspan(RatesOffset + Index * 4, 4), u8{0});
		Passed &= ExpectError("each advertised rate must be positive", DecodeServerWelcome(ZeroRate), ServerWelcomeError::InvalidRates);
	}
	return Passed;
}

static bool CheckAllowedBounds()
{
	std::array<char, 64> Name;
	Name.fill('A');
	Name[62] = static_cast<char>(0xc2);
	Name[63] = static_cast<char>(0xa2);
	auto Welcome = Expected;
	Welcome.WorldEpoch = 0;
	Welcome.MapId = {Name.data(), Name.size()};
	Welcome.Rules = {};
	std::array<u8, 192> Maximum{};
	const auto Written = EncodeServerWelcome(Welcome, Maximum);
	bool Passed = Expect("maximum map ID fits exact frame size", Written == Maximum.size() && Maximum[8] == 162 && Maximum[9] == 0 &&
		Maximum[MapLengthOffset] == 64 && Maximum[MapLengthOffset + 1] == 0);
	const auto Read = DecodeServerWelcome(Maximum);
	Passed &= Expect("zero initial epoch and zero-valued optional rules survive encoding", Read && Matches(*Read, Welcome));

	Welcome.MapId = "A"sv;
	Welcome.Rates = {1, 1, 1, 1};
	std::array<u8, 129> Minimum{};
	const auto MinimumSize = EncodeServerWelcome(Welcome, Minimum);
	const auto MinimumRead = DecodeServerWelcome(Minimum);
	Passed &= Expect("one-byte map and smallest positive rates are representable", MinimumSize == Minimum.size() && MinimumRead && Matches(*MinimumRead, Welcome));
	return Passed;
}

static bool CheckAtomicEncoding()
{
	std::array<u8, 192> Storage;
	Storage.fill(Canary);
	const auto Saved = Storage;
	bool Passed = ExpectError("default empty output", EncodeServerWelcome(Expected, {}), ServerWelcomeError::OutputTooSmall);
	for (std::size_t Size = 0; Size < sizeof(Golden); ++Size)
	{
		if (!ExpectError("short welcome output", EncodeServerWelcome(Expected, xr_span(Storage).first(Size)), ServerWelcomeError::OutputTooSmall) ||
			!Expect("short welcome output changed bytes", Storage == Saved))
		{
			Passed = false;
			break;
		}
	}
	auto Invalid = Expected;
	Invalid.ServerSession = 0;
	Passed &= ExpectError("encode rejects an unassigned session", EncodeServerWelcome(Invalid, Storage), ServerWelcomeError::UnboundWelcome);
	Passed &= Expect("unassigned session encode changed bytes", Storage == Saved);
	Invalid = Expected;
	Invalid.ConnectionEpoch = 0;
	Passed &= ExpectError("encode rejects an unassigned connection", EncodeServerWelcome(Invalid, Storage), ServerWelcomeError::UnboundWelcome);
	Passed &= Expect("unassigned connection encode changed bytes", Storage == Saved);
	std::array<char, 65> TooLong;
	TooLong.fill('A');
	for (const xr_string_view Map : {""sv, "mp\0atp"sv, "\xff"sv, xr_string_view(TooLong.data(), TooLong.size())})
	{
		Invalid = Expected;
		Invalid.MapId = Map;
		Passed &= ExpectError("encode rejects an invalid map ID", EncodeServerWelcome(Invalid, Storage), ServerWelcomeError::InvalidMapId);
		Passed &= Expect("bad map encode changed bytes", Storage == Saved);
	}
	constexpr u32 WelcomeRates::* Fields[] = {&WelcomeRates::GameplayMilliHz, &WelcomeRates::PhysicsMilliHz, &WelcomeRates::SnapshotMilliHz, &WelcomeRates::InputMilliHz};
	for (const auto Field : Fields)
	{
		Invalid = Expected;
		Invalid.Rates.*Field = 0;
		Passed &= ExpectError("encode rejects a zero rate", EncodeServerWelcome(Invalid, Storage), ServerWelcomeError::InvalidRates);
		Passed &= Expect("invalid rate encode changed bytes", Storage == Saved);
	}
	std::ranges::copy(Expected.MapId, reinterpret_cast<char*>(Storage.data() + 3));
	auto Overlap = Expected;
	Overlap.MapId = {reinterpret_cast<const char*>(Storage.data() + 3), 6};
	const auto Size = EncodeServerWelcome(Overlap, Storage);
	Passed &= Expect("overlapping map ID encodes identically to the golden frame", Size == sizeof(Golden) && std::ranges::equal(xr_span(Storage).first(sizeof(Golden)), Golden));
	return Passed;
}

bool Run()
{
	bool Passed = CheckGolden();
	Passed &= CheckFraming();
	Passed &= CheckRejectedValues();
	Passed &= CheckAllowedBounds();
	Passed &= CheckAtomicEncoding();
	if (Passed)
	{
		std::puts("Server welcome checks passed");
	}
	return Passed;
}
}
