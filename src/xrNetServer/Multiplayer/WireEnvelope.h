#pragma once

#include "../../xrCore/ScalarTypes.h"

#include <cstddef>
#include <expected>
#include <span>

namespace Multiplayer::Wire
{
inline constexpr std::size_t EnvelopeHeaderSize = 30;
inline constexpr std::size_t MaxMessageSize = 1100;
inline constexpr std::size_t MaxPayloadSize = MaxMessageSize - EnvelopeHeaderSize;

enum class MessageType : u8
{
	ClientHello = 0x01,
	ServerWelcome = 0x02,
	AdmissionReject = 0x03,
	ClientMapLoaded = 0x04,
	ReadyAcknowledgement = 0x05,
	BaselineBegin = 0x06,
	BaselineChunk = 0x07,
	BaselineEnd = 0x08,
	ServerJoinBarrier = 0x09,
};

enum class EnvelopeError
{
	ShortHeader,
	MessageTooLarge,
	InvalidMagic,
	UnsupportedVersion,
	UnknownMessageType,
	UnsupportedFlags,
	PayloadLengthMismatch,
	OutputTooSmall,
};

struct EnvelopeHeader
{
	MessageType Type;
	u64 ServerSession;
	u32 WorldEpoch;
	u32 ConnectionEpoch;
	u32 MessageSequence;

	[[nodiscard]] constexpr bool HasUnboundIdentity() const noexcept
	{
		return ServerSession == 0 && WorldEpoch == 0 && ConnectionEpoch == 0;
	}

	bool operator==(const EnvelopeHeader&) const = default;
};

struct EnvelopeView
{
	EnvelopeHeader Header;
	std::span<const u8> Payload;
};

// Keep the message buffer alive and stable while using the returned payload view.
[[nodiscard]] std::expected<EnvelopeView, EnvelopeError> DecodeEnvelope(std::span<const u8> Bytes) noexcept;

// Returns the number of bytes written. The payload may overlap the output.
// The output is unchanged on validation failure.
[[nodiscard]] std::expected<std::size_t, EnvelopeError> EncodeEnvelope(EnvelopeHeader Header, std::span<const u8> Payload, std::span<u8> Output) noexcept;
}
