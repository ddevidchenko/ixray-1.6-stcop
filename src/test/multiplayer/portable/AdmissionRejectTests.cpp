#include "AdmissionReject.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace AdmissionRejectTests
{
using namespace Multiplayer::Wire;

constexpr u8 Golden[] = {
	0x49, 0x58, 0x4d, 0x50, 0x01, 0x00, 0x03, 0x00, 0x01, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x98, 0xba, 0xdc, 0xfe,
	0x06,
};
constexpr AdmissionReject Expected{0xfedcba98u, AdmissionRejectReason::CredentialsRejected};

constexpr std::size_t HeaderBytes = 30;
constexpr u8 Canary = 0x7b;

struct ReasonCase
{
	u8 Byte;
	AdmissionRejectReason Reason;
};

constexpr ReasonCase Reasons[] = {
	{0x01, AdmissionRejectReason::MalformedHello},
	{0x02, AdmissionRejectReason::UnsupportedProtocol},
	{0x03, AdmissionRejectReason::IncompatibleBuild},
	{0x04, AdmissionRejectReason::IncompatibleContent},
	{0x05, AdmissionRejectReason::IncompatibleRules},
	{0x06, AdmissionRejectReason::CredentialsRejected},
	{0x07, AdmissionRejectReason::ServerFull},
	{0x08, AdmissionRejectReason::ServerBusy},
	{0x09, AdmissionRejectReason::AdmissionTimedOut},
	{0x0a, AdmissionRejectReason::InvalidState},
};

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

template <class Value>
static bool ExpectError(const char* Step, const std::expected<Value, AdmissionRejectError>& Result, AdmissionRejectError Error)
{
	if (!Result && Result.error() == Error)
	{
		return true;
	}
	std::fprintf(stderr, "%s: expected error %d, got %d (-1 means success)\n", Step, static_cast<int>(Error), Result ? -1 : static_cast<int>(Result.error()));
	return false;
}

static bool CheckKnownReasons()
{
	bool Passed = Expect("hand-authored admission reject did not decode to its expected fields", DecodeAdmissionReject(Golden) == Expected);
	for (const ReasonCase& Case : Reasons)
	{
		auto Bytes = std::to_array(Golden);
		Bytes[HeaderBytes] = Case.Byte;
		const AdmissionReject Rejection{Expected.MessageSequence, Case.Reason};
		Passed &= Expect("assigned reason byte did not decode to its named reason", DecodeAdmissionReject(Bytes) == Rejection);

		std::array<u8, sizeof(Golden) + 2> Storage;
		Storage.fill(Canary);
		const auto Output = xr_span(Storage).subspan(1);
		const auto Written = EncodeAdmissionReject(Rejection, Output);
		Passed &= Expect("assigned reason did not encode as the independent frame", Written == Bytes.size() && std::ranges::equal(Output.first(Bytes.size()), Bytes));
		Passed &= Expect("admission reject encode changed bytes outside the frame", Storage.front() == Canary && Storage.back() == Canary);
	}
	return Passed;
}

static bool CheckUnknownReasons()
{
	for (u16 Raw = 0; Raw <= 0xff; ++Raw)
	{
		if (std::ranges::any_of(Reasons, [Raw](const ReasonCase& Case) { return Case.Byte == Raw; }))
		{
			continue;
		}
		auto Bytes = std::to_array(Golden);
		Bytes[HeaderBytes] = static_cast<u8>(Raw);
		bool Passed = ExpectError("unassigned reason byte accepted on decode", DecodeAdmissionReject(Bytes), AdmissionRejectError::UnknownReason);
		std::array<u8, sizeof(Golden)> Output;
		Output.fill(Canary);
		const auto Saved = Output;
		const AdmissionReject Rejection{Expected.MessageSequence, static_cast<AdmissionRejectReason>(static_cast<u8>(Raw))};
		Passed &= ExpectError("unassigned reason value accepted on encode", EncodeAdmissionReject(Rejection, Output), AdmissionRejectError::UnknownReason);
		Passed &= Expect("unassigned reason changed output", Output == Saved);
		if (!Passed)
		{
			return false;
		}
	}
	return true;
}

static bool CheckFraming()
{
	bool Passed = true;
	for (std::size_t Size = 0; Size < sizeof(Golden); ++Size)
	{
		if (!ExpectError("truncated reject frame", DecodeAdmissionReject(xr_span(Golden).first(Size)), AdmissionRejectError::InvalidEnvelope))
		{
			Passed = false;
			break;
		}
	}
	auto Bytes = std::to_array(Golden);
	Bytes[8] = 0;
	Passed &= ExpectError("valid envelope with no reason", DecodeAdmissionReject(xr_span(Bytes).first(HeaderBytes)), AdmissionRejectError::MissingReason);
	std::array<u8, sizeof(Golden) + 1> Trailing{};
	std::ranges::copy(Golden, Trailing.begin());
	Trailing[8] = 2;
	Passed &= ExpectError("valid envelope with trailing reject payload", DecodeAdmissionReject(Trailing), AdmissionRejectError::TrailingPayload);
	std::array<u8, HeaderBytes + 257> WidePayload{};
	std::ranges::copy(Golden, WidePayload.begin());
	WidePayload[9] = 1;
	Passed &= ExpectError("typed size check rejects a complete 257-byte payload", DecodeAdmissionReject(WidePayload), AdmissionRejectError::TrailingPayload);

	for (u8 Tag : {u8{0x01}, u8{0x02}})
	{
		Bytes = std::to_array(Golden);
		Bytes[6] = Tag;
		Passed &= ExpectError("other admission tag cannot decode as reject", DecodeAdmissionReject(Bytes), AdmissionRejectError::WrongMessageType);
	}
	Bytes = std::to_array(Golden);
	Bytes[4] = 2;
	Passed &= ExpectError("unsupported envelope version", DecodeAdmissionReject(Bytes), AdmissionRejectError::InvalidEnvelope);
	Bytes = std::to_array(Golden);
	Bytes[7] = 1;
	Passed &= ExpectError("reserved envelope flag", DecodeAdmissionReject(Bytes), AdmissionRejectError::InvalidEnvelope);

	for (const std::size_t Offset : {std::size_t{17}, std::size_t{21}, std::size_t{25}})
	{
		Bytes = std::to_array(Golden);
		Bytes[Offset] = 1;
		Passed &= ExpectError("admission reject must have unbound identity", DecodeAdmissionReject(Bytes), AdmissionRejectError::BoundReject);
	}
	return Passed;
}

static bool CheckAtomicEncoding()
{
	std::array<u8, sizeof(Golden)> Output;
	Output.fill(Canary);
	const auto Saved = Output;
	bool Passed = ExpectError("default empty output", EncodeAdmissionReject(Expected, {}), AdmissionRejectError::OutputTooSmall);
	for (std::size_t Size = 0; Size < sizeof(Golden); ++Size)
	{
		if (!ExpectError("short reject encode output", EncodeAdmissionReject(Expected, xr_span(Output).first(Size)), AdmissionRejectError::OutputTooSmall) ||
			!Expect("short reject encode changed output", Output == Saved))
		{
			Passed = false;
			break;
		}
	}
	Passed &= ExpectError("default rejection has no assigned reason", EncodeAdmissionReject({}, Output), AdmissionRejectError::UnknownReason);
	Passed &= Expect("default rejection changed output", Output == Saved);
	const auto Written = EncodeAdmissionReject(Expected, Output);
	Passed &= Expect("exact output capacity works after failed encodes", Written == Output.size() && std::ranges::equal(Output, Golden));

	auto SequenceZero = std::to_array(Golden);
	std::ranges::fill(xr_span(SequenceZero).subspan(26, 4), u8{0});
	const AdmissionReject First{0, Expected.Reason};
	Passed &= Expect("the first server sequence may be zero", DecodeAdmissionReject(SequenceZero) == First &&
		EncodeAdmissionReject(First, Output) == Output.size() && Output == SequenceZero);
	return Passed;
}

bool Run()
{
	bool Passed = CheckKnownReasons();
	Passed &= CheckUnknownReasons();
	Passed &= CheckFraming();
	Passed &= CheckAtomicEncoding();
	if (Passed)
	{
		std::puts("Admission reject checks passed");
	}
	return Passed;
}
}
