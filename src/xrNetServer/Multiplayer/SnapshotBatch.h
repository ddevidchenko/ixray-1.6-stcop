#pragma once

#include "BoundMessage.h"
#include "WirePayload.h"

#include <optional>

namespace Multiplayer::Wire
{
inline constexpr u16 MaxSnapshotRecords = 32;

struct SnapshotBatchHeader
{
	JoinIdentity Identity;
	u32 MessageSequence;
	u32 SnapshotId;
	u32 ServerTick;
	u64 ServerTimeMs;
	u32 RequiredEventWatermark;

	bool operator==(const SnapshotBatchHeader&) const = default;
};

enum class SnapshotRecordKind : u8
{
	HumanCombatant = 0x01,
	StalkerCombatant = 0x02,
};

struct SnapshotRecordView
{
	SnapshotRecordKind Kind;
	xr_span<const u8> Payload;
};

// The cursor and records borrow the original datagram. Keep it alive and unchanged.
class SnapshotBatch
{
public:
	// Validates the entire batch framing. Typed payload validation belongs to the record codec.
	[[nodiscard]] static std::expected<SnapshotBatch, BoundMessageError> Decode(xr_span<const u8> Bytes) noexcept;

	[[nodiscard]] const SnapshotBatchHeader& GetHeader() const noexcept { return Header; }
	[[nodiscard]] u16 GetRecordCount() const noexcept { return RecordCount; }
	[[nodiscard]] std::optional<SnapshotRecordView> Next() noexcept;

private:
	SnapshotBatch(SnapshotBatchHeader Header, u16 RecordCount, PayloadReader Records) noexcept;

	SnapshotBatchHeader Header;
	u16 RecordCount;
	PayloadReader Records;
};

// The source record payloads may overlap Output. Failure leaves Output unchanged.
[[nodiscard]] std::expected<std::size_t, BoundMessageError> EncodeSnapshotBatch(SnapshotBatchHeader Header,
	xr_span<const SnapshotRecordView> Records, xr_span<u8> Output) noexcept;
}
