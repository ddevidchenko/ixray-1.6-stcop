#pragma once

#include "CombatantIdentity.h"
#include "SnapshotBatch.h"

#include <variant>

namespace Multiplayer::Wire
{
enum class SnapshotStance : u8
{
	Standing = 0,
	Crouched = 1,
};

inline constexpr u8 SnapshotAirborne = 0x01;
inline constexpr u8 SnapshotSprinting = 0x02;
inline constexpr u8 SnapshotSpawnProtected = 0x04;

enum class SnapshotWeaponAction : u8
{
	Idle = 0,
	Firing = 1,
	Reloading = 2,
	Equipping = 3,
	Holstering = 4,
};

struct PublicCombatantState
{
	ParticipantId Participant;
	EntityRef Body;
	u32 BodyProfileId;
	xr_array<f32, 3> Position;
	xr_array<f32, 3> Velocity;
	u16 BodyYaw;
	u16 AimYaw;
	s16 AimPitch;
	SnapshotStance Stance;
	u8 Flags;
	u32 WeaponProfileId;
	SnapshotWeaponAction WeaponAction;
	u32 WeaponActionStartTick;
	u32 SpawnProtectionEndTick;

	bool operator==(const PublicCombatantState&) const = default;
};

struct HumanCombatantSnapshot
{
	PublicCombatantState State;

	bool operator==(const HumanCombatantSnapshot&) const = default;
};

struct SnapshotAnimationChannel
{
	u16 SemanticId;
	u16 Phase;
	u32 StartTick;

	bool operator==(const SnapshotAnimationChannel&) const = default;
};

enum class SnapshotRootMotion : u8
{
	Simulation = 0,
	AnimationDriven = 1,
};

enum class SnapshotTraversal : u8
{
	None = 0,
	ClimbingUp = 1,
	ClimbingDown = 2,
};

struct StalkerCombatantSnapshot
{
	PublicCombatantState State;
	u32 AnimationProfileId;
	SnapshotAnimationChannel Locomotion;
	SnapshotAnimationChannel Action;
	SnapshotRootMotion RootMotion;
	SnapshotTraversal Traversal;
	u32 TraversalLinkId;

	bool operator==(const StalkerCombatantSnapshot&) const = default;
};

using CombatantSnapshot = std::variant<HumanCombatantSnapshot, StalkerCombatantSnapshot>;

[[nodiscard]] inline const PublicCombatantState& GetPublicCombatantState(const CombatantSnapshot& Snapshot) noexcept
{
	return std::visit([](const auto& Typed) noexcept -> const PublicCombatantState& { return Typed.State; }, Snapshot);
}

enum class CombatantRecordError
{
	UnknownKind,
	InvalidSize,
	InvalidFields,
	OutputTooSmall,
};

inline constexpr std::size_t HumanCombatantRecordSize = 63;
inline constexpr std::size_t StalkerCombatantRecordSize = 89;

[[nodiscard]] std::expected<CombatantSnapshot, CombatantRecordError> DecodeCombatantSnapshot(SnapshotRecordView Record) noexcept;

// Success returns a view borrowing the written portion of Output. Failure leaves Output unchanged.
[[nodiscard]] std::expected<SnapshotRecordView, CombatantRecordError> EncodeCombatantSnapshot(const HumanCombatantSnapshot& Snapshot,
	xr_span<u8> Output) noexcept;
[[nodiscard]] std::expected<SnapshotRecordView, CombatantRecordError> EncodeCombatantSnapshot(const StalkerCombatantSnapshot& Snapshot,
	xr_span<u8> Output) noexcept;

class CombatantSnapshotBatch
{
public:
	// Decodes and owns every public combatant in the datagram before exposing the batch.
	[[nodiscard]] static std::expected<CombatantSnapshotBatch, BoundMessageError> Decode(xr_span<const u8> Bytes) noexcept;

	[[nodiscard]] const SnapshotBatchHeader& GetHeader() const noexcept { return Header; }
	[[nodiscard]] xr_span<const CombatantSnapshot> GetRecords() const noexcept { return xr_span(Records).first(Count); }

private:
	explicit CombatantSnapshotBatch(SnapshotBatchHeader Header) noexcept : Header(Header) {}

	SnapshotBatchHeader Header;
	xr_array<CombatantSnapshot, MaxSnapshotRecords> Records{};
	std::size_t Count = 0;
};
}
