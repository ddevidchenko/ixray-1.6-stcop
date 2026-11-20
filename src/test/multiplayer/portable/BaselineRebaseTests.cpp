#include "BaselineStaging.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace BaselineRebaseTests
{
using namespace Multiplayer;
using namespace Multiplayer::Wire;

constexpr JoinIdentity Identity{0x8877665544332211ull, 17, 9};
constexpr u32 PreviousId = 21;
constexpr u32 ReplacementId = 22;
constexpr BaselineBegin Previous{Identity, 5, {PreviousId, 100, 45, 1027, 4, 2}};
constexpr BaselineBegin Replacement{Identity, 7, {ReplacementId, 101, 45, 1027, 4, 2}};
constexpr u8 Tail[] = {0xa4, 0x00, 0xe7};

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

static bool CheckReplacement()
{
	std::array<u8, 1027> Storage{};
	std::array<u8, 1024> Full;
	Full.fill(0x21);
	BaselineStagingBudget Budget(Storage.size(), 1);
	BaselineStaging Receiver(Identity, Storage, Budget.TryReserve(Storage.size()).value());
	bool Passed = Expect("fixture starts the previous transfer", Receiver.Begin(Previous).has_value() &&
		Receiver.AddChunk({Identity, 6, PreviousId, 0, Full}).has_value());
	Passed &= Expect("newer same-round declaration rebases the active receiver", Receiver.Rebase(Replacement).has_value());
	Passed &= ExpectError("superseded begin cannot roll back the replacement", Receiver.Begin(Previous), BaselineStagingError::ConflictingManifest);
	Passed &= ExpectError("superseded chunk cannot satisfy replacement", Receiver.AddChunk({Identity, 6, PreviousId, 0, Full}), BaselineStagingError::BaselineMismatch);
	Passed &= ExpectError("superseded end cannot finish replacement", Receiver.Finish({Identity, 10, PreviousId}), BaselineStagingError::BaselineMismatch);
	Passed &= Expect("new transfer accepts its own tail", Receiver.AddChunk({Identity, 9, ReplacementId, 1, Tail}).has_value());
	Passed &= ExpectError("old receipt bits cannot satisfy a replacement's missing chunk", Receiver.Finish({Identity, 10, ReplacementId}), BaselineStagingError::Incomplete);
	Passed &= ExpectError("repeating rebase cannot erase accepted replacement chunks", Receiver.Rebase(Replacement), BaselineStagingError::StaleBaseline);
	const auto ExtraBuffer = Budget.TryReserve(0);
	Passed &= Expect("rebasing keeps the receiver's existing buffer place", !ExtraBuffer && ExtraBuffer.error() == BaselineStagingBudgetError::BufferLimit);
	Full.fill(0x39);
	Passed &= Expect("new transfer accepts its own first chunk", Receiver.AddChunk({Identity, 8, ReplacementId, 0, Full}).has_value());
	const auto Result = Receiver.Finish({Identity, 10, ReplacementId});
	Passed &= Expect("completion exposes the new declaration and bytes", Result && Result->Manifest == Replacement.Manifest &&
		Result->Identity == Identity && Result->Data.data() == Storage.data() && Result->Data.size() == 1027 && std::ranges::equal(Result->Data.first(1024), Full) &&
		std::ranges::equal(Result->Data.subspan(1024), Tail));
	return Passed;
}

static bool CheckRejectedReplacement()
{
	std::array<u8, 1027> Storage{};
	std::array<u8, 1024> Full;
	Full.fill(0x61);
	BaselineStagingBudget Budget(Storage.size(), 1);
	BaselineStaging Receiver(Identity, Storage, Budget.TryReserve(Storage.size()).value());
	bool Passed = ExpectError("a replacement requires a current transfer", Receiver.Rebase(Replacement), BaselineStagingError::InvalidState);
	Passed &= Expect("fixture keeps a partially received current baseline", Receiver.Begin(Previous).has_value() &&
		Receiver.AddChunk({Identity, 6, PreviousId, 0, Full}).has_value());
	const auto Prior = Storage;

	constexpr JoinIdentity Foreign[] = {
		{Identity.ServerSession + 1, Identity.WorldEpoch, Identity.ConnectionEpoch},
		{Identity.ServerSession, Identity.WorldEpoch + 1, Identity.ConnectionEpoch},
		{Identity.ServerSession, Identity.WorldEpoch, Identity.ConnectionEpoch + 1},
	};
	for (const auto& Other : Foreign)
	{
		auto Unbound = Replacement;
		Unbound.Identity = Other;
		Passed &= ExpectError("replacement must match the full current identity", Receiver.Rebase(Unbound), BaselineStagingError::IdentityMismatch);
	}

	struct Case
	{
		const char* Name;
		u32 BaselineManifest::* Field;
		u32 Value;
	};
	constexpr Case Cases[] = {
		{"same baseline ID cannot replace even at a newer tick", &BaselineManifest::BaselineId, PreviousId},
		{"older baseline ID is stale", &BaselineManifest::BaselineId, PreviousId - 1},
		{"half-range baseline ID is ambiguous", &BaselineManifest::BaselineId, PreviousId + 0x80000000u},
		{"same capture tick is not a new boundary", &BaselineManifest::CaptureTick, 100},
		{"older capture tick cannot replace even with a new ID", &BaselineManifest::CaptureTick, 99},
		{"half-range capture tick is ambiguous", &BaselineManifest::CaptureTick, 100 + 0x80000000u},
		{"older event watermark cannot replace", &BaselineManifest::EventWatermark, 44},
		{"half-range event watermark is ambiguous", &BaselineManifest::EventWatermark, 45 + 0x80000000u},
	};
	for (const auto& Scenario : Cases)
	{
		auto Stale = Replacement;
		Stale.Manifest.*Scenario.Field = Scenario.Value;
		Passed &= ExpectError(Scenario.Name, Receiver.Rebase(Stale), BaselineStagingError::StaleBaseline);
	}

	auto Bad = Replacement;
	Bad.Manifest.ChunkCount = 1;
	Passed &= ExpectError("newer declaration still requires a valid manifest", Receiver.Rebase(Bad), BaselineStagingError::InvalidManifest);
	Bad = Replacement;
	++Bad.Manifest.ByteCount;
	Passed &= ExpectError("newer declaration must fit reserved storage", Receiver.Rebase(Bad), BaselineStagingError::InsufficientStorage);
	Passed &= Expect("rejected replacements leave the existing storage untouched", Storage == Prior);
	Passed &= Expect("receiving the original tail still succeeds", Receiver.AddChunk({Identity, 9, PreviousId, 1, Tail}).has_value());
	const auto Result = Receiver.Finish({Identity, 10, PreviousId});
	Passed &= Expect("rejected replacements preserve manifest and earlier receipts", Result && Result->Manifest == Previous.Manifest &&
		Result->Data.size() == 1027 && std::ranges::equal(Result->Data.first(1024), Full));
	return Passed;
}

static bool CheckCompletedAndWorldAdvance()
{
	std::array<u8, 3> Storage{};
	BaselineStagingBudget Budget(Storage.size(), 1);
	BaselineStaging Receiver(Identity, Storage, Budget.TryReserve(Storage.size()).value());
	constexpr BaselineBegin First{Identity, 2, {PreviousId, 100, 45, 2, 1, 1}};
	constexpr u32 LaterId = ReplacementId + 2;
	constexpr BaselineBegin Next{Identity, 5, {LaterId, 104, 50, 3, 1, 1}};
	bool Passed = Expect("fixture receives a nonempty first baseline", Receiver.Begin(First).has_value() &&
		Receiver.AddChunk({Identity, 3, PreviousId, 0, xr_span(Tail).first(2)}).has_value());
	const auto FirstView = Receiver.Finish({Identity, 4, PreviousId});
	Passed &= Expect("fixture completes the first baseline", FirstView.has_value());
	Passed &= ExpectError("a stale replacement cannot reopen a completed transfer", Receiver.Rebase(First), BaselineStagingError::StaleBaseline);
	Passed &= ExpectError("rejected rebase keeps completion one-shot", Receiver.Finish({Identity, 4, PreviousId}), BaselineStagingError::InvalidState);
	Passed &= Expect("rejected rebase preserves the completed bytes", FirstView && std::ranges::equal(FirstView->Data, xr_span(Tail).first(2)));
	Passed &= Expect("same-world rebase also replaces a completed transfer", Receiver.Rebase(Next).has_value());
	Passed &= ExpectError("completed old buffer cannot publish replacement data", Receiver.Finish({Identity, 7, LaterId}), BaselineStagingError::Incomplete);
	Passed &= Expect("replacement may use a different valid byte count in the same buffer", Receiver.AddChunk({Identity, 6, LaterId, 0, Tail}).has_value());
	const auto Result = Receiver.Finish({Identity, 7, LaterId});
	Passed &= Expect("second completion exposes only the new view", Result && Result->Manifest == Next.Manifest && std::ranges::equal(Result->Data, Tail));

	constexpr JoinIdentity NewWorld{Identity.ServerSession, Identity.WorldEpoch + 1, Identity.ConnectionEpoch};
	auto NewWorldFirst = First;
	NewWorldFirst.Identity = NewWorld;
	Passed &= Expect("trusted world change clears replacement history", Receiver.AdvanceWorld(NewWorld.WorldEpoch));
	Passed &= ExpectError("old-world replacement cannot reopen after reset", Receiver.Rebase(Next), BaselineStagingError::IdentityMismatch);
	Passed &= ExpectError("new world requires an ordinary initial declaration", Receiver.Rebase(NewWorldFirst), BaselineStagingError::InvalidState);
	Passed &= Expect("new world can reuse the earlier numeric baseline ID", Receiver.Begin(NewWorldFirst).has_value());
	return Passed;
}

static bool CheckSerialWrap()
{
	BaselineStagingBudget Budget(0, 1);
	BaselineStaging Receiver(Identity, {}, Budget.TryReserve(0).value());
	constexpr BaselineBegin BeforeWrap{Identity, 10, {0xffffffffu, 0xffffffffu, 0xffffffffu, 0, 0, 0}};
	constexpr BaselineBegin AfterWrap{Identity, 11, {0, 0, 0, 0, 0, 0}};
	bool Passed = Expect("wrap fixture starts below rollover", Receiver.Begin(BeforeWrap).has_value());
	Passed &= Expect("baseline ID, tick and event watermark can advance across wrap", Receiver.Rebase(AfterWrap).has_value());
	const auto Result = Receiver.Finish({Identity, 12, 0});
	Passed &= Expect("zero serials remain valid after wrap", Result && Result->Manifest == AfterWrap.Manifest && Result->Data.empty());
	return Passed;
}

bool Run()
{
	bool Passed = CheckReplacement();
	Passed &= CheckRejectedReplacement();
	Passed &= CheckCompletedAndWorldAdvance();
	Passed &= CheckSerialWrap();
	if (!Passed)
	{
		return false;
	}
	std::puts("Baseline rebase checks passed");
	return true;
}
}
