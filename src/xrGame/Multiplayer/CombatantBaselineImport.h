#pragma once

#include "CombatantCaptureApply.h"
#include "../../xrNetServer/Multiplayer/ClientCatchUp.h"
#include "../../xrNetServer/Multiplayer/CombatantBaseline.h"

#include <expected>
#include <variant>

namespace Multiplayer
{
class ICombatantBaselineApplier
{
public:
	virtual ~ICombatantBaselineApplier() = default;
	// Resolve content and replace the public combatant world as one operation.
	// Failure leaves the previous world intact. The callback does not reenter the import or either tracker.
	virtual bool ApplyBaseline(const CombatantBaseline& Baseline) noexcept = 0;
};

using CombatantBaselineImportError = std::variant<ClientCatchUpError, ReplicaBaselineError>;

// The client owner serializes baseline import with lifecycle, catch-up and live application.
[[nodiscard]] inline std::expected<void, CombatantBaselineImportError> ImportCombatantBaseline(const CombatantBaseline& Baseline,
	ClientCatchUp& CatchUp, CombatantReplicas& Replicas, ICombatantBaselineApplier& Applier) noexcept
{
	const auto Receiver = CatchUp.GetIdentity();
	const WorldIdentity World{Receiver.ServerSession, Receiver.WorldEpoch};
	if (Replicas.GetWorldIdentity() != World)
	{
		return std::unexpected(CombatantBaselineImportError{ReplicaBaselineError::WorldMismatch});
	}

	const auto Boundary = Baseline.GetBoundary();
	std::expected<void, ReplicaBaselineError> ReplicasApplied;
	const auto Confirmed = CatchUp.ApplyBaseline(Baseline.GetIdentity(), Boundary, [&]() noexcept
	{
		ReplicasApplied = Detail::ApplyCombatantCapture(Replicas, World, Boundary.CaptureTick, Baseline.GetRecords(), [&]() noexcept
		{
			return Applier.ApplyBaseline(Baseline);
		});
		return ReplicasApplied.has_value();
	});
	if (!ReplicasApplied)
	{
		return std::unexpected(CombatantBaselineImportError{ReplicasApplied.error()});
	}
	if (!Confirmed)
	{
		return std::unexpected(CombatantBaselineImportError{Confirmed.error()});
	}
	return {};
}
}
