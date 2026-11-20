#include "JoinTypes.h"
#include "SerialSequence.h"

namespace Multiplayer
{
bool IsNewerBaseline(BaselineBoundary Candidate, BaselineBoundary Current) noexcept
{
	return IsSequenceNewer(Candidate.BaselineId, Current.BaselineId) && IsSequenceNewer(Candidate.CaptureTick, Current.CaptureTick) &&
		IsSequenceCurrentOrNewer(Candidate.EventWatermark, Current.EventWatermark);
}
}
