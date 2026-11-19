#pragma once

template <class T>
struct _vector3;

namespace WeaponSpread
{
void ApplyLegacy(_vector3<float>& Direction, const _vector3<float>& Aim, float Dispersion);
}
