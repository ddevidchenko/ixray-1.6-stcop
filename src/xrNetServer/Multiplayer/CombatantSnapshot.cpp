#include "CombatantSnapshot.h"

#include <algorithm>

namespace Multiplayer::Wire
{
constexpr s16 MaxAimPitch = 16384;
constexpr u8 SupportedFlags = SnapshotAirborne | SnapshotSprinting | SnapshotSpawnProtected;

static bool IsValidPublicState(const PublicCombatantState& State) noexcept
{
	return State.Participant.IsAssigned() && State.Body.IsAssigned() && State.BodyProfileId != 0 &&
		State.AimPitch >= -MaxAimPitch && State.AimPitch <= MaxAimPitch && State.Stance <= SnapshotStance::Crouched &&
		(State.Flags & ~SupportedFlags) == 0 && State.WeaponAction <= SnapshotWeaponAction::Holstering &&
		(State.WeaponProfileId != 0 || State.WeaponAction == SnapshotWeaponAction::Idle);
}

static bool IsValidChannel(const SnapshotAnimationChannel& Channel) noexcept
{
	return Channel.SemanticId != 0 || (Channel.Phase == 0 && Channel.StartTick == 0);
}

static bool IsValidStalkerExtension(const StalkerCombatantSnapshot& Snapshot) noexcept
{
	const bool HasChannel = Snapshot.Locomotion.SemanticId != 0 || Snapshot.Action.SemanticId != 0;
	const bool IsClimbing = Snapshot.Traversal != SnapshotTraversal::None;
	return Snapshot.AnimationProfileId != 0 && IsValidChannel(Snapshot.Locomotion) && IsValidChannel(Snapshot.Action) &&
		Snapshot.RootMotion <= SnapshotRootMotion::AnimationDriven && (HasChannel || Snapshot.RootMotion == SnapshotRootMotion::Simulation) &&
		Snapshot.Traversal <= SnapshotTraversal::ClimbingDown && IsClimbing == (Snapshot.TraversalLinkId != 0);
}

// The record decoder checks the exact body size before reading fixed-width fields.
static std::optional<PublicCombatantState> ReadPublicState(PayloadReader& Reader) noexcept
{
	PublicCombatantState State{};
	State.Participant = {*Reader.ReadInteger<u32>()};
	State.Body = {*Reader.ReadInteger<u16>(), *Reader.ReadInteger<u32>(), *Reader.ReadInteger<u32>()};
	State.BodyProfileId = *Reader.ReadInteger<u32>();
	for (auto* Vector : {&State.Position, &State.Velocity})
	{
		for (auto& Component : *Vector)
		{
			const auto Value = Reader.ReadFiniteFloat();
			if (!Value)
			{
				return std::nullopt;
			}
			Component = *Value;
		}
	}

	State.BodyYaw = *Reader.ReadInteger<u16>();
	State.AimYaw = *Reader.ReadInteger<u16>();
	State.AimPitch = *Reader.ReadInteger<s16>();
	State.Stance = static_cast<SnapshotStance>(*Reader.ReadInteger<u8>());
	State.Flags = *Reader.ReadInteger<u8>();
	State.WeaponProfileId = *Reader.ReadInteger<u32>();
	State.WeaponAction = static_cast<SnapshotWeaponAction>(*Reader.ReadInteger<u8>());
	State.WeaponActionStartTick = *Reader.ReadInteger<u32>();
	State.SpawnProtectionEndTick = *Reader.ReadInteger<u32>();
	if (!IsValidPublicState(State))
	{
		return std::nullopt;
	}
	return State;
}

static bool WritePublicState(PayloadWriter& Writer, const PublicCombatantState& State) noexcept
{
	if (!IsValidPublicState(State) || !Writer.WriteInteger(State.Participant.Value) || !Writer.WriteInteger(State.Body.ObjectId) ||
		!Writer.WriteInteger(State.Body.Generation) || !Writer.WriteInteger(State.Body.LifeId) || !Writer.WriteInteger(State.BodyProfileId))
	{
		return false;
	}
	for (const auto* Vector : {&State.Position, &State.Velocity})
	{
		for (const auto Component : *Vector)
		{
			if (!Writer.WriteFiniteFloat(Component))
			{
				return false;
			}
		}
	}

	return Writer.WriteInteger(State.BodyYaw) && Writer.WriteInteger(State.AimYaw) && Writer.WriteInteger(State.AimPitch) &&
		Writer.WriteInteger(static_cast<u8>(State.Stance)) && Writer.WriteInteger(State.Flags) && Writer.WriteInteger(State.WeaponProfileId) &&
		Writer.WriteInteger(static_cast<u8>(State.WeaponAction)) && Writer.WriteInteger(State.WeaponActionStartTick) &&
		Writer.WriteInteger(State.SpawnProtectionEndTick);
}

static SnapshotAnimationChannel ReadChannel(PayloadReader& Reader) noexcept
{
	return {*Reader.ReadInteger<u16>(), *Reader.ReadInteger<u16>(), *Reader.ReadInteger<u32>()};
}

static bool WriteChannel(PayloadWriter& Writer, const SnapshotAnimationChannel& Channel) noexcept
{
	return Writer.WriteInteger(Channel.SemanticId) && Writer.WriteInteger(Channel.Phase) && Writer.WriteInteger(Channel.StartTick);
}

std::expected<CombatantSnapshot, CombatantRecordError> DecodeCombatantSnapshot(SnapshotRecordView Record) noexcept
{
	std::size_t ExpectedSize;
	switch (Record.Kind)
	{
	case SnapshotRecordKind::HumanCombatant:
		ExpectedSize = HumanCombatantRecordSize;
		break;
	case SnapshotRecordKind::StalkerCombatant:
		ExpectedSize = StalkerCombatantRecordSize;
		break;
	default:
		return std::unexpected(CombatantRecordError::UnknownKind);
	}

	if (Record.Payload.size() != ExpectedSize)
	{
		return std::unexpected(CombatantRecordError::InvalidSize);
	}
	PayloadReader Reader(Record.Payload);
	const auto State = ReadPublicState(Reader);
	if (!State)
	{
		return std::unexpected(CombatantRecordError::InvalidFields);
	}
	if (Record.Kind == SnapshotRecordKind::HumanCombatant)
	{
		return HumanCombatantSnapshot{*State};
	}

	const StalkerCombatantSnapshot Snapshot{
		*State,
		*Reader.ReadInteger<u32>(),
		ReadChannel(Reader),
		ReadChannel(Reader),
		static_cast<SnapshotRootMotion>(*Reader.ReadInteger<u8>()),
		static_cast<SnapshotTraversal>(*Reader.ReadInteger<u8>()),
		*Reader.ReadInteger<u32>(),
	};
	if (!IsValidStalkerExtension(Snapshot))
	{
		return std::unexpected(CombatantRecordError::InvalidFields);
	}
	return Snapshot;
}

static std::expected<SnapshotRecordView, CombatantRecordError> PublishRecord(SnapshotRecordKind Kind, xr_span<const u8> Bytes,
	xr_span<u8> Output) noexcept
{
	PayloadWriter Writer(Output);
	if (!Writer.WriteBytes(Bytes))
	{
		return std::unexpected(CombatantRecordError::OutputTooSmall);
	}
	return SnapshotRecordView{Kind, Writer.Written()};
}

std::expected<SnapshotRecordView, CombatantRecordError> EncodeCombatantSnapshot(const HumanCombatantSnapshot& Snapshot,
	xr_span<u8> Output) noexcept
{
	xr_array<u8, HumanCombatantRecordSize> Bytes;
	PayloadWriter Writer(Bytes);
	if (!WritePublicState(Writer, Snapshot.State))
	{
		return std::unexpected(CombatantRecordError::InvalidFields);
	}
	return PublishRecord(SnapshotRecordKind::HumanCombatant, Writer.Written(), Output);
}

std::expected<SnapshotRecordView, CombatantRecordError> EncodeCombatantSnapshot(const StalkerCombatantSnapshot& Snapshot,
	xr_span<u8> Output) noexcept
{
	xr_array<u8, StalkerCombatantRecordSize> Bytes;
	PayloadWriter Writer(Bytes);
	if (!IsValidStalkerExtension(Snapshot) || !WritePublicState(Writer, Snapshot.State) || !Writer.WriteInteger(Snapshot.AnimationProfileId) ||
		!WriteChannel(Writer, Snapshot.Locomotion) || !WriteChannel(Writer, Snapshot.Action) ||
		!Writer.WriteInteger(static_cast<u8>(Snapshot.RootMotion)) || !Writer.WriteInteger(static_cast<u8>(Snapshot.Traversal)) ||
		!Writer.WriteInteger(Snapshot.TraversalLinkId))
	{
		return std::unexpected(CombatantRecordError::InvalidFields);
	}
	return PublishRecord(SnapshotRecordKind::StalkerCombatant, Writer.Written(), Output);
}

static const PublicCombatantState& GetPublicState(const CombatantSnapshot& Snapshot) noexcept
{
	return std::visit([](const auto& Typed) noexcept -> const PublicCombatantState& { return Typed.State; }, Snapshot);
}

std::expected<CombatantSnapshotBatch, BoundMessageError> CombatantSnapshotBatch::Decode(xr_span<const u8> Bytes) noexcept
{
	auto Batch = SnapshotBatch::Decode(Bytes);
	if (!Batch)
	{
		return std::unexpected(Batch.error());
	}

	CombatantSnapshotBatch Result(Batch->GetHeader());
	for (u16 Index = 0; Index < Batch->GetRecordCount(); ++Index)
	{
		const auto View = Batch->Next();
		if (!View)
		{
			return std::unexpected(BoundMessageError::TruncatedPayload);
		}
		const auto Record = DecodeCombatantSnapshot(*View);
		if (!Record)
		{
			return std::unexpected(BoundMessageError::InvalidPayload);
		}
		const auto& State = GetPublicState(*Record);
		if (std::ranges::any_of(Result.GetRecords(), [&State](const PublicCombatantState& Previous)
		{
			return Previous.Participant == State.Participant || Previous.Body.ObjectId == State.Body.ObjectId;
		}, GetPublicState))
		{
			return std::unexpected(BoundMessageError::InvalidPayload);
		}
		Result.Records[Result.Count++] = *Record;
	}
	return Result;
}
}
