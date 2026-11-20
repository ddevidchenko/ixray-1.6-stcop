#pragma once

#include "../../xrCore/ScalarTypes.h"

namespace Multiplayer
{
// Within one stream, a serial is newer at a forward distance of 1 through 2^31 - 1.
[[nodiscard]] bool IsSequenceNewer(u32 Candidate, u32 Reference) noexcept;

[[nodiscard]] inline bool IsSequenceCurrentOrNewer(u32 Candidate, u32 Reference) noexcept
{
	return Candidate == Reference || IsSequenceNewer(Candidate, Reference);
}
}
