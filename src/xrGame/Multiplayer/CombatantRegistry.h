#pragma once

#include "../../xrNetServer/Multiplayer/CombatantIdentity.h"
#include "../../xrNetServer/Multiplayer/JoinTypes.h"
#include "../../xrCore/_stl_extensions_nonalloc.h"

#include <algorithm>
#include <limits>
#include <optional>

namespace Multiplayer
{
namespace Detail
{
[[nodiscard]] constexpr std::optional<u32> NextCombatantIdentity(u32 LastIssued) noexcept
{
	if (LastIssued == std::numeric_limits<u32>::max())
	{
		return std::nullopt;
	}
	return LastIssued + 1u;
}
}

// The match owner serializes operations. Each entry represents a combatant with at most one live body.
class CombatantRegistry
{
public:
	explicit CombatantRegistry(WorldIdentity Identity) noexcept : Identity(Identity) {}
	CombatantRegistry(const CombatantRegistry&) = delete;
	CombatantRegistry& operator=(const CombatantRegistry&) = delete;

	[[nodiscard]] std::optional<ParticipantId> AddParticipant() noexcept
	{
		const auto* Participant = CreateParticipant();
		if (!Participant)
		{
			return std::nullopt;
		}
		return Participant->Id;
	}

	// Disconnect can arrive after a round change within the same session.
	[[nodiscard]] bool RemoveParticipant(u64 ServerSession, ParticipantId Participant) noexcept
	{
		if (ServerSession != Identity.ServerSession)
		{
			return false;
		}
		auto* Match = FindParticipant(Participant);
		if (!Match)
		{
			return false;
		}
		*Match = {};
		return true;
	}

	// The admission owner supplies the accepted connection and publishes admissions in increasing epoch order.
	[[nodiscard]] std::optional<ParticipantId> AddAdmittedParticipant(AdmittedConnection Connection) noexcept
	{
		if (Connection.ServerSession != Identity.ServerSession || Connection.Handle == 0 || Connection.ConnectionEpoch <= LastConnectionEpoch)
		{
			return std::nullopt;
		}
		if (std::ranges::any_of(Participants, [Handle = Connection.Handle](const Entry& Other)
		{
			return Other.Connection && Other.Connection->Handle == Handle;
		}))
		{
			return std::nullopt;
		}

		auto* Participant = CreateParticipant();
		if (!Participant)
		{
			return std::nullopt;
		}
		Participant->Connection = Connection;
		LastConnectionEpoch = Connection.ConnectionEpoch;
		return Participant->Id;
	}

	// Identify the sender. The caller still checks readiness and the active body before applying commands.
	[[nodiscard]] std::optional<ParticipantId> ResolveConnection(AdmittedConnection ReceivedOn, JoinIdentity Message) const noexcept
	{
		if (Message != JoinIdentity{Identity.ServerSession, Identity.WorldEpoch, ReceivedOn.ConnectionEpoch})
		{
			return std::nullopt;
		}
		const auto Match = FindConnection(ReceivedOn);
		if (!Match)
		{
			return std::nullopt;
		}
		return Participants[*Match].Id;
	}

	[[nodiscard]] std::optional<ParticipantId> Disconnect(AdmittedConnection Connection) noexcept
	{
		const auto Match = FindConnection(Connection);
		if (!Match)
		{
			return std::nullopt;
		}
		const auto Id = Participants[*Match].Id;
		Participants[*Match] = {};
		return Id;
	}

	// The owner supplies an engine object slot after creating the body.
	[[nodiscard]] std::optional<EntityRef> BindBody(WorldIdentity World, ParticipantId Participant, u16 ObjectId) noexcept
	{
		if (World != Identity || ObjectId == InvalidObjectId)
		{
			return std::nullopt;
		}
		auto* Match = FindParticipant(Participant);
		if (!Match || Match->Body || std::ranges::any_of(Participants, [ObjectId](const Entry& Other)
		{
			return Other.Body && Other.Body->ObjectId == ObjectId;
		}))
		{
			return std::nullopt;
		}

		const auto Generation = Detail::NextCombatantIdentity(LastGeneration);
		const auto Life = Detail::NextCombatantIdentity(Match->LastLifeId);
		if (!Generation || !Life)
		{
			return std::nullopt;
		}

		const EntityRef Reference{ObjectId, *Generation, *Life};
		Match->Body = Reference;
		Match->LastLifeId = *Life;
		LastGeneration = *Generation;
		return Reference;
	}

	[[nodiscard]] std::optional<ParticipantId> Resolve(WorldIdentity World, EntityRef Reference) const noexcept
	{
		const auto Match = FindBody(World, Reference);
		if (!Match)
		{
			return std::nullopt;
		}
		return Participants[*Match].Id;
	}

	[[nodiscard]] bool RetireBody(WorldIdentity World, EntityRef Reference) noexcept
	{
		const auto Match = FindBody(World, Reference);
		if (!Match)
		{
			return false;
		}
		Participants[*Match].Body.reset();
		return true;
	}

	// Only the match owner may advance the world. A new session gets its own registry.
	[[nodiscard]] bool AdvanceWorld(u32 Epoch) noexcept
	{
		if (Identity.ServerSession == 0 || Epoch <= Identity.WorldEpoch)
		{
			return false;
		}

		Identity.WorldEpoch = Epoch;
		LastGeneration = 0;
		for (auto& Participant : Participants)
		{
			Participant.Body.reset();
			Participant.LastLifeId = 0;
		}
		return true;
	}

private:
	struct Entry
	{
		ParticipantId Id;
		std::optional<AdmittedConnection> Connection;
		std::optional<EntityRef> Body;
		u32 LastLifeId = 0;
	};

	[[nodiscard]] Entry* CreateParticipant() noexcept
	{
		if (Identity.ServerSession == 0)
		{
			return nullptr;
		}
		const auto Available = std::ranges::find(Participants, ParticipantId{}, &Entry::Id);
		const auto NextId = Detail::NextCombatantIdentity(LastParticipantId);
		if (Available == Participants.end() || !NextId)
		{
			return nullptr;
		}

		Available->Id = ParticipantId{*NextId};
		LastParticipantId = *NextId;
		return &*Available;
	}

	[[nodiscard]] Entry* FindParticipant(ParticipantId Participant) noexcept
	{
		if (Participant.Value == 0)
		{
			return nullptr;
		}
		const auto Match = std::ranges::find(Participants, Participant, &Entry::Id);
		return Match == Participants.end() ? nullptr : &*Match;
	}

	[[nodiscard]] std::optional<std::size_t> FindBody(WorldIdentity World, EntityRef Reference) const noexcept
	{
		if (World != Identity)
		{
			return std::nullopt;
		}
		const auto Match = std::ranges::find(Participants, std::optional{Reference}, &Entry::Body);
		if (Match == Participants.end())
		{
			return std::nullopt;
		}
		return static_cast<std::size_t>(Match - Participants.begin());
	}

	[[nodiscard]] std::optional<std::size_t> FindConnection(AdmittedConnection Connection) const noexcept
	{
		const auto Match = std::ranges::find(Participants, std::optional{Connection}, &Entry::Connection);
		if (Match == Participants.end())
		{
			return std::nullopt;
		}
		return static_cast<std::size_t>(Match - Participants.begin());
	}

	static constexpr std::size_t Capacity = 32;
	WorldIdentity Identity;
	xr_array<Entry, Capacity> Participants{};
	u32 LastParticipantId = 0;
	u32 LastGeneration = 0;
	u32 LastConnectionEpoch = 0;
};
}
