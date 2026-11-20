#include "stdafx.h"
#include "NET_Common.h"
#include "NET_Compressor.h"
#include "Multiplayer/LegacyCompression.h"

namespace
{
using Multiplayer::Legacy::BodyFraming;

#if !NET_USE_COMPRESSION
constexpr auto Framing = BodyFraming::Untagged;
#elif NET_USE_COMPRESSION_CRC
constexpr auto Framing = BodyFraming::Checksummed;
#else
constexpr auto Framing = BodyFraming::Tagged;
#endif

#if NET_DUMP_COMPRESSION
FILE* g_CompressionDump = nullptr;
#endif
}

NET_Compressor::NET_Compressor() = default;

NET_Compressor::~NET_Compressor()
{
#if NET_DUMP_COMPRESSION
	if (g_CompressionDump)
	{
		fclose(g_CompressionDump);
		g_CompressionDump = nullptr;
	}
#endif
}

u16 NET_Compressor::compressed_size(const u32& Count)
{
	if constexpr (Framing == BodyFraming::Untagged)
	{
		return std::in_range<u16>(Count) ? static_cast<u16>(Count) : 0;
	}
	const u64 Bound = rtc9_csize(Count);
	constexpr auto PackedPrefix = 1 + (Framing == BodyFraming::Checksummed ? sizeof(u32) : 0);
	const u64 Size = Bound + PackedPrefix;
	return Bound != 0 && std::in_range<u16>(Size) ? static_cast<u16>(Size) : 0;
}

XRNETSERVER_API bool g_net_compressor_enabled = false;
XRNETSERVER_API bool g_net_compressor_gather_stats = false;

u16 NET_Compressor::Compress(BYTE* Destination, const u32& Capacity, BYTE* Source, const u32& Count)
{
	using namespace Multiplayer::Legacy;
	if (!Destination || !Source)
	{
		return 0;
	}

	const bool Candidate = Count > 36;
	const bool GatherStats = Candidate && g_net_compressor_gather_stats;
	const bool TryCompression = Framing != BodyFraming::Untagged && Candidate &&
		g_net_compressor_enabled && !psNET_direct_connect;
	std::optional<xrCriticalSectionGuard> Guard;
	if (TryCompression || GatherStats)
	{
		Guard.emplace(CS);
	}

	const auto Encode = [&](xr_span<const u8> Input, xr_span<u8> Output) noexcept -> std::size_t
	{
		if (!TryCompression)
		{
			return 0;
		}
		const u32 Produced = rtc9_compress(Output.data(), static_cast<u32>(Output.size()),
			Input.data(), static_cast<u32>(Input.size()));
		if (GatherStats && Produced)
		{
			constexpr auto PackedPrefix = 1u + (Framing == BodyFraming::Checksummed ? sizeof(u32) : 0);
			m_stats.total_compressed_bytes += Produced + static_cast<u32>(PackedPrefix);
		}
		return Produced;
	};

	const auto Size = static_cast<u16>(EncodeBody(xr_span(Source, Count), xr_span(Destination, Capacity), Framing, Encode));
	if (Size == 0)
	{
		return 0;
	}
	const bool Packed = Framing != BodyFraming::Untagged && Destination[0] == CompressedTag;
	if (GatherStats)
	{
		auto& Stats = *m_stats.get(Count);
		++Stats.hit_count;
		m_stats.total_uncompressed_bytes += Count;
		if (!Packed && Framing != BodyFraming::Untagged)
		{
			++Stats.unlucky_attempts;
		}
		Stats.compressed_size += Size;
	}

#if NET_LOG_COMPRESSION
	Msg("#compress%s %u->%u", Packed ? "" : "/as-is", Count, static_cast<u32>(Size));
#endif
#if NET_DUMP_COMPRESSION
	if (Packed)
	{
		if (!g_CompressionDump)
		{
			g_CompressionDump = fopen("net-compression.log", "w+b");
		}
		if (g_CompressionDump)
		{
			fprintf(g_CompressionDump, "LZO compress %2.0f%% %u->%u\r\n",
				100.0 * Size / Count, Count, static_cast<u32>(Size));
		}
	}
#endif
	return Size;
}

u16 NET_Compressor::Decompress(BYTE* Destination, const u32& Capacity, BYTE* Source, const u32& Count)
{
	using namespace Multiplayer::Legacy;
	if (!Destination || !Source)
	{
		return 0;
	}

	const auto Decode = [&](xr_span<const u8> Input, xr_span<u8> Output) noexcept
	{
		const xrCriticalSectionGuard Guard(CS);
		return rtc9_decompress(Output.data(), static_cast<u32>(Output.size()),
			Input.data(), static_cast<u32>(Input.size()));
	};
	const auto Size = static_cast<u16>(DecodeBody(xr_span(Source, Count), xr_span(Destination, Capacity), Framing, Decode));
#if NET_LOG_COMPRESSION
	Msg("#decompress %u->%u", Count, static_cast<u32>(Size));
#endif
	return Size;
}

void NET_Compressor::DumpStats(bool brief)
{
	const xrCriticalSectionGuard Guard(CS);
	xr_map<u32,SCompressorStats::SStatPacket>::const_iterator it		= m_stats.m_packets.begin();
	xr_map<u32,SCompressorStats::SStatPacket>::const_iterator it_e	= m_stats.m_packets.end();

	Msg("---------NET_Compressor::DumpStats-----------");
	
	Msg("Active=[%s]",g_net_compressor_enabled?"yes":"no");

	Msg("uncompressed [%d]",m_stats.total_uncompressed_bytes);
	Msg("compressed   [%d]",m_stats.total_compressed_bytes);
	
	u32 total_hits		= 0;
	u32 unlucky_hits	= 0;
	
	for(; it!=it_e; ++it)
	{
		total_hits		+= it->second.hit_count;
		unlucky_hits	+= it->second.unlucky_attempts;
		if(!brief)
		{
			Msg	("size[%d] count[%d] unlucky[%d] avg_c[%d]",it->first, it->second.hit_count, it->second.unlucky_attempts, iFloor(float(it->second.compressed_size)/float(it->second.hit_count)) );
		}
	}
	Msg("total   [%d]",	total_hits);
	Msg("unlucky [%d]",	unlucky_hits);
}
