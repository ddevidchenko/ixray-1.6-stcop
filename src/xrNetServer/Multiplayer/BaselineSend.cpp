#include "BaselineSend.h"

#include <algorithm>

namespace Multiplayer
{
BaselineSend::BaselineSend(JoinIdentity Identity, Wire::BaselineManifest Manifest, xr_span<const u8> Source, u64 DeadlineMs) noexcept
	: Identity(Identity), Manifest(Manifest), Source(Source), DeadlineMs(DeadlineMs)
{
	if (!Identity.IsBound() || !Wire::IsValidBaselineManifest(Manifest) || Source.size() != Manifest.ByteCount)
	{
		Failure = BaselineSendResult::InvalidSource;
	}
}

std::expected<std::size_t, Wire::BoundMessageError> BaselineSend::EncodeNext(u32 Sequence, xr_span<u8> Output) const noexcept
{
	if (SubmittedCount == 0)
	{
		return Wire::EncodeBaselineBegin({Identity, Sequence, Manifest}, Output);
	}
	if (SubmittedCount > Manifest.ChunkCount)
	{
		return Wire::EncodeBaselineEnd({Identity, Sequence, Manifest.BaselineId}, Output);
	}

	const auto Index = static_cast<u16>(SubmittedCount - 1);
	const auto Remaining = Source.subspan(u32{Index} * Wire::MaxBaselineChunkBytes);
	const auto ByteCount = std::min<std::size_t>(Wire::MaxBaselineChunkBytes, Remaining.size());
	return Wire::EncodeBaselineChunk({Identity, Sequence, Manifest.BaselineId, Index, Remaining.first(ByteCount)}, Output);
}
}
