#pragma once

#include "../../xrCore/ScalarTypes.h"

namespace Multiplayer
{
struct JoinIdentity
{
	u64 ServerSession = 0;
	u32 WorldEpoch = 0;
	u32 ConnectionEpoch = 0;

	[[nodiscard]] constexpr bool IsBound() const noexcept
	{
		return ServerSession != 0 && ConnectionEpoch != 0;
	}

	bool operator==(const JoinIdentity&) const = default;
};

struct BaselineBoundary
{
	u32 BaselineId;
	u32 CaptureTick;
	u32 EventWatermark;

	bool operator==(const BaselineBoundary&) const = default;
};

[[nodiscard]] bool IsNewerBaseline(BaselineBoundary Candidate, BaselineBoundary Current) noexcept;

struct JoinBarrier
{
	u32 BaselineId;
	u32 BarrierId;
	u32 SnapshotTick;
	u32 EventWatermark;

	bool operator==(const JoinBarrier&) const = default;
};
}
