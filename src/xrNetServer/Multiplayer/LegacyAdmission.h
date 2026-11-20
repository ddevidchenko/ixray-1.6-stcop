#pragma once

#include "../../xrCore/ScalarTypes.h"
#include "../../xrCore/_stl_extensions_nonalloc.h"

#include <algorithm>
#include <optional>

namespace Multiplayer
{
enum class LegacyAdmissionDecision
{
	Accept,
	Duplicate,
	SessionFull,
	Banned,
	AddressDenied,
	PasswordRejected,
};

struct LegacyAdmissionChecks
{
	std::size_t CurrentConnections = 0;
	std::size_t MaxConnections = 0;
	bool Duplicate = false;
	bool Banned = false;
	bool LocalHostCandidate = false;
	bool AddressAllowed = false;
	std::optional<xr_string_view> PresentedPassword;
	xr_string_view ServerPassword;
};

[[nodiscard]] inline LegacyAdmissionDecision CheckLegacyAdmission(const LegacyAdmissionChecks& Checks) noexcept
{
	if (Checks.Duplicate)
	{
		return LegacyAdmissionDecision::Duplicate;
	}
	if (Checks.CurrentConnections >= Checks.MaxConnections)
	{
		return LegacyAdmissionDecision::SessionFull;
	}
	if (Checks.Banned)
	{
		return LegacyAdmissionDecision::Banned;
	}
	if (Checks.LocalHostCandidate)
	{
		return LegacyAdmissionDecision::Accept;
	}
	if (!Checks.AddressAllowed)
	{
		return LegacyAdmissionDecision::AddressDenied;
	}
	if (Checks.PresentedPassword && *Checks.PresentedPassword != Checks.ServerPassword)
	{
		return LegacyAdmissionDecision::PasswordRejected;
	}
	return LegacyAdmissionDecision::Accept;
}

// Incoming handles are collected during a native callback batch and drained afterwards.
class LegacyAdmissionBatch
{
public:
	explicit LegacyAdmissionBatch(xr_span<u32> Storage) noexcept : Storage(Storage) {}

	[[nodiscard]] bool TryRecord(u32 Handle) noexcept
	{
		if (Handle == 0)
		{
			return false;
		}
		if (std::ranges::find(GetRequests(), Handle) != GetRequests().end())
		{
			return true;
		}
		if (Count == Storage.size())
		{
			return false;
		}
		Storage[Count++] = Handle;
		return true;
	}

	[[nodiscard]] xr_span<const u32> GetRequests() const noexcept { return Storage.first(Count); }

private:
	const xr_span<u32> Storage;
	std::size_t Count = 0;
};
}
