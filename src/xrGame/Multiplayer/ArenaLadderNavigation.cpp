#include "StdAfx.h"
#include "ArenaLadderNavigation.h"
#include "ArenaLadderRoute.h"
#include "ArenaNavigationPackage.h"
#include "../ClimableObject.h"
#include "../Level.h"
#include "../restricted_object.h"

namespace Multiplayer
{
using namespace ArenaLadders;

namespace
{
bool Suitable(const Fvector& Landing, const Fvector& End)
{
	return std::abs(Landing.y - End.y) <= 0.55f && Landing.distance_to_xz_sqr(End) <= _sqr(1.35f);
}
}

bool ArenaLadderNavigation::Load(xr_string_view Map, xr_string_view Version, xr_span<const u8> GridBytes)
{
	using namespace ArenaNavigation;
	const auto Report = [&](Result Error)
	{
		Msg("! Arena navigation: map=%.*s version=%.*s file=%s reason=%s",
			int(Map.size()), Map.data(), int(Version.size()), Version.data(), Error.File.data(), Describe(Error.Code));
		return false;
	};

	if (!FS.exist("$level$", PackageName))
	{
		return Report(Issue(Status::MissingPackage));
	}
	auto Package = FS.rg_open("$level$", PackageName);
	if (!Package)
	{
		return Report(Issue(Status::UnreadableAsset));
	}
	if (Package->length() <= 0 || Package->length() > MaxPackageBytes)
	{
		return Report(Issue(Status::InvalidSize));
	}

	auto Collision = FS.rg_open("$level$", "level.cform");
	if (!Collision || Collision->length() <= 0)
	{
		return Report(Issue(Status::UnreadableAsset, "level.cform"));
	}
	static_assert(sizeof(hdrCFORM) == CollisionHeaderSize);
	const Bytes CollisionBytes{static_cast<const u8*>(Collision->pointer()), std::size_t(Collision->length())};
	const auto Parts = ReadCollisionParts(CollisionBytes);
	if (!Parts)
	{
		return Report(Issue(Status::InvalidPartCount, "level.cform"));
	}

	const auto& Header = Grid.header();
	const Identity Expected{Map, Version, AgentProfile,
		{{Header.guid().g[0], Header.guid().g[1]}, Header.vertex_count()}, *Parts};
	xr_vector<Record> Records;
	const xr_string_view Text(static_cast<const char*>(Package->pointer()), std::size_t(Package->length()));
	const auto Checked = Validate(Text, Expected, Records, [&](xr_string_view Name)
	{
		if (Name == "level.ai" || Name == "level.cform")
		{
			return AssetDigest{AssetState::Present, Sha256(Name == "level.ai" ? GridBytes : CollisionBytes)};
		}

		const char* Alias = Name == "gamemtl.xr" ? _game_data_ : "$level$";
		// The validator passes only schema-defined, NUL-terminated file names.
		if (!FS.exist(Alias, Name.data()))
		{
			return AssetDigest{};
		}
		auto Input = FS.rg_open(Alias, Name.data());
		if (!Input || Input->length() <= 0)
		{
			return AssetDigest{AssetState::Unreadable};
		}
		return AssetDigest{AssetState::Present,
			Sha256({static_cast<const u8*>(Input->pointer()), std::size_t(Input->length())})};
	});
	if (Checked.Code != Status::Ready)
	{
		return Report(Checked);
	}

	for (const auto& Record : Records)
	{
		auto Match = std::find_if(Bindings.begin(), Bindings.end(), [&](const Binding& Candidate)
		{
			return xr_string_view(Candidate.Name) == Record.Name;
		});
		const u32 Index = u32(Match - Bindings.begin());
		if (Match == Bindings.end())
		{
			Bindings.push_back({xr_string(Record.Name), {}, {}});
		}
		Links.push_back({Index, Record.Travel, Record.Entry, Record.Exit, Record.Cost});
	}
	std::stable_sort(Links.begin(), Links.end(), [](const Edge& Left, const Edge& Right)
	{
		return Left.Entry < Right.Entry;
	});
	Msg("Arena navigation: map=%.*s version=%.*s profile=%s file=%s links=%u",
		int(Map.size()), Map.data(), int(Version.size()), Version.data(), ArenaNavigation::AgentProfile.data(),
		ArenaNavigation::PackageName, u32(Links.size()));
	return true;
}

void ArenaLadderNavigation::Register(const CClimableObject& Ladder)
{
	for (auto& Binding : Bindings)
	{
		if (Binding.Name == Ladder.cName().c_str())
		{
			Binding.Objects.push_back(Ladder.ID());
			++Binding.Revision;
			if (Binding.Objects.size() > 1)
			{
				Msg("! Arena ladders: duplicate live ladder name %s", Binding.Name.c_str());
			}
			return;
		}
	}
}

void ArenaLadderNavigation::Unregister(const CClimableObject& Ladder)
{
	for (auto& Binding : Bindings)
	{
		if (std::erase(Binding.Objects, Ladder.ID()))
		{
			++Binding.Revision;
		}
	}
}

CClimableObject* ArenaLadderNavigation::Resolve(u32 Index, u16 Owner) const
{
	const auto& Edge = Links[Index];
	const auto& Binding = Bindings[Edge.BindingId];
	if (Binding.Objects.size() != 1 || !Binding.Reservation.Available(Owner))
	{
		return nullptr;
	}
	auto* Object = Level().Objects.net_Find(Binding.Objects.front());
	auto* GameObject = Object ? Object->cast_game_object() : nullptr;
	auto* Ladder = GameObject && !GameObject->getDestroy() ? GameObject->cast_climable_object() : nullptr;
	if (!Ladder)
	{
		return nullptr;
	}

	Fvector Lower, Upper, Axis, Normal;
	Ladder->LowerPoint(Lower);
	Ladder->UpperPoint(Upper);
	const float Height = Upper.y - Lower.y;
	if (!_valid(Lower) || !_valid(Upper) || Height < 0.75f || Height > 24.f ||
		Ladder->DDAxis(Axis) <= EPS_L || Axis.y < 0.9f ||
		Ladder->DDNorm(Normal) <= EPS_L || std::abs(Normal.y) > 0.25f)
	{
		return nullptr;
	}

	const auto Entry = Grid.vertex_position(Edge.Entry);
	const auto Exit = Grid.vertex_position(Edge.Exit);
	const bool Up = Edge.Travel == Direction::Up;
	if ((Up ? Exit.y - Entry.y : Entry.y - Exit.y) < 0.6f ||
		!Suitable(Entry, Up ? Lower : Upper) || !Suitable(Exit, Up ? Upper : Lower))
	{
		return nullptr;
	}
	return Ladder;
}

bool ArenaLadderNavigation::Acquire(u32 Index, u16 Owner)
{
	return Resolve(Index, Owner) && Bindings[Links[Index].BindingId].Reservation.TryAcquire(Owner);
}

void ArenaLadderNavigation::Release(u32 Index, u16 Owner)
{
	Bindings[Links[Index].BindingId].Reservation.Release(Owner);
}

bool ArenaLadderNavigation::SameLadder(u32 First, u32 Second) const
{
	return Links[First].BindingId == Links[Second].BindingId;
}

bool ArenaLadderNavigation::AtLanding(u32 Node, const Fvector& Position) const
{
	return Grid.inside(Node, Position) && Grid.vertex_position(Node).distance_to_xz_sqr(Position) <= _sqr(0.24f) &&
		std::abs(Grid.vertex_plane_y(Node, Position.x, Position.z) - Position.y) <= 0.2f;
}

bool ArenaLadderNavigation::Plan(const CRestrictedObject& Restrictions, u16 Owner, u32 Start, u32 Goal, u32 Budget,
	u32 Avoid, xr_vector<Step>& Path) const
{
	Path.clear();
	if (!Grid.valid_vertex_id(Start) || !Grid.valid_vertex_id(Goal) || !Restrictions.accessible(Goal))
	{
		return false;
	}

	const auto Target = Grid.vertex_position(Goal);
	// Scope the native masks for static/dynamic obstacles to this synchronous query.
	Restrictions.add_border(Start, Goal);
	const xr_scope_exit RemoveBorder([&] { Restrictions.remove_border(); });
	const auto Estimate = [&](u32 Vertex) { return Grid.vertex_position(Vertex).distance_to(Target); };
	return FindRoute(Start, Goal, Budget, Path, [&](u32 From, auto Visit)
	{
		const auto Position = Grid.vertex_position(From);
		const auto Eligible = [&](u32 Node) { return Grid.is_accessible(Node) && Restrictions.accessible(Node); };
		for (u8 Slot = 0; Slot < 4; ++Slot)
		{
			const u32 To = Grid.vertex(From)->link(Slot);
			if (Eligible(To))
			{
				Visit({To}, std::max(Grid.header().cell_size(), Position.distance_to(Grid.vertex_position(To))));
			}
		}

		for (auto It = std::lower_bound(Links.begin(), Links.end(), From, [](const Edge& Edge, u32 Node)
		{
			return Edge.Entry < Node;
		}); It != Links.end() && It->Entry == From; ++It)
		{
			const u32 Index = u32(It - Links.begin());
			if ((Avoid == NoLink || !SameLadder(Index, Avoid)) && Eligible(It->Exit) && Resolve(Index, Owner))
			{
				Visit({It->Exit, Index}, std::max(It->Cost, Position.distance_to(Grid.vertex_position(It->Exit))));
			}
		}
	}, Estimate) == RouteResult::Found;
}
}
