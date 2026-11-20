#pragma once

#include "../xrCore/xrSyncronize.h"
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4995)
#endif
#include "Multiplayer/GnsEndpoint.h"
#include "Multiplayer/GnsRuntime.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace Multiplayer
{
using GnsProcessRuntime = GnsRuntime<xrCriticalSection, xrCriticalSectionGuard>;
using GnsProcessEndpoint = GnsEndpoint<GnsProcessRuntime::Lease>;

[[nodiscard]] GnsProcessRuntime& GetGnsProcessRuntime() noexcept;
}
