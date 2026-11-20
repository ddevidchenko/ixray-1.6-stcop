#include "CombatantBaseline.h"

namespace Multiplayer
{
static_assert(Wire::HumanCombatantRecordSize <= Wire::StalkerCombatantRecordSize);
static_assert(MaxCombatantBaselineBytes <= Wire::MaxBaselineBytes && MaxCombatants <= Wire::MaxBaselineRecords);

class CombatantBaselineSchema final : public IBaselineRecordSchema
{
public:
	bool Validate(BaselineRecordView Record) const noexcept override
	{
		switch (static_cast<Wire::SnapshotRecordKind>(Record.Kind))
		{
		case Wire::SnapshotRecordKind::HumanCombatant:
			return Record.Payload.size() == Wire::HumanCombatantRecordSize;
		case Wire::SnapshotRecordKind::StalkerCombatant:
			return Record.Payload.size() == Wire::StalkerCombatantRecordSize;
		}
		return false;
	}
};

std::expected<CombatantBaseline, BaselineRecordsError> CombatantBaseline::Decode(const StagedBaseline& Baseline,
	JoinIdentity Receiver, BaselineBoundary Selected) noexcept
{
	if (!Receiver.IsBound() || Baseline.Identity != Receiver)
	{
		return std::unexpected(BaselineRecordsError::IdentityMismatch);
	}
	if (Baseline.Manifest.GetBoundary() != Selected)
	{
		return std::unexpected(BaselineRecordsError::BaselineMismatch);
	}
	if (Baseline.Manifest.RecordCount > MaxCombatants)
	{
		return std::unexpected(BaselineRecordsError::InvalidRecord);
	}

	const CombatantBaselineSchema Schema;
	auto Stream = BaselineRecords::Validate(Baseline, Receiver, Selected.BaselineId, Schema);
	if (!Stream)
	{
		return std::unexpected(Stream.error());
	}

	CombatantBaseline Result(Receiver, Selected);
	for (u16 Index = 0; Index < Baseline.Manifest.RecordCount; ++Index)
	{
		const auto View = Stream->Next();
		if (!View)
		{
			return std::unexpected(BaselineRecordsError::MissingRecords);
		}
		const auto Record = Wire::DecodeCombatantSnapshot({static_cast<Wire::SnapshotRecordKind>(View->Kind), View->Payload});
		if (!Record || Wire::HasCombatantIdentityConflict(Wire::GetPublicCombatantState(*Record), Result.GetRecords()))
		{
			return std::unexpected(BaselineRecordsError::InvalidRecord);
		}
		Result.Records[Result.Count++] = *Record;
	}
	return Result;
}

std::expected<Wire::BaselineManifest, CombatantBaselineEncodeError> EncodeCombatantBaseline(BaselineBoundary Boundary,
	xr_span<const Wire::CombatantSnapshot> Records, xr_span<u8> Output) noexcept
{
	if (Records.size() > MaxCombatants)
	{
		return std::unexpected(CombatantBaselineEncodeError::InvalidRecord);
	}

	xr_array<u8, MaxCombatantBaselineBytes> Stream;
	Wire::PayloadWriter Writer(Stream);
	xr_array<u8, Wire::StalkerCombatantRecordSize> Body;
	for (std::size_t Index = 0; Index < Records.size(); ++Index)
	{
		const auto& Record = Records[Index];
		if (Wire::HasCombatantIdentityConflict(Wire::GetPublicCombatantState(Record), Records.first(Index)))
		{
			return std::unexpected(CombatantBaselineEncodeError::InvalidRecord);
		}
		const auto Encoded = std::visit([&Body](const auto& Snapshot) noexcept
		{
			return Wire::EncodeCombatantSnapshot(Snapshot, Body);
		}, Record);
		if (!Encoded || !Writer.WriteRecord({static_cast<u8>(Encoded->Kind), Encoded->Payload}))
		{
			return std::unexpected(CombatantBaselineEncodeError::InvalidRecord);
		}
	}

	const auto Bytes = Writer.Written();
	Wire::PayloadWriter OutputWriter(Output);
	if (!OutputWriter.WriteBytes(Bytes))
	{
		return std::unexpected(CombatantBaselineEncodeError::OutputTooSmall);
	}
	const auto ByteCount = static_cast<u32>(Bytes.size());
	const auto Chunks = static_cast<u16>(Wire::GetBaselineChunkCount(ByteCount));
	return Wire::BaselineManifest{Boundary.BaselineId, Boundary.CaptureTick, Boundary.EventWatermark, ByteCount,
		static_cast<u16>(Records.size()), Chunks};
}
}
