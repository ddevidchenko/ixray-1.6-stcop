#pragma once

#include "WireEndian.h"

#include <bit>
#include <expected>
#include <type_traits>

namespace Multiplayer::Wire
{
enum class PayloadError
{
	Truncated,
	OutputTooSmall,
	LengthOutOfRange,
	InvalidUtf8,
	NonFiniteFloat,
};

struct PayloadRecordView
{
	u8 Kind;
	xr_span<const u8> Payload;
};

enum class PayloadRecordError
{
	TruncatedHeader,
	TruncatedPayload,
};

template <class Value>
concept WireInteger = std::same_as<Value, u8> || std::same_as<Value, u16> || std::same_as<Value, u32> || std::same_as<Value, u64> ||
	std::same_as<Value, s8> || std::same_as<Value, s16>;

// Reads borrow the payload buffer. Each failed read leaves the cursor in place.
class PayloadReader
{
public:
	explicit PayloadReader(xr_span<const u8> Payload) noexcept : Unread(Payload) {}

	template <WireInteger Value>
	[[nodiscard]] std::expected<Value, PayloadError> ReadInteger() noexcept
	{
		if (Unread.size() < sizeof(Value))
		{
			return std::unexpected(PayloadError::Truncated);
		}
		const auto Bits = Detail::ReadLittleEndian<std::make_unsigned_t<Value>>(Unread.first<sizeof(Value)>());
		Unread = Unread.subspan<sizeof(Value)>();
		return std::bit_cast<Value>(Bits);
	}

	[[nodiscard]] std::expected<xr_span<const u8>, PayloadError> ReadBytes(std::size_t Count) noexcept;
	[[nodiscard]] std::expected<PayloadRecordView, PayloadRecordError> ReadRecord() noexcept;
	[[nodiscard]] std::expected<f32, PayloadError> ReadFiniteFloat() noexcept;
	[[nodiscard]] std::expected<xr_string_view, PayloadError> ReadUtf8(std::size_t MaxBytes) noexcept;
	[[nodiscard]] std::size_t RemainingBytes() const noexcept { return Unread.size(); }

private:
	xr_span<const u8> Unread;
};

// A failed write preserves the cursor and all bytes in the caller's buffer.
class PayloadWriter
{
public:
	explicit PayloadWriter(xr_span<u8> Buffer) noexcept : Output(Buffer) {}

	template <WireInteger Value>
	[[nodiscard]] std::expected<void, PayloadError> WriteInteger(Value Number) noexcept
	{
		if (Output.size() - Position < sizeof(Value))
		{
			return std::unexpected(PayloadError::OutputTooSmall);
		}
		Detail::WriteLittleEndian(Output.subspan(Position).first<sizeof(Value)>(), std::bit_cast<std::make_unsigned_t<Value>>(Number));
		Position += sizeof(Value);
		return {};
	}

	[[nodiscard]] std::expected<void, PayloadError> WriteBytes(xr_span<const u8> Bytes) noexcept;
	[[nodiscard]] std::expected<void, PayloadError> WriteFiniteFloat(f32 Value) noexcept;
	[[nodiscard]] std::expected<void, PayloadError> WriteUtf8(xr_string_view Value, std::size_t MaxBytes) noexcept;
	[[nodiscard]] xr_span<const u8> Written() const noexcept { return Output.first(Position); }

private:
	xr_span<u8> Output;
	std::size_t Position = 0;
};
}
