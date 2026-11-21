#pragma once

#include "../../xrCore/ScalarTypes.h"
#include "../../xrCore/_stl_extensions_nonalloc.h"

#include <cmath>
#include <optional>

namespace Multiplayer::ArenaLadders
{
struct Point
{
	f32 X, Y, Z;

	Point operator+(Point Other) const { return {X + Other.X, Y + Other.Y, Z + Other.Z}; }
	Point operator-(Point Other) const { return {X - Other.X, Y - Other.Y, Z - Other.Z}; }
	Point operator*(f32 Factor) const { return {X * Factor, Y * Factor, Z * Factor}; }
};

inline bool Finite(Point Value)
{
	return std::isfinite(Value.X) && std::isfinite(Value.Y) && std::isfinite(Value.Z);
}

inline f32 Dot(Point First, Point Second)
{
	return First.X * Second.X + First.Y * Second.Y + First.Z * Second.Z;
}

inline Point Cross(Point First, Point Second)
{
	return {First.Y * Second.Z - First.Z * Second.Y, First.Z * Second.X - First.X * Second.Z,
		First.X * Second.Y - First.Y * Second.X};
}

using Basis = xr_array<Point, 3>;
using HalfSizes = xr_array<f32, 3>;

inline Point ClimbEndpoint(Point Center, Point Axis, Point Normal, bool Upper)
{
	return (Upper ? Center + Axis : Center - Axis) + Normal;
}

struct Geometry
{
	Point Center, Axis, Side, Normal;
	HalfSizes Half;

	Point Lower() const { return ClimbEndpoint(Center, Axis, Normal, false); }
	Point Upper() const { return ClimbEndpoint(Center, Axis, Normal, true); }
};

// This is the packet's o_Angle convention, matching Fmatrix::setXYZ in CGameObject::net_Spawn.
inline Basis SpawnBasis(Point Euler)
{
	const f32 Sh = std::sin(Euler.Y), Ch = std::cos(Euler.Y);
	const f32 Sp = std::sin(Euler.X), Cp = std::cos(Euler.X);
	const f32 Sb = std::sin(Euler.Z), Cb = std::cos(Euler.Z);
	const f32 Cc = Ch * Cb, Cs = Ch * Sb, Sc = Sh * Cb, Ss = Sh * Sb;
	return {{{Cc - Sp * Ss, -Cp * Sb, Sp * Cs + Sc},
		{Sp * Sc + Cs, Cp * Cb, Ss - Sp * Cc}, {-Cp * Sh, Sp, Cp * Ch}}};
}

inline xr_array<int, 3> SortLadderAxes(const HalfSizes& Half)
{
	// Retain the live object's ordering for equal half-sizes.
	const auto [X, Y, Z] = Half;
	if (X < Y)
	{
		return X < Z ? (Y < Z ? xr_array<int, 3>{2, 1, 0} : xr_array<int, 3>{1, 2, 0}) : xr_array<int, 3>{1, 0, 2};
	}
	if (Y < Z)
	{
		return X > Z ? xr_array<int, 3>{0, 2, 1} : xr_array<int, 3>{2, 0, 1};
	}
	return {0, 1, 2};
}

inline bool ValidHalfSizes(const HalfSizes& Half)
{
	return Finite({Half[0], Half[1], Half[2]}) && Half[0] >= 0.f && Half[1] >= 0.f && Half[2] >= 0.f &&
		(Half[0] > 0.f) + (Half[1] > 0.f) + (Half[2] > 0.f) >= 2;
}

inline std::optional<Geometry> BuildGeometry(Point Origin, const Basis& Axes, HalfSizes Half)
{
	if (!ValidHalfSizes(Half) || !Finite(Origin) || !Finite(Axes[0]) || !Finite(Axes[1]) || !Finite(Axes[2]))
	{
		return {};
	}
	const auto [Long, Wide, Thin] = SortLadderAxes(Half);
	Geometry Shape{Origin, Axes[Long] * Half[Long], Axes[Wide] * Half[Wide], {}, Half};
	constexpr f32 MinimumThickness = 0.2f;
	if (Half[Thin] < MinimumThickness)
	{
		Shape.Center = Origin - Axes[Thin] * MinimumThickness;
		Shape.Half[Thin] = MinimumThickness;
	}
	Shape.Normal = Axes[Thin] * Shape.Half[Thin];
	if (Shape.Axis.Y < 0.f)
	{
		Shape.Axis = Shape.Axis * -1.f;
		Shape.Side = Shape.Side * -1.f;
	}
	return Shape;
}

inline bool SupportsClimbing(Point Lower, Point Upper, Point Axis, Point Normal)
{
	const f32 Height = Upper.Y - Lower.Y;
	const f32 AxisLength = std::sqrt(Dot(Axis, Axis));
	const f32 NormalLength = std::sqrt(Dot(Normal, Normal));
	return Finite(Lower) && Finite(Upper) && Height >= 0.75f && Height <= 24.f &&
		AxisLength > 0.001f && Axis.Y >= AxisLength * 0.9f &&
		NormalLength > 0.001f && std::abs(Normal.Y) <= NormalLength * 0.25f;
}

inline bool NearLadderEnd(Point Node, Point End)
{
	const auto Offset = Node - End;
	return std::abs(Offset.Y) <= 0.55f && Offset.X * Offset.X + Offset.Z * Offset.Z <= 1.35f * 1.35f;
}

inline bool MatchesLandings(Point Lower, Point Upper, Point LowerNode, Point UpperNode)
{
	return UpperNode.Y - LowerNode.Y >= 0.6f && NearLadderEnd(LowerNode, Lower) && NearLadderEnd(UpperNode, Upper);
}
}
