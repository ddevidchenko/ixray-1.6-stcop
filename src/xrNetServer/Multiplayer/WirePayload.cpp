#include "WirePayload.h"

#include <cstring>
#include <limits>

namespace Multiplayer::Wire
{
static_assert(sizeof(f32) == sizeof(u32) && std::numeric_limits<f32>::is_iec559 &&
	std::numeric_limits<f32>::digits == 24 && std::numeric_limits<f32>::max_exponent == 128);

static bool IsFiniteBinary32(u32 Bits) noexcept
{
	// Classify the wire exponent directly even when the compiler assumes finite arithmetic.
	constexpr u32 ExponentMask = 0x7f800000u;
	return (Bits & ExponentMask) != ExponentMask;
}

static bool IsValidUtf8(xr_string_view Text) noexcept
{
	while (!Text.empty())
	{
		const auto Lead = static_cast<u8>(Text.front());
		Text.remove_prefix(1);
		if (Lead <= 0x7f)
		{
			continue;
		}
		if (Lead < 0xc2 || Lead > 0xf4)
		{
			return false;
		}

		std::size_t TailSize;
		u32 CodePoint;
		u32 Minimum;
		if (Lead <= 0xdf)
		{
			TailSize = 1;
			CodePoint = Lead & 0x1fu;
			Minimum = 0x80;
		}
		else if (Lead <= 0xef)
		{
			TailSize = 2;
			CodePoint = Lead & 0x0fu;
			Minimum = 0x800;
		}
		else
		{
			TailSize = 3;
			CodePoint = Lead & 0x07u;
			Minimum = 0x10000;
		}
		if (Text.size() < TailSize)
		{
			return false;
		}
		for (const char Character : Text.substr(0, TailSize))
		{
			const auto Byte = static_cast<u8>(Character);
			if ((Byte & 0xc0u) != 0x80u)
			{
				return false;
			}
			CodePoint = (CodePoint << 6) | (Byte & 0x3fu);
		}
		if (CodePoint < Minimum || CodePoint > 0x10ffff || (CodePoint >= 0xd800 && CodePoint <= 0xdfff))
		{
			return false;
		}
		Text.remove_prefix(TailSize);
	}
	return true;
}

std::expected<xr_span<const u8>, PayloadError> PayloadReader::ReadBytes(std::size_t Count) noexcept
{
	if (Count > Unread.size())
	{
		return std::unexpected(PayloadError::Truncated);
	}
	const auto Bytes = Unread.first(Count);
	Unread = Unread.subspan(Count);
	return Bytes;
}

std::expected<PayloadRecordView, PayloadRecordError> PayloadReader::ReadRecord() noexcept
{
	auto Cursor = *this;
	const auto Kind = Cursor.ReadInteger<u8>();
	const auto Size = Cursor.ReadInteger<u16>();
	if (!Kind || !Size)
	{
		return std::unexpected(PayloadRecordError::TruncatedHeader);
	}
	const auto Payload = Cursor.ReadBytes(*Size);
	if (!Payload)
	{
		return std::unexpected(PayloadRecordError::TruncatedPayload);
	}
	*this = Cursor;
	return PayloadRecordView{*Kind, *Payload};
}

std::expected<f32, PayloadError> PayloadReader::ReadFiniteFloat() noexcept
{
	auto Cursor = *this;
	const auto Bits = Cursor.ReadInteger<u32>();
	if (!Bits)
	{
		return std::unexpected(Bits.error());
	}
	if (!IsFiniteBinary32(*Bits))
	{
		return std::unexpected(PayloadError::NonFiniteFloat);
	}
	Unread = Cursor.Unread;
	return std::bit_cast<f32>(*Bits);
}

std::expected<xr_string_view, PayloadError> PayloadReader::ReadUtf8(std::size_t MaxBytes) noexcept
{
	auto Cursor = *this;
	const auto Length = Cursor.ReadInteger<u16>();
	if (!Length)
	{
		return std::unexpected(Length.error());
	}
	if (*Length > MaxBytes)
	{
		return std::unexpected(PayloadError::LengthOutOfRange);
	}
	if (*Length > Cursor.Unread.size())
	{
		return std::unexpected(PayloadError::Truncated);
	}
	const xr_string_view Text(reinterpret_cast<const char*>(Cursor.Unread.data()), *Length);
	if (!IsValidUtf8(Text))
	{
		return std::unexpected(PayloadError::InvalidUtf8);
	}
	Unread = Cursor.Unread.subspan(*Length);
	return Text;
}

std::expected<void, PayloadError> PayloadWriter::WriteBytes(xr_span<const u8> Bytes) noexcept
{
	if (Bytes.size() > Output.size() - Position)
	{
		return std::unexpected(PayloadError::OutputTooSmall);
	}
	if (!Bytes.empty())
	{
		std::memmove(Output.data() + Position, Bytes.data(), Bytes.size());
		Position += Bytes.size();
	}
	return {};
}

std::expected<void, PayloadError> PayloadWriter::WriteRecord(PayloadRecordView Record) noexcept
{
	if (Record.Payload.size() > std::numeric_limits<u16>::max())
	{
		return std::unexpected(PayloadError::LengthOutOfRange);
	}
	const std::size_t EncodedSize = PayloadRecordHeaderSize + Record.Payload.size();
	if (EncodedSize > Output.size() - Position)
	{
		return std::unexpected(PayloadError::OutputTooSmall);
	}

	const auto Field = Output.subspan(Position, EncodedSize);
	if (!Record.Payload.empty())
	{
		std::memmove(Field.data() + PayloadRecordHeaderSize, Record.Payload.data(), Record.Payload.size());
	}
	Field[0] = Record.Kind;
	Detail::WriteLittleEndian(Field.subspan<sizeof(u8), sizeof(u16)>(), static_cast<u16>(Record.Payload.size()));
	Position += EncodedSize;
	return {};
}

std::expected<void, PayloadError> PayloadWriter::WriteFiniteFloat(f32 Value) noexcept
{
	const auto Bits = std::bit_cast<u32>(Value);
	if (!IsFiniteBinary32(Bits))
	{
		return std::unexpected(PayloadError::NonFiniteFloat);
	}
	return WriteInteger(Bits);
}

std::expected<void, PayloadError> PayloadWriter::WriteUtf8(xr_string_view Value, std::size_t MaxBytes) noexcept
{
	if (Value.size() > MaxBytes || Value.size() > std::numeric_limits<u16>::max())
	{
		return std::unexpected(PayloadError::LengthOutOfRange);
	}
	const std::size_t EncodedSize = sizeof(u16) + Value.size();
	if (EncodedSize > Output.size() - Position)
	{
		return std::unexpected(PayloadError::OutputTooSmall);
	}
	if (!IsValidUtf8(Value))
	{
		return std::unexpected(PayloadError::InvalidUtf8);
	}
	const auto Field = Output.subspan(Position, EncodedSize);
	if (!Value.empty())
	{
		std::memmove(Field.data() + sizeof(u16), Value.data(), Value.size());
	}
	Detail::WriteLittleEndian(Field.first<sizeof(u16)>(), static_cast<u16>(Value.size()));
	Position += EncodedSize;
	return {};
}
}
