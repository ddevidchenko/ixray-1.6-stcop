#pragma once

#include "../../xrCore/_stl_extensions_nonalloc.h"

namespace StalkerNavigation
{
[[nodiscard]] constexpr bool W1Requested(bool DedicatedDeathmatch, xr_string_view ServerOptions) noexcept
{
	if (!DedicatedDeathmatch)
	{
		return false;
	}

	bool Requested = false;
	auto Separator = ServerOptions.find('/');
	while (Separator != xr_string_view::npos)
	{
		ServerOptions.remove_prefix(Separator + 1);
		Separator = ServerOptions.find('/');
		const auto Option = ServerOptions.substr(0, Separator);
		if (Option == "w1_stalker_navigation" || Option.starts_with("w1_stalker_navigation="))
		{
			if (Requested || Option != "w1_stalker_navigation=1")
			{
				return false;
			}
			Requested = true;
		}
	}
	return Requested;
}
}
