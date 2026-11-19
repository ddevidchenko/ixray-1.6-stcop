#include "StdAfx.h"
#include "WeaponSpreadLegacy.h"
#include "WeaponSpread.h"

#include <cmath>
#include <cstdlib>

namespace WeaponSpread
{
void ApplyLegacy(Fvector& Direction, const Fvector& Aim, float Dispersion)
{
	struct LegacyRandom
	{
		float NextUnit() const
		{
			return ::Random.randF();
		}

		bool NextPositiveSign() const
		{
			return (std::rand() & 1) != 0;
		}
	};

	LegacyRandom Source;
	const Angles Spread = Sample(Dispersion, Source);
	const float Radius = tan(Spread.Inclination);
	Fvector Up, Right, Offset;
	Fvector::generate_orthonormal_basis(Aim, Up, Right);
	Up.mul(Radius * std::sin(Spread.Azimuth));
	Right.mul(Radius * std::cos(Spread.Azimuth));
	Offset.add(Up, Right);
	Direction.add(Aim, Offset).normalize();
}
}
