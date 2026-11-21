#pragma once

#include "ArenaLadderTraversal.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>
#include <optional>

namespace Multiplayer::ArenaStalkerPose
{
enum class Stage : u8 { Ground, MountUp, Up, ExitUp, SettleUp, MountDown, Down, ExitDown, SettleDown };
struct Pose
{
	Stage Action = Stage::Ground;
	u16 Progress = 0;

	bool operator==(const Pose&) const = default;
};

struct Snapshot
{
	u32 Life = 0;
	u32 Tick = 0;
	f32 X = 0, Y = 0, Z = 0;
	f32 Yaw = 0;
	Pose Ladder;
};

struct Clip
{
	xr_string_view Name;
	f32 Frame = 0;
};

inline Pose Measure(ArenaLadders::Phase Phase, ArenaLadders::Direction Direction, f32 Travel, f32 Length)
{
	using enum ArenaLadders::Phase;
	const bool Down = Direction == ArenaLadders::Direction::Down;
	Travel = std::isfinite(Travel) && std::isfinite(Length) ? std::clamp(Travel, 0.f, std::max(Length, 0.f)) : 0.f;
	const f32 Fraction = Length > 0.001f ? Travel / Length : 0.f;
	const u16 Transition = u16(std::lround(Fraction * 65535.f));
	switch (Phase)
	{
	case Mount: return {Down ? Stage::MountDown : Stage::MountUp, Transition};
	case Climb:
		// Keep the cycle count unwrapped so interpolation across a rung cycle has the correct direction.
		return {Down ? Stage::Down : Stage::Up, u16(std::min(Travel * (256.f / 0.8f), 65535.f))};
	case Dismount: return {Down ? Stage::ExitDown : Stage::ExitUp, Transition};
	case Settle: return {Down ? Stage::SettleDown : Stage::SettleUp, 65535};
	default: return {};
	}
}

inline Clip SelectClip(Pose State, const auto& HasMotion)
{
	if (State.Action == Stage::Ground)
	{
		return {};
	}
	constexpr xr_string_view Ascend = "cl_run_fwd_0";
	constexpr xr_string_view Descend = "cl_run_back_0";
	constexpr xr_string_view Idle = "cl_idle_1";
	constexpr xr_string_view Exit = "cr_idle_0_out";
	const bool Down = State.Action >= Stage::MountDown;
	const bool IsClimb = State.Action == Stage::Up || State.Action == Stage::Down;
	const bool IsMount = State.Action == Stage::MountUp || State.Action == Stage::MountDown;
	const f32 Fraction = IsClimb ? f32(State.Progress % 256) / 256.f : f32(State.Progress) / 65535.f;
	if (!IsClimb)
	{
		const auto Transition = IsMount ? Idle : Exit;
		if (HasMotion(Transition))
		{
			return {Transition, Fraction};
		}
	}
	if (HasMotion(Down ? Descend : Ascend))
	{
		return {Down ? Descend : Ascend, Fraction};
	}
	if (HasMotion(Down ? Ascend : Descend))
	{
		return {Down ? Ascend : Descend, 1.f - Fraction};
	}
	return HasMotion(Idle) ? Clip{Idle, 0.f} : Clip{};
}

inline f32 SampleTime(Pose State, Clip Selected, f32 Length, f32 KeyInterval)
{
	if (State.Action == Stage::Up || State.Action == Stage::Down)
	{
		// The last key of a cycle blends into the first.
		return Selected.Frame < 1.f ? Selected.Frame * Length : 0.f;
	}
	return Selected.Frame * std::max(0.f, Length - KeyInterval);
}

constexpr std::size_t WireSize = 28;
constexpr u8 WireVersion = 1;
using Wire = xr_array<u8, WireSize>;

inline bool Valid(const Snapshot& State)
{
	const auto Action = State.Ladder.Action;
	return State.Life != 0 && Action <= Stage::SettleDown && std::isfinite(State.X) &&
		std::isfinite(State.Y) && std::isfinite(State.Z) && std::abs(State.Yaw) <= std::numbers::pi_v<f32> &&
		(Action != Stage::Ground || State.Ladder.Progress == 0) &&
		((Action != Stage::SettleUp && Action != Stage::SettleDown) || State.Ladder.Progress == 65535);
}

inline Wire Encode(const Snapshot& State)
{
	Wire Data{};
	const auto Put = [&](u32 Value, std::size_t Offset, u32 Width)
	{
		for (u32 Byte = 0; Byte < Width; ++Byte)
		{
			Data[Offset + Byte] = u8(Value >> (8 * Byte));
		}
	};
	Data[0] = WireVersion;
	Data[1] = u8(State.Ladder.Action);
	Put(State.Ladder.Progress, 2, 2);
	Put(State.Life, 4, 4);
	Put(State.Tick, 8, 4);
	Put(std::bit_cast<u32>(State.X), 12, 4);
	Put(std::bit_cast<u32>(State.Y), 16, 4);
	Put(std::bit_cast<u32>(State.Z), 20, 4);
	Put(std::bit_cast<u32>(State.Yaw), 24, 4);
	return Data;
}

inline std::optional<Snapshot> Decode(xr_span<const u8> Data)
{
	if (Data.size() != WireSize || Data[0] != WireVersion)
	{
		return {};
	}
	const auto Get = [&](std::size_t Offset, u32 Width)
	{
		u32 Value = 0;
		for (u32 Byte = 0; Byte < Width; ++Byte)
		{
			Value |= u32(Data[Offset + Byte]) << (8 * Byte);
		}
		return Value;
	};
	const auto Float = [&](std::size_t Offset) { return std::bit_cast<f32>(Get(Offset, 4)); };
	Snapshot State{Get(4, 4), Get(8, 4), Float(12), Float(16), Float(20), Float(24), {Stage(Data[1]), u16(Get(2, 2))}};
	return Valid(State) ? std::optional{State} : std::nullopt;
}

inline bool TickAfter(u32 Left, u32 Right)
{
	const u32 Delta = Left - Right;
	return Delta && Delta < (1u << 31);
}

inline bool Follows(const Snapshot& State, const Snapshot& Previous)
{
	return State.Life == Previous.Life && TickAfter(State.Tick, Previous.Tick);
}

class Replica
{
	static constexpr u32 Timeout = 1000;
	xr_array<Snapshot, 16> Samples{};
	std::size_t Count = 0;

	static Snapshot Interpolate(const Snapshot& Older, const Snapshot& New, u32 Tick)
	{
		const f32 T = f32(Tick - Older.Tick) / f32(New.Tick - Older.Tick);
		Snapshot State = Older;
		State.Tick = Tick;
		State.X = std::lerp(Older.X, New.X, T);
		State.Y = std::lerp(Older.Y, New.Y, T);
		State.Z = std::lerp(Older.Z, New.Z, T);
		constexpr f32 Turn = 2.f * std::numbers::pi_v<f32>;
		State.Yaw = std::remainder(Older.Yaw + T * std::remainder(New.Yaw - Older.Yaw, Turn), Turn);
		if (Older.Ladder.Action == New.Ladder.Action)
		{
			State.Ladder.Progress = u16(std::lround(std::lerp(f32(Older.Ladder.Progress), f32(New.Ladder.Progress), T)));
		}
		return State;
	}

public:
	bool Bound() const { return Count != 0; }
	void Clear() { Count = 0; }

	bool Start(const Snapshot& State)
	{
		Clear();
		if (!Valid(State))
		{
			return false;
		}
		Samples[Count++] = State;
		return true;
	}

	bool Accept(const Snapshot& State)
	{
		if (!Bound() || !Valid(State) || !Follows(State, Samples[Count - 1]))
		{
			return false;
		}
		if (Count == Samples.size())
		{
			std::move(Samples.begin() + 1, Samples.end(), Samples.begin());
			--Count;
		}
		Samples[Count++] = State;
		return true;
	}

	std::optional<Snapshot> Sample(u32 Tick) const
	{
		if (!Bound())
		{
			return {};
		}
		const auto& Latest = Samples[Count - 1];
		if (!TickAfter(Latest.Tick, Tick))
		{
			return Tick - Latest.Tick <= Timeout ? std::optional{Latest} : std::nullopt;
		}
		if (!TickAfter(Tick, Samples[0].Tick))
		{
			return Samples[0];
		}
		for (std::size_t Index = 1; Index < Count; ++Index)
		{
			if (TickAfter(Samples[Index].Tick, Tick))
			{
				return Interpolate(Samples[Index - 1], Samples[Index], Tick);
			}
		}
		return Latest;
	}
};
}
