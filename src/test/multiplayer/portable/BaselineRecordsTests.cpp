#include "BaselineRecords.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace BaselineRecordsTests
{
using namespace Multiplayer;

constexpr JoinIdentity Identity{0x8877665544332211ull, 37, 11};
constexpr u32 Id = 0x12345678u;
constexpr u8 Golden[] = {0x29, 0x03, 0x00, 0xa7, 0x00, 0xfe, 0x85, 0x00, 0x00};
constexpr u8 FirstPayload[] = {0xa7, 0x00, 0xfe};
constexpr Wire::BaselineManifest Manifest{Id, 0xfffffffau, 17, sizeof(Golden), 2, 1};

class FixtureSchema final : public IBaselineRecordSchema
{
public:
	bool Validate(BaselineRecordView Record) const noexcept override
	{
		++Calls;
		return (Record.Kind == 0x29 && std::ranges::equal(Record.Payload, FirstPayload)) ||
			(Record.Kind == 0x85 && Record.Payload.empty()) ||
			(Record.Kind == 0xc2 && std::ranges::all_of(Record.Payload, [](u8 Byte) { return Byte == 0x2d; }));
	}

	mutable int Calls = 0;
};

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

static bool ExpectError(const char* Step, const std::expected<BaselineRecords, BaselineRecordsError>& Result, BaselineRecordsError Error)
{
	if (!Result && Result.error() == Error)
	{
		return true;
	}
	std::fprintf(stderr, "%s: expected %d, got %d\n", Step, static_cast<int>(Error), Result ? -1 : static_cast<int>(Result.error()));
	return false;
}

static bool CheckGolden()
{
	const FixtureSchema Schema;
	auto Records = BaselineRecords::Validate({Identity, Manifest, Golden}, Identity, Id, Schema);
	if (!Expect("golden baseline validates the whole stream before returning a cursor", Records && Schema.Calls == 2))
	{
		return false;
	}
	const auto First = Records->Next();
	const auto Second = Records->Next();
	bool Passed = Expect("cursor retains baseline identity and manifest", Records->GetIdentity() == Identity && Records->GetManifest() == Manifest);
	Passed &= Expect("first record matches literal kind and payload", First && First->Kind == 0x29 && std::ranges::equal(First->Payload, FirstPayload));
	Passed &= Expect("schema-permitted empty record is counted and returned", Second && Second->Kind == 0x85 && Second->Payload.empty());
	Passed &= Expect("cursor ends exactly after the declared records", !Records->Next() && !Records->Next() && Schema.Calls == 2);
	return Passed;
}

static bool CheckStagedInput()
{
	const FixtureSchema Schema;
	constexpr JoinIdentity Foreign[] = {
		{Identity.ServerSession + 1, Identity.WorldEpoch, Identity.ConnectionEpoch},
		{Identity.ServerSession, Identity.WorldEpoch - 1, Identity.ConnectionEpoch},
		{Identity.ServerSession, Identity.WorldEpoch, Identity.ConnectionEpoch + 1},
	};
	bool Passed = true;
	for (const auto& Other : Foreign)
	{
		Passed &= ExpectError("staged data must match the current session, world and connection", BaselineRecords::Validate({Other, Manifest, Golden}, Identity, Id, Schema),
			BaselineRecordsError::IdentityMismatch);
	}
	constexpr JoinIdentity Unbound{Identity.ServerSession, Identity.WorldEpoch, 0};
	Passed &= ExpectError("matching unbound identities still fail", BaselineRecords::Validate({Unbound, Manifest, Golden}, Unbound, Id, Schema), BaselineRecordsError::IdentityMismatch);
	constexpr u32 CurrentId = Id + 1;
	Passed &= ExpectError("a same-round baseline superseded by rebase cannot reach record validation",
		BaselineRecords::Validate({Identity, Manifest, Golden}, Identity, CurrentId, Schema), BaselineRecordsError::BaselineMismatch);
	auto Bad = Manifest;
	Bad.ChunkCount = 2;
	Passed &= ExpectError("manually constructed views still require a valid manifest", BaselineRecords::Validate({Identity, Bad, Golden}, Identity, Id, Schema),
		BaselineRecordsError::InvalidManifest);
	Bad = Manifest;
	Bad.ByteCount = 0xffffffffu;
	Passed &= ExpectError("bad byte limits fail before framing", BaselineRecords::Validate({Identity, Bad, Golden}, Identity, Id, Schema), BaselineRecordsError::InvalidManifest);
	for (std::size_t Length = 0; Length < sizeof(Golden); ++Length)
	{
		Passed &= ExpectError("shorter staged storage fails before parsing records", BaselineRecords::Validate({Identity, Manifest, xr_span(Golden).first(Length)}, Identity, Id, Schema),
			BaselineRecordsError::ByteCountMismatch);
	}
	std::array<u8, 10> Extra{};
	std::ranges::copy(Golden, Extra.begin());
	Passed &= ExpectError("larger staged storage must also match the declared byte count", BaselineRecords::Validate({Identity, Manifest, Extra}, Identity, Id, Schema),
		BaselineRecordsError::ByteCountMismatch);
	Passed &= Expect("rejected identity, baseline, manifest and size never invoke the schema", Schema.Calls == 0);
	return Passed;
}

static bool CheckFraming()
{
	bool Passed = true;
	const FixtureSchema Schema;
	for (u32 Length = 1; Length < 6; ++Length)
	{
		const Wire::BaselineManifest Declaration{Id, 10, 17, Length, 1, 1};
		const auto Error = Length < 3 ? BaselineRecordsError::TruncatedHeader : BaselineRecordsError::TruncatedPayload;
		Passed &= ExpectError("a full staged range can contain a truncated record", BaselineRecords::Validate({Identity, Declaration, xr_span(Golden).first(Length)}, Identity, Id, Schema), Error);
	}
	Passed &= Expect("partial record cannot reach the schema", Schema.Calls == 0);

	const FixtureSchema LateSchema;
	auto Damaged = std::to_array(Golden);
	Damaged[7] = 1;
	Passed &= ExpectError("a truncated last record fails the whole baseline", BaselineRecords::Validate({Identity, Manifest, Damaged}, Identity, Id, LateSchema),
		BaselineRecordsError::TruncatedPayload);
	Passed &= Expect("only the complete prefix was eligible for schema validation", LateSchema.Calls == 1);

	auto Count = Manifest;
	--Count.ByteCount;
	const FixtureSchema ShortHeaderSchema;
	Passed &= ExpectError("a short final header fails after a valid prefix", BaselineRecords::Validate({Identity, Count, xr_span(Golden).first(8)}, Identity, Id, ShortHeaderSchema),
		BaselineRecordsError::TruncatedHeader);
	Passed &= Expect("a short final header never reaches the schema", ShortHeaderSchema.Calls == 1);

	Count = Manifest;
	Count.RecordCount = 3;
	Passed &= ExpectError("exact record boundary before the declared count is missing records", BaselineRecords::Validate({Identity, Count, Golden}, Identity, Id, Schema),
		BaselineRecordsError::MissingRecords);
	Count.RecordCount = 1;
	Passed &= ExpectError("an extra complete record is trailing data", BaselineRecords::Validate({Identity, Count, Golden}, Identity, Id, Schema), BaselineRecordsError::TrailingData);
	std::array<u8, 10> Extra{};
	std::ranges::copy(Golden, Extra.begin());
	Count = Manifest;
	Count.ByteCount = Extra.size();
	Passed &= ExpectError("a trailing byte is not silently ignored", BaselineRecords::Validate({Identity, Count, Extra}, Identity, Id, Schema), BaselineRecordsError::TrailingData);
	return Passed;
}

static bool CheckSchema()
{
	auto WrongKind = std::to_array(Golden);
	WrongKind[6] = 0x57;
	const FixtureSchema Schema;
	bool Passed = ExpectError("late unknown kind prevents obtaining any record cursor", BaselineRecords::Validate({Identity, Manifest, WrongKind}, Identity, Id, Schema),
		BaselineRecordsError::InvalidRecord);
	Passed &= Expect("even the last framed record reaches the trusted schema", Schema.Calls == 2);
	auto WrongPayload = std::to_array(Golden);
	WrongPayload[5] ^= 0x80;
	Passed &= ExpectError("known kind with bad payload fails the baseline", BaselineRecords::Validate({Identity, Manifest, WrongPayload}, Identity, Id, Schema),
		BaselineRecordsError::InvalidRecord);
	return Passed;
}

static bool CheckCountLimits()
{
	const FixtureSchema Schema;
	auto Empty = BaselineRecords::Validate({Identity, {Id, 0, 0, 0, 0, 0}, {}}, Identity, Id, Schema);
	bool Passed = Expect("empty baseline is validated without schema calls", Empty && !Empty->Next() && Schema.Calls == 0);
	std::array<u8, 24576> Data{};
	for (std::size_t Offset = 0; Offset < Data.size(); Offset += 3)
	{
		Data[Offset] = 0x85;
	}
	constexpr Wire::BaselineManifest MaxCount{Id, 10, 17, 24576, 8192, 24};
	auto Excess = MaxCount;
	++Excess.RecordCount;
	Passed &= ExpectError("record cap applies before schema calls", BaselineRecords::Validate({Identity, Excess, Data}, Identity, Id, Schema), BaselineRecordsError::InvalidManifest);
	Passed &= Expect("over-count baseline reached no schema calls", Schema.Calls == 0);
	auto Records = BaselineRecords::Validate({Identity, MaxCount, Data}, Identity, Id, Schema);
	if (!Expect("all 8,192 bounded records validate", Records && Schema.Calls == 8192))
	{
		return false;
	}
	for (int Index = 0; Index < 8192; ++Index)
	{
		const auto Record = Records->Next();
		if (!Expect("cursor visits each framed record once", Record && Record->Kind == 0x85 && Record->Payload.empty()))
		{
			return false;
		}
	}
	Passed &= Expect("fully consumed cursor is exhausted", !Records->Next());
	return Passed;
}

static bool CheckMaximumPayload()
{
	std::array<u8, 65538> Data;
	Data.fill(0x2d);
	Data[0] = 0xc2;
	Data[1] = 0xff;
	Data[2] = 0xff;
	constexpr Wire::BaselineManifest Large{Id, 10, 17, 65538, 1, 65};
	const FixtureSchema Schema;
	auto Records = BaselineRecords::Validate({Identity, Large, Data}, Identity, Id, Schema);
	if (!Expect("u16 length accepts its full unsigned range", Records.has_value()))
	{
		return false;
	}
	const auto Record = Records->Next();
	bool Passed = Expect("large payload borrows exactly the framed bytes", Record && Record->Kind == 0xc2 && Record->Payload.size() == 65535 &&
		Record->Payload.data() == Data.data() + 3 && !Records->Next());
	auto Short = Large;
	--Short.ByteCount;
	Passed &= ExpectError("one missing byte of a maximum payload is detected", BaselineRecords::Validate({Identity, Short, xr_span(Data).first(65537)}, Identity, Id, Schema),
		BaselineRecordsError::TruncatedPayload);
	return Passed;
}

static bool CheckChunkBoundary()
{
	std::array<u8, 1029> Source;
	Source.fill(0x2d);
	Source[0] = 0xc2;
	Source[1] = 0xfc;
	Source[2] = 0x03;
	std::ranges::copy(xr_span(Golden).first(6), Source.begin() + 1023);
	std::array<u8, 1029> Storage;
	BaselineStagingBudget Budget(Storage.size(), 1);
	BaselineStaging Receiver(Identity, Storage, Budget.TryReserve(Storage.size()).value());
	constexpr Wire::BaselineManifest Split{Id, 10, 17, 1029, 2, 2};
	if (!Expect("fixture assembles canonical chunks with a split record header", Receiver.Begin({Identity, 1, Split}).has_value() &&
		Receiver.AddChunk({Identity, 3, Id, 1, xr_span(Source).subspan(1024)}).has_value() &&
		Receiver.AddChunk({Identity, 2, Id, 0, xr_span(Source).first(1024)}).has_value()))
	{
		return false;
	}
	const auto Staged = Receiver.Finish({Identity, 4, Id});
	if (!Expect("fixture has a complete staged baseline", Staged.has_value()))
	{
		return false;
	}
	const FixtureSchema Schema;
	auto Records = BaselineRecords::Validate(*Staged, Identity, Id, Schema);
	if (!Expect("staged records validate across the chunk boundary", Records && Schema.Calls == 2))
	{
		return false;
	}
	const auto First = Records->Next();
	const auto Second = Records->Next();
	return Expect("cursor treats the split header as one record", First && First->Kind == 0xc2 && First->Payload.size() == 1020 &&
		Second && Second->Kind == 0x29 && std::ranges::equal(Second->Payload, FirstPayload) && !Records->Next());
}

bool Run()
{
	bool Passed = CheckGolden();
	Passed &= CheckStagedInput();
	Passed &= CheckFraming();
	Passed &= CheckSchema();
	Passed &= CheckCountLimits();
	Passed &= CheckMaximumPayload();
	Passed &= CheckChunkBoundary();
	if (!Passed)
	{
		return false;
	}
	std::puts("Baseline record checks passed");
	return true;
}
}
