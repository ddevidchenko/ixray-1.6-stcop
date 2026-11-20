#include "LegacyMultipacket.h"

#include <algorithm>
#include <cstdio>

namespace LegacyMultipacketTests
{
using namespace Multiplayer::Legacy;

constexpr u8 Single[]{0xe0, 0x03, 0x00, 0xc0, 0x11, 0x00, 0x97};
constexpr u8 Merged[]{0xe1, 0x09, 0x00, 0xc0, 0x03, 0x00, 0x21, 0x00, 0x98, 0x02, 0x00, 0x22, 0x00};

static bool Expect(const char* Failure, bool Condition)
{
	if (!Condition)
	{
		std::fprintf(stderr, "%s\n", Failure);
	}
	return Condition;
}

static bool CheckValidFrames()
{
	constexpr u8 SingleRecord[]{0x11, 0x00, 0x97};
	constexpr u8 FirstRecord[]{0x21, 0x00, 0x98};
	constexpr u8 SecondRecord[]{0x22, 0x00};
	const auto One = ReadEnvelope(Single);
	const auto Many = ReadEnvelope(Merged);
	bool Passed = Expect("the golden single frame borrows its raw payload and decodes an exact LE size",
		One && One->Layout == PacketLayout::Single && One->Encoding == BodyEncoding::Raw &&
		One->DecodedSize == sizeof(SingleRecord) && One->Payload.data() == Single + 4);
	Passed &= Expect("the golden merged frame retains its declared size and layout",
		Many && Many->Layout == PacketLayout::Merged && Many->DecodedSize == 9 && Many->Payload.data() == Merged + 4);
	if (!One || !Many)
	{
		return false;
	}
	auto SingleRecords = MultipacketRecords::Validate(*One, One->Payload);
	auto MergedRecords = MultipacketRecords::Validate(*Many, Many->Payload);
	Passed &= Expect("both raw packet styles validate before exposing records", SingleRecords && MergedRecords);
	if (!SingleRecords || !MergedRecords)
	{
		return false;
	}
	const auto Whole = SingleRecords->Next();
	const auto First = MergedRecords->Next();
	const auto Second = MergedRecords->Next();
	Passed &= Expect("single and odd-offset merged records preserve exact data and order",
		Whole && std::ranges::equal(*Whole, SingleRecord) && !SingleRecords->Next() &&
		First && std::ranges::equal(*First, FirstRecord) && Second && std::ranges::equal(*Second, SecondRecord) && !MergedRecords->Next());
	return Passed;
}

static bool CheckFramingBounds()
{
	bool Passed = true;
	for (std::size_t Length = 0; Length < sizeof(Merged); ++Length)
	{
		Passed &= Expect("a truncated golden frame is rejected at every prefix",
			!ReadEnvelope(xr_span(Merged).first(Length)));
	}

	constexpr u8 WrongOuter[]{0xd0, 2, 0, 0xc0, 0x01, 0};
	constexpr u8 WrongInner[]{0xe0, 2, 0, 0xd0, 0x01, 0};
	constexpr u8 TooShortSingle[]{0xe0, 1, 0, 0xc0, 0x01};
	constexpr u8 TooShortMerged[]{0xe1, 3, 0, 0xc0, 1, 0, 2};
	constexpr u8 TooLargeDeclared[]{0xe1, 1, 0x80, 0xc0, 2, 0, 1, 0};
	constexpr u8 RawTrailing[]{0xe0, 2, 0, 0xc0, 0x01, 0, 0x55};
	Passed &= Expect("unknown outer and compression tags are distinct framing errors",
		ReadEnvelope(WrongOuter) == std::unexpected(MultipacketError::InvalidTag) &&
		ReadEnvelope(WrongInner) == std::unexpected(MultipacketError::InvalidCompressionTag));
	Passed &= Expect("impossible declared sizes fail before body processing",
		ReadEnvelope(TooShortSingle) == std::unexpected(MultipacketError::InvalidDecodedSize) &&
		ReadEnvelope(TooShortMerged) == std::unexpected(MultipacketError::InvalidDecodedSize) &&
		ReadEnvelope(TooLargeDeclared) == std::unexpected(MultipacketError::InvalidDecodedSize));
	Passed &= Expect("raw frames must consume exactly the declared bytes",
		ReadEnvelope(RawTrailing) == std::unexpected(MultipacketError::DecodedSizeMismatch));

	xr_array<u8, 32769> OverLimit{};
	std::ranges::copy(Single, OverLimit.begin());
	Passed &= Expect("datagrams beyond the fixed receive limit fail without parsing their bodies",
		ReadEnvelope(OverLimit) == std::unexpected(MultipacketError::DatagramTooLarge));
	return Passed;
}

static bool CheckInvalidRecordBatches()
{
	constexpr u8 TruncatedLast[]{0xe1, 7, 0, 0xc0, 2, 0, 0x10, 0, 2, 0, 0x20};
	constexpr u8 PartialLength[]{0xe1, 5, 0, 0xc0, 2, 0, 0x10, 0, 0xff};
	constexpr u8 EmptyLast[]{0xe1, 6, 0, 0xc0, 2, 0, 0x10, 0, 0, 0};
	constexpr u8 ShortRecord[]{0xe1, 4, 0, 0xc0, 1, 0, 0x10, 0};
	const xr_span<const u8> Invalid[]{TruncatedLast, PartialLength, EmptyLast, ShortRecord};
	bool Passed = true;
	for (const auto Frame : Invalid)
	{
		const auto Envelope = ReadEnvelope(Frame);
		Passed &= Expect("outer framing can succeed for a malformed record stream", Envelope.has_value());
		if (Envelope)
		{
			Passed &= Expect("a malformed record invalidates the whole batch before any application",
				MultipacketRecords::Validate(*Envelope, Envelope->Payload) == std::unexpected(MultipacketError::InvalidRecord));
		}
	}
	return Passed;
}

static bool CheckPackedFramingAndDecodedBounds()
{
	constexpr u8 Packed[]{0xe1, 4, 0, 0xc1, 0x12, 0x34, 0x56, 0x78, 0x99};
	constexpr u8 Decoded[]{2, 0, 0x76, 0};
	constexpr u8 WithoutChecksum[]{0xe0, 2, 0, 0xc1, 0x88};
	constexpr u8 WithoutTags[]{0xe0, 2, 0, 0x77, 0x00};
	bool Passed = Expect("a checksummed block requires its complete prefix and some encoded data",
		ReadEnvelope(xr_span(Packed).first(8)) == std::unexpected(MultipacketError::TruncatedCompressedBody));
	const auto Envelope = ReadEnvelope(Packed);
	const auto Tagged = ReadEnvelope(WithoutChecksum, BodyFraming::Tagged);
	const auto Untagged = ReadEnvelope(WithoutTags, BodyFraming::Untagged);
	Passed &= Expect("optional build formats preserve their declared framing",
		Envelope && Envelope->Encoding == BodyEncoding::Compressed && Envelope->Payload.data() == Packed + 4 &&
		Tagged && Tagged->Encoding == BodyEncoding::Compressed && Untagged && Untagged->Encoding == BodyEncoding::Raw &&
		Untagged->Payload.data() == WithoutTags + 3);
	if (!Envelope)
	{
		return false;
	}
	Passed &= Expect("a missing or short decoder result never exposes a record",
		MultipacketRecords::Validate(*Envelope, {}) == std::unexpected(MultipacketError::DecodedSizeMismatch) &&
		MultipacketRecords::Validate(*Envelope, xr_span(Decoded).first(3)) == std::unexpected(MultipacketError::DecodedSizeMismatch));
	constexpr u8 LongDecode[]{2, 0, 0x76, 0, 0};
	constexpr u8 MalformedDecode[]{3, 0, 0x76, 0};
	Passed &= Expect("extra decoded bytes and invalid decoded records both fail before dispatch",
		MultipacketRecords::Validate(*Envelope, LongDecode) == std::unexpected(MultipacketError::DecodedSizeMismatch) &&
		MultipacketRecords::Validate(*Envelope, MalformedDecode) == std::unexpected(MultipacketError::InvalidRecord));
	auto Records = MultipacketRecords::Validate(*Envelope, Decoded);
	const auto Message = Records ? Records->Next() : std::nullopt;
	Passed &= Expect("complete decompressed bytes use the same validated record format",
		Message && std::ranges::equal(*Message, xr_span(Decoded).last(2)) && !Records->Next());
	return Passed;
}

static bool CheckMaximumSizes()
{
	xr_array<u8, 32768> Full{};
	Full[0] = 0xe1;
	Full[1] = 0xfc;
	Full[2] = 0x7f;
	Full[3] = 0xc0;
	Full[4] = 0xfc;
	Full[5] = 0x3f;
	Full[16386] = 0xfc;
	Full[16387] = 0x3f;
	const auto Envelope = ReadEnvelope(Full);
	auto Records = Envelope ? MultipacketRecords::Validate(*Envelope, Envelope->Payload) :
		std::expected<MultipacketRecords, MultipacketError>(std::unexpected(MultipacketError::InvalidRecord));
	const auto First = Records ? Records->Next() : std::nullopt;
	const auto Second = Records ? Records->Next() : std::nullopt;
	bool Passed = Expect("the full fixed-size envelope can carry multiple legal records",
		First && First->size() == 16380 && Second && Second->size() == 16380 && !Records->Next());

	xr_array<u8, 16390> OversizedRecord{};
	OversizedRecord[0] = 0xe1;
	OversizedRecord[1] = 2;
	OversizedRecord[2] = 0x40;
	OversizedRecord[3] = 0xc0;
	OversizedRecord[5] = 0x40;
	const auto Oversized = ReadEnvelope(OversizedRecord);
	Passed &= Expect("a record at the exclusive game-packet limit is invalid inside a legal envelope",
		Oversized && MultipacketRecords::Validate(*Oversized, Oversized->Payload) == std::unexpected(MultipacketError::InvalidRecord));

	xr_array<u8, 16387> LargestSingle{};
	LargestSingle[0] = 0xe0;
	LargestSingle[1] = 0xff;
	LargestSingle[2] = 0x3f;
	LargestSingle[3] = 0xc0;
	const auto One = ReadEnvelope(LargestSingle);
	auto SingleRecords = One ? MultipacketRecords::Validate(*One, One->Payload) :
		std::expected<MultipacketRecords, MultipacketError>(std::unexpected(MultipacketError::InvalidRecord));
	const auto SingleRecord = SingleRecords ? SingleRecords->Next() : std::nullopt;
	Passed &= Expect("a single record one byte below the game-packet limit remains valid",
		SingleRecord && SingleRecord->size() == 16383 && !SingleRecords->Next());
	return Passed;
}

bool Run()
{
	bool Passed = CheckValidFrames();
	Passed &= CheckFramingBounds();
	Passed &= CheckInvalidRecordBatches();
	Passed &= CheckPackedFramingAndDecodedBounds();
	Passed &= CheckMaximumSizes();
	return Passed;
}
}
