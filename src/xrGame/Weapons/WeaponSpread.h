#pragma once

#include <cmath>
#include <numbers>

namespace WeaponSpread
{
struct Angles
{
	float Inclination;
	float Azimuth;
};

namespace Detail
{
template <class RandomSource>
float SampleInclination(float Dispersion, RandomSource& Source)
{
	const float Sigma = Dispersion / 3.0f;
	if (Sigma == 0.0f)
	{
		return Dispersion < 0.0f ? -Dispersion : 0.0f;
	}

	for (;;)
	{
		const float Unit = Source.NextUnit();
		const float AcceptanceDraw = Source.NextUnit();
		if (Unit == 0.0f)
		{
			// The legacy sampler accepts this endpoint only when both draws are zero.
			if (AcceptanceDraw == 0.0f)
			{
				const float Limit = std::abs(Dispersion);
				return Source.NextPositiveSign() ? Limit : -Limit;
			}
			continue;
		}

		const float Magnitude = -std::log(Unit);
		const float Offset = Magnitude - 1.0f;
		if (AcceptanceDraw > std::exp(-(Offset * Offset) * 0.5f))
		{
			continue;
		}

		constexpr float InverseScale = 1.0f / 0.7975f;
		const float SignedMagnitude = Source.NextPositiveSign() ? Magnitude : -Magnitude;
		const float Inclination = SignedMagnitude * Sigma * InverseScale;
		// Preserve the legacy comparison order for negative dispersion values.
		if (Inclination < -Dispersion)
		{
			return -Dispersion;
		}
		if (Inclination > Dispersion)
		{
			return Dispersion;
		}
		return Inclination;
	}
}
}

// Dispersion and both result angles use radians.
// NextUnit returns floats in [0, 1]. NextPositiveSign supplies the sign after acceptance.
// The azimuth consumes one unit draw even when dispersion is zero.
template <class RandomSource>
[[nodiscard]] Angles Sample(float Dispersion, RandomSource& Source)
{
	const float Inclination = Detail::SampleInclination(Dispersion, Source);
	return {Inclination, Source.NextUnit() * std::numbers::pi_v<float>};
}
}
