#include "SnapshotBatch.h"

namespace Multiplayer::Wire
{
constexpr std::size_t SnapshotHeaderSize = sizeof(u32) + sizeof(u32) + sizeof(u64) + sizeof(u32) + sizeof(u16);
static_assert(SnapshotHeaderSize == 22 && SnapshotHeaderSize < MaxPayloadSize);

static bool IsSupportedKind(SnapshotRecordKind Kind) noexcept
{
	switch (Kind)
	{
	case SnapshotRecordKind::HumanCombatant:
	case SnapshotRecordKind::StalkerCombatant:
		return true;
	}
	return false;
}

SnapshotBatch::SnapshotBatch(SnapshotBatchHeader Header, u16 RecordCount, PayloadReader Records) noexcept
	: Header(Header), RecordCount(RecordCount), Records(Records)
{
}

std::expected<SnapshotBatch, BoundMessageError> SnapshotBatch::Decode(xr_span<const u8> Bytes) noexcept
{
	const auto Frame = Detail::DecodeBoundMessage(Bytes, MessageType::SnapshotBatch, SnapshotHeaderSize, MaxPayloadSize);
	if (!Frame)
	{
		return std::unexpected(Frame.error());
	}

	// DecodeBoundMessage checked the full fixed header before these reads.
	PayloadReader Records(Frame->Payload);
	const SnapshotBatchHeader Header{
		Frame->Identity,
		Frame->MessageSequence,
		*Records.ReadInteger<u32>(),
		*Records.ReadInteger<u32>(),
		*Records.ReadInteger<u64>(),
		*Records.ReadInteger<u32>(),
	};
	const u16 RecordCount = *Records.ReadInteger<u16>();
	if (RecordCount == 0 || RecordCount > MaxSnapshotRecords)
	{
		return std::unexpected(BoundMessageError::InvalidPayload);
	}

	auto Validation = Records;
	for (u16 Index = 0; Index < RecordCount; ++Index)
	{
		const auto Record = Validation.ReadRecord();
		if (!Record)
		{
			return std::unexpected(BoundMessageError::TruncatedPayload);
		}
		if (!IsSupportedKind(static_cast<SnapshotRecordKind>(Record->Kind)) || Record->Payload.empty())
		{
			return std::unexpected(BoundMessageError::InvalidPayload);
		}
	}
	if (Validation.RemainingBytes() != 0)
	{
		return std::unexpected(BoundMessageError::TrailingPayload);
	}
	return SnapshotBatch(Header, RecordCount, Records);
}

std::optional<SnapshotRecordView> SnapshotBatch::Next() noexcept
{
	const auto Record = Records.ReadRecord();
	if (!Record)
	{
		return std::nullopt;
	}
	return SnapshotRecordView{static_cast<SnapshotRecordKind>(Record->Kind), Record->Payload};
}

std::expected<std::size_t, BoundMessageError> EncodeSnapshotBatch(SnapshotBatchHeader Header, xr_span<const SnapshotRecordView> Records,
	xr_span<u8> Output) noexcept
{
	if (Records.empty() || Records.size() > MaxSnapshotRecords)
	{
		return std::unexpected(BoundMessageError::InvalidPayload);
	}

	xr_array<u8, MaxPayloadSize> Payload;
	PayloadWriter Writer(Payload);
	if (!Writer.WriteInteger(Header.SnapshotId) || !Writer.WriteInteger(Header.ServerTick) ||
		!Writer.WriteInteger(Header.ServerTimeMs) || !Writer.WriteInteger(Header.RequiredEventWatermark) ||
		!Writer.WriteInteger(static_cast<u16>(Records.size())))
	{
		return std::unexpected(BoundMessageError::InvalidPayload);
	}

	for (const auto& Record : Records)
	{
		if (!IsSupportedKind(Record.Kind) || Record.Payload.empty() || Record.Payload.size() > MaxPayloadSize ||
			!Writer.WriteInteger(static_cast<u8>(Record.Kind)) || !Writer.WriteInteger(static_cast<u16>(Record.Payload.size())) ||
			!Writer.WriteBytes(Record.Payload))
		{
			return std::unexpected(BoundMessageError::InvalidPayload);
		}
	}
	return Detail::EncodeBoundMessage(MessageType::SnapshotBatch, Header.Identity, Header.MessageSequence, Writer.Written(), Output);
}
}
