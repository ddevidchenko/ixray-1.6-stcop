#include "../../../xrCore/compression/lzo/LzoBlock.h"
#include "../../../xrNetServer/Multiplayer/LegacyCompression.h"

#include <algorithm>
#include <cstdio>
#include <limits>

namespace
{
using namespace Multiplayer::Legacy;

constexpr u8 Sentinel = 0x5a;
constexpr xr_array<u8, 68> MergedBody = []
{
	xr_array<u8, 68> Body{};
	Body[0] = 32;
	std::fill_n(Body.begin() + 2, 32, 'A');
	Body[34] = 32;
	std::fill_n(Body.begin() + 36, 32, 'B');
	return Body;
}();

// Literal frame from LZO 2.10 with the checksum calculated independently over its 16 encoded bytes.
constexpr xr_array<u8, 24> PackedDatagram = {
	0xe1, 0x44, 0x00, 0xc1, 0xd0, 0x25, 0x72, 0xd1,
	0x14, 0x20, 0x00, 0x41, 0x3d, 0x03, 0x00, 0x20, 0x00, 0x42, 0x3d, 0x00, 0x00, 0x11, 0x00, 0x00
};

constexpr auto PackedBody = xr_span(PackedDatagram).subspan<3>();
constexpr auto LzoBytes = xr_span(PackedDatagram).subspan<8>();
constexpr auto NoCodec = [](xr_span<const u8>, xr_span<u8>) noexcept -> std::size_t { return 0; };
constexpr auto DecodeLzo = [](xr_span<const u8> Input, xr_span<u8> Output) noexcept
{
	return Lzo::DecompressBlock(Input, Output);
};

class TestLzoWork
{
public:
	lzo_align_t Storage[(LZO1X_999_MEM_COMPRESS + sizeof(lzo_align_t) - 1) / sizeof(lzo_align_t)];
};

bool CheckGoldenChecksumAndRecords()
{
	constexpr xr_array<u8, 9> CheckValue = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
	if (PackedChecksum(CheckValue) != 0xcbf43926u || PackedChecksum(LzoBytes) != 0xd17225d0u)
	{
		return false;
	}

	const auto Envelope = ReadEnvelope(PackedDatagram);
	if (!Envelope || Envelope->Encoding != BodyEncoding::Compressed || Envelope->DecodedSize != MergedBody.size())
	{
		return false;
	}

	xr_array<u8, MergedBody.size()> Decoded{};
	if (DecodeBody(PackedBody, Decoded, BodyFraming::Checksummed, DecodeLzo) != Decoded.size() || Decoded != MergedBody)
	{
		return false;
	}
	auto Records = MultipacketRecords::Validate(*Envelope, Decoded);
	if (!Records)
	{
		return false;
	}
	const auto First = Records->Next();
	const auto Second = Records->Next();
	return First && Second && First->size() == 32 && Second->size() == 32 &&
		std::ranges::all_of(*First, [](u8 Byte) { return Byte == 'A'; }) &&
		std::ranges::all_of(*Second, [](u8 Byte) { return Byte == 'B'; }) && !Records->Next();
}

bool CheckFramingBeforeDecode()
{
	xr_array<u8, MergedBody.size()> Output;
	Output.fill(Sentinel);
	unsigned Calls = 0;
	const auto Probe = [&](xr_span<const u8>, xr_span<u8>) noexcept -> std::size_t
	{
		++Calls;
		return 1;
	};

	for (std::size_t Size = 0; Size <= 5; ++Size)
	{
		if (DecodeBody(PackedBody.first(Size), Output, BodyFraming::Checksummed, Probe) != 0)
		{
			return false;
		}
	}
	for (std::size_t Index = 1; Index < PackedBody.size(); ++Index)
	{
		xr_array<u8, PackedBody.size()> Corrupted{};
		std::ranges::copy(PackedBody, Corrupted.begin());
		Corrupted[Index] ^= 1;
		if (DecodeBody(Corrupted, Output, BodyFraming::Checksummed, Probe) != 0)
		{
			return false;
		}
	}
	constexpr xr_array<u8, 3> UnknownTag = {0xff, 0x42, 0x43};
	if (DecodeBody(UnknownTag, Output, BodyFraming::Checksummed, Probe) != 0 || Calls != 0 ||
		!std::ranges::all_of(Output, [](u8 Byte) { return Byte == Sentinel; }))
	{
		return false;
	}
	const auto TooMuch = [&](xr_span<const u8> Input, xr_span<u8> Destination) noexcept
	{
		++Calls;
		if (!std::ranges::equal(Input, LzoBytes) || Destination.size() != Output.size())
		{
			return std::size_t{1};
		}
		return Destination.size() + 1;
	};
	return DecodeBody(PackedBody, Output, BodyFraming::Checksummed, TooMuch) == 0 && Calls == 1;
}

bool CheckRawAndFallback()
{
	constexpr xr_array<u8, 3> Source = {0xc1, 0x43, 0x21};
	constexpr xr_array<u8, 4> Raw = {0xc0, 0xc1, 0x43, 0x21};
	xr_array<u8, Raw.size()> Encoded;
	Encoded.fill(Sentinel);
	xr_array<u8, Source.size()> Decoded;
	Decoded.fill(Sentinel);
	unsigned Calls = 0;
	const auto Probe = [&](xr_span<const u8>, xr_span<u8>) noexcept -> std::size_t
	{
		++Calls;
		return 1;
	};
	if (EncodeBody(Source, xr_span(Encoded).first(3), BodyFraming::Checksummed, Probe) != 0 ||
		DecodeBody(Raw, xr_span(Decoded).first(2), BodyFraming::Checksummed, Probe) != 0 ||
		Calls != 0 || Encoded.front() != Sentinel || Decoded.front() != Sentinel)
	{
		return false;
	}
	if (EncodeBody(Source, Encoded, BodyFraming::Untagged, Probe) != Source.size() ||
		!std::ranges::equal(xr_span(Encoded).first<Source.size()>(), Source) ||
		DecodeBody(Source, Decoded, BodyFraming::Untagged, Probe) != Source.size() || Decoded != Source || Calls != 0)
	{
		return false;
	}
	return EncodeBody(Source, Encoded, BodyFraming::Checksummed, NoCodec) == Raw.size() && Encoded == Raw &&
		DecodeBody(Raw, Decoded, BodyFraming::Checksummed, Probe) == Source.size() && Decoded == Source && Calls == 0;
}

bool CheckPackingDecision()
{
	xr_array<u8, 256> Encoded;
	Encoded.fill(Sentinel);
	const auto CopyFixture = [](xr_span<const u8>, xr_span<u8> Output) noexcept
	{
		std::ranges::copy(LzoBytes, Output.begin());
		return LzoBytes.size();
	};
	if (EncodeBody(MergedBody, Encoded, BodyFraming::Checksummed, CopyFixture) != PackedBody.size() ||
		!std::ranges::equal(xr_span(Encoded).first<PackedBody.size()>(), PackedBody) || Encoded[PackedBody.size()] != Sentinel)
	{
		return false;
	}

	const auto TiesOriginalSize = [](xr_span<const u8> Input, xr_span<u8> Output) noexcept
	{
		const auto Size = Input.size() - 5;
		std::fill_n(Output.begin(), Size, Sentinel);
		return Size;
	};
	if (EncodeBody(MergedBody, Encoded, BodyFraming::Checksummed, TiesOriginalSize) != MergedBody.size() + 1 ||
		Encoded[0] != RawTag || !std::ranges::equal(xr_span(Encoded).subspan<1, MergedBody.size()>(), MergedBody))
	{
		return false;
	}

	const auto TooMuch = [](xr_span<const u8>, xr_span<u8> Output) noexcept { return Output.size() + 1; };
	if (EncodeBody(MergedBody, Encoded, BodyFraming::Checksummed, TooMuch) != MergedBody.size() + 1 ||
		Encoded[0] != RawTag || !std::ranges::equal(xr_span(Encoded).subspan<1, MergedBody.size()>(), MergedBody))
	{
		return false;
	}

	const auto TaggedSize = EncodeBody(MergedBody, Encoded, BodyFraming::Tagged, CopyFixture);
	xr_array<u8, MergedBody.size()> Decoded{};
	return TaggedSize == LzoBytes.size() + 1 && Encoded[0] == CompressedTag &&
		std::ranges::equal(xr_span(Encoded).subspan<1, LzoBytes.size()>(), LzoBytes) &&
		DecodeBody(xr_span(Encoded).first(TaggedSize), Decoded, BodyFraming::Tagged, DecodeLzo) == Decoded.size() &&
		Decoded == MergedBody;
}

bool CheckNativeBounds()
{
	constexpr std::size_t Bound = 139;
	const auto Max = Lzo::MaxCompressedSize(MergedBody.size());
	if (Max != Bound || Lzo::MaxCompressedSize(std::numeric_limits<std::size_t>::max()))
	{
		return false;
	}
	TestLzoWork Work;
	xr_array<u8, Bound + 1> Encoded;
	Encoded.fill(Sentinel);
	if (Lzo::CompressBlock(MergedBody, xr_span(Encoded).first(Bound - 1), Work.Storage) != 0 ||
		!std::ranges::all_of(Encoded, [](u8 Byte) { return Byte == Sentinel; }))
	{
		return false;
	}
	const auto Size = EncodeBody(MergedBody, Encoded, BodyFraming::Checksummed,
		[&](xr_span<const u8> Input, xr_span<u8> Output) noexcept
		{
			return Lzo::CompressBlock(Input, Output, Work.Storage);
		});
	// Framing consumes part of the destination before the native bound is checked.
	if (Size != MergedBody.size() + 1 || Encoded[0] != RawTag || Encoded.back() != Sentinel)
	{
		return false;
	}
	const auto NativeSize = Lzo::CompressBlock(MergedBody, xr_span(Encoded).first(Bound), Work.Storage);
	xr_array<u8, MergedBody.size() + 1> Decoded;
	Decoded.fill(Sentinel);
	if (NativeSize == 0 || NativeSize > Bound || Encoded.back() != Sentinel ||
		Lzo::DecompressBlock(xr_span(Encoded).first(NativeSize), xr_span(Decoded).first<MergedBody.size()>()) != MergedBody.size() ||
		!std::ranges::equal(xr_span(Decoded).first<MergedBody.size()>(), MergedBody) || Decoded.back() != Sentinel)
	{
		return false;
	}

	Decoded.fill(Sentinel);
	if (Lzo::DecompressBlock(LzoBytes, xr_span(Decoded).first(MergedBody.size() - 1)) != 0 ||
		Decoded[MergedBody.size() - 1] != Sentinel || Decoded.back() != Sentinel)
	{
		return false;
	}
	for (std::size_t Size = 0; Size < LzoBytes.size(); ++Size)
	{
		if (Lzo::DecompressBlock(LzoBytes.first(Size), xr_span(Decoded).first<MergedBody.size()>()) != 0)
		{
			return false;
		}
	}
	xr_array<u8, LzoBytes.size() + 1> TrailingBytes{};
	std::ranges::copy(LzoBytes, TrailingBytes.begin());
	return Lzo::DecompressBlock(TrailingBytes, xr_span(Decoded).first<MergedBody.size()>()) == 0 && Decoded.back() == Sentinel;
}

bool CheckPackedRoundTripAndRejection()
{
	TestLzoWork Work;
	xr_array<u8, 256> Datagram{};
	std::ranges::copy(xr_span(PackedDatagram).first<HeaderSize>(), Datagram.begin());
	const auto Body = xr_span(Datagram).subspan<HeaderSize>();
	const auto EncodeLzo = [&](xr_span<const u8> Input, xr_span<u8> Output) noexcept
	{
		return Lzo::CompressBlock(Input, Output, Work.Storage);
	};

	const auto DecodeRecords = [&](xr_span<const u8> Input)
	{
		const auto Envelope = ReadEnvelope(Input);
		if (!Envelope || Envelope->Encoding != BodyEncoding::Compressed)
		{
			return false;
		}
		xr_array<u8, MaxDecodedSize> Storage;
		const auto Output = xr_span(Storage).first(Envelope->DecodedSize);
		const auto Size = DecodeBody(Input.subspan(HeaderSize), Output, BodyFraming::Checksummed, DecodeLzo);
		return Size == Output.size() && MultipacketRecords::Validate(*Envelope, Output).has_value();
	};

	const auto Size = EncodeBody(MergedBody, Body, BodyFraming::Checksummed, EncodeLzo);
	if (Size == 0 || Size > Body.size())
	{
		return false;
	}
	const auto Frame = xr_span(Datagram).first(HeaderSize + Size);
	if (!DecodeRecords(Frame))
	{
		return false;
	}
	Datagram[1] = static_cast<u8>(MergedBody.size() - 1);
	if (DecodeRecords(Frame))
	{
		return false;
	}
	Datagram[1] = static_cast<u8>(MergedBody.size() + 1);
	if (DecodeRecords(Frame))
	{
		return false;
	}

	auto Malformed = MergedBody;
	Malformed[34] = 33;
	Datagram[1] = static_cast<u8>(Malformed.size());
	const auto MalformedSize = EncodeBody(Malformed, Body, BodyFraming::Checksummed, EncodeLzo);
	if (MalformedSize == 0 || MalformedSize > Body.size() || Body.front() != CompressedTag)
	{
		return false;
	}
	return !DecodeRecords(xr_span(Datagram).first(HeaderSize + MalformedSize));
}

bool CheckDictionary()
{
	constexpr auto Dictionary = []
	{
		xr_array<u8, 1024> Bytes{};
		for (std::size_t Index = 0; Index < Bytes.size(); ++Index)
		{
			Bytes[Index] = static_cast<u8>(Index * 113 + Index / 11);
		}
		return Bytes;
	}();
	const auto Source = xr_span(Dictionary).subspan<400, 128>();
	TestLzoWork Work;
	xr_array<u8, 256> Encoded{};
	const auto Size = Lzo::CompressBlock(Source, Encoded, Work.Storage, Dictionary);
	if (Size == 0 || Size > Encoded.size())
	{
		return false;
	}
	xr_array<u8, Source.size() + 1> Decoded;
	Decoded.fill(Sentinel);
	const auto Block = xr_span(Encoded).first(Size);
	const auto Output = xr_span(Decoded).first<Source.size()>();
	if (Lzo::DecompressBlock(Block, Output) != 0)
	{
		return false;
	}
	return Lzo::DecompressBlock(Block, Output, Dictionary) == Source.size() && std::ranges::equal(Output, Source) &&
		Lzo::DecompressBlock(Block, Output.first(Source.size() - 1), Dictionary) == 0 && Decoded.back() == Sentinel;
}
}

int main()
{
	const xr_array Tests = {
		xr_pair{"golden checksum and merged records", &CheckGoldenChecksumAndRecords},
		xr_pair{"framing and checksum before codec", &CheckFramingBeforeDecode},
		xr_pair{"unchanged raw framing and failure fallback", &CheckRawAndFallback},
		xr_pair{"packed header and compression threshold", &CheckPackingDecision},
		xr_pair{"native LZO input and output bounds", &CheckNativeBounds},
		xr_pair{"packed round trip and record rejection", &CheckPackedRoundTripAndRejection},
		xr_pair{"dictionary-dependent bounded LZO", &CheckDictionary},
	};
	bool Passed = true;
	for (const auto& [Name, Test] : Tests)
	{
		if (!Test())
		{
			std::fprintf(stderr, "Failed: %s\n", Name);
			Passed = false;
		}
	}
	if (!Passed)
	{
		return 1;
	}
	std::puts("Legacy compression checks passed");
	return 0;
}
