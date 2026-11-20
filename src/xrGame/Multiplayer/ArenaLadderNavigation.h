#pragma once

#include "ArenaLadderTraversal.h"

class CClimableObject;
class CRestrictedObject;
class ILevelGraph;

namespace Multiplayer
{
class ArenaLadderNavigation
{
	struct Binding
	{
		xr_string Name;
		xr_vector<u16> Objects;
		ArenaLadders::Reservation Reservation;
		u32 Revision = 0;
	};

public:
	struct Edge
	{
		u32 BindingId;
		ArenaLadders::Direction Travel;
		u32 Entry;
		u32 Exit;
		f32 Cost;
	};

	ArenaLadderNavigation(const ILevelGraph& Grid, u32 Generation) : Grid(Grid), MapGeneration(Generation) {}

	bool Load();
	u32 Generation() const { return MapGeneration; }
	u32 BindingRevision(u32 Index) const { return Bindings[Links[Index].BindingId].Revision; }
	bool HasLinks() const { return !Links.empty(); }
	void Register(const CClimableObject& Ladder);
	void Unregister(const CClimableObject& Ladder);
	const Edge& Link(u32 Index) const { return Links[Index]; }

	CClimableObject* Resolve(u32 Index, u16 Owner) const;
	bool Acquire(u32 Index, u16 Owner);
	void Release(u32 Index, u16 Owner);
	bool SameLadder(u32 First, u32 Second) const;
	bool Plan(const CRestrictedObject& Restrictions, u16 Owner, u32 Start, u32 Goal, u32 Budget,
		u32 Avoid, xr_vector<ArenaLadders::Step>& Path) const;
	bool AtLanding(u32 Node, const Fvector& Position) const;

private:
	const ILevelGraph& Grid;
	u32 MapGeneration;
	xr_vector<Binding> Bindings;
	xr_vector<Edge> Links;
};
}
