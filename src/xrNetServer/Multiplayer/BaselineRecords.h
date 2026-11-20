#pragma once

#include "BaselineStaging.h"
#include "WirePayload.h"

#include <optional>

namespace Multiplayer
{
struct BaselineRecordView
{
	u8 Kind;
	xr_span<const u8> Payload;
};

class IBaselineRecordSchema
{
public:
	virtual ~IBaselineRecordSchema() = default;
	// Validate the kind and payload without publishing or retaining the record.
	virtual bool Validate(BaselineRecordView Record) const noexcept = 0;
};

enum class BaselineRecordsError
{
	IdentityMismatch,
	InvalidManifest,
	ByteCountMismatch,
	MissingRecords,
	TruncatedHeader,
	TruncatedPayload,
	InvalidRecord,
	TrailingData,
	BaselineMismatch,
};

// The cursor and its records borrow the immutable staged data for the same lifetime.
class BaselineRecords
{
public:
	// Returns a cursor only after every record frame and payload has been validated.
	[[nodiscard]] static std::expected<BaselineRecords, BaselineRecordsError> Validate(const StagedBaseline& Baseline,
		JoinIdentity CurrentIdentity, u32 ExpectedBaselineId, const IBaselineRecordSchema& Schema) noexcept;

	[[nodiscard]] const JoinIdentity& GetIdentity() const noexcept { return Identity; }
	[[nodiscard]] const Wire::BaselineManifest& GetManifest() const noexcept { return Manifest; }
	[[nodiscard]] std::optional<BaselineRecordView> Next() noexcept;

private:
	explicit BaselineRecords(const StagedBaseline& Baseline) noexcept;

	JoinIdentity Identity;
	Wire::BaselineManifest Manifest;
	Wire::PayloadReader Reader;
};
}
