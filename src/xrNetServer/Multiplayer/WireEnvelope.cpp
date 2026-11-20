#include "WireEnvelope.h"
#include "WireEndian.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace Multiplayer::Wire
{
constexpr std::array<u8, 4> Magic{0x49, 0x58, 0x4d, 0x50};
constexpr u16 ProtocolVersion = 1;
constexpr std::size_t VersionOffset = Magic.size();
constexpr std::size_t TypeOffset = VersionOffset + sizeof(u16);
constexpr std::size_t FlagsOffset = TypeOffset + sizeof(u8);
constexpr std::size_t LengthOffset = FlagsOffset + sizeof(u8);
constexpr std::size_t SessionOffset = LengthOffset + sizeof(u16);
constexpr std::size_t WorldOffset = SessionOffset + sizeof(u64);
constexpr std::size_t ConnectionOffset = WorldOffset + sizeof(u32);
constexpr std::size_t SequenceOffset = ConnectionOffset + sizeof(u32);

static_assert(SequenceOffset + sizeof(u32) == EnvelopeHeaderSize);
static_assert(MaxPayloadSize <= std::numeric_limits<u16>::max());

static bool IsSupportedType(MessageType Type) noexcept
{
	switch (Type)
	{
	case MessageType::ClientHello:
	case MessageType::ServerWelcome:
	case MessageType::AdmissionReject:
		return true;
	}
	return false;
}

std::expected<EnvelopeView, EnvelopeError> DecodeEnvelope(std::span<const u8> Bytes) noexcept
{
	if (Bytes.size() < EnvelopeHeaderSize)
	{
		return std::unexpected(EnvelopeError::ShortHeader);
	}
	if (Bytes.size() > MaxMessageSize)
	{
		return std::unexpected(EnvelopeError::MessageTooLarge);
	}

	const auto Header = Bytes.first<EnvelopeHeaderSize>();
	if (!std::ranges::equal(Header.first<Magic.size()>(), Magic))
	{
		return std::unexpected(EnvelopeError::InvalidMagic);
	}
	if (Detail::ReadLittleEndian<u16>(Header.subspan<VersionOffset, sizeof(u16)>()) != ProtocolVersion)
	{
		return std::unexpected(EnvelopeError::UnsupportedVersion);
	}
	const auto Type = static_cast<MessageType>(Header[TypeOffset]);
	if (!IsSupportedType(Type))
	{
		return std::unexpected(EnvelopeError::UnknownMessageType);
	}
	if (Header[FlagsOffset] != 0)
	{
		return std::unexpected(EnvelopeError::UnsupportedFlags);
	}
	const auto Payload = Bytes.subspan<EnvelopeHeaderSize>();
	if (Detail::ReadLittleEndian<u16>(Header.subspan<LengthOffset, sizeof(u16)>()) != Payload.size())
	{
		return std::unexpected(EnvelopeError::PayloadLengthMismatch);
	}

	return EnvelopeView{
		{
			Type,
			Detail::ReadLittleEndian<u64>(Header.subspan<SessionOffset, sizeof(u64)>()),
			Detail::ReadLittleEndian<u32>(Header.subspan<WorldOffset, sizeof(u32)>()),
			Detail::ReadLittleEndian<u32>(Header.subspan<ConnectionOffset, sizeof(u32)>()),
			Detail::ReadLittleEndian<u32>(Header.subspan<SequenceOffset, sizeof(u32)>()),
		},
		Payload,
	};
}

std::expected<std::size_t, EnvelopeError> EncodeEnvelope(EnvelopeHeader Header, std::span<const u8> Payload, std::span<u8> Output) noexcept
{
	if (!IsSupportedType(Header.Type))
	{
		return std::unexpected(EnvelopeError::UnknownMessageType);
	}
	if (Payload.size() > MaxPayloadSize)
	{
		return std::unexpected(EnvelopeError::MessageTooLarge);
	}
	const std::size_t MessageSize = EnvelopeHeaderSize + Payload.size();
	if (Output.size() < MessageSize)
	{
		return std::unexpected(EnvelopeError::OutputTooSmall);
	}

	// The caller may be prepending a header to a payload already in the output buffer.
	if (!Payload.empty())
	{
		std::memmove(Output.data() + EnvelopeHeaderSize, Payload.data(), Payload.size());
	}
	const auto Bytes = Output.first<EnvelopeHeaderSize>();
	std::ranges::copy(Magic, Bytes.begin());
	Detail::WriteLittleEndian(Bytes.subspan<VersionOffset, sizeof(u16)>(), ProtocolVersion);
	Bytes[TypeOffset] = static_cast<u8>(Header.Type);
	Bytes[FlagsOffset] = 0;
	Detail::WriteLittleEndian(Bytes.subspan<LengthOffset, sizeof(u16)>(), static_cast<u16>(Payload.size()));
	Detail::WriteLittleEndian(Bytes.subspan<SessionOffset, sizeof(u64)>(), Header.ServerSession);
	Detail::WriteLittleEndian(Bytes.subspan<WorldOffset, sizeof(u32)>(), Header.WorldEpoch);
	Detail::WriteLittleEndian(Bytes.subspan<ConnectionOffset, sizeof(u32)>(), Header.ConnectionEpoch);
	Detail::WriteLittleEndian(Bytes.subspan<SequenceOffset, sizeof(u32)>(), Header.MessageSequence);
	return MessageSize;
}
}
