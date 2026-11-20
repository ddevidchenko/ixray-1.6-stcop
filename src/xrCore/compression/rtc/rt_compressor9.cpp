#include "stdafx.h"
#include "rt_compressor.h"
#include "../lzo/LzoBlock.h"

namespace
{
thread_local lzo_align_t Work[(LZO1X_999_MEM_COMPRESS + sizeof(lzo_align_t) - 1) / sizeof(lzo_align_t)];
u8* g_Dictionary = nullptr;
u32 g_DictionarySize = 0;
}

void rtc9_initialize()
{
	[[maybe_unused]] static const bool Initialized = []
	{
		if (!Lzo::Initialize())
		{
			return false;
		}

		string_path Path;
		FS.update_path(Path, _game_config_, "mp\\lzo-dict.bin");
		IReader* Reader = FS.exist(Path) ? FS.r_open(Path) : nullptr;
		if (!Reader)
		{
			Msg("\"%s\" not found", Path);
			return true;
		}

		const xr_scope_exit Close([&Reader] { FS.r_close(Reader); });
		g_DictionarySize = Reader->length();
		if (g_DictionarySize != 0)
		{
			g_Dictionary = static_cast<u8*>(xr_malloc(g_DictionarySize));
			Reader->r(g_Dictionary, g_DictionarySize);
			Msg("using LZO-dictionary \"%s\"", Path);
		}
		return true;
	}();
}

void rtc9_uninitialize()
{
	xr_free(g_Dictionary);
	g_DictionarySize = 0;
}

u32 rtc9_csize(u32 InputSize)
{
	const auto Bound = Lzo::MaxCompressedSize(InputSize);
	return Bound && std::in_range<u32>(*Bound) ? static_cast<u32>(*Bound) : 0;
}

u32 rtc9_compress(void* Destination, u32 DestinationSize, const void* Source, u32 SourceSize)
{
	if (!Destination || !Source)
	{
		return 0;
	}
	rtc9_initialize();
	return static_cast<u32>(Lzo::CompressBlock(
		xr_span(static_cast<const u8*>(Source), SourceSize),
		xr_span(static_cast<u8*>(Destination), DestinationSize), Work,
		xr_span(g_Dictionary, g_DictionarySize)));
}

u32 rtc9_decompress(void* Destination, u32 DestinationSize, const void* Source, u32 SourceSize)
{
	if (!Destination || !Source)
	{
		return 0;
	}
	rtc9_initialize();
	return static_cast<u32>(Lzo::DecompressBlock(
		xr_span(static_cast<const u8*>(Source), SourceSize),
		xr_span(static_cast<u8*>(Destination), DestinationSize),
		xr_span(g_Dictionary, g_DictionarySize)));
}
