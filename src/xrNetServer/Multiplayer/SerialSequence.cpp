#include "SerialSequence.h"

bool Multiplayer::IsSequenceNewer(u32 Candidate, u32 Reference) noexcept
{
	constexpr u32 HalfRange = u32{1} << 31;
	const u32 Distance = Candidate - Reference;
	return Distance != 0 && Distance < HalfRange;
}
