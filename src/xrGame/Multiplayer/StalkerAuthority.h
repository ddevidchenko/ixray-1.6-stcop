#pragma once

namespace Multiplayer
{
enum class StalkerRole
{
	Inactive,
	Authority,
	Replica,
};

[[nodiscard]] constexpr StalkerRole ResolveStalkerRole(bool Server, bool Local) noexcept
{
	if (Server != Local)
	{
		return StalkerRole::Inactive;
	}
	return Server ? StalkerRole::Authority : StalkerRole::Replica;
}
}
