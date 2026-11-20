#pragma once

#include "CombatantReplicas.h"
#include "../../xrNetServer/Multiplayer/CombatantSnapshot.h"

#include <algorithm>
#include <expected>
#include <type_traits>

namespace Multiplayer::Detail
{
template <class Applier>
	requires std::is_nothrow_invocable_r_v<bool, Applier&>
[[nodiscard]] std::expected<void, ReplicaBaselineError> ApplyCombatantCapture(CombatantReplicas& Replicas, WorldIdentity World, u32 Tick,
	xr_span<const Wire::CombatantSnapshot> Records, Applier&& ApplyState) noexcept
{
	if (Records.size() > MaxCombatants)
	{
		return std::unexpected(ReplicaBaselineError::InvalidBody);
	}
	xr_array<ReplicaBodyIdentity, MaxCombatants> Bodies;
	std::ranges::transform(Records, Bodies.begin(), [](const Wire::CombatantSnapshot& Record) noexcept
	{
		const auto& State = Wire::GetPublicCombatantState(Record);
		return ReplicaBodyIdentity{State.Participant, State.Body};
	});
	return Replicas.ReplaceFromBaseline(World, Tick, xr_span(Bodies).first(Records.size()), ApplyState);
}
}
