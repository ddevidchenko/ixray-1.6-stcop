#pragma once

#include "JoinTypes.h"
#include "../../xrCore/_stl_extensions_nonalloc.h"

namespace Multiplayer
{
struct CatchUpEvent
{
	JoinIdentity Identity;
	u32 BaselineId;
	u32 Sequence;
	xr_span<const u8> Bytes;
};
}
