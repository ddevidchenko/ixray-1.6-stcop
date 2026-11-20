#include "BaselineRecords.h"

namespace Multiplayer
{
static std::expected<BaselineRecordView, BaselineRecordsError> ReadRecord(Wire::PayloadReader& Reader) noexcept
{
	// Commit the cursor advance only after the entire record fits.
	auto Candidate = Reader;
	const auto Kind = Candidate.ReadInteger<u8>();
	const auto Size = Candidate.ReadInteger<u16>();
	if (!Kind || !Size)
	{
		return std::unexpected(BaselineRecordsError::TruncatedHeader);
	}
	const auto Payload = Candidate.ReadBytes(*Size);
	if (!Payload)
	{
		return std::unexpected(BaselineRecordsError::TruncatedPayload);
	}
	Reader = Candidate;
	return BaselineRecordView{*Kind, *Payload};
}

BaselineRecords::BaselineRecords(const StagedBaseline& Baseline) noexcept
	: Identity(Baseline.Identity), Manifest(Baseline.Manifest), Reader(Baseline.Data)
{
}

std::expected<BaselineRecords, BaselineRecordsError> BaselineRecords::Validate(const StagedBaseline& Baseline,
	JoinIdentity CurrentIdentity, const IBaselineRecordSchema& Schema) noexcept
{
	if (!CurrentIdentity.IsBound() || Baseline.Identity != CurrentIdentity)
	{
		return std::unexpected(BaselineRecordsError::IdentityMismatch);
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
		const auto Record = ReadRecord(Validation);
		if (!Record)
		{
			return std::unexpected(Record.error());
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
	const auto Record = ReadRecord(Reader);
	if (!Record)
	{
		return std::nullopt;
	}
	return *Record;
}
}
