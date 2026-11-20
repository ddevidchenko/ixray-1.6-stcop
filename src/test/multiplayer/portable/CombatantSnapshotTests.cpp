#include "CombatantSnapshot.h"

#include <algorithm>
#include <bit>
#include <cstdio>

namespace CombatantSnapshotTests
{
using namespace Multiplayer;
using namespace Multiplayer::Wire;

constexpr u8 GoldenHuman[] = {
	0x04, 0x03, 0x02, 0x01, 0x34, 0x12, 0x44, 0x33, 0x22, 0x11, 0x88, 0x77, 0x66, 0x55, 0xcc, 0xbb, 0xaa, 0x99,
	0x00, 0x00, 0x80, 0x3f, 0x00, 0x00, 0x20, 0xc0, 0x00, 0x00, 0x00, 0x80,
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x40, 0x00, 0x00, 0x40, 0xbf,
	0x23, 0x81, 0xfe, 0xff, 0x00, 0xc0, 0x01, 0x05, 0x0d, 0x0c, 0x0b, 0x0a,
	0x02, 0xa4, 0xa3, 0xa2, 0xa1, 0x00, 0x00, 0x00, 0x00,
};
constexpr u8 GoldenStalkerSuffix[] = {
	0x24, 0x23, 0x22, 0x21, 0x43, 0x32, 0x65, 0x54, 0x04, 0x03, 0x02, 0x01,
	0x87, 0x76, 0xa9, 0x98, 0x08, 0x07, 0x06, 0x05, 0x01, 0x02, 0x0c, 0x0b, 0x0a, 0x09,
};
constexpr auto GoldenStalker = []
{
	xr_array<u8, sizeof(GoldenHuman) + sizeof(GoldenStalkerSuffix)> Bytes{};
	std::ranges::copy(GoldenHuman, Bytes.begin());
	std::ranges::copy(GoldenStalkerSuffix, Bytes.begin() + sizeof(GoldenHuman));
	Bytes[0] = 0x06;
	Bytes[4] = 0x48;
	return Bytes;
}();

constexpr HumanCombatantSnapshot Human{{
	{0x01020304}, {0x1234, 0x11223344, 0x55667788}, 0x99aabbcc, {1.0f, -2.5f, -0.0f},
	{std::bit_cast<f32>(u32{1}), 4.5f, -0.75f}, 0x8123, 0xfffe, -16384, SnapshotStance::Crouched,
	SnapshotAirborne | SnapshotSpawnProtected, 0x0a0b0c0d, SnapshotWeaponAction::Reloading, 0xa1a2a3a4, 0,
}};
constexpr StalkerCombatantSnapshot Stalker = []
{
	StalkerCombatantSnapshot Result{Human.State, 0x21222324, {0x3243, 0x5465, 0x01020304},
		{0x7687, 0x98a9, 0x05060708}, SnapshotRootMotion::AnimationDriven, SnapshotTraversal::ClimbingDown, 0x090a0b0c};
	Result.State.Participant.Value = 0x01020306;
	Result.State.Body.ObjectId = 0x1248;
	return Result;
}();

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

template <class Value, std::size_t Size>
static bool CheckGolden(const Value& Expected, SnapshotRecordKind Kind, xr_span<const u8, Size> Bytes)
{
	const auto Decoded = DecodeCombatantSnapshot({Kind, Bytes});
	const auto* Typed = Decoded ? std::get_if<Value>(&*Decoded) : nullptr;
	bool Passed = Expect("independent bytes decode into the right combatant type and every field", Typed && *Typed == Expected);
	Passed &= Expect("finite float decoding preserves signed zero and subnormal bits", Typed &&
		std::bit_cast<u32>(Typed->State.Position[2]) == 0x80000000u && std::bit_cast<u32>(Typed->State.Velocity[0]) == 1);
	constexpr u8 Canary = 0x7b;
	xr_array<u8, Size + 2> Output;
	Output.fill(Canary);
	const auto Encoded = EncodeCombatantSnapshot(Expected, xr_span(Output).subspan(1));
	Passed &= Expect("typed record encoding matches independent bytes and borrows caller output", Encoded && Encoded->Kind == Kind &&
		Encoded->Payload.data() == Output.data() + 1 && std::ranges::equal(Encoded->Payload, Bytes));
	Passed &= Expect("encoding keeps surrounding bytes", Output.front() == Canary && Output.back() == Canary);
	return Passed;
}

template <class Value, class Error>
static bool ExpectError(const char* Step, const std::expected<Value, Error>& Result, Error Expected)
{
	return Expect(Step, !Result && Result.error() == Expected);
}

static bool CheckShape(xr_span<const u8> Bytes, SnapshotRecordKind Kind, SnapshotRecordKind Other)
{
	bool Passed = ExpectError("unknown record kind is rejected",
		DecodeCombatantSnapshot({static_cast<SnapshotRecordKind>(0xff), Bytes}), CombatantRecordError::UnknownKind);
	Passed &= ExpectError("one combatant kind cannot use the other kind's body layout",
		DecodeCombatantSnapshot({Other, Bytes}), CombatantRecordError::InvalidSize);
	for (std::size_t Size = 0; Size < Bytes.size(); ++Size)
	{
		Passed &= ExpectError("every shortened typed record is rejected",
			DecodeCombatantSnapshot({Kind, Bytes.first(Size)}), CombatantRecordError::InvalidSize);
	}
	xr_array<u8, 90> Trailing{};
	std::ranges::copy(Bytes, Trailing.begin());
	Passed &= ExpectError("an extra byte is rejected",
		DecodeCombatantSnapshot({Kind, xr_span(Trailing).first(Bytes.size() + 1)}), CombatantRecordError::InvalidSize);
	return Passed;
}

struct FieldMutation
{
	std::size_t Offset;
	std::size_t Width;
	u32 Bits;
	const char* Reason;
};

static bool CheckFields(xr_span<const u8> Golden, SnapshotRecordKind Kind, xr_span<const FieldMutation> Mutations)
{
	xr_array<u8, 89> Storage;
	const auto Bytes = xr_span(Storage).first(Golden.size());
	bool Passed = true;
	for (const auto& Mutation : Mutations)
	{
		std::ranges::copy(Golden, Bytes.begin());
		for (std::size_t Byte = 0; Byte < Mutation.Width; ++Byte)
		{
			Bytes[Mutation.Offset + Byte] = static_cast<u8>(Mutation.Bits >> (Byte * 8));
		}
		Passed &= ExpectError(Mutation.Reason, DecodeCombatantSnapshot({Kind, Bytes}), CombatantRecordError::InvalidFields);
	}
	return Passed;
}

static bool CheckInvalidFields()
{
	constexpr FieldMutation Common[] = {
		{0, 4, 0, "participant must be assigned"},
		{4, 2, 0xffff, "reserved engine slot is rejected"},
		{6, 4, 0, "generation must be assigned"},
		{10, 4, 0, "life must be assigned"},
		{14, 4, 0, "body profile must be assigned"},
		{18, 4, 0x7f800000, "position X cannot be infinite"},
		{22, 4, 0xff800000, "position Y cannot be negatively infinite"},
		{26, 4, 0x7fc00001, "position Z cannot be NaN"},
		{30, 4, 0x7f800001, "velocity X cannot be NaN"},
		{34, 4, 0xff800000, "velocity Y cannot be negatively infinite"},
		{38, 4, 0x7f800000, "velocity Z cannot be infinite"},
		{46, 2, 0x4001, "pitch above positive 90 degrees is rejected"},
		{46, 2, 0xbfff, "pitch below negative 90 degrees is rejected"},
		{48, 1, 0x02, "unknown stance is rejected"},
		{49, 1, 0x85, "reserved public-state flags are rejected"},
		{50, 4, 0, "non-idle weapon action needs an equipped profile"},
		{54, 1, 0x05, "unknown weapon action is rejected"},
	};
	constexpr FieldMutation StalkerOnly[] = {
		{63, 4, 0, "animation profile must be assigned"},
		{67, 2, 0, "absent locomotion cannot carry a phase or tick"},
		{75, 2, 0, "absent action cannot carry a phase or tick"},
		{83, 1, 2, "unknown root-motion mode is rejected"},
		{84, 1, 3, "unknown traversal is rejected"},
		{84, 1, 0, "a nonclimbing body cannot carry a ladder link"},
		{85, 4, 0, "climbing requires an assigned link"},
	};
	bool Passed = CheckFields(GoldenHuman, SnapshotRecordKind::HumanCombatant, Common);
	Passed &= CheckFields(GoldenStalker, SnapshotRecordKind::StalkerCombatant, Common);
	Passed &= CheckFields(GoldenStalker, SnapshotRecordKind::StalkerCombatant, StalkerOnly);
	auto NoChannels = GoldenStalker;
	std::ranges::fill(xr_span(NoChannels).subspan(67, 16), u8{0});
	Passed &= ExpectError("animation-driven root motion with no active channels is rejected on decode",
		DecodeCombatantSnapshot({SnapshotRecordKind::StalkerCombatant, NoChannels}), CombatantRecordError::InvalidFields);
	return Passed;
}

static bool CheckBoundariesAndAtomicWrites()
{
	xr_array<u8, 91> Output;
	Output.fill(0xb7);
	const auto Before = Output;
	bool Passed = ExpectError("undersized human output is rejected", EncodeCombatantSnapshot(Human, xr_span(Output).first(62)),
		CombatantRecordError::OutputTooSmall);
	Passed &= ExpectError("undersized stalker output is rejected", EncodeCombatantSnapshot(Stalker, xr_span(Output).first(88)),
		CombatantRecordError::OutputTooSmall);
	auto BadHuman = Human;
	BadHuman.State.Velocity[2] = std::bit_cast<f32>(0x7f800000u);
	Passed &= ExpectError("nonfinite local state fails encoding", EncodeCombatantSnapshot(BadHuman, Output), CombatantRecordError::InvalidFields);
	BadHuman = Human;
	BadHuman.State.AimPitch = 16385;
	Passed &= ExpectError("invalid local angles fail encoding", EncodeCombatantSnapshot(BadHuman, Output), CombatantRecordError::InvalidFields);
	auto BadStalker = Stalker;
	BadStalker.TraversalLinkId = 0;
	Passed &= ExpectError("local climbing state without a link fails encoding", EncodeCombatantSnapshot(BadStalker, Output),
		CombatantRecordError::InvalidFields);
	BadStalker = Stalker;
	BadStalker.Locomotion = {};
	BadStalker.Action = {};
	Passed &= ExpectError("animation-driven root motion needs an active channel", EncodeCombatantSnapshot(BadStalker, Output),
		CombatantRecordError::InvalidFields);
	Passed &= Expect("invalid or short output leaves all bytes unchanged", Output == Before);

	auto Boundary = Stalker;
	Boundary.State.Body = {0xfffe, 0xffffffffu, 0xffffffffu};
	Boundary.State.AimPitch = 16384;
	Boundary.State.WeaponProfileId = 0;
	Boundary.State.WeaponAction = SnapshotWeaponAction::Idle;
	Boundary.State.Flags = SnapshotSprinting;
	Boundary.State.SpawnProtectionEndTick = 0xffffffffu;
	Boundary.Locomotion = {};
	Boundary.Action = {0xffff, 0xffff, 0xffffffffu};
	Boundary.Traversal = SnapshotTraversal::None;
	Boundary.TraversalLinkId = 0;

	auto Encoded = EncodeCombatantSnapshot(Boundary, Output);
	auto Decoded = Encoded ? DecodeCombatantSnapshot(*Encoded) : std::unexpected(CombatantRecordError::InvalidFields);
	Passed &= Expect("maximum valid values and a global action without locomotion are valid", Decoded &&
		std::get_if<StalkerCombatantSnapshot>(&*Decoded) && std::get<StalkerCombatantSnapshot>(*Decoded) == Boundary);

	Boundary.RootMotion = SnapshotRootMotion::Simulation;
	Boundary.Action = {};
	Encoded = EncodeCombatantSnapshot(Boundary, Output);
	Decoded = Encoded ? DecodeCombatantSnapshot(*Encoded) : std::unexpected(CombatantRecordError::InvalidFields);
	Passed &= Expect("simulation-driven state can use the profile default pose", Decoded &&
		std::get_if<StalkerCombatantSnapshot>(&*Decoded) && std::get<StalkerCombatantSnapshot>(*Decoded) == Boundary);
	return Passed;
}

static bool CheckMixedBatch()
{
	constexpr SnapshotBatchHeader BatchHeader{{19, 8, 3}, 5, 100, 103, 233, 7};
	SnapshotRecordView Records[] = {
		{SnapshotRecordKind::HumanCombatant, GoldenHuman},
		{SnapshotRecordKind::StalkerCombatant, GoldenStalker},
	};
	xr_array<u8, 211> Datagram{};
	const auto Written = EncodeSnapshotBatch(BatchHeader, Records, Datagram);
	if (!Expect("the mixed fixture can be framed", Written && *Written == 210))
	{
		return false;
	}
	auto Bytes = xr_span(Datagram).first(*Written);
	const auto Batch = CombatantSnapshotBatch::Decode(Bytes);
	auto BadStalker = GoldenStalker;
	BadStalker[84] = 0xff;
	Records[1].Payload = BadStalker;
	const auto Replaced = EncodeSnapshotBatch(BatchHeader, Records, Datagram);
	bool Passed = Expect("a correctly framed datagram may still have invalid body fields", Replaced && SnapshotBatch::Decode(Bytes).has_value());
	Passed &= ExpectError("the typed decoder preserves envelope errors", CombatantSnapshotBatch::Decode(Bytes.first(209)),
		BoundMessageError::InvalidEnvelope);
	Passed &= ExpectError("a bad later body invalidates the whole typed batch", CombatantSnapshotBatch::Decode(Bytes),
		BoundMessageError::InvalidPayload);
	Passed &= Expect("decoded typed values and header survive reuse of receive storage", Batch && Batch->GetHeader() == BatchHeader &&
		Batch->GetRecords().size() == 2 && Batch->GetRecords()[0] == CombatantSnapshot{Human} &&
		Batch->GetRecords()[1] == CombatantSnapshot{Stalker});

	BadStalker = GoldenStalker;
	BadStalker[0] = GoldenHuman[0];
	Passed &= Expect("a repeated participant can be framed", EncodeSnapshotBatch(BatchHeader, Records, Datagram).has_value());
	Passed &= ExpectError("one tick cannot carry two bodies for the same participant in a batch", CombatantSnapshotBatch::Decode(Bytes),
		BoundMessageError::InvalidPayload);
	BadStalker = GoldenStalker;
	BadStalker[4] = GoldenHuman[4];
	BadStalker[6] = 0x45;
	Passed &= Expect("a repeated object slot with a distinct generation can be framed", EncodeSnapshotBatch(BatchHeader, Records, Datagram).has_value());
	Passed &= ExpectError("one tick cannot carry two bodies for the same slot in a batch", CombatantSnapshotBatch::Decode(Bytes),
		BoundMessageError::InvalidPayload);
	return Passed;
}

bool Run()
{
	bool Passed = CheckGolden(Human, SnapshotRecordKind::HumanCombatant, xr_span(GoldenHuman));
	Passed &= CheckGolden(Stalker, SnapshotRecordKind::StalkerCombatant, xr_span(GoldenStalker));
	Passed &= CheckShape(GoldenHuman, SnapshotRecordKind::HumanCombatant, SnapshotRecordKind::StalkerCombatant);
	Passed &= CheckShape(GoldenStalker, SnapshotRecordKind::StalkerCombatant, SnapshotRecordKind::HumanCombatant);
	Passed &= CheckInvalidFields();
	Passed &= CheckBoundariesAndAtomicWrites();
	Passed &= CheckMixedBatch();
	if (!Passed)
	{
		return false;
	}
	std::puts("Combatant snapshot codec checks passed");
	return true;
}
}
