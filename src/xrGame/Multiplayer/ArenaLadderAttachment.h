#pragma once

#include "ArenaLadderTraversal.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace Multiplayer::ArenaLadders
{
constexpr u32 MaxAttachmentBytes = 16 * 1024;
constexpr u32 MaxLinks = 128;

namespace Detail
{
inline xr_string_view Token(xr_string_view& Line)
{
	constexpr xr_string_view Whitespace = " \t\r";
	const auto First = Line.find_first_not_of(Whitespace);
	if (First == xr_string_view::npos)
	{
		Line = {};
		return {};
	}
	Line.remove_prefix(First);
	const auto End = Line.find_first_of(Whitespace);
	const auto Word = Line.substr(0, End);
	Line.remove_prefix(Word.size());
	return Word;
}

inline xr_string_view NextLine(xr_string_view& Text)
{
	const auto End = Text.find('\n');
	const auto Line = Text.substr(0, End);
	Text.remove_prefix(End == xr_string_view::npos ? Text.size() : End + 1);
	return Line;
}

template <class T, class... Args>
bool Number(xr_string_view Word, T& Value, Args... Format)
{
	if (Word.empty())
	{
		return false;
	}
	const auto End = Word.data() + Word.size();
	const auto Result = std::from_chars(Word.data(), End, Value, Format...);
	return Result.ec == std::errc{} && Result.ptr == End;
}
}

// Record names refer to the supplied text until the caller copies them.
template <class Records>
bool ReadAttachment(xr_string_view Text, xr_array<u64, 2> Guid, u32 NodeCount, Records& Out)
{
	Out.clear();
	if (Text.size() > MaxAttachmentBytes)
	{
		return false;
	}

	auto Header = Detail::NextLine(Text);
	xr_array<u64, 2> ActualGuid{};
	if (Detail::Token(Header) != "arena_ladders" || Detail::Token(Header) != "1" ||
		!Detail::Number(Detail::Token(Header), ActualGuid[0], 16) ||
		!Detail::Number(Detail::Token(Header), ActualGuid[1], 16) ||
		ActualGuid != Guid || !Detail::Token(Header).empty())
	{
		return false;
	}

	Records Parsed;
	while (!Text.empty())
	{
		auto Line = Detail::NextLine(Text);
		const auto Name = Detail::Token(Line);
		if (Name.empty())
		{
			continue;
		}

		const auto Travel = Detail::Token(Line);
		Record Link{Name, Travel == "up" ? Direction::Up : Direction::Down, 0, 0, 0.f};
		if (Parsed.size() == MaxLinks || Name.size() >= 64 || Name.find('\0') != xr_string_view::npos ||
			(Travel != "up" && Travel != "down") ||
			!Detail::Number(Detail::Token(Line), Link.Entry) || Link.Entry >= NodeCount ||
			!Detail::Number(Detail::Token(Line), Link.Exit) || Link.Exit >= NodeCount || Link.Entry == Link.Exit ||
			!Detail::Number(Detail::Token(Line), Link.Cost) || !std::isfinite(Link.Cost) || Link.Cost <= 0.f ||
			!Detail::Token(Line).empty() ||
			std::any_of(Parsed.begin(), Parsed.end(), [&](const Record& Existing)
			{
				return Existing.Name == Link.Name && Existing.Travel == Link.Travel;
			}))
		{
			return false;
		}
		Parsed.push_back(Link);
	}
	Out.swap(Parsed);
	return true;
}

}
