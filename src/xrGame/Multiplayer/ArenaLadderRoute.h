#pragma once

#include "ArenaLadderTraversal.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <memory_resource>
#include <queue>
#include <unordered_map>
#include <vector>

namespace Multiplayer::ArenaLadders
{
constexpr auto AttachmentName = "level.navlinks";
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

// Expand supplies eligible directed neighbors and their positive costs. Estimate must
// be a consistent lower bound (zero is valid). A budget miss returns no partial route.
template <class Path, class Expand, class Estimate>
RouteResult FindRoute(u32 Start, u32 Goal, u32 NodeBudget, Path& Out, Expand Neighbors, Estimate Heuristic)
{
	struct Previous
	{
		u32 Vertex;
		u32 Link;
		f32 Cost;
	};
	struct Candidate
	{
		u32 Vertex;
		f32 Cost;
		f32 Priority;
	};
	const auto Compare = [](const Candidate& A, const Candidate& B)
	{
		if (A.Priority != B.Priority)
		{
			return A.Priority > B.Priority;
		}
		return A.Vertex > B.Vertex;
	};

	Out.clear();
	if (!NodeBudget)
	{
		return RouteResult::BudgetExceeded;
	}
	std::pmr::monotonic_buffer_resource Storage;
	std::pmr::unordered_map<u32, Previous> Visited{&Storage};
	std::priority_queue<Candidate, std::pmr::vector<Candidate>, decltype(Compare)> Queue(
		Compare, std::pmr::vector<Candidate>{&Storage});
	Visited.emplace(Start, Previous{Start, NoLink, 0.f});
	Queue.push({Start, 0.f, Heuristic(Start)});

	while (!Queue.empty())
	{
		const auto Current = Queue.top();
		Queue.pop();
		if (Visited.at(Current.Vertex).Cost != Current.Cost)
		{
			continue;
		}
		if (Current.Vertex == Goal)
		{
			for (u32 Vertex = Goal; Vertex != Start;)
			{
				const auto& From = Visited.at(Vertex);
				Out.push_back({Vertex, From.Link});
				Vertex = From.Vertex;
			}
			Out.push_back({Start});
			std::reverse(Out.begin(), Out.end());
			return RouteResult::Found;
		}

		bool LimitReached = false;
		Neighbors(Current.Vertex, [&](Step Edge, f32 Cost)
		{
			const f32 Total = Current.Cost + Cost;
			const auto Existing = Visited.find(Edge.Vertex);
			if (Existing == Visited.end())
			{
				if (Visited.size() == NodeBudget)
				{
					LimitReached = true;
					return;
				}
				Visited.emplace(Edge.Vertex, Previous{Current.Vertex, Edge.Link, Total});
			}
			else
			{
				if (Existing->second.Cost <= Total)
				{
					return;
				}
				Existing->second = {Current.Vertex, Edge.Link, Total};
			}
			Queue.push({Edge.Vertex, Total, Total + Heuristic(Edge.Vertex)});
		});
		if (LimitReached)
		{
			return RouteResult::BudgetExceeded;
		}
	}
	return RouteResult::Unreachable;
}

}
