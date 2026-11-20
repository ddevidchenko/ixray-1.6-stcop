#include "ServerWelcome.h"
#include "WireEnvelope.h"
#include "WirePayload.h"

#include <algorithm>

namespace Multiplayer::Wire
{
constexpr std::size_t MaxMapIdBytes = 64;
constexpr std::size_t IdBytes = CompatibilityId{}.size();
constexpr std::size_t MaxWelcomePayload = 2 * IdBytes + sizeof(u16) + MaxMapIdBytes + 8 * sizeof(u32);
static_assert(MaxWelcomePayload <= MaxPayloadSize);

static bool HasAssignedConnection(u64 ServerSession, u32 ConnectionEpoch) noexcept
{
	return ServerSession != 0 && ConnectionEpoch != 0;
}

static bool HasPositiveRates(const WelcomeRates& Rates) noexcept
{
	return Rates.GameplayMilliHz != 0 && Rates.PhysicsMilliHz != 0 && Rates.SnapshotMilliHz != 0 && Rates.InputMilliHz != 0;
}

static bool IsMapIdBounded(xr_string_view MapId) noexcept
{
	return !MapId.empty() && MapId.size() <= MaxMapIdBytes && !MapId.contains('\0');
}

std::expected<ServerWelcome, ServerWelcomeError> DecodeServerWelcome(xr_span<const u8> Bytes) noexcept
{
	const auto Frame = DecodeEnvelope(Bytes);
	if (!Frame)
	{
		return std::unexpected(ServerWelcomeError::InvalidEnvelope);
	}
	const auto& Header = Frame->Header;
	if (Header.Type != MessageType::ServerWelcome)
	{
		return std::unexpected(ServerWelcomeError::WrongMessageType);
	}
	if (!HasAssignedConnection(Header.ServerSession, Header.ConnectionEpoch))
	{
		return std::unexpected(ServerWelcomeError::UnboundWelcome);
	}

	PayloadReader Reader(Frame->Payload);
	const auto Ids = Reader.ReadBytes(2 * IdBytes);
	if (!Ids)
	{
		return std::unexpected(ServerWelcomeError::TruncatedPayload);
	}
	const auto MapId = Reader.ReadUtf8(MaxMapIdBytes);
	if (!MapId)
	{
		return std::unexpected(MapId.error() == PayloadError::Truncated ? ServerWelcomeError::TruncatedPayload : ServerWelcomeError::InvalidMapId);
	}
	if (!IsMapIdBounded(*MapId))
	{
		return std::unexpected(ServerWelcomeError::InvalidMapId);
	}

	const auto Gameplay = Reader.ReadInteger<u32>();
	const auto Physics = Reader.ReadInteger<u32>();
	const auto Snapshot = Reader.ReadInteger<u32>();
	const auto Input = Reader.ReadInteger<u32>();
	const auto RespawnDelay = Reader.ReadInteger<u32>();
	const auto SpawnProtection = Reader.ReadInteger<u32>();
	const auto TimeLimit = Reader.ReadInteger<u32>();
	const auto FragLimit = Reader.ReadInteger<u32>();
	if (!Gameplay || !Physics || !Snapshot || !Input || !RespawnDelay || !SpawnProtection || !TimeLimit || !FragLimit)
	{
		return std::unexpected(ServerWelcomeError::TruncatedPayload);
	}
	const WelcomeRates Rates{*Gameplay, *Physics, *Snapshot, *Input};
	if (!HasPositiveRates(Rates))
	{
		return std::unexpected(ServerWelcomeError::InvalidRates);
	}
	if (Reader.RemainingBytes() != 0)
	{
		return std::unexpected(ServerWelcomeError::TrailingPayload);
	}

	ServerWelcome Welcome;
	Welcome.ServerSession = Header.ServerSession;
	Welcome.WorldEpoch = Header.WorldEpoch;
	Welcome.ConnectionEpoch = Header.ConnectionEpoch;
	Welcome.MessageSequence = Header.MessageSequence;
	std::ranges::copy(Ids->first<IdBytes>(), Welcome.ContentId.begin());
	std::ranges::copy(Ids->subspan<IdBytes, IdBytes>(), Welcome.RulesId.begin());
	Welcome.MapId = *MapId;
	Welcome.Rates = Rates;
	Welcome.Rules = {*RespawnDelay, *SpawnProtection, *TimeLimit, *FragLimit};
	return Welcome;
}

std::expected<std::size_t, ServerWelcomeError> EncodeServerWelcome(const ServerWelcome& Welcome, xr_span<u8> Output) noexcept
{
	if (!HasAssignedConnection(Welcome.ServerSession, Welcome.ConnectionEpoch))
	{
		return std::unexpected(ServerWelcomeError::UnboundWelcome);
	}
	if (!IsMapIdBounded(Welcome.MapId))
	{
		return std::unexpected(ServerWelcomeError::InvalidMapId);
	}
	if (!HasPositiveRates(Welcome.Rates))
	{
		return std::unexpected(ServerWelcomeError::InvalidRates);
	}

	xr_array<u8, MaxWelcomePayload> Payload;
	PayloadWriter Writer(Payload);
	if (!Writer.WriteBytes(Welcome.ContentId) || !Writer.WriteBytes(Welcome.RulesId))
	{
		return std::unexpected(ServerWelcomeError::OutputTooSmall);
	}
	if (!Writer.WriteUtf8(Welcome.MapId, MaxMapIdBytes))
	{
		return std::unexpected(ServerWelcomeError::InvalidMapId);
	}
	const auto& Rates = Welcome.Rates;
	const auto& Rules = Welcome.Rules;
	if (!Writer.WriteInteger(Rates.GameplayMilliHz) || !Writer.WriteInteger(Rates.PhysicsMilliHz) ||
		!Writer.WriteInteger(Rates.SnapshotMilliHz) || !Writer.WriteInteger(Rates.InputMilliHz) ||
		!Writer.WriteInteger(Rules.RespawnDelayMs) || !Writer.WriteInteger(Rules.SpawnProtectionMs) ||
		!Writer.WriteInteger(Rules.RoundTimeLimitMs) || !Writer.WriteInteger(Rules.RoundFragLimit))
	{
		return std::unexpected(ServerWelcomeError::OutputTooSmall);
	}

	const EnvelopeHeader Header{MessageType::ServerWelcome, Welcome.ServerSession, Welcome.WorldEpoch, Welcome.ConnectionEpoch, Welcome.MessageSequence};
	const auto Size = EncodeEnvelope(Header, Writer.Written(), Output);
	if (!Size)
	{
		return std::unexpected(ServerWelcomeError::OutputTooSmall);
	}
	return *Size;
}
}
