#include "WirePayload.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace WirePayloadTests
{
using namespace Multiplayer::Wire;
using namespace std::string_view_literals;

constexpr u8 Canary = 0xa7;
constexpr xr_string_view Text = "A\0\xc2\xa2\xe2\x82\xac\xf0\x9f\x99\x82"sv;
constexpr u8 Golden[] = {
	0xfd, 0x80, 0x12, 0x40, 0xcc, 0xed,
	0xef, 0xcd, 0xab, 0x89, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
	0x00, 0x00, 0x20, 0xc0, 0x0b, 0x00,
	0x41, 0x00, 0xc2, 0xa2, 0xe2, 0x82, 0xac, 0xf0, 0x9f, 0x99, 0x82,
	0x58,
};

static bool Expect(const char* Case, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Case);
	}
	return Passed;
}

template <class Value>
static bool IsError(const std::expected<Value, PayloadError>& Result, PayloadError Error)
{
	return !Result && Result.error() == Error;
}

static bool CheckGolden()
{
	PayloadReader Reader(Golden);
	bool Passed = Expect("golden unsigned byte", Reader.ReadInteger<u8>() == u8{0xfd});
	Passed &= Expect("golden signed byte", Reader.ReadInteger<s8>() == s8{-128});
	Passed &= Expect("golden unsigned 16 bits", Reader.ReadInteger<u16>() == u16{0x4012});
	Passed &= Expect("golden signed 16 bits", Reader.ReadInteger<s16>() == s16{-0x1234});
	Passed &= Expect("golden unsigned 32 bits", Reader.ReadInteger<u32>() == 0x89abcdefu);
	Passed &= Expect("golden unsigned 64 bits", Reader.ReadInteger<u64>() == u64{0x8877665544332211ull});
	Passed &= Expect("golden binary32", Reader.ReadFiniteFloat() == -2.5f);
	const auto String = Reader.ReadUtf8(32);
	Passed &= Expect("golden UTF-8 includes the embedded zero", String == Text);
	Passed &= Expect("decoded string borrows the payload", String && String->data() == reinterpret_cast<const char*>(Golden + 24));
	Passed &= Expect("golden following field stays readable", Reader.ReadInteger<u8>() == u8{0x58} && Reader.RemainingBytes() == 0);

	std::array<u8, sizeof(Golden) + 1> Output;
	Output.fill(Canary);
	PayloadWriter Writer(Output);
	bool Written = Writer.WriteInteger(u8{0xfd}).has_value();
	Written &= Writer.WriteInteger(s8{-128}).has_value();
	Written &= Writer.WriteInteger(u16{0x4012}).has_value();
	Written &= Writer.WriteInteger(s16{-0x1234}).has_value();
	Written &= Writer.WriteInteger(u32{0x89abcdefu}).has_value();
	Written &= Writer.WriteInteger(u64{0x8877665544332211ull}).has_value();
	Written &= Writer.WriteFiniteFloat(-2.5f).has_value();
	Written &= Writer.WriteUtf8(Text, 32).has_value();
	Written &= Writer.WriteInteger(u8{0x58}).has_value();
	Passed &= Expect("encoded fields match the independent golden payload", Written && std::ranges::equal(Writer.Written(), Golden));
	Passed &= Expect("golden writes preserve the byte past the payload", Output.back() == Canary);
	return Passed;
}

static bool CheckUtf8()
{
	constexpr xr_string_view Valid[] = {
		""sv, "\x7f"sv, "\xc2\x80"sv, "\xdf\xbf"sv, "\xe0\xa0\x80"sv, "\xed\x9f\xbf"sv, "\xee\x80\x80"sv,
		"\xef\xbf\xbf"sv, "\xf0\x90\x80\x80"sv, "\xf4\x8f\xbf\xbf"sv,
	};
	constexpr xr_string_view Invalid[] = {
		"\x80"sv, "\xc0\x80"sv, "\xc1\xbf"sv, "\xc2"sv, "\xc2\x7f"sv, "\xc2\xc2"sv,
		"\xe0\x9f\xbf"sv, "\xe1\x80"sv, "\xe1\x80\x40"sv, "\xed\xa0\x80"sv, "\xed\xbf\xbf"sv,
		"\xf0\x8f\xbf\xbf"sv, "\xf1\x80\x80"sv, "\xf1\x80\x80\x40"sv, "\xf4\x90\x80\x80"sv,
		"\xf5\x80\x80\x80"sv, "\xff"sv, "\0\x80"sv,
	};

	bool Passed = true;
	const auto CheckOne = [&Passed](xr_string_view Value, bool Allowed)
	{
		std::array<u8, 6> Input{static_cast<u8>(Value.size()), 0};
		if (!Value.empty())
		{
			std::memcpy(Input.data() + 2, Value.data(), Value.size());
		}
		const auto Field = xr_span(Input).first(Value.size() + 2);
		PayloadReader Reader(Field);
		const auto Read = Reader.ReadUtf8(8);

		std::array<u8, 6> Output;
		Output.fill(Canary);
		const auto Saved = Output;
		PayloadWriter Writer(Output);
		const auto Written = Writer.WriteUtf8(Value, 8);
		if (Allowed)
		{
			Passed &= Expect("valid UTF-8 boundary must decode", Read == Value && Reader.RemainingBytes() == 0);
			Passed &= Expect("valid UTF-8 boundary must encode", Written && std::ranges::equal(Writer.Written(), Field));
			return;
		}
		Passed &= Expect("invalid UTF-8 must be rejected without consuming the field", IsError(Read, PayloadError::InvalidUtf8) && Reader.RemainingBytes() == Field.size());
		Passed &= Expect("invalid UTF-8 must be rejected without changing output", IsError(Written, PayloadError::InvalidUtf8) && Writer.Written().empty() && Output == Saved);
	};
	for (const auto Value : Valid)
	{
		CheckOne(Value, true);
	}
	for (const auto Value : Invalid)
	{
		CheckOne(Value, false);
	}
	return Passed;
}

static bool CheckBoundsAndRecovery()
{
	constexpr u8 StringField[] = {0x04, 0x00, 0xf0, 0x9f, 0x99, 0x82};
	bool Passed = true;
	for (std::size_t Size = 0; Size < sizeof(StringField); ++Size)
	{
		PayloadReader Reader(xr_span(StringField).first(Size));
		Passed &= Expect("truncated UTF-8 field leaves its cursor unchanged", IsError(Reader.ReadUtf8(4), PayloadError::Truncated) && Reader.RemainingBytes() == Size);
	}
	constexpr u8 PrefixedString[] = {0x58, 0x04, 0x00, 0xf0, 0x9f, 0x99, 0x82};
	PayloadReader Retry(PrefixedString);
	Passed &= Expect("reader prefix", Retry.ReadInteger<u8>() == u8{0x58});
	Passed &= Expect("limit applies to bytes instead of code points", IsError(Retry.ReadUtf8(3), PayloadError::LengthOutOfRange) && Retry.RemainingBytes() == sizeof(StringField));
	Passed &= Expect("exact byte limit is accepted after failed read", Retry.ReadUtf8(4) == "\xf0\x9f\x99\x82"sv && Retry.RemainingBytes() == 0);

	std::array<u8, 4> Storage{Canary, Canary, Canary, Canary};
	PayloadWriter Writer(Storage);
	Passed &= Expect("prefix write", Writer.WriteInteger(u8{0x58}).has_value());
	const auto Saved = Storage;
	Passed &= Expect("small output rejects integer", IsError(Writer.WriteInteger(u32{0}), PayloadError::OutputTooSmall));
	Passed &= Expect("small output rejects finite float", IsError(Writer.WriteFiniteFloat(1.0f), PayloadError::OutputTooSmall));
	Passed &= Expect("small output rejects UTF-8", IsError(Writer.WriteUtf8("hi"sv, 2), PayloadError::OutputTooSmall));
	Passed &= Expect("string limit is checked before writing", IsError(Writer.WriteUtf8("hi"sv, 1), PayloadError::LengthOutOfRange));
	static constexpr std::array<char, 65536> TooLong{};
	Passed &= Expect("UTF-8 length must fit the wire prefix", IsError(Writer.WriteUtf8({TooLong.data(), TooLong.size()}, TooLong.size()), PayloadError::LengthOutOfRange));
	Passed &= Expect("failed writes leave both bytes and position unchanged", Storage == Saved && Writer.Written().size() == 1);
	Passed &= Expect("write can use remaining exact capacity after failures", Writer.WriteUtf8("i"sv, 1).has_value() && Storage == std::array<u8, 4>{0x58, 0x01, 0x00, 0x69});

	std::array<char, 256> ExtendedText;
	ExtendedText.fill('A');
	std::array<u8, 258> ExtendedOutput{};
	PayloadWriter ExtendedWriter(ExtendedOutput);
	const xr_string_view Extended(ExtendedText.data(), ExtendedText.size());
	Passed &= Expect("UTF-8 length uses both little-endian bytes", ExtendedWriter.WriteUtf8(Extended, 256).has_value() && ExtendedOutput[0] == 0 && ExtendedOutput[1] == 1);
	PayloadReader ExtendedReader(ExtendedOutput);
	Passed &= Expect("both UTF-8 length bytes are read", ExtendedReader.ReadUtf8(256) == Extended && ExtendedReader.RemainingBytes() == 0);
	return Passed;
}

static bool CheckFloatBits()
{
	constexpr u32 Finite[] = {0, 0x80000000u, 0x00000001u, 0x80000001u, 0x007fffffu, 0x00800000u, 0x7f7fffffu, 0xff7fffffu};
	constexpr u32 NonFinite[] = {0x7f800000u, 0xff800000u, 0x7fc00000u, 0xffc00001u, 0x7f800001u};
	bool Passed = true;
	for (const u32 Bits : Finite)
	{
		const u8 Bytes[] = {static_cast<u8>(Bits), static_cast<u8>(Bits >> 8), static_cast<u8>(Bits >> 16), static_cast<u8>(Bits >> 24)};
		PayloadReader Reader(Bytes);
		const auto Value = Reader.ReadFiniteFloat();
		Passed &= Expect("finite binary32 bits round trip through the reader", Value && std::bit_cast<u32>(*Value) == Bits && Reader.RemainingBytes() == 0);

		std::array<u8, 4> Output{};
		PayloadWriter Writer(Output);
		Passed &= Expect("finite binary32 bits are preserved by the writer", Writer.WriteFiniteFloat(std::bit_cast<f32>(Bits)).has_value() && std::ranges::equal(Output, Bytes));
	}
	for (const u32 Bits : NonFinite)
	{
		const u8 Bytes[] = {static_cast<u8>(Bits), static_cast<u8>(Bits >> 8), static_cast<u8>(Bits >> 16), static_cast<u8>(Bits >> 24)};
		PayloadReader Reader(Bytes);
		Passed &= Expect("nonfinite binary32 does not advance the reader", IsError(Reader.ReadFiniteFloat(), PayloadError::NonFiniteFloat) && Reader.RemainingBytes() == 4);
		Passed &= Expect("same bytes are still present after rejecting nonfinite binary32", Reader.ReadInteger<u32>() == Bits);
		std::array<u8, 4> Output{Canary, Canary, Canary, Canary};
		PayloadWriter Writer(Output);
		Passed &= Expect("nonfinite binary32 does not change output", IsError(Writer.WriteFiniteFloat(std::bit_cast<f32>(Bits)), PayloadError::NonFiniteFloat) &&
			Writer.Written().empty() && Output == std::array<u8, 4>{Canary, Canary, Canary, Canary});
	}
	return Passed;
}

static bool CheckShortScalarsAndOverlap()
{
	bool Passed = true;
	for (std::size_t Size = 0; Size < sizeof(u64); ++Size)
	{
		PayloadReader Reader(xr_span(Golden).first(Size));
		Passed &= Expect("short integer read leaves cursor in place", IsError(Reader.ReadInteger<u64>(), PayloadError::Truncated) && Reader.RemainingBytes() == Size);
		if (Size < sizeof(f32))
		{
			Passed &= Expect("short float read leaves cursor in place", IsError(Reader.ReadFiniteFloat(), PayloadError::Truncated) && Reader.RemainingBytes() == Size);
		}
	}
	std::array<u8, 6> Bytes{0x58, 0x61, 0x62, 0x63, Canary, Canary};
	PayloadWriter Writer(Bytes);
	Passed &= Expect("prefix survives overlapping string write", Writer.WriteInteger(u8{0x58}).has_value());
	const xr_string_view Overlap(reinterpret_cast<const char*>(Bytes.data() + 1), 3);
	Passed &= Expect("overlapping string must move data before writing its prefix", Writer.WriteUtf8(Overlap, 3).has_value() &&
		Bytes == std::array<u8, 6>{0x58, 0x03, 0x00, 0x61, 0x62, 0x63});
	return Passed;
}

bool Run()
{
	bool Passed = CheckGolden();
	Passed &= CheckUtf8();
	Passed &= CheckBoundsAndRecovery();
	Passed &= CheckFloatBits();
	Passed &= CheckShortScalarsAndOverlap();
	if (Passed)
	{
		std::puts("Wire payload checks passed");
	}
	return Passed;
}
}
