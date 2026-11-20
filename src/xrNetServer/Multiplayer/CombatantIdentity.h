#pragma once

#include "../../xrCore/ScalarTypes.h"

#include <cstddef>

namespace Multiplayer
{
inline constexpr std::size_t MaxCombatants = 32;

struct WorldIdentity
{
	u64 ServerSession = 0;
	u32 WorldEpoch = 0;

	bool operator==(const WorldIdentity&) const = default;
};

struct ParticipantId
{
	u32 Value = 0;

	[[nodiscard]] constexpr bool IsAssigned() const noexcept { return Value != 0; }
	bool operator==(const ParticipantId&) const = default;
};

inline constexpr u16 InvalidObjectId = u16(-1);

struct EntityRef
{
	u16 ObjectId = InvalidObjectId;
	u32 Generation = 0;
	u32 LifeId = 0;

	[[nodiscard]] constexpr bool IsAssigned() const noexcept
	{
		return ObjectId != InvalidObjectId && Generation != 0 && LifeId != 0;
	}

	bool operator==(const EntityRef&) const = default;
};
}
