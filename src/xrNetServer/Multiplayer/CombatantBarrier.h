#pragma once

#include "CombatantBaseline.h"

#include <variant>

namespace Multiplayer::Wire
{
struct CombatantBarrierChunk
{
	JoinIdentity Identity;
	u32 MessageSequence;
	JoinBarrier Barrier;
	u32 ByteCount;
	u16 RecordCount;
	u16 ChunkIndex;
	xr_span<const u8> Data;
};

// The decoded data borrows the datagram. Failed encodes preserve Output, even if Data overlaps it.
[[nodiscard]] std::expected<CombatantBarrierChunk, BoundMessageError> DecodeCombatantBarrierChunk(xr_span<const u8> Bytes) noexcept;
[[nodiscard]] std::expected<std::size_t, BoundMessageError> EncodeCombatantBarrierChunk(const CombatantBarrierChunk& Chunk,
	xr_span<u8> Output) noexcept;
}

namespace Multiplayer
{
enum class CombatantBarrierScopeError
{
	IdentityMismatch,
	BarrierMismatch,
};

using CombatantBarrierReceiveError = std::variant<CombatantBarrierScopeError, BaselineStagingError>;
using CombatantBarrierFinishError = std::variant<BaselineStagingError, BaselineRecordsError>;

class CombatantBarrierSnapshot
{
public:
	[[nodiscard]] JoinIdentity GetIdentity() const noexcept { return Roster.GetIdentity(); }
	[[nodiscard]] JoinBarrier GetBarrier() const noexcept { return Barrier; }
	[[nodiscard]] xr_span<const Wire::CombatantSnapshot> GetRecords() const noexcept { return Roster.GetRecords(); }

private:
	friend class CombatantBarrierStaging;
	CombatantBarrierSnapshot(JoinBarrier Barrier, CombatantBaseline&& Roster) noexcept;

	JoinBarrier Barrier;
	CombatantBaseline Roster;
};

// The owner selects a declared barrier and keeps the reserved transfer buffer exclusive.
class CombatantBarrierStaging
{
public:
	CombatantBarrierStaging(JoinIdentity Identity, JoinBarrier Barrier, xr_span<u8> Storage,
		BaselineStagingReservation&& Reservation) noexcept;

	[[nodiscard]] std::expected<void, CombatantBarrierReceiveError> AddChunk(const Wire::CombatantBarrierChunk& Chunk) noexcept;
	[[nodiscard]] std::expected<CombatantBarrierSnapshot, CombatantBarrierFinishError> Finish() noexcept;

private:
	const JoinIdentity Identity;
	const JoinBarrier Barrier;
	BaselineStaging Staging;
};
}
