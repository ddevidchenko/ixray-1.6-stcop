#include "ClientHello.h"
#include "WireEnvelope.h"
#include "WirePayload.h"

#include <algorithm>

namespace Multiplayer::Wire
{
constexpr std::size_t IdBytes = CompatibilityId{}.size();
constexpr std::size_t HelloIdentityBytes = 3 * IdBytes;
constexpr std::size_t MaxDisplayNameBytes = 32;
constexpr std::size_t MaxCredentialBytes = 128;
constexpr std::size_t MaxHelloPayload = HelloIdentityBytes + 2 * sizeof(u16) + MaxDisplayNameBytes + MaxCredentialBytes;
static_assert(MaxHelloPayload <= MaxPayloadSize);

std::expected<ClientHello, ClientHelloError> DecodeClientHello(xr_span<const u8> Bytes) noexcept
{
	const auto Frame = DecodeEnvelope(Bytes);
	if (!Frame)
	{
		return std::unexpected(Frame.error() == EnvelopeError::UnsupportedVersion ? ClientHelloError::UnsupportedVersion : ClientHelloError::InvalidEnvelope);
	}
	const auto& Header = Frame->Header;
	if (Header.Type != MessageType::ClientHello)
	{
		return std::unexpected(ClientHelloError::WrongMessageType);
	}
	if (!Header.HasUnboundIdentity())
	{
		return std::unexpected(ClientHelloError::BoundHello);
	}

	PayloadReader Reader(Frame->Payload);
	const auto Identity = Reader.ReadBytes(HelloIdentityBytes);
	if (!Identity)
	{
		return std::unexpected(ClientHelloError::TruncatedPayload);
	}
	const auto Name = Reader.ReadUtf8(MaxDisplayNameBytes);
	if (!Name)
	{
		return std::unexpected(Name.error() == PayloadError::Truncated ? ClientHelloError::TruncatedPayload : ClientHelloError::InvalidDisplayName);
	}
	if (Name->contains('\0'))
	{
		return std::unexpected(ClientHelloError::InvalidDisplayName);
	}
	const auto CredentialLength = Reader.ReadInteger<u16>();
	if (!CredentialLength)
	{
		return std::unexpected(ClientHelloError::TruncatedPayload);
	}
	if (*CredentialLength > MaxCredentialBytes)
	{
		return std::unexpected(ClientHelloError::CredentialsTooLong);
	}
	const auto Credentials = Reader.ReadBytes(*CredentialLength);
	if (!Credentials)
	{
		return std::unexpected(ClientHelloError::TruncatedPayload);
	}
	if (Reader.RemainingBytes() != 0)
	{
		return std::unexpected(ClientHelloError::TrailingPayload);
	}

	ClientHello Hello;
	Hello.MessageSequence = Header.MessageSequence;
	std::ranges::copy(Identity->first<IdBytes>(), Hello.Build.begin());
	std::ranges::copy(Identity->subspan<IdBytes, IdBytes>(), Hello.Content.begin());
	std::ranges::copy(Identity->subspan<2 * IdBytes, IdBytes>(), Hello.Rules.begin());
	Hello.DisplayName = *Name;
	Hello.Credentials = *Credentials;
	return Hello;
}

std::expected<std::size_t, ClientHelloError> EncodeClientHello(const ClientHello& Hello, xr_span<u8> Output) noexcept
{
	if (Hello.DisplayName.size() > MaxDisplayNameBytes || Hello.DisplayName.contains('\0'))
	{
		return std::unexpected(ClientHelloError::InvalidDisplayName);
	}
	if (Hello.Credentials.size() > MaxCredentialBytes)
	{
		return std::unexpected(ClientHelloError::CredentialsTooLong);
	}

	// Validate and stage the fields before writing any part of the caller's message.
	xr_array<u8, MaxHelloPayload> Payload;
	PayloadWriter Writer(Payload);
	if (!Writer.WriteBytes(Hello.Build) || !Writer.WriteBytes(Hello.Content) || !Writer.WriteBytes(Hello.Rules))
	{
		return std::unexpected(ClientHelloError::OutputTooSmall);
	}
	if (!Writer.WriteUtf8(Hello.DisplayName, MaxDisplayNameBytes))
	{
		return std::unexpected(ClientHelloError::InvalidDisplayName);
	}
	if (!Writer.WriteInteger(static_cast<u16>(Hello.Credentials.size())) || !Writer.WriteBytes(Hello.Credentials))
	{
		return std::unexpected(ClientHelloError::OutputTooSmall);
	}
	const auto Size = EncodeEnvelope({MessageType::ClientHello, 0, 0, 0, Hello.MessageSequence}, Writer.Written(), Output);
	if (!Size)
	{
		return std::unexpected(ClientHelloError::OutputTooSmall);
	}
	return *Size;
}
}
