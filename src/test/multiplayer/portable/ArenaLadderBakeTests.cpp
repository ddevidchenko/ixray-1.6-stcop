#include "../../../utils/ArenaNavPack/LadderBake.h"

#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>

namespace ArenaLadderBakeTests
{
using namespace Multiplayer::ArenaLadders;
using namespace Multiplayer::ArenaLadders::Bake;

static bool Expect(bool Passed, const char* Description)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Description);
	}
	return Passed;
}

using Buffer = std::vector<u8>;

static void Put(Buffer& Out, u32 Value, u32 Bytes = 4)
{
	for (u32 Byte = 0; Byte < Bytes; ++Byte)
	{
		Out.push_back(u8(Value >> (8 * Byte)));
	}
}

static void PutFloat(Buffer& Out, f32 Value) { Put(Out, std::bit_cast<u32>(Value)); }

static void PutPoint(Buffer& Out, Point Value)
{
	PutFloat(Out, Value.X);
	PutFloat(Out, Value.Y);
	PutFloat(Out, Value.Z);
}

static void PutString(Buffer& Out, xr_string_view Value)
{
	Out.insert(Out.end(), Value.begin(), Value.end());
	Out.push_back(0);
}

static Buffer SpawnPacket(xr_string_view Section, xr_string_view Name, u16 Version = 135, u16 Mode = 2,
	u8 ShapeType = 1, Point Rotation = {}, f32 Thin = 0.f)
{
	Buffer Packet;
	Put(Packet, 1, 2);
	PutString(Packet, Section);
	PutString(Packet, Name);
	Put(Packet, 0, 2); // legacy mode and respawn point
	PutPoint(Packet, {0, 2, 0});
	PutPoint(Packet, Rotation);
	Put(Packet, 0, 2); // respawn time
	Put(Packet, 0xffff, 2); // ID
	Put(Packet, 0xffff, 2); // parent
	Put(Packet, 0xffff, 2); // phantom
	Put(Packet, 1 << 5, 2);
	Put(Packet, Version, 2);
	Put(Packet, Mode, 2);
	Put(Packet, 0, 2); // script version
	Put(Packet, 2, 2); // client data size
	Put(Packet, 0xbeef, 2);
	Put(Packet, 0xffff, 2); // spawn ID
	Buffer State;
	Put(State, 0xffff, 2); // graph ID
	PutFloat(State, 0.f);
	Put(State, 0); // direct control
	Put(State, u32(-1)); // level node
	Put(State, 0); // flags
	PutString(State, "editor custom data");
	Put(State, u32(-1)); // story ID
	Put(State, u32(-1)); // spawn story ID
	Put(State, 1, 1); // shape count
	Put(State, ShapeType, 1);
	if (ShapeType == 1)
	{
		PutPoint(State, {Thin, 0, 0});
		PutPoint(State, {0, 2, 0});
		PutPoint(State, {0, 0, 0.45f});
		PutPoint(State, {});
	}
	else
	{
		PutPoint(State, {});
		PutFloat(State, 1.f);
	}
	if (Version >= 127)
	{
		PutString(State, "materials\\ladder");
	}
	Put(Packet, u32(State.size() + 2), 2);
	Packet.insert(Packet.end(), State.begin(), State.end());
	return Packet;
}

static void AppendChunk(Buffer& File, u32 Id, const Buffer& Packet)
{
	Put(File, Id);
	Put(File, u32(Packet.size()));
	File.insert(File.end(), Packet.begin(), Packet.end());
}

static bool Near(Point A, Point B)
{
	return std::abs(A.X - B.X) < 0.0001f && std::abs(A.Y - B.Y) < 0.0001f && std::abs(A.Z - B.Z) < 0.0001f;
}

static bool GeometryAndSpawn()
{
	constexpr f32 Pi = std::numbers::pi_v<f32>;
	Buffer File;
	AppendChunk(File, 0, SpawnPacket("physic_object", "clmbl#0"));
	AppendChunk(File, 1, SpawnPacket("climable_object", "straight", 124));
	AppendChunk(File, 2, SpawnPacket("climable_object", "turned", 135, 2, 1, {0, Pi / 2, 0}));
	AppendChunk(File, 3, SpawnPacket("climable_object", "reversed", 135, 2, 1, {0, 0, Pi}));
	AppendChunk(File, 4, SpawnPacket("climable_object", "disabled", 135, 1));
	AppendChunk(File, 5, SpawnPacket("climable_object", "sphere", 135, 2, 0));
	AppendChunk(File, 6, SpawnPacket("climable_object", "nonfinite", 135, 2, 1, {}, std::numeric_limits<f32>::infinity()));
	std::vector<Ladder> Ladders;
	const auto Status = ReadSpawn(File, Ladders);
	const bool Decoded = Ladders.size() == 6;
	bool Ok = Expect(Status == SpawnStatus::Ready && Decoded, "Only semantic ladder sections are extracted.");
	if (Decoded)
	{
		Ok &= Expect(Ladders[0].Shape && Ladders[0].Material == "materials\\fake_ladders" &&
			Near(Ladders[0].Shape->Lower(), {}) && Near(Ladders[0].Shape->Upper(), {0, 4, 0}),
			"CoP records use the default material and the live planar-box offset.");
		Ok &= Expect(Ladders[1].Shape && Near(Ladders[1].Shape->Normal, {0, 0, 0.2f}),
			"Heading rotates the ladder face normal.");
		Ok &= Expect(Ladders[2].Shape && Near(Ladders[2].Shape->Axis, {0, 2, 0}) &&
			Near(Ladders[2].Shape->Normal, {-0.2f, 0, 0}), "Inverted axes retain the side and positive vertical climb orientation.");
		Ok &= Expect(!Ladders[3].Deathmatch && !Ladders[4].Shape && !Ladders[5].Shape,
			"Mode-disabled records, sphere shapes and nonfinite half-extents remain diagnostic candidates.");
	}
	const auto Shallow = BuildGeometry({0, 2, 0}, SpawnBasis({0, 0, Pi / 4}), {0, 2, .45f});
	Ok &= Expect(Shallow && !SupportsClimbing(Shallow->Lower(), Shallow->Upper(), Shallow->Axis, Shallow->Normal),
		"A valid box outside the stalker climb profile is rejected.");
	const auto Slanted = BuildGeometry({0, 2, 0}, SpawnBasis({0.15f, 0, 0}), {0, 2, .45f});
	Ok &= Expect(Slanted && SupportsClimbing(Slanted->Lower(), Slanted->Upper(), Slanted->Axis, Slanted->Normal),
		"A mild pitch inside the profile preserves its physical endpoints.");
	File.pop_back();
	Ok &= Expect(ReadSpawn(File, Ladders) == SpawnStatus::Invalid && Ladders.empty(),
		"A truncated chunk never exposes a partial ladder table.");
	File.clear();
	auto TruncatedPacket = SpawnPacket("climable_object", "truncated");
	TruncatedPacket.pop_back();
	AppendChunk(File, 0, TruncatedPacket);
	Ok &= Expect(ReadSpawn(File, Ladders) == SpawnStatus::Invalid && Ladders.empty(),
		"A complete chunk cannot hide a truncated entity state.");
	File.clear();
	AppendChunk(File, 0, SpawnPacket("climable_object", "future", 136));
	Ok &= Expect(ReadSpawn(File, Ladders) == SpawnStatus::Unsupported && Ladders.empty(),
		"An unknown object version cannot be decoded using guessed offsets.");
	File[3] = 0x80;
	Ok &= Expect(ReadSpawn(File, Ladders) == SpawnStatus::Unsupported, "Compressed spawn chunks are detected explicitly.");
	return Ok;
}

static Buffer Grid(u32 Version, u32 NodeSize, u32 Bits)
{
	Buffer Bytes;
	Put(Bytes, Version);
	Put(Bytes, 4);
	PutFloat(Bytes, .7f);
	PutFloat(Bytes, 8.f);
	PutPoint(Bytes, {});
	PutPoint(Bytes, {2.1f, 8.f, 1.4f});
	Put(Bytes, 0x42);
	Put(Bytes, 0);
	Put(Bytes, 0x27);
	Put(Bytes, 0);
	for (u32 Index = 0; Index < 4; ++Index)
	{
		Buffer Node(NodeSize - 5, 0xff);
		const u32 Adjacent = (Index + 2) % 4;
		for (u32 Bit = 0; Bit < Bits; ++Bit)
		{
			if (!(Adjacent & (1u << Bit)))
			{
				const u32 PackedBit = Index * Bits + Bit;
				Node[PackedBit / 8] &= u8(~(1u << (PackedBit % 8)));
			}
		}
		Put(Node, Index < 2 ? 3 : 6, 3);
		Put(Node, Index % 2 ? 32768 : 0, 2);
		Bytes.insert(Bytes.end(), Node.begin(), Node.end());
	}
	return Bytes;
}

static Buffer Collision(u32 Version, xr_span<const Triangle> Triangles)
{
	Buffer Bytes;
	Put(Bytes, Version);
	Put(Bytes, u32(Triangles.size() * 3));
	Put(Bytes, u32(Triangles.size()));
	PutPoint(Bytes, {-8, -8, -8});
	PutPoint(Bytes, {8, 8, 8});
	for (const auto& Triangle : Triangles)
	{
		for (const auto& Point : Triangle.Points) { PutPoint(Bytes, Point); }
	}
	for (u32 Index = 0; Index < Triangles.size(); ++Index)
	{
		Put(Bytes, Index * 3);
		Put(Bytes, Index * 3 + 1);
		Put(Bytes, Index * 3 + 2);
		Put(Bytes, 0);
	}
	return Bytes;
}

static bool Landings()
{
	std::vector<WalkNode> Nodes;
	const bool GridFits = ReadWalkGrid(Grid(13, 25, 26), Nodes);
	bool Ok = Expect(GridFits && Nodes.size() == 4 && Nodes[0].CanEnter && Nodes[1].CanLeave &&
		Near(Nodes[1].Position, {0.7f, 4.f, 0.f}),
		"Stacked native nodes preserve their IDs, floor height and adjacency.");
	Ok &= Expect(ReadWalkGrid(Grid(10, 23, 23), Nodes) && Nodes.size() == 4 && Nodes[2].CanEnter &&
		Nodes[3].CanLeave && Nodes[1].CanEnter, "Stock CoP grids preserve their node coordinates and links.");
	Ok &= Expect(ReadWalkGrid(Grid(8, 21, 23), Nodes) && Nodes.size() == 4 && Nodes[1].CanEnter,
		"Version 8 grids preserve their shorter native node layout.");
	auto BadGrid = Grid(11, 24, 25);
	BadGrid[56 + 24 * 3 + 19] = 0;
	Ok &= Expect(!ReadWalkGrid(BadGrid, Nodes) && Nodes.empty(), "An out-of-order XZ index invalidates the grid.");
	ReadWalkGrid(Grid(13, 25, 26), Nodes);

	const Triangle Floor0{{{{-2, 0, -3}, {-2, 0, 3}, {5, 0, 0}}}};
	const Triangle Floor4{{{{-2, 4, -3}, {-2, 4, 3}, {5, 4, 0}}}};
	const Triangle Blocker{{{{0.5f, 0.25f, -1}, {0.5f, 2, 0}, {0.5f, 0.25f, 1}}}};
	const Triangle NearMiss{{{{0.5f, 0.25f, 0.6f}, {0.5f, 2, 0.6f}, {0.5f, 0.25f, 1}}}};
	const Triangle Floors[] = {Floor0, Floor4, NearMiss};
	std::vector<Triangle> Faces;
	Ok &= Expect(AppendCollision(Collision(4, {Floors, 1}), 4, Faces) &&
		AppendCollision(Collision(6, {Floors + 1, 2}), 6, Faces) && Faces.size() == 3,
		"Vanilla and split collision use the same triangle validation.");
	auto BadCollision = Collision(6, {Floors, 1});
	BadCollision[36 + 36] = 20;
	Ok &= Expect(!AppendCollision(BadCollision, 6, Faces) && Faces.size() == 3,
		"Invalid triangle indices do not leave a partially appended collision part.");

	const auto Shape = BuildGeometry({0, 2, 0}, SpawnBasis({}), {0.f, 2.f, .45f});
	const Ladder Ladders[]{{"ladder", {}, Shape, true}, {"ladder", {}, Shape, false}, {"broken", {}, {}, true}};
	auto Results = FindCandidates(Ladders, Nodes, Faces);
	if (!Expect(Results.size() == 3, "Every semantic ladder produces a diagnostic."))
	{
		return false;
	}
	Ok &= Expect(Results[0].Status == CandidateStatus::Ready && Results[0].Lower == 0 && Results[0].Upper == 1 &&
		Results[0].Cost >= 4.f, "Matching free and supported landings produce a bidirectional candidate.");
	Ok &= Expect(Results[1].Status == CandidateStatus::Disabled && Results[2].Status == CandidateStatus::InvalidShape,
		"A disabled duplicate does not shadow the active ladder.");
	Faces.push_back(Blocker);
	Results = FindCandidates(Ladders, Nodes, Faces);
	Ok &= Expect(Results[0].Status == CandidateStatus::BlockedLower, "A wall across the foot approach prevents a usable link.");
	Results = FindCandidates(Ladders, Nodes, {Floors, 1});
	Ok &= Expect(Results[0].Status == CandidateStatus::BlockedUpper, "Floating native nodes cannot supply an upper landing.");
	const Triangle Underside[] = {Floor0, {{{Floor4.Points[1], Floor4.Points[0], Floor4.Points[2]}}}};
	Results = FindCandidates(Ladders, Nodes, Underside);
	Ok &= Expect(Results[0].Status == CandidateStatus::BlockedUpper, "A ceiling underside cannot provide ground support.");
	if (!Nodes.empty())
	{
		Nodes[1].Position.Y = 2.f;
	}
	Results = FindCandidates(Ladders, Nodes, Floors);
	Ok &= Expect(Results[0].Status == CandidateStatus::MissingUpper, "The wrong layer in the same XZ cell cannot replace an upper landing.");
	const Ladder Duplicate[]{{"ladder", {}, Shape, true}, {"ladder", {}, Shape, true}};
	Results = FindCandidates(Duplicate, Nodes, Faces);
	Ok &= Expect(Results[0].Status == CandidateStatus::DuplicateName && Results[1].Status == CandidateStatus::DuplicateName,
		"Duplicate live names are both rejected before choosing endpoints.");
	return Ok;
}

bool Run()
{
	bool Ok = GeometryAndSpawn();
	Ok &= Landings();
	if (Ok)
	{
		std::puts("Arena ladder bake checks passed");
	}
	return Ok;
}
}
