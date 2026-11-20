#pragma once

#include "BaselineRecords.h"
#include "CombatantSnapshot.h"

namespace Multiplayer
{
inline constexpr std::size_t MaxCombatantBaselineBytes = MaxCombatants * (Wire::PayloadRecordHeaderSize + Wire::StalkerCombatantRecordSize);

enum class CombatantBaselineEncodeError
{
	InvalidRecord,
	OutputTooSmall,
};

// The capture owns its checked public combatant values independently of transfer storage.
class CombatantBaseline
{
public:
	[[nodiscard]] static std::expected<CombatantBaseline, BaselineRecordsError> Decode(const StagedBaseline& Baseline,
		JoinIdentity Receiver, BaselineBoundary Selected) noexcept;

	[[nodiscard]] JoinIdentity GetIdentity() const noexcept { return Identity; }
	[[nodiscard]] BaselineBoundary GetBoundary() const noexcept { return Boundary; }
	[[nodiscard]] xr_span<const Wire::CombatantSnapshot> GetRecords() const noexcept { return xr_span(Records).first(Count); }

private:
	CombatantBaseline(JoinIdentity Identity, BaselineBoundary Boundary) noexcept : Identity(Identity), Boundary(Boundary) {}

	JoinIdentity Identity;
	BaselineBoundary Boundary;
	xr_array<Wire::CombatantSnapshot, MaxCombatants> Records{};
	std::size_t Count = 0;
};

// The manifest describes exactly the bytes written. Failure preserves Output.
[[nodiscard]] std::expected<Wire::BaselineManifest, CombatantBaselineEncodeError> EncodeCombatantBaseline(BaselineBoundary Boundary,
	xr_span<const Wire::CombatantSnapshot> Records, xr_span<u8> Output) noexcept;
}
