#pragma once

#include "../../xrNetServer/Multiplayer/CombatantIdentity.h"
#include "../../xrNetServer/Multiplayer/JoinTypes.h"
#include "../../xrNetServer/Multiplayer/SerialSequence.h"
#include "../../xrCore/_stl_extensions_nonalloc.h"

#include <algorithm>
#include <expected>
#include <optional>
#include <type_traits>

namespace Multiplayer
{
enum class ReplicaBodyState
{
	Invalid,
	Unknown,
	Stale,
	Conflict,
	Current,
};

struct ReplicaSnapshot
{
	JoinIdentity Identity;
	ParticipantId Participant;
	EntityRef Body;
	u32 ServerTick;
	u32 RequiredEventWatermark;
};

enum class ReplicaSnapshotError
{
	IdentityMismatch,
	InvalidReference,
	UnknownReference,
	StaleReference,
	ConflictingReference,
	StaleTick,
	EventsPending,
	AmbiguousWatermark,
	ApplicationFailed,
};

using RetiredCombatantGenerations = xr_array<u32, InvalidObjectId>;

// The owner serializes operations and keeps the retirement storage exclusive for this registry's lifetime.
class CombatantReplicas
{
public:
	CombatantReplicas(WorldIdentity Identity, xr_span<u32, InvalidObjectId> RetiredGenerations) noexcept
		: Identity(Identity), RetiredGenerations(RetiredGenerations)
	{
		std::ranges::fill(RetiredGenerations, 0u);
	}
	CombatantReplicas(const CombatantReplicas&) = delete;
	CombatantReplicas& operator=(const CombatantReplicas&) = delete;

	// The owner calls this for trusted baseline or lifecycle data and supplies the tick of any imported state.
	[[nodiscard]] bool RegisterBody(WorldIdentity World, ParticipantId Participant, EntityRef Body,
		std::optional<u32> AppliedStateTick = std::nullopt) noexcept
	{
		const auto State = Classify(World, Participant, Body);
		if (State == ReplicaBodyState::Current)
		{
			return true;
		}
		if (State != ReplicaBodyState::Unknown || std::ranges::any_of(Active, [Participant, ObjectId = Body.ObjectId](const Entry& Other)
		{
			return Other.Participant == Participant || Other.Body.ObjectId == ObjectId;
		}))
		{
			return false;
		}

		const auto Available = std::ranges::find(Active, ParticipantId{}, &Entry::Participant);
		if (Available == Active.end())
		{
			return false;
		}
		*Available = {Participant, Body, AppliedStateTick};
		return true;
	}

	[[nodiscard]] bool Despawn(WorldIdentity World, ParticipantId Participant, EntityRef Body) noexcept
	{
		if (!HasValidIdentity(World, Participant, Body))
		{
			return false;
		}
		const auto Slot = FindSlot(Body.ObjectId);
		if (!Slot || !Active[*Slot].Matches(Participant, Body))
		{
			return false;
		}

		RetiredGenerations[Body.ObjectId] = Body.Generation;
		Active[*Slot] = {};
		return true;
	}

	// Read the identity state before applying snapshot data.
	[[nodiscard]] ReplicaBodyState Classify(WorldIdentity World, ParticipantId Participant, EntityRef Body) const noexcept
	{
		if (!HasValidIdentity(World, Participant, Body))
		{
			return ReplicaBodyState::Invalid;
		}
		if (Body.Generation <= RetiredGenerations[Body.ObjectId])
		{
			return ReplicaBodyState::Stale;
		}

		if (const auto Slot = FindSlot(Body.ObjectId))
		{
			const auto& Entry = Active[*Slot];
			if (Entry.Matches(Participant, Body))
			{
				return ReplicaBodyState::Current;
			}
			if (Body.Generation < Entry.Body.Generation)
			{
				return ReplicaBodyState::Stale;
			}
			if (Body.Generation == Entry.Body.Generation)
			{
				return ReplicaBodyState::Conflict;
			}
		}
		return ReplicaBodyState::Unknown;
	}

	// After synchronization, the owner supplies the current receiver and locally applied event frontier.
	// ApplyState applies the complete update atomically and does not reenter this registry.
	template <class Applier>
		requires std::is_nothrow_invocable_r_v<bool, Applier&>
	[[nodiscard]] std::expected<void, ReplicaSnapshotError> ApplyLiveSnapshot(JoinIdentity Receiver, std::optional<u32> AppliedWatermark,
		const ReplicaSnapshot& Snapshot, Applier&& ApplyState) noexcept
	{
		const WorldIdentity World{Receiver.ServerSession, Receiver.WorldEpoch};
		if (!Receiver.IsBound() || Snapshot.Identity != Receiver || World != Identity)
		{
			return std::unexpected(ReplicaSnapshotError::IdentityMismatch);
		}

		switch (Classify(World, Snapshot.Participant, Snapshot.Body))
		{
		case ReplicaBodyState::Invalid:
			return std::unexpected(ReplicaSnapshotError::InvalidReference);
		case ReplicaBodyState::Unknown:
			return std::unexpected(ReplicaSnapshotError::UnknownReference);
		case ReplicaBodyState::Stale:
			return std::unexpected(ReplicaSnapshotError::StaleReference);
		case ReplicaBodyState::Conflict:
			return std::unexpected(ReplicaSnapshotError::ConflictingReference);
		case ReplicaBodyState::Current:
			break;
		}

		const auto ServerTick = Snapshot.ServerTick;
		auto& AppliedTick = Active[*FindSlot(Snapshot.Body.ObjectId)].AppliedSnapshotTick;
		if (AppliedTick && !IsSequenceNewer(ServerTick, *AppliedTick))
		{
			return std::unexpected(ReplicaSnapshotError::StaleTick);
		}
		if (!AppliedWatermark || IsSequenceNewer(Snapshot.RequiredEventWatermark, *AppliedWatermark))
		{
			return std::unexpected(ReplicaSnapshotError::EventsPending);
		}
		if (!IsSequenceCurrentOrNewer(*AppliedWatermark, Snapshot.RequiredEventWatermark))
		{
			return std::unexpected(ReplicaSnapshotError::AmbiguousWatermark);
		}
		const bool Applied = ApplyState();
		if (!Applied)
		{
			return std::unexpected(ReplicaSnapshotError::ApplicationFailed);
		}
		AppliedTick = ServerTick;
		return {};
	}

	// Only an owner-accepted world transition resets the retirement history.
	[[nodiscard]] bool AdvanceWorld(u32 Epoch) noexcept
	{
		if (Identity.ServerSession == 0 || Epoch <= Identity.WorldEpoch)
		{
			return false;
		}

		Identity.WorldEpoch = Epoch;
		Active = {};
		std::ranges::fill(RetiredGenerations, 0u);
		return true;
	}

private:
	struct Entry
	{
		ParticipantId Participant;
		EntityRef Body;
		std::optional<u32> AppliedSnapshotTick;

		[[nodiscard]] bool Matches(ParticipantId Owner, EntityRef Reference) const noexcept
		{
			return Participant == Owner && Body == Reference;
		}
	};

	[[nodiscard]] bool HasValidIdentity(WorldIdentity World, ParticipantId Participant, EntityRef Body) const noexcept
	{
		return Identity.ServerSession != 0 && World == Identity && Participant.IsAssigned() && Body.IsAssigned();
	}

	[[nodiscard]] std::optional<std::size_t> FindSlot(u16 ObjectId) const noexcept
	{
		const auto Match = std::ranges::find_if(Active, [ObjectId](const Entry& Other) { return Other.Body.ObjectId == ObjectId; });
		if (Match == Active.end())
		{
			return std::nullopt;
		}
		return static_cast<std::size_t>(Match - Active.begin());
	}

	static constexpr std::size_t Capacity = 32;
	WorldIdentity Identity;
	const xr_span<u32, InvalidObjectId> RetiredGenerations;
	xr_array<Entry, Capacity> Active{};
};
}
