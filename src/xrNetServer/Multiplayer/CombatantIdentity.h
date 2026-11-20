#pragma once

#include "../../xrCore/ScalarTypes.h"

namespace Multiplayer
{
struct WorldIdentity
{
	u64 ServerSession = 0;
	u32 WorldEpoch = 0;

	bool operator==(const WorldIdentity&) const = default;
};

struct ParticipantId
{
	u32 Value = 0;

	bool operator==(const ParticipantId&) const = default;
};

inline constexpr u16 InvalidObjectId = u16(-1);

struct EntityRef
{
	u16 ObjectId = InvalidObjectId;
	u32 Generation = 0;
	u32 LifeId = 0;

	bool operator==(const EntityRef&) const = default;
};
}
