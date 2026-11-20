#include "SnapshotBatch.h"

#include <algorithm>
#include <cstdio>

namespace SnapshotBatchTests
{
using namespace Multiplayer;
using namespace Multiplayer::Wire;

constexpr u8 Golden[] = {
	0x49, 0x58, 0x4d, 0x50, 0x01, 0x00, 0x0b, 0x00, 0x21, 0x00,
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
	0x00, 0x00, 0x00, 0x00, 0x04, 0x03, 0x02, 0x01, 0xef, 0xcd, 0xab, 0x89,
	0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55,
	0x91, 0x82, 0x73, 0x64, 0x55, 0x46, 0x37, 0x28, 0x78, 0x56, 0x34, 0x12, 0x02, 0x00,
	0x01, 0x02, 0x00, 0xca, 0xfe,
	0x02, 0x03, 0x00, 0x00, 0xfe, 0x80,
};
constexpr SnapshotBatchHeader Header{
	{0x8877665544332211ull, 0, 0x01020304u}, 0x89abcdefu, 0x11223344u, 0x55667788u, 0x2837465564738291ull, 0x12345678u,
};
constexpr u8 FirstPayload[]{0xca, 0xfe};
constexpr u8 SecondPayload[]{0x00, 0xfe, 0x80};
constexpr SnapshotRecordView Input[]{
	{SnapshotRecordKind::HumanCombatant, FirstPayload},
	{SnapshotRecordKind::StalkerCombatant, SecondPayload},
};
constexpr u8 Canary = 0xc7;

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

static bool CheckGolden()
{
	auto Batch = SnapshotBatch::Decode(Golden);
	bool Passed = Expect("independent snapshot datagram decodes with the declared header and count",
		Batch && Batch->GetHeader() == Header && Batch->GetRecordCount() == 2);
	if (Batch)
	{
		const auto First = Batch->Next();
		const auto Second = Batch->Next();
		Passed &= Expect("human and stalker record kinds and boundaries follow the fixture order",
			First && Second && First->Kind == SnapshotRecordKind::HumanCombatant && Second->Kind == SnapshotRecordKind::StalkerCombatant &&
			std::ranges::equal(First->Payload, FirstPayload) && std::ranges::equal(Second->Payload, SecondPayload));
		Passed &= Expect("record bytes borrow the correct portion of the datagram",
			First && Second && First->Payload.data() == Golden + 55 && Second->Payload.data() == Golden + 60);
		Passed &= Expect("record cursor stays at end", !Batch->Next() && !Batch->Next() && Batch->GetRecordCount() == 2);
	}

	xr_array<u8, sizeof(Golden) + 2> Storage;
	Storage.fill(Canary);
	const auto Output = xr_span(Storage).subspan(1);
	const auto Written = EncodeSnapshotBatch(Header, Input, Output);
	Passed &= Expect("snapshot encoding matches independent bytes",
		Written && *Written == sizeof(Golden) && std::ranges::equal(Output.first(sizeof(Golden)), Golden));
	Passed &= Expect("encoding preserves bytes around the datagram", Storage.front() == Canary && Storage.back() == Canary);
	return Passed;
}

template <class Value>
static bool ExpectError(const char* Step, const std::expected<Value, BoundMessageError>& Result, BoundMessageError Error)
{
	return Expect(Step, !Result && Result.error() == Error);
}

static bool CheckMalformedFrames()
{
	const auto Datagram = xr_span(Golden);
	bool Passed = true;
	for (std::size_t Size = 0; Size < Datagram.size(); ++Size)
	{
		Passed &= ExpectError("every shortened datagram fails envelope validation",
			SnapshotBatch::Decode(Datagram.first(Size)), BoundMessageError::InvalidEnvelope);
		if (Size >= 30)
		{
			auto Bytes = std::to_array(Golden);
			Bytes[8] = static_cast<u8>(Size - 30);
			Passed &= ExpectError("every shortened but correctly framed payload fails validation",
				SnapshotBatch::Decode(xr_span(Bytes).first(Size)), BoundMessageError::TruncatedPayload);
		}
	}

	auto Bytes = std::to_array(Golden);
	Bytes[6] = 0x0a;
	Passed &= ExpectError("another supported message kind is not a snapshot batch", SnapshotBatch::Decode(Bytes),
		BoundMessageError::WrongMessageType);
	Bytes = std::to_array(Golden);
	std::ranges::fill(xr_span(Bytes).subspan(10, 8), 0);
	Passed &= ExpectError("snapshot batch requires a server session", SnapshotBatch::Decode(Bytes), BoundMessageError::UnboundJoin);
	Bytes = std::to_array(Golden);
	std::ranges::fill(xr_span(Bytes).subspan(22, 4), 0);
	Passed &= ExpectError("snapshot batch requires a connection epoch", SnapshotBatch::Decode(Bytes), BoundMessageError::UnboundJoin);
	return Passed;
}

static bool CheckMalformedRecords()
{
	bool Passed = true;
	for (const u16 Count : {u16{0}, u16{33}, u16{0x0102}})
	{
		auto Bytes = std::to_array(Golden);
		Bytes[50] = static_cast<u8>(Count);
		Bytes[51] = static_cast<u8>(Count >> 8);
		Passed &= ExpectError("record count must be between 1 and 32", SnapshotBatch::Decode(Bytes), BoundMessageError::InvalidPayload);
	}

	auto Bytes = std::to_array(Golden);
	Bytes[50] = 1;
	Passed &= ExpectError("a declared count cannot hide a trailing record", SnapshotBatch::Decode(Bytes), BoundMessageError::TrailingPayload);
	Bytes[50] = 3;
	Passed &= ExpectError("a batch with missing records exposes no cursor", SnapshotBatch::Decode(Bytes), BoundMessageError::TruncatedPayload);

	for (const u8 InvalidKind : {u8{0}, u8{3}, u8{0xff}})
	{
		Bytes = std::to_array(Golden);
		Bytes[57] = InvalidKind;
		Passed &= ExpectError("an unknown later kind invalidates the complete batch", SnapshotBatch::Decode(Bytes), BoundMessageError::InvalidPayload);
	}
	Bytes = std::to_array(Golden);
	Bytes[58] = 0;
	Passed &= ExpectError("a declared empty record fails even with an earlier valid record", SnapshotBatch::Decode(Bytes),
		BoundMessageError::InvalidPayload);
	Bytes[59] = 0xff;
	Passed &= ExpectError("a two-byte record length must fit the bytes still available", SnapshotBatch::Decode(Bytes),
		BoundMessageError::TruncatedPayload);

	xr_array<u8, sizeof(Golden) + 1> Extra{};
	std::ranges::copy(Golden, Extra.begin());
	Extra[8] = 34;
	Passed &= ExpectError("even one trailing byte invalidates the complete batch", SnapshotBatch::Decode(Extra),
		BoundMessageError::TrailingPayload);
	return Passed;
}

static bool CheckLimits()
{
	xr_array<u8, 1101> Output;
	Output.fill(Canary);
	xr_array<u8, 1046> Payload{};
	Payload.front() = 0xaa;
	Payload[1044] = 0x55;
	SnapshotRecordView Record{SnapshotRecordKind::HumanCombatant, xr_span(Payload).first(1045)};
	const SnapshotBatchHeader ZeroCounters{Header.Identity, 0, 0, 0, 0, 0};
	const auto MaxSize = EncodeSnapshotBatch(ZeroCounters, xr_span(&Record, 1), Output);
	bool Passed = Expect("one maximal record fills exactly a 1100-byte frame", MaxSize && *MaxSize == 1100 && Output.back() == Canary);
	auto FullBatch = SnapshotBatch::Decode(xr_span(Output).first(1100));
	Passed &= Expect("maximum frame preserves zero-valued counters", FullBatch && FullBatch->GetHeader() == ZeroCounters);
	if (FullBatch)
	{
		const auto FullRecord = FullBatch->Next();
		Passed &= Expect("the maximum record length is decoded in full", FullRecord &&
			std::ranges::equal(FullRecord->Payload, Record.Payload) && !FullBatch->Next());
	}
	Passed &= ExpectError("oversized datagram is rejected before record reads", SnapshotBatch::Decode(Output), BoundMessageError::InvalidEnvelope);

	const auto Before = Output;
	Record.Payload = Payload;
	Passed &= ExpectError("one byte beyond the batch cap fails encoding", EncodeSnapshotBatch(Header, xr_span(&Record, 1), Output),
		BoundMessageError::InvalidPayload);
	Passed &= Expect("an oversized record leaves existing output unchanged", Output == Before);
	const SnapshotRecordView Combined[] = {
		{SnapshotRecordKind::HumanCombatant, xr_span(Payload).first(524)},
		{SnapshotRecordKind::StalkerCombatant, xr_span(Payload).first(519)},
	};
	Passed &= ExpectError("valid individual lengths can exceed the combined budget", EncodeSnapshotBatch(Header, Combined, Output),
		BoundMessageError::InvalidPayload);
	Passed &= Expect("a later record exceeding the budget leaves output unchanged", Output == Before);

	xr_array<SnapshotRecordView, 33> Many;
	Many.fill({SnapshotRecordKind::StalkerCombatant, SecondPayload});
	const auto MaxCount = EncodeSnapshotBatch(Header, xr_span(Many).first(32), Output);
	Passed &= Expect("32 small records fit in one batch", MaxCount && *MaxCount == 244);
	if (MaxCount && *MaxCount == 244)
	{
		auto Batch = SnapshotBatch::Decode(xr_span(Output).first(*MaxCount));
		Passed &= Expect("a maximum-count batch decodes", Batch && Batch->GetRecordCount() == 32);
		if (Batch)
		{
			for (std::size_t Index = 0; Index < 32; ++Index)
			{
				const auto Item = Batch->Next();
				Passed &= Expect("all 32 records are available", Item && Item->Kind == SnapshotRecordKind::StalkerCombatant &&
					std::ranges::equal(Item->Payload, SecondPayload));
			}
			Passed &= Expect("maximum-count cursor ends at the declared record", !Batch->Next());
		}
	}
	Passed &= ExpectError("an empty batch cannot be encoded", EncodeSnapshotBatch(Header, {}, Output), BoundMessageError::InvalidPayload);
	Passed &= ExpectError("33 small records exceed the record count cap", EncodeSnapshotBatch(Header, Many, Output),
		BoundMessageError::InvalidPayload);
	return Passed;
}

static bool CheckAtomicEncoding()
{
	xr_array<u8, sizeof(Golden)> Output;
	Output.fill(Canary);
	const auto Before = Output;
	bool Passed = ExpectError("insufficient output space fails cleanly", EncodeSnapshotBatch(Header, Input, xr_span(Output).first(62)),
		BoundMessageError::OutputTooSmall);
	auto Unbound = Header;
	Unbound.Identity.ConnectionEpoch = 0;
	Passed &= ExpectError("unbound snapshot encoding fails cleanly", EncodeSnapshotBatch(Unbound, Input, Output), BoundMessageError::UnboundJoin);
	auto LaterInvalid = std::to_array(Input);
	LaterInvalid[1].Kind = static_cast<SnapshotRecordKind>(0xff);
	Passed &= ExpectError("an unsupported kind in a later record fails encoding", EncodeSnapshotBatch(Header, LaterInvalid, Output),
		BoundMessageError::InvalidPayload);
	LaterInvalid[1] = {SnapshotRecordKind::StalkerCombatant, {}};
	Passed &= ExpectError("a later empty record fails encoding", EncodeSnapshotBatch(Header, LaterInvalid, Output),
		BoundMessageError::InvalidPayload);
	Passed &= Expect("all failed encodes leave output entirely unchanged", Output == Before);

	std::ranges::copy(FirstPayload, Output.begin() + 3);
	std::ranges::copy(SecondPayload, Output.begin());
	const SnapshotRecordView Aliased[] = {
		{SnapshotRecordKind::HumanCombatant, xr_span(Output).subspan(3, 2)},
		{SnapshotRecordKind::StalkerCombatant, xr_span(Output).first(3)},
	};
	const auto Written = EncodeSnapshotBatch(Header, Aliased, Output);
	Passed &= Expect("multiple borrowed record payloads can overlap the output datagram", Written && *Written == sizeof(Golden) &&
		std::ranges::equal(Output, Golden));
	return Passed;
}

bool Run()
{
	bool Passed = CheckGolden();
	Passed &= CheckMalformedFrames();
	Passed &= CheckMalformedRecords();
	Passed &= CheckLimits();
	Passed &= CheckAtomicEncoding();
	if (!Passed)
	{
		return false;
	}
	std::puts("Snapshot batch codec checks passed");
	return true;
}
}
