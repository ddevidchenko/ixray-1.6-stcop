#include "CombatantBarrier.h"
#include "ClientCatchUp.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace CombatantBarrierTests
{
using namespace Multiplayer;
using namespace Multiplayer::Wire;

constexpr JoinIdentity Identity{0x8877665544332211ull, 0x01020304u, 0x090a0b0cu};
constexpr JoinBarrier Barrier{0x11020304u, 0x21020304u, 0x31020304u, 0x41020304u};
constexpr u32 MessageSequence = 0xa1a2a3a4u;
constexpr u8 Canary = 0xb7;

constexpr auto Fragment = []
{
	xr_array<u8, 66> Bytes{};
	Bytes.front() = 0xfe;
	Bytes.back() = 0x81;
	return Bytes;
}();
constexpr CombatantBarrierChunk Chunk{Identity, MessageSequence, Barrier, 66, 1, 0, Fragment};
constexpr u8 GoldenHeader[]{
	0x49, 0x58, 0x4d, 0x50, 0x01, 0x00, 0x0c, 0x00, 0x5a, 0x00,
	0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
	0x04, 0x03, 0x02, 0x01, 0x0c, 0x0b, 0x0a, 0x09, 0xa4, 0xa3, 0xa2, 0xa1,
	0x04, 0x03, 0x02, 0x11, 0x04, 0x03, 0x02, 0x21, 0x04, 0x03, 0x02, 0x31, 0x04, 0x03, 0x02, 0x41,
	0x42, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
};
constexpr auto Golden = []
{
	xr_array<u8, 120> Bytes{};
	std::ranges::copy(GoldenHeader, Bytes.begin());
	std::ranges::copy(Fragment, Bytes.begin() + 54);
	return Bytes;
}();

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
	const auto Decode = DecodeCombatantBarrierChunk(Golden);
	bool Passed = Expect("golden chunk decodes every barrier field and borrows only its data", Decode &&
		Decode->Identity == Identity && Decode->MessageSequence == MessageSequence && Decode->Barrier == Barrier &&
		Decode->ByteCount == 66 && Decode->RecordCount == 1 && Decode->ChunkIndex == 0 && Decode->Data.data() == Golden.data() + 54 &&
		std::ranges::equal(Decode->Data, Fragment));

	xr_array<u8, 122> Output;
	Output.fill(Canary);
	const auto Bytes = xr_span(Output).subspan(1, 120);
	const auto Encode = EncodeCombatantBarrierChunk(Chunk, Bytes);
	Passed &= Expect("encoded barrier chunk matches independent bytes", Encode == 120 && std::ranges::equal(Bytes, Golden));
	Passed &= Expect("successful encoding leaves surrounding bytes intact", Output.front() == Canary && Output.back() == Canary);
	return Passed;
}

template <class Value, class ResultError, class Error>
static bool HasError(const std::expected<Value, ResultError>& Result, Error Code)
{
	return !Result && Result.error() == ResultError{Code};
}

static bool CheckWireRejection()
{
	const auto Datagram = xr_span(Golden);
	bool Passed = true;
	for (std::size_t Bytes = 0; Bytes < Datagram.size(); ++Bytes)
	{
		Passed &= Expect("every shortened datagram fails envelope validation",
			HasError(DecodeCombatantBarrierChunk(Datagram.first(Bytes)), BoundMessageError::InvalidEnvelope));
		if (Bytes >= 30)
		{
			auto Framed = Golden;
			Framed[8] = static_cast<u8>(Bytes - 30);
			const auto Error = Bytes < 54 ? BoundMessageError::TruncatedPayload : BoundMessageError::InvalidPayload;
			Passed &= Expect("every shortened chunk with a corrected length still fails",
				HasError(DecodeCombatantBarrierChunk(xr_span(Framed).first(Bytes)), Error));
		}
	}

	auto Bytes = Golden;
	Bytes[6] = 0x09;
	Passed &= Expect("a barrier declaration is not a fragment",
		HasError(DecodeCombatantBarrierChunk(Bytes), BoundMessageError::WrongMessageType));
	Bytes = Golden;
	std::ranges::fill(xr_span(Bytes).subspan(22, 4), 0);
	Passed &= Expect("the wire chunk requires a bound receiver",
		HasError(DecodeCombatantBarrierChunk(Bytes), BoundMessageError::UnboundJoin));
	Bytes = Golden;
	Bytes[52] = 1;
	Passed &= Expect("the fragment index must match the declared total",
		HasError(DecodeCombatantBarrierChunk(Bytes), BoundMessageError::InvalidPayload));
	Bytes = Golden;
	Bytes[47] = 0x01;
	Passed &= Expect("all bytes of an oversized total are checked",
		HasError(DecodeCombatantBarrierChunk(Bytes), BoundMessageError::InvalidPayload));
	Bytes = Golden;
	Bytes[51] = 1;
	Passed &= Expect("all bytes of the record count are checked",
		HasError(DecodeCombatantBarrierChunk(Bytes), BoundMessageError::InvalidPayload));

	xr_array<u8, 121> Impossible{};
	std::ranges::copy(Golden, Impossible.begin());
	Impossible[8] = 91;
	Impossible[46] = 67;
	Passed &= Expect("declared bytes must match a possible mix of typed record sizes",
		HasError(DecodeCombatantBarrierChunk(Impossible), BoundMessageError::InvalidPayload));
	return Passed;
}

static bool CheckEncodeAndLimits()
{
	xr_array<u8, 1100> Output;
	Output.fill(Canary);
	const auto Before = Output;
	auto Invalid = Chunk;
	Invalid.RecordCount = 0;
	bool Passed = Expect("nonempty data requires an assigned record count",
		HasError(EncodeCombatantBarrierChunk(Invalid, Output), BoundMessageError::InvalidPayload));
	Invalid.RecordCount = 33;
	Passed &= Expect("the public roster count is bounded",
		HasError(EncodeCombatantBarrierChunk(Invalid, Output), BoundMessageError::InvalidPayload));
	Invalid = Chunk;
	Invalid.Identity.ServerSession = 0;
	Passed &= Expect("unbound scope cannot be encoded",
		HasError(EncodeCombatantBarrierChunk(Invalid, Output), BoundMessageError::UnboundJoin));
	Passed &= Expect("short output fails atomically",
		HasError(EncodeCombatantBarrierChunk(Chunk, xr_span(Output).first(119)), BoundMessageError::OutputTooSmall) && Output == Before);

	const auto Empty = CombatantBarrierChunk{{Identity.ServerSession, 0, Identity.ConnectionEpoch}, 0, {}, 0, 0, 0, {}};
	const auto EmptySize = EncodeCombatantBarrierChunk(Empty, Output);
	const auto EmptyDecoded = EmptySize == std::size_t{54} ? DecodeCombatantBarrierChunk(xr_span(Output).first(*EmptySize)) :
		std::unexpected(BoundMessageError::InvalidPayload);
	Passed &= Expect("empty snapshot uses one explicit header and accepts zero counters",
		EmptyDecoded && EmptyDecoded->Identity == Empty.Identity && EmptyDecoded->Barrier == Empty.Barrier && EmptyDecoded->Data.empty());
	Invalid = Empty;
	Invalid.ChunkIndex = 1;
	Passed &= Expect("an empty snapshot has only index zero",
		HasError(EncodeCombatantBarrierChunk(Invalid, Output), BoundMessageError::InvalidPayload));

	xr_array<u8, 1024> Full{};
	const CombatantBarrierChunk Maximum{Identity, MessageSequence, Barrier, 2944, 32, 0, Full};
	const auto MaxFrame = EncodeCombatantBarrierChunk(Maximum, Output);
	Passed &= Expect("a full fragment fits the common envelope", MaxFrame == std::size_t{1078} &&
		DecodeCombatantBarrierChunk(xr_span(Output).first(1078)).has_value() && Output[1078] == Canary);
	Invalid = Maximum;
	Invalid.ByteCount++;
	Passed &= Expect("the complete capture byte cap is enforced",
		HasError(EncodeCombatantBarrierChunk(Invalid, Output), BoundMessageError::InvalidPayload));
	Invalid = Maximum;
	Invalid.ChunkIndex = 2;
	Passed &= Expect("the tail cannot carry a full chunk's byte count",
		HasError(EncodeCombatantBarrierChunk(Invalid, Output), BoundMessageError::InvalidPayload));
	Invalid.Data = xr_span(Full).first(896);
	const auto Tail = EncodeCombatantBarrierChunk(Invalid, Output);
	Passed &= Expect("the exact canonical tail passes",
		Tail == std::size_t{950} && DecodeCombatantBarrierChunk(xr_span(Output).first(950)).has_value());

	std::ranges::copy(Fragment, Output.begin());
	auto Aliased = Chunk;
	Aliased.Data = xr_span(Output).first(66);
	const auto AliasSize = EncodeCombatantBarrierChunk(Aliased, Output);
	Passed &= Expect("fragment data can alias the header being written",
		AliasSize == std::size_t{120} && std::ranges::equal(xr_span(Output).first(120), Golden));
	return Passed;
}

static StalkerCombatantSnapshot MakeStalker(u32 Index)
{
	StalkerCombatantSnapshot State{};
	State.State.Participant = {Index + 1};
	State.State.Body = {static_cast<u16>(Index), Index + 1, 1};
	State.State.BodyProfileId = 42;
	State.State.Position[0] = static_cast<f32>(Index);
	State.AnimationProfileId = 89;
	return State;
}

static bool CheckStaging()
{
	constexpr BaselineBoundary Boundary{Barrier.BaselineId, Barrier.SnapshotTick, Barrier.EventWatermark};
	xr_array<CombatantSnapshot, 32> Bodies{};
	for (u32 Index = 0; Index < Bodies.size(); ++Index)
	{
		Bodies[Index] = MakeStalker(Index);
	}

	xr_array<u8, 2944> Encoded;
	const auto Manifest = EncodeCombatantBaseline(Boundary, Bodies, Encoded);
	if (!Expect("fixture produces the maximum three-fragment public roster",
		Manifest && Manifest->ByteCount == 2944 && Manifest->ChunkCount == 3))
	{
		return false;
	}

	xr_array<u8, 1100> Datagram;
	const auto Size = EncodeServerJoinBarrier({Identity, MessageSequence, Barrier}, Datagram);
	const auto Declaration = Size ? DecodeServerJoinBarrier(xr_span(Datagram).first(*Size)) : std::unexpected(BoundMessageError::InvalidEnvelope);
	ClientCatchUp CatchUp(Identity, {Barrier.BaselineId, Barrier.SnapshotTick - 1, Barrier.EventWatermark - 1});
	if (!Expect("the trusted tracker selects the received declaration",
		Declaration && CatchUp.DeclareBarrier(Declaration->Identity, Declaration->Barrier).has_value()))
	{
		return false;
	}

	xr_array<u8, 2944> Storage;
	BaselineStagingBudget Budget(Storage.size(), 1);
	std::optional<CombatantBarrierSnapshot> Owned;
	bool Passed = true;
	{
		auto Reservation = Budget.TryReserve(Storage.size());
		if (!Expect("barrier transfer uses the shared staging budget", Reservation.has_value()))
		{
			return false;
		}
		CombatantBarrierStaging Staging(Identity, Declaration->Barrier, Storage, std::move(*Reservation));
		Passed &= Expect("finish cannot publish data before any fragment has arrived",
			HasError(Staging.Finish(), BaselineStagingError::InvalidState));

		auto Send = [&](CombatantBarrierChunk Fragment)
		{
			const auto Written = EncodeCombatantBarrierChunk(Fragment, Datagram);
			if (!Written)
			{
				return false;
			}
			const auto Decoded = DecodeCombatantBarrierChunk(xr_span(Datagram).first(*Written));
			const bool Accepted = Decoded && Staging.AddChunk(*Decoded).has_value();
			Datagram.fill(Canary);
			return Accepted;
		};
		const CombatantBarrierChunk Tail{Identity, 9, Barrier, 2944, 32, 2, xr_span(Encoded).subspan(2048)};
		Passed &= Expect("the last fragment can arrive before the prefix and a duplicate is idempotent",
			Send(Tail) && Send(Tail) && HasError(Staging.Finish(), BaselineStagingError::Incomplete));

		constexpr JoinIdentity Foreign[]{
			{Identity.ServerSession + 1, Identity.WorldEpoch, Identity.ConnectionEpoch},
			{Identity.ServerSession, Identity.WorldEpoch + 1, Identity.ConnectionEpoch},
			{Identity.ServerSession, Identity.WorldEpoch, Identity.ConnectionEpoch + 1},
		};
		auto Different = Tail;
		for (const auto Other : Foreign)
		{
			Different.Identity = Other;
			Passed &= Expect("foreign session, round or connection cannot affect the current transfer",
				HasError(Staging.AddChunk(Different), CombatantBarrierScopeError::IdentityMismatch));
		}
		Different = Tail;
		Different.Barrier.BaselineId++;
		Passed &= Expect("a reused barrier ID in a different baseline cannot affect the current transfer",
			HasError(Staging.AddChunk(Different), CombatantBarrierScopeError::BarrierMismatch));
		Different = Tail;
		Different.Barrier.BarrierId++;
		Passed &= Expect("a different barrier ID cannot affect the current transfer",
			HasError(Staging.AddChunk(Different), CombatantBarrierScopeError::BarrierMismatch));
		Different = Tail;
		Different.Barrier.SnapshotTick++;
		Passed &= Expect("changing the declared snapshot tick rejects the chunk",
			HasError(Staging.AddChunk(Different), CombatantBarrierScopeError::BarrierMismatch));
		Different = Tail;
		Different.Barrier.EventWatermark++;
		Passed &= Expect("changing the declared watermark rejects the chunk",
			HasError(Staging.AddChunk(Different), CombatantBarrierScopeError::BarrierMismatch));

		xr_array<u8, 896> Conflicting;
		std::ranges::copy(Tail.Data, Conflicting.begin());
		Conflicting[0] ^= 1;
		Different = Tail;
		Different.Data = Conflicting;
		Passed &= Expect("a changed duplicate cannot replace already staged bytes",
			HasError(Staging.AddChunk(Different), BaselineStagingError::ConflictingChunk));

		const CombatantBarrierChunk First{Identity, 7, Barrier, 2944, 32, 0, xr_span(Encoded).first(1024)};
		Different = First;
		Different.ByteCount -= 26;
		Passed &= Expect("all fragments must agree on the manifest",
			HasError(Staging.AddChunk(Different), BaselineStagingError::ConflictingManifest));
		Passed &= Expect("a short manually constructed fragment is rejected",
			HasError(Staging.AddChunk({Identity, 7, Barrier, 2944, 32, 0, xr_span(Encoded).first(1023)}),
				BaselineStagingError::InvalidChunk));
		Passed &= Expect("missing fragments can arrive without replacing the accepted tail",
			Send({Identity, 8, Barrier, 2944, 32, 1, xr_span(Encoded).subspan(1024, 1024)}) && Send(First));
		auto Snapshot = Staging.Finish();
		if (!Expect("all canonical fragments yield one typed roster", Snapshot.has_value()))
		{
			return false;
		}
		Owned = std::move(*Snapshot);
		Passed &= Expect("a finished transfer yields no second publication",
			HasError(Staging.Finish(), BaselineStagingError::InvalidState));
	}
	Storage.fill(0);
	Passed &= Expect("the exact checked roster and barrier outlive staging and receive buffers",
		Owned && Owned->GetIdentity() == Identity && Owned->GetBarrier() == Barrier &&
		std::ranges::equal(Owned->GetRecords(), Bodies) && !CatchUp.GetAppliedBarrier() && Budget.TryReserve(Storage.size()).has_value());
	return Passed;
}

static bool CheckEmptyAndInvalidRecords()
{
	BaselineStagingBudget Budget(67, 1);
	bool Passed = true;
	{
		CombatantBarrierStaging Staging(Identity, Barrier, {}, Budget.TryReserve(0).value());
		const auto Empty = CombatantBarrierChunk{Identity, 1, Barrier, 0, 0, 0, {}};
		Passed &= Expect("even empty state requires explicit transfer data before completion",
			HasError(Staging.Finish(), BaselineStagingError::InvalidState) && Staging.AddChunk(Empty).has_value());
		const auto Finished = Staging.Finish();
		Passed &= Expect("the empty typed roster retains its exact scope", Finished && Finished->GetBarrier() == Barrier &&
			Finished->GetIdentity() == Identity && Finished->GetRecords().empty());
	}
	{
		xr_array<u8, 65> Short;
		CombatantBarrierStaging Staging(Identity, Barrier, Short, Budget.TryReserve(Short.size()).value());
		Passed &= Expect("insufficient storage fails before opening the transfer",
			HasError(Staging.AddChunk(Chunk), BaselineStagingError::InsufficientStorage) &&
			HasError(Staging.Finish(), BaselineStagingError::InvalidState));
	}
	{
		xr_array<u8, 67> Storage;
		CombatantBarrierStaging Staging(Identity, Barrier, Storage, Budget.TryReserve(Storage.size()).value());
		const xr_array<u8, 67> Impossible{};
		const CombatantBarrierChunk InvalidTotal{Identity, 1, Barrier, 67, 1, 0, Impossible};
		Passed &= Expect("an impossible first fragment cannot reserve conflicting transfer metadata",
			HasError(Staging.AddChunk(InvalidTotal), BaselineStagingError::InvalidChunk) &&
			HasError(Staging.Finish(), BaselineStagingError::InvalidState));
		Passed &= Expect("a complete frame with unsupported record data cannot publish a snapshot",
			Staging.AddChunk(Chunk).has_value() && HasError(Staging.Finish(), BaselineRecordsError::InvalidRecord));
		Passed &= Expect("a failed typed decode cannot later publish the same corrupt transfer",
			HasError(Staging.Finish(), BaselineStagingError::InvalidState));
	}
	return Passed;
}

bool Run()
{
	bool Passed = CheckGolden();
	Passed &= CheckWireRejection();
	Passed &= CheckEncodeAndLimits();
	Passed &= CheckStaging();
	Passed &= CheckEmptyAndInvalidRecords();
	if (!Passed)
	{
		return false;
	}
	std::puts("Combatant barrier snapshot checks passed");
	return true;
}
}
