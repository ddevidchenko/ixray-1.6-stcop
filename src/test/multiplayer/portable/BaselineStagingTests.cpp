#include "BaselineStaging.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <memory>

namespace BaselineStagingTests
{
using namespace Multiplayer;
using namespace Multiplayer::Wire;

constexpr JoinIdentity Identity{0x8877665544332211ull, 37, 11};
constexpr u32 Id = 0x12345678u;
constexpr BaselineBegin Declaration{Identity, 9, {Id, 0xfffffffau, 17, 1027, 19, 2}};
constexpr BaselineEnd End{Identity, 12, Id};
constexpr u8 Tail[] = {0x06, 0xa7, 0x00};

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

template <class Value>
static bool ExpectError(const char* Step, const std::expected<Value, BaselineStagingError>& Result, BaselineStagingError Error)
{
	if (!Result && Result.error() == Error)
	{
		return true;
	}
	std::fprintf(stderr, "%s: expected %d, got %d\n", Step, static_cast<int>(Error), Result ? -1 : static_cast<int>(Result.error()));
	return false;
}

static bool CheckAssembly()
{
	std::array<u8, 1031> Storage;
	Storage.fill(0xcc);
	std::array<u8, 1024> Full;
	Full.fill(0x25);
	std::array<u8, 1060> Datagram;
	BaselineStagingBudget Budget(Storage.size(), 1);
	BaselineStaging Receiver(Identity, Storage, Budget.TryReserve(Storage.size()).value());
	const auto BeginSize = EncodeBaselineBegin(Declaration, Datagram);
	const auto DecodedBegin = BeginSize ? DecodeBaselineBegin(xr_span(Datagram).first(*BeginSize)) : std::unexpected(BoundMessageError::InvalidEnvelope);
	bool Passed = Expect("decoded manifest starts a transfer in caller storage", DecodedBegin && Receiver.Begin(*DecodedBegin).has_value());

	const auto ReceiveChunk = [&](const BaselineChunk& Chunk)
	{
		const auto Size = EncodeBaselineChunk(Chunk, Datagram);
		const auto Decoded = Size ? DecodeBaselineChunk(xr_span(Datagram).first(*Size)) : std::unexpected(BoundMessageError::InvalidEnvelope);
		const bool Accepted = Decoded && Receiver.AddChunk(*Decoded).has_value();
		Datagram.fill(0x99);
		return Accepted;
	};
	Passed &= Expect("tail can arrive before first chunk", ReceiveChunk({Identity, 11, Id, 1, Tail}));
	Passed &= Expect("first chunk completes the declared bytes", ReceiveChunk({Identity, 10, Id, 0, Full}));
	const auto EndSize = EncodeBaselineEnd(End, Datagram);
	const auto DecodedEnd = EndSize ? DecodeBaselineEnd(xr_span(Datagram).first(*EndSize)) : std::unexpected(BoundMessageError::InvalidEnvelope);
	if (!Expect("end fixture decodes", DecodedEnd.has_value()))
	{
		return false;
	}
	const auto Baseline = Receiver.Finish(*DecodedEnd);
	Passed &= Expect("matching end exposes the exact manifest and staged bytes", Baseline && Baseline->Identity == Identity &&
		Baseline->Manifest == Declaration.Manifest && Baseline->Data.size() == 1027 && Baseline->Data.data() == Storage.data());
	Passed &= Expect("canonical offsets retain the decoded data after datagram reuse", Baseline && Baseline->Data.size() == 1027 &&
		std::ranges::equal(Baseline->Data.first(1024), Full) && std::ranges::equal(Baseline->Data.subspan(1024), Tail));
	Passed &= Expect("assembly touches only bytes declared in the manifest", std::ranges::all_of(xr_span(Storage).subspan(1027), [](u8 Byte) { return Byte == 0xcc; }));
	return Passed;
}

static bool CheckBeginValidation()
{
	std::array<u8, 1026> Storage;
	Storage.fill(0xcc);
	const auto Prior = Storage;
	BaselineStagingBudget Budget(Storage.size(), 2);
	BaselineStaging Receiver(Identity, Storage, Budget.TryReserve(Storage.size()).value());
	constexpr JoinIdentity Foreign[] = {
		{Identity.ServerSession + 1, Identity.WorldEpoch, Identity.ConnectionEpoch},
		{Identity.ServerSession, Identity.WorldEpoch + 1, Identity.ConnectionEpoch},
		{Identity.ServerSession, Identity.WorldEpoch, Identity.ConnectionEpoch + 1},
	};
	bool Passed = true;
	for (const auto& Other : Foreign)
	{
		auto Wrong = Declaration;
		Wrong.Identity = Other;
		Passed &= ExpectError("a new transfer must match the full bound identity", Receiver.Begin(Wrong), BaselineStagingError::IdentityMismatch);
	}
	auto Invalid = Declaration;
	Invalid.Manifest.ByteCount = 0xffffffffu;
	Passed &= ExpectError("invalid typed manifest fails before a capacity decision", Receiver.Begin(Invalid), BaselineStagingError::InvalidManifest);
	Invalid = Declaration;
	Invalid.Manifest.ChunkCount = 1;
	Passed &= ExpectError("typed manifest still requires canonical chunk count", Receiver.Begin(Invalid), BaselineStagingError::InvalidManifest);
	Passed &= ExpectError("owner storage smaller than the declaration rejects it", Receiver.Begin(Declaration), BaselineStagingError::InsufficientStorage);
	Passed &= Expect("rejected declarations do not write storage", Storage == Prior);
	Passed &= ExpectError("a rejected declaration cannot be finished", Receiver.Finish(End), BaselineStagingError::InvalidState);

	constexpr BaselineBegin Small{Identity, 2, {Id, 100, 25, 3, 1, 1}};
	Passed &= Expect("a smaller transfer can use the available buffer", Receiver.Begin(Small).has_value() &&
		Receiver.AddChunk({Identity, 3, Id, 0, Tail}).has_value() && Receiver.Finish(End).has_value());

	const JoinIdentity Unbound{0, Identity.WorldEpoch, Identity.ConnectionEpoch};
	BaselineStaging Unassigned(Unbound, {}, Budget.TryReserve(0).value());
	Passed &= ExpectError("matching unbound identities cannot start a transfer", Unassigned.Begin({Unbound, 0, {Id, 0, 0, 0, 0, 0}}),
		BaselineStagingError::IdentityMismatch);
	return Passed;
}

static bool CheckRejectedUpdates()
{
	std::array<u8, 1027> Storage;
	Storage.fill(0xcc);
	std::array<u8, 1024> Chunk;
	Chunk.fill(0x25);
	const BaselineChunk First{Identity, 10, Id, 0, Chunk};
	const BaselineChunk Last{Identity, 11, Id, 1, Tail};
	BaselineStagingBudget Budget(Storage.size(), 1);
	BaselineStaging Receiver(Identity, Storage, Budget.TryReserve(Storage.size()).value());
	bool Passed = ExpectError("data cannot arrive before begin", Receiver.AddChunk(First), BaselineStagingError::InvalidState);
	Passed &= ExpectError("end cannot arrive before begin", Receiver.Finish(End), BaselineStagingError::InvalidState);
	Passed &= Expect("begin then one canonical chunk succeeds", Receiver.Begin(Declaration).has_value() && Receiver.AddChunk(First).has_value());
	const auto Prior = Storage;

	auto Repeated = Declaration;
	++Repeated.MessageSequence;
	Passed &= Expect("identical declaration is idempotent across message sequences", Receiver.Begin(Repeated).has_value());
	++Repeated.Manifest.EventWatermark;
	Passed &= ExpectError("changed manifest cannot replace the active one", Receiver.Begin(Repeated), BaselineStagingError::ConflictingManifest);
	Repeated = Declaration;
	++Repeated.Manifest.BaselineId;
	Passed &= ExpectError("a different baseline cannot replace the active transfer", Receiver.Begin(Repeated), BaselineStagingError::ConflictingManifest);
	Passed &= ExpectError("different baseline chunk is rejected", Receiver.AddChunk({Identity, 11, Id + 1, 1, Tail}), BaselineStagingError::BaselineMismatch);
	Passed &= ExpectError("different baseline end is rejected", Receiver.Finish({Identity, 12, Id + 1}), BaselineStagingError::BaselineMismatch);

	constexpr JoinIdentity OtherConnection{Identity.ServerSession, Identity.WorldEpoch, Identity.ConnectionEpoch + 1};
	Passed &= ExpectError("foreign connection data is rejected even with matching baseline ID", Receiver.AddChunk({OtherConnection, 11, Id, 1, Tail}),
		BaselineStagingError::IdentityMismatch);
	Passed &= ExpectError("foreign connection end is rejected", Receiver.Finish({OtherConnection, 12, Id}), BaselineStagingError::IdentityMismatch);
	Passed &= ExpectError("declared chunk count limits indices", Receiver.AddChunk({Identity, 11, Id, 2, Tail}), BaselineStagingError::InvalidChunk);
	Passed &= ExpectError("a short nonfinal chunk is rejected", Receiver.AddChunk({Identity, 10, Id, 0, xr_span(Chunk).first(1023)}), BaselineStagingError::InvalidChunk);
	Passed &= ExpectError("final chunk must match the remainder", Receiver.AddChunk({Identity, 11, Id, 1, xr_span(Tail).first(2)}), BaselineStagingError::InvalidChunk);
	Passed &= ExpectError("full chunk cannot overrun the final remainder", Receiver.AddChunk({Identity, 11, Id, 1, Chunk}), BaselineStagingError::InvalidChunk);
	Passed &= Expect("identical chunk replay is idempotent", Receiver.AddChunk(First).has_value());
	Chunk.back() = 0x12;
	Passed &= ExpectError("conflicting duplicate is rejected", Receiver.AddChunk(First), BaselineStagingError::ConflictingChunk);
	Passed &= ExpectError("duplicate and rejected chunks cannot make an early end complete", Receiver.Finish(End), BaselineStagingError::Incomplete);
	Passed &= Expect("rejected updates and early end preserve storage", Storage == Prior);

	Passed &= Expect("missing chunk can still be supplied after early end", Receiver.AddChunk(Last).has_value());
	const auto Baseline = Receiver.Finish(End);
	Passed &= Expect("original manifest and accepted bytes survived failed updates", Baseline && Baseline->Manifest == Declaration.Manifest &&
		Baseline->Data.size() == 1027 && Baseline->Data[1023] == 0x25 && std::ranges::equal(Baseline->Data.subspan(1024), Tail));
	Passed &= ExpectError("completion can be observed only once", Receiver.Finish(End), BaselineStagingError::InvalidState);
	Passed &= ExpectError("completed transfer rejects following data", Receiver.AddChunk(Last), BaselineStagingError::InvalidState);
	Passed &= ExpectError("completed world cannot start a second transfer", Receiver.Begin(Declaration), BaselineStagingError::InvalidState);
	return Passed;
}

static bool CheckRoundReset()
{
	std::array<u8, 1027> Storage{};
	std::array<u8, 1024> Full;
	Full.fill(0x51);
	BaselineStagingBudget Budget(Storage.size(), 1);
	BaselineStaging Receiver(Identity, Storage, Budget.TryReserve(Storage.size()).value());
	bool Passed = Expect("fixture receives the old world's first chunk", Receiver.Begin(Declaration).has_value() &&
		Receiver.AddChunk({Identity, 10, Id, 0, Full}).has_value());
	Passed &= Expect("duplicate and older owner updates do not advance the world", !Receiver.AdvanceWorld(37) && !Receiver.AdvanceWorld(36));
	Passed &= ExpectError("a rejected reset preserves the outstanding manifest", Receiver.Finish(End), BaselineStagingError::Incomplete);
	Full.back() = 0x52;
	Passed &= ExpectError("a rejected reset preserves already received chunks", Receiver.AddChunk({Identity, 10, Id, 0, Full}),
		BaselineStagingError::ConflictingChunk);
	constexpr JoinIdentity Next{Identity.ServerSession, 38, Identity.ConnectionEpoch};
	Passed &= Expect("owner accepts a new same-map round", Receiver.AdvanceWorld(Next.WorldEpoch));
	Passed &= ExpectError("old-world begin cannot reset the new round's stage", Receiver.Begin(Declaration), BaselineStagingError::IdentityMismatch);
	auto NextDeclaration = Declaration;
	NextDeclaration.Identity = Next;
	Passed &= Expect("next world may reuse the same numeric baseline ID", Receiver.Begin(NextDeclaration).has_value());
	Passed &= ExpectError("old-world chunk cannot satisfy the new transfer", Receiver.AddChunk({Identity, 10, Id, 0, Full}), BaselineStagingError::IdentityMismatch);
	Passed &= ExpectError("old-world end cannot complete the transfer", Receiver.Finish(End), BaselineStagingError::IdentityMismatch);
	Passed &= Expect("new world can stage its tail", Receiver.AddChunk({Next, 11, Id, 1, Tail}).has_value());
	Passed &= ExpectError("previously received chunk cannot satisfy the reset bitmap", Receiver.Finish({Next, 12, Id}), BaselineStagingError::Incomplete);
	Full.fill(0x78);
	Passed &= Expect("new-world data replaces old bytes at their canonical offsets", Receiver.AddChunk({Next, 10, Id, 0, Full}).has_value());
	const auto Baseline = Receiver.Finish({Next, 12, Id});
	Passed &= Expect("only the newly staged world is exposed", Baseline && Baseline->Identity == Next && Baseline->Data.size() == 1027 &&
		std::ranges::equal(Baseline->Data.first(1024), Full) && std::ranges::equal(Baseline->Data.subspan(1024), Tail));
	Passed &= Expect("ready storage can be reused for a later world but the epoch cannot wrap", Receiver.AdvanceWorld(0xffffffffu) && !Receiver.AdvanceWorld(0));
	return Passed;
}

static bool CheckAliasedInput()
{
	std::array<u8, 6> Storage{0x77, 0x06, 0xa7, 0x00, 0xcc, 0xcc};
	const auto Source = xr_span(Storage).subspan(1, 3);
	BaselineStagingBudget Budget(Storage.size(), 1);
	BaselineStaging Receiver(Identity, Storage, Budget.TryReserve(Storage.size()).value());
	bool Passed = Expect("overlap fixture starts", Receiver.Begin({Identity, 1, {Id, 10, 0, 3, 1, 1}}).has_value());
	Passed &= Expect("chunk source may overlap the target bytes", Receiver.AddChunk({Identity, 2, Id, 0, Source}).has_value());
	const auto Baseline = Receiver.Finish(End);
	Passed &= Expect("overlapping input keeps its original values and leaves the unused tail", Baseline && std::ranges::equal(Baseline->Data, Tail) &&
		Storage[3] == 0 && Storage[4] == 0xcc && Storage[5] == 0xcc);
	return Passed;
}

static bool CheckEmptyAndMaximum()
{
	BaselineStagingBudget Budget(4194304, 2);
	BaselineStaging Empty(Identity, {}, Budget.TryReserve(0).value());
	bool Passed = Expect("an empty baseline needs no payload buffer", Empty.Begin({Identity, 1, {Id, 10, 0, 0, 0, 0}}).has_value());
	Passed &= ExpectError("an empty baseline never accepts a chunk", Empty.AddChunk({Identity, 2, Id, 0, Tail}), BaselineStagingError::InvalidChunk);
	const auto Blank = Empty.Finish(End);
	Passed &= Expect("empty baseline yields an empty view only at end", Blank && Blank->Data.empty() && Blank->Manifest.ChunkCount == 0);

	const auto Storage = std::make_unique<u8[]>(4194304);
	BaselineStaging FullStage(Identity, {Storage.get(), 4194304}, Budget.TryReserve(4194304).value());
	std::array<u8, 1024> Data;
	Passed &= Expect("accepts the exact four MiB staging capacity", FullStage.Begin({Identity, 1, {Id, 10, 5, 4194304, 8192, 4096}}).has_value());
	for (u16 Index = 4095; Index > 0; --Index)
	{
		Data.fill(static_cast<u8>(Index));
		if (!Expect("all bounded indices are accepted, including the largest", FullStage.AddChunk({Identity, 2, Id, Index, Data}).has_value()))
		{
			return false;
		}
	}
	Passed &= ExpectError("one missing chunk out of 4096 prevents completion", FullStage.Finish(End), BaselineStagingError::Incomplete);
	Data.fill(0x49);
	Passed &= Expect("accepts the final missing chunk", FullStage.AddChunk({Identity, 2, Id, 0, Data}).has_value());
	const auto Maximum = FullStage.Finish(End);
	Passed &= Expect("full buffer exposes the first and last chunk", Maximum && Maximum->Data.size() == 4194304 && Maximum->Data.front() == 0x49 &&
		Maximum->Data.back() == 0xff);
	return Passed;
}

bool Run()
{
	bool Passed = CheckAssembly();
	Passed &= CheckBeginValidation();
	Passed &= CheckRejectedUpdates();
	Passed &= CheckRoundReset();
	Passed &= CheckAliasedInput();
	Passed &= CheckEmptyAndMaximum();
	if (!Passed)
	{
		return false;
	}
	std::puts("Baseline staging checks passed");
	return true;
}
}
