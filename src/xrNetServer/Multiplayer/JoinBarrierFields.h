#pragma once

#include "JoinTypes.h"
#include "WireEndian.h"

namespace Multiplayer::Wire::Detail
{
inline constexpr std::size_t BaselineOffset = 0;
inline constexpr std::size_t BarrierOffset = BaselineOffset + sizeof(u32);
inline constexpr std::size_t TickOffset = BarrierOffset + sizeof(u32);
inline constexpr std::size_t WatermarkOffset = TickOffset + sizeof(u32);
inline constexpr std::size_t BarrierPayloadSize = WatermarkOffset + sizeof(u32);
static_assert(BarrierPayloadSize == 16);

[[nodiscard]] inline JoinBarrier ReadJoinBarrier(xr_span<const u8, BarrierPayloadSize> Payload) noexcept
{
	return {
		ReadLittleEndian<u32>(Payload.subspan<BaselineOffset, sizeof(u32)>()),
		ReadLittleEndian<u32>(Payload.subspan<BarrierOffset, sizeof(u32)>()),
		ReadLittleEndian<u32>(Payload.subspan<TickOffset, sizeof(u32)>()),
		ReadLittleEndian<u32>(Payload.subspan<WatermarkOffset, sizeof(u32)>()),
	};
}

inline void WriteJoinBarrier(xr_span<u8, BarrierPayloadSize> Payload, JoinBarrier Barrier) noexcept
{
	WriteLittleEndian(Payload.subspan<BaselineOffset, sizeof(u32)>(), Barrier.BaselineId);
	WriteLittleEndian(Payload.subspan<BarrierOffset, sizeof(u32)>(), Barrier.BarrierId);
	WriteLittleEndian(Payload.subspan<TickOffset, sizeof(u32)>(), Barrier.SnapshotTick);
	WriteLittleEndian(Payload.subspan<WatermarkOffset, sizeof(u32)>(), Barrier.EventWatermark);
}
}
