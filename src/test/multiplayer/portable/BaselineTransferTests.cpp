#include "BaselineTransfer.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace BaselineTransferTests
{
using namespace Multiplayer;
using namespace Multiplayer::Wire;

constexpr u8 GoldenBegin[] = {
	0x49, 0x58, 0x4d, 0x50, 0x01, 0x00, 0x06, 0x00, 0x14, 0x00,
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
	0xd4, 0xc3, 0xb2, 0xa1, 0x04, 0x03, 0x02, 0x01, 0xfe, 0xff, 0xff, 0xff,
	0x78, 0x56, 0x34, 0x12, 0xe0, 0xac, 0x68, 0x24, 0xef, 0xcd, 0xab, 0x89,
	0x03, 0x04, 0x00, 0x00, 0x02, 0x01, 0x02, 0x00,
};
constexpr u8 GoldenChunk[] = {
	0x49, 0x58, 0x4d, 0x50, 0x01, 0x00, 0x07, 0x00, 0x09, 0x00,
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
	0xd4, 0xc3, 0xb2, 0xa1, 0x04, 0x03, 0x02, 0x01, 0xff, 0xff, 0xff, 0xff,
	0x78, 0x56, 0x34, 0x12, 0x01, 0x00, 0x9a, 0x00, 0xd1,
};
constexpr u8 GoldenEnd[] = {
	0x49, 0x58, 0x4d, 0x50, 0x01, 0x00, 0x08, 0x00, 0x04, 0x00,
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
	0xd4, 0xc3, 0xb2, 0xa1, 0x04, 0x03, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00,
	0x78, 0x56, 0x34, 0x12,
};
constexpr JoinIdentity Identity{0x8877665544332211ull, 0xa1b2c3d4u, 0x01020304u};
constexpr u32 Id = 0x12345678u;
constexpr u8 Data[] = {0x9a, 0x00, 0xd1};
constexpr BaselineBegin Begin{Identity, 0xfffffffeu, {Id, 0x2468ace0u, 0x89abcdefu, 1027, 258, 2}};
constexpr BaselineChunk Chunk{Identity, 0xffffffffu, Id, 1, Data};
constexpr BaselineEnd End{Identity, 0, Id};
constexpr std::size_t HeaderSize = 30;
constexpr std::size_t ChunkDataOffset = 36;

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

template <class Value>
static bool ExpectError(const char* Step, const std::expected<Value, BoundMessageError>& Result, BoundMessageError Error)
{
	if (!Result && Result.error() == Error)
	{
		return true;
	}
	std::fprintf(stderr, "%s: expected %d, got %d\n", Step, static_cast<int>(Error), Result ? -1 : static_cast<int>(Result.error()));
	return false;
}

static void SetFixtureInteger(xr_span<u8> Field, u32 Value)
{
	for (auto& Byte : Field)
	{
		Byte = static_cast<u8>(Value);
		Value >>= 8;
	}
}

static bool CheckGolden()
{
	const auto Opened = DecodeBaselineBegin(GoldenBegin);
	const auto Received = DecodeBaselineChunk(GoldenChunk);
	const auto Closed = DecodeBaselineEnd(GoldenEnd);
	bool Passed = Expect("baseline begin preserves identity, sequence and declared totals", Opened && Opened->Identity == Identity &&
		Opened->MessageSequence == Begin.MessageSequence && Opened->Manifest == Begin.Manifest);
	Passed &= Expect("baseline chunk preserves identity, sequence, zero-based index and bytes", Received && Received->Identity == Identity &&
		Received->MessageSequence == Chunk.MessageSequence && Received->BaselineId == Id && Received->ChunkIndex == 1 &&
		std::ranges::equal(Received->Data, Data) && Received->Data.data() == GoldenChunk + ChunkDataOffset);
	Passed &= Expect("baseline end preserves identity, sequence and baseline ID", Closed && Closed->Identity == Identity &&
		Closed->MessageSequence == End.MessageSequence && Closed->BaselineId == Id);

	std::array<u8, sizeof(GoldenBegin)> OpenBytes{};
	Passed &= Expect("baseline begin matches literal datagram", EncodeBaselineBegin(Begin, OpenBytes) == sizeof(GoldenBegin) &&
		std::ranges::equal(OpenBytes, GoldenBegin));
	std::array<u8, sizeof(GoldenChunk)> ChunkBytes{};
	Passed &= Expect("baseline chunk matches literal datagram", EncodeBaselineChunk(Chunk, ChunkBytes) == sizeof(GoldenChunk) &&
		std::ranges::equal(ChunkBytes, GoldenChunk));
	std::array<u8, sizeof(GoldenEnd)> EndBytes{};
	Passed &= Expect("baseline end matches literal datagram", EncodeBaselineEnd(End, EndBytes) == sizeof(GoldenEnd) &&
		std::ranges::equal(EndBytes, GoldenEnd));
	return Passed;
}

template <std::size_t Size, class Decoder>
static bool CheckFraming(const u8 (&Golden)[Size], Decoder Decode, u8 OtherTag, std::size_t MinPayload)
{
	bool Passed = true;
	for (std::size_t Length = 0; Length < Size; ++Length)
	{
		Passed &= ExpectError("raw frame prefix cannot decode", Decode(xr_span(Golden).first(Length)), BoundMessageError::InvalidEnvelope);
	}
	for (std::size_t Length = HeaderSize; Length < HeaderSize + MinPayload; ++Length)
	{
		auto Bytes = std::to_array(Golden);
		Bytes[8] = static_cast<u8>(Length - HeaderSize);
		Passed &= ExpectError("reframed short payload cannot decode", Decode(xr_span(Bytes).first(Length)), BoundMessageError::TruncatedPayload);
	}
	auto Bytes = std::to_array(Golden);
	Bytes[6] = OtherTag;
	Passed &= ExpectError("a different assigned tag selects a different message", Decode(Bytes), BoundMessageError::WrongMessageType);
	Bytes[6] = 0xff;
	Passed &= ExpectError("unassigned tag cannot decode", Decode(Bytes), BoundMessageError::InvalidEnvelope);
	Bytes = std::to_array(Golden);
	std::fill_n(Bytes.begin() + 10, 8, 0);
	Passed &= ExpectError("zero session is unbound", Decode(Bytes), BoundMessageError::UnboundJoin);
	Bytes = std::to_array(Golden);
	std::fill_n(Bytes.begin() + 22, 4, 0);
	Passed &= ExpectError("zero connection epoch is unbound", Decode(Bytes), BoundMessageError::UnboundJoin);
	Bytes = std::to_array(Golden);
	std::fill_n(Bytes.begin() + 18, 4, 0);
	const auto InitialWorld = Decode(Bytes);
	Passed &= Expect("epoch zero still belongs to a bound connection", InitialWorld && InitialWorld->Identity == JoinIdentity{Identity.ServerSession, 0, Identity.ConnectionEpoch});
	return Passed;
}

template <std::size_t Size, class Decoder>
static bool CheckFixedSize(const u8 (&Golden)[Size], Decoder Decode)
{
	std::array<u8, Size + 1> Extra{};
	std::ranges::copy(Golden, Extra.begin());
	bool Passed = ExpectError("extra unframed byte fails framing", Decode(Extra), BoundMessageError::InvalidEnvelope);
	++Extra[8];
	Passed &= ExpectError("fixed-size payload rejects framed trailing byte", Decode(Extra), BoundMessageError::TrailingPayload);
	return Passed;
}

template <std::size_t Size, class Message, class Encoder>
static bool CheckOutput(const u8 (&Golden)[Size], Message Original, Encoder Encode)
{
	std::array<u8, Size + 4> Storage;
	Storage.fill(0x77);
	const auto Sentinel = Storage;
	bool Passed = true;
	for (std::size_t Length = 0; Length < Size; ++Length)
	{
		Passed &= ExpectError("undersized output rejects encode", Encode(Original, xr_span(Storage).first(Length)), BoundMessageError::OutputTooSmall);
		Passed &= Expect("failed encode leaves the full buffer intact", Storage == Sentinel);
	}
	for (const JoinIdentity Unbound : {JoinIdentity{0, Identity.WorldEpoch, Identity.ConnectionEpoch}, JoinIdentity{Identity.ServerSession, Identity.WorldEpoch, 0}})
	{
		auto Invalid = Original;
		Invalid.Identity = Unbound;
		Passed &= ExpectError("invalid outgoing identity rejects encode", Encode(Invalid, Storage), BoundMessageError::UnboundJoin);
		Passed &= Expect("invalid identity leaves the full output intact", Storage == Sentinel);
	}
	Passed &= Expect("successful write reports its exact size and leaves its tail", Encode(Original, Storage) == Size &&
		std::ranges::equal(xr_span(Storage).first(Size), Golden) && std::ranges::equal(xr_span(Storage).subspan(Size), xr_span(Sentinel).subspan(Size)));
	return Passed;
}

static bool CheckManifestBounds()
{
	struct Case
	{
		u32 ByteCount;
		u16 RecordCount;
		u16 ChunkCount;
		bool Valid;
	};
	constexpr Case Cases[] = {
		{0, 0, 0, true}, {1, 1, 1, true}, {1023, 5, 1, true}, {1024, 1, 1, true}, {1025, 1, 2, true}, {4194304, 8192, 4096, true},
		{4194305, 8192, 4096, false}, {0xffffffffu, 1, 1, false}, {4194304, 8193, 4096, false}, {4194304, 8192, 4097, false},
		{1024, 1, 2, false}, {1025, 1, 1, false}, {100, 1, 0, false}, {0, 1, 0, false}, {0, 0, 1, false}, {1, 0, 1, false}, {1, 2, 1, false},
	};
	bool Passed = true;
	for (const auto& Entry : Cases)
	{
		auto Frame = std::to_array(GoldenBegin);
		SetFixtureInteger(xr_span(Frame).subspan(42, 4), Entry.ByteCount);
		SetFixtureInteger(xr_span(Frame).subspan(46, 2), Entry.RecordCount);
		SetFixtureInteger(xr_span(Frame).subspan(48, 2), Entry.ChunkCount);
		auto Declaration = Begin;
		Declaration.Manifest.ByteCount = Entry.ByteCount;
		Declaration.Manifest.RecordCount = Entry.RecordCount;
		Declaration.Manifest.ChunkCount = Entry.ChunkCount;
		const auto Decoded = DecodeBaselineBegin(Frame);
		std::array<u8, sizeof(GoldenBegin)> Output;
		Output.fill(0x77);
		const auto Sentinel = Output;
		const auto Encoded = EncodeBaselineBegin(Declaration, Output);
		if (Entry.Valid)
		{
			Passed &= Expect("bounded canonical declaration decodes exactly", Decoded && Decoded->Manifest == Declaration.Manifest);
			Passed &= Expect("bounded canonical declaration encodes the independent frame", Encoded == Frame.size() && Output == Frame);
		}
		else
		{
			Passed &= ExpectError("impossible or excessive declaration fails decode", Decoded, BoundMessageError::InvalidPayload);
			Passed &= ExpectError("impossible or excessive declaration fails encode", Encoded, BoundMessageError::InvalidPayload);
			Passed &= Expect("invalid declaration leaves output untouched", Output == Sentinel);
		}
	}
	return Passed;
}

static bool CheckChunkBounds()
{
	std::array<u8, 1061> Frame;
	Frame.fill(0x9d);
	std::ranges::copy(xr_span(GoldenChunk).first(ChunkDataOffset), Frame.begin());
	SetFixtureInteger(xr_span(Frame).subspan(8, 2), 1030);
	SetFixtureInteger(xr_span(Frame).subspan(34, 2), 4095);
	const auto FullChunk = DecodeBaselineChunk(xr_span(Frame).first(1060));
	bool Passed = Expect("maximal data and last possible index decode", FullChunk && FullChunk->ChunkIndex == 4095 && FullChunk->Data.size() == 1024 &&
		std::ranges::all_of(FullChunk->Data, [](u8 Byte) { return Byte == 0x9d; }));
	std::array<u8, 1061> Output{};
	auto Message = Chunk;
	Message.ChunkIndex = 4095;
	Message.Data = xr_span(Frame).subspan(36, 1024);
	Passed &= Expect("full-size chunk encodes without touching the extra byte", EncodeBaselineChunk(Message, Output) == 1060 &&
		std::ranges::equal(xr_span(Output).first(1060), xr_span(Frame).first(1060)) && Output.back() == 0);

	SetFixtureInteger(xr_span(Frame).subspan(34, 2), 4096);
	Passed &= ExpectError("out-of-range chunk index fails decode", DecodeBaselineChunk(xr_span(Frame).first(1060)), BoundMessageError::InvalidPayload);
	const auto Prior = Output;
	Message.ChunkIndex = 4096;
	Passed &= ExpectError("out-of-range chunk index fails encode", EncodeBaselineChunk(Message, Output), BoundMessageError::InvalidPayload);
	SetFixtureInteger(xr_span(Frame).subspan(34, 2), 0);
	SetFixtureInteger(xr_span(Frame).subspan(8, 2), 1031);
	Passed &= ExpectError("oversized data fails before publication", DecodeBaselineChunk(Frame), BoundMessageError::TrailingPayload);
	Message.ChunkIndex = 0;
	Message.Data = xr_span(Frame).subspan(36, 1025);
	Passed &= ExpectError("oversized outgoing data fails", EncodeBaselineChunk(Message, Output), BoundMessageError::InvalidPayload);
	Message.Data = {};
	Passed &= ExpectError("empty outgoing chunk fails", EncodeBaselineChunk(Message, Output), BoundMessageError::InvalidPayload);
	Passed &= Expect("invalid data and index leave output unchanged", Output == Prior);
	SetFixtureInteger(xr_span(Frame).subspan(8, 2), 7);
	const auto Smallest = DecodeBaselineChunk(xr_span(Frame).first(37));
	Passed &= Expect("a one-byte chunk at index zero is legal", Smallest && Smallest->ChunkIndex == 0 && Smallest->Data.size() == 1 && Smallest->Data.front() == 0x9d);
	return Passed;
}

static bool CheckOverlappingChunk()
{
	bool Passed = true;
	for (const std::size_t SourceOffset : {std::size_t{1}, ChunkDataOffset + 1})
	{
		std::array<u8, sizeof(GoldenChunk) + 4> Buffer;
		Buffer.fill(0x77);
		std::ranges::copy(Data, Buffer.begin() + SourceOffset);
		auto Overlap = Chunk;
		Overlap.Data = xr_span(Buffer).subspan(SourceOffset, sizeof(Data));
		const auto Prior = Buffer;
		Passed &= ExpectError("undersized overlapping encode fails", EncodeBaselineChunk(Overlap, xr_span(Buffer).first(sizeof(GoldenChunk) - 1)),
			BoundMessageError::OutputTooSmall);
		Passed &= Expect("failed overlapping write preserves the source", Buffer == Prior);
		Passed &= Expect("a successful encoding reads aliased source before overwriting it", EncodeBaselineChunk(Overlap, Buffer) == sizeof(GoldenChunk) &&
			std::ranges::equal(xr_span(Buffer).first(sizeof(GoldenChunk)), GoldenChunk));
		Passed &= Expect("aliased output preserves bytes past the datagram", std::ranges::equal(xr_span(Buffer).subspan(sizeof(GoldenChunk)),
			xr_span(Prior).subspan(sizeof(GoldenChunk))));
	}
	return Passed;
}

bool Run()
{
	bool Passed = CheckGolden();
	Passed &= CheckFraming(GoldenBegin, DecodeBaselineBegin, 0x07, 20);
	Passed &= CheckFraming(GoldenChunk, DecodeBaselineChunk, 0x08, 7);
	Passed &= CheckFraming(GoldenEnd, DecodeBaselineEnd, 0x06, 4);
	Passed &= CheckFixedSize(GoldenBegin, DecodeBaselineBegin);
	Passed &= CheckFixedSize(GoldenEnd, DecodeBaselineEnd);
	Passed &= CheckOutput(GoldenBegin, Begin, EncodeBaselineBegin);
	Passed &= CheckOutput(GoldenChunk, Chunk, EncodeBaselineChunk);
	Passed &= CheckOutput(GoldenEnd, End, EncodeBaselineEnd);
	Passed &= CheckManifestBounds();
	Passed &= CheckChunkBounds();
	Passed &= CheckOverlappingChunk();
	if (!Passed)
	{
		return false;
	}
	std::puts("Baseline transfer codec checks passed");
	return true;
}
}
