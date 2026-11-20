#include "BaselineRecords.h"

namespace Multiplayer
{
BaselineRecords::BaselineRecords(const StagedBaseline& Baseline) noexcept
	: Identity(Baseline.Identity), Manifest(Baseline.Manifest), Reader(Baseline.Data)
{
}

std::expected<BaselineRecords, BaselineRecordsError> BaselineRecords::Validate(const StagedBaseline& Baseline,
	JoinIdentity CurrentIdentity, u32 ExpectedBaselineId, const IBaselineRecordSchema& Schema) noexcept
{
	if (!CurrentIdentity.IsBound() || Baseline.Identity != CurrentIdentity)
	{
		return std::unexpected(BaselineRecordsError::IdentityMismatch);
	}
	if (Baseline.Manifest.BaselineId != ExpectedBaselineId)
	{
		return std::unexpected(BaselineRecordsError::BaselineMismatch);
	}
	if (!Wire::IsValidBaselineManifest(Baseline.Manifest))
	{
		return std::unexpected(BaselineRecordsError::InvalidManifest);
	}
	if (Baseline.Manifest.ByteCount != Baseline.Data.size())
	{
		return std::unexpected(BaselineRecordsError::ByteCountMismatch);
	}

	Wire::PayloadReader Validation(Baseline.Data);
	for (u16 Index = 0; Index < Baseline.Manifest.RecordCount; ++Index)
	{
		if (Validation.RemainingBytes() == 0)
		{
			return std::unexpected(BaselineRecordsError::MissingRecords);
		}
		const auto Record = Validation.ReadRecord();
		if (!Record)
		{
			return std::unexpected(Record.error() == Wire::PayloadRecordError::TruncatedHeader ?
				BaselineRecordsError::TruncatedHeader : BaselineRecordsError::TruncatedPayload);
		}
		if (!Schema.Validate(*Record))
		{
			return std::unexpected(BaselineRecordsError::InvalidRecord);
		}
	}
	if (Validation.RemainingBytes() != 0)
	{
		return std::unexpected(BaselineRecordsError::TrailingData);
	}
	return BaselineRecords(Baseline);
}

std::optional<BaselineRecordView> BaselineRecords::Next() noexcept
{
	if (Reader.RemainingBytes() == 0)
	{
		return std::nullopt;
	}
	const auto Record = Reader.ReadRecord();
	if (!Record)
	{
		return std::nullopt;
	}
	return *Record;
}
}
