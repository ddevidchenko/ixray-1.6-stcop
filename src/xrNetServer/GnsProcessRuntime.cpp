#include "stdafx.h"
#include "GnsProcessRuntime.h"

Multiplayer::GnsProcessRuntime& Multiplayer::GetGnsProcessRuntime() noexcept
{
	static GnsProcessRuntime Runtime;
	return Runtime;
}
