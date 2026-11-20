#pragma once

#include "ArenaLadderAttachment.h"

#include <algorithm>
#include <memory_resource>
#include <queue>
#include <unordered_map>
#include <vector>

namespace Multiplayer::ArenaLadders
{
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
