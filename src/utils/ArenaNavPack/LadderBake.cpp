#include "LadderBake.h"

#include <bit>
#include <limits>
#include <map>

namespace Multiplayer::ArenaLadders::Bake
{
namespace
{
u32 ReadInteger(Bytes Data, std::size_t Offset, u32 Count = 4)
{
	u32 Result = 0;
	for (u32 Index = 0; Index < Count; ++Index)
	{
		Result |= u32(Data[Offset + Index]) << (8 * Index);
	}
	return Result;
}

f32 ReadFloat(Bytes Data, std::size_t Offset)
{
	return std::bit_cast<f32>(ReadInteger(Data, Offset));
}

Point ReadPoint(Bytes Data, std::size_t Offset)
{
	return {ReadFloat(Data, Offset), ReadFloat(Data, Offset + 4), ReadFloat(Data, Offset + 8)};
}

class PacketReader
{
	Bytes Remaining;
	bool Valid = true;

public:
	explicit PacketReader(Bytes Data) : Remaining(Data) {}
	bool Good() const { return Valid; }
	bool Empty() const { return Remaining.empty(); }

	Bytes Take(std::size_t Size)
	{
		if (Size > Remaining.size())
		{
			Valid = false;
			return {};
		}
		const auto Slice = Remaining.first(Size);
		Remaining = Remaining.subspan(Size);
		return Slice;
	}

	u32 Integer(u32 Size)
	{
		const auto Slice = Take(Size);
		return Slice.empty() ? 0 : ReadInteger(Slice, 0, Size);
	}

	Point Vector()
	{
		const auto Slice = Take(12);
		return Slice.empty() ? Point{} : ReadPoint(Slice, 0);
	}

	xr_string_view String()
	{
		const auto Null = std::find(Remaining.begin(), Remaining.end(), 0);
		if (Null == Remaining.end())
		{
			Valid = false;
			return {};
		}
		const auto Size = std::size_t(Null - Remaining.begin());
		const auto Slice = Take(Size + 1);
		return {reinterpret_cast<const char*>(Slice.data()), Size};
	}
};

constexpr xr_string_view ClimbableSection = "climable_object";

SpawnStatus ReadClimbable(PacketReader& Packet, std::vector<Ladder>& Parsed)
{
	auto Name = Packet.String();
	if (Name.empty())
	{
		Name = ClimbableSection;
	}
	Packet.Take(2); // legacy mode and respawn point
	const auto Origin = Packet.Vector();
	const auto Euler = Packet.Vector();
	Packet.Take(4); // respawn time and entity ID
	const auto Parent = Packet.Integer(2);
	Packet.Take(2); // phantom ID
	const auto Flags = Packet.Integer(2);
	const auto Version = Packet.Integer(2);
	if (!Packet.Good())
	{
		return SpawnStatus::Invalid;
	}
	// CSE_Abstract's game mask arrived in 121. Climable's material arrived in 127.
	if (!(Flags & (1u << 5)) || Version < 121 || Version > 135)
	{
		return SpawnStatus::Unsupported;
	}
	const bool Deathmatch = (Packet.Integer(2) & 2u) != 0;
	Packet.Take(2); // script version
	Packet.Take(Packet.Integer(2)); // client data
	Packet.Take(2); // spawn ID
	const auto StateSize = Packet.Integer(2);
	if (StateSize < 2)
	{
		return SpawnStatus::Invalid;
	}
	PacketReader State(Packet.Take(StateSize - 2));
	State.Take(18); // CSE_ALifeObject: graph ID, distance, direct control, node, flags
	State.String(); // custom data
	State.Take(8); // story IDs
	const u32 ShapeCount = State.Integer(1);
	std::optional<Geometry> Shape;
	for (u32 Index = 0; Index < ShapeCount; ++Index)
	{
		const auto Type = State.Integer(1);
		switch (Type)
		{
		case 0:
			State.Take(16); // sphere
			break;
		case 1:
		{
			const auto I = State.Vector();
			const auto J = State.Vector();
			const auto K = State.Vector();
			const auto Center = State.Vector();
			// The compiler writes one origin-centered diagonal box. Other shape semantics need author review.
			if (ShapeCount == 1 && Parent == 0xffff && Finite(Euler) && I.Y == 0 && I.Z == 0 && J.X == 0 &&
				J.Z == 0 && K.X == 0 && K.Y == 0 && Center.X == 0 && Center.Y == 0 && Center.Z == 0)
			{
				Shape = BuildGeometry(Origin, SpawnBasis(Euler), {I.X, J.Y, K.Z});
			}
			break;
		}
		default:
			return SpawnStatus::Unsupported;
		}
	}
	const auto Material = Version >= 127 ? State.String() : xr_string_view(R"(materials\fake_ladders)");
	if (!Packet.Good() || !Packet.Empty() || !State.Good() || !State.Empty())
	{
		return SpawnStatus::Invalid;
	}
	if (Name.size() >= 64 || !std::all_of(Name.begin(), Name.end(), [](char Ch) { return Ch >= 33 && Ch <= 126; }))
	{
		Name = {};
		Shape.reset();
	}
	Parsed.push_back({Name, Material, Shape, Deathmatch});
	return SpawnStatus::Ready;
}

bool Separates(Point Axis, Point First, Point Second, Point Third, Point Half)
{
	const f32 A = Dot(Axis, First), B = Dot(Axis, Second), C = Dot(Axis, Third);
	const f32 Radius = std::abs(Axis.X) * Half.X + std::abs(Axis.Y) * Half.Y + std::abs(Axis.Z) * Half.Z;
	return std::min({A, B, C}) > Radius || std::max({A, B, C}) < -Radius;
}

bool IntersectsBox(Point First, Point Second, Point Third, Point Half)
{
	constexpr Basis Axes{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
	for (const auto Axis : Axes)
	{
		if (Separates(Axis, First, Second, Third, Half))
		{
			return false;
		}
	}
	const Basis Edges{Second - First, Third - Second, First - Third};
	if (Separates(Cross(Edges[0], Edges[1]), First, Second, Third, Half))
	{
		return false;
	}
	for (const auto Edge : Edges)
	{
		for (const auto Axis : Axes)
		{
			if (Separates(Cross(Edge, Axis), First, Second, Third, Half))
			{
				return false;
			}
		}
	}
	return true;
}

bool SupportsFeet(Point Feet, const Triangle& Face)
{
	const auto Offset = Feet - Face.Points[0];
	const auto U = Face.Points[1] - Face.Points[0];
	const auto V = Face.Points[2] - Face.Points[0];
	const auto Normal = Cross(U, V);
	const f32 Denominator = -Normal.Y;
	if (Normal.Y <= 0.00001f || Normal.Y < 0.7f * std::sqrt(Dot(Normal, Normal)))
	{
		return false;
	}
	const f32 Alpha = (Offset.X * V.Z - Offset.Z * V.X) / Denominator;
	const f32 Beta = (U.X * Offset.Z - U.Z * Offset.X) / Denominator;
	const f32 Height = Offset.Y - Alpha * U.Y - Beta * V.Y;
	return Alpha >= -0.0001f && Beta >= -0.0001f && Alpha + Beta <= 1.0001f && Height >= -0.1f && Height <= 0.23f;
}

// The standing stalker profile uses a 0.35 m half-width and 1.8 m body height.
// The box encloses both the standing pose and the straight approach to the climb face.
constexpr f32 BodyHalfWidth = 0.35f;
constexpr f32 BodyHeight = 1.8f;
constexpr f32 MountOffset = BodyHalfWidth + 0.025f;

bool ClearLanding(Point Feet, Point End, Point Facing, f32 Lift, xr_span<const Triangle> Triangles)
{
	const Point Side{Facing.Z, 0, -Facing.X};
	const auto Local = [&](Point World)
	{
		const auto Relative = World - Feet;
		return Point{Dot(Relative, Facing), Relative.Y, Dot(Relative, Side)};
	};
	const auto Mount = Local(End + Facing * MountOffset);
	const Point Min{std::min(Mount.X, 0.f) - BodyHalfWidth, 0.12f, std::min(Mount.Z, 0.f) - BodyHalfWidth};
	const Point Max{std::max(Mount.X, 0.f) + BodyHalfWidth, std::max(Mount.Y, 0.f) + Lift + BodyHeight,
		std::max(Mount.Z, 0.f) + BodyHalfWidth};
	const auto Center = (Min + Max) * 0.5f;
	const auto Half = (Max - Min) * 0.5f;
	bool Supported = false;
	for (const auto& Face : Triangles)
	{
		if (IntersectsBox(Local(Face.Points[0]) - Center, Local(Face.Points[1]) - Center,
			Local(Face.Points[2]) - Center, Half))
		{
			return false;
		}
		Supported = Supported || SupportsFeet(Feet, Face);
	}
	return Supported;
}

struct Landing
{
	u32 Node;
	f32 Rank;
};

std::vector<Landing> FindLandings(Point End, Point Facing, xr_span<const WalkNode> Nodes)
{
	std::vector<Landing> Choices;
	const auto Approach = End + Facing * MountOffset;
	for (u32 Index = 0; Index < Nodes.size(); ++Index)
	{
		const auto& Node = Nodes[Index];
		if (Node.CanEnter && Node.CanLeave && NearLadderEnd(Node.Position, End))
		{
			const auto Delta = Node.Position - Approach;
			Choices.push_back({Index, Dot(Delta, Delta)});
		}
	}
	std::stable_sort(Choices.begin(), Choices.end(), [](const Landing& A, const Landing& B) { return A.Rank < B.Rank; });
	return Choices;
}

Candidate ValidateShape(const Geometry& Shape, xr_span<const WalkNode> Nodes, xr_span<const Triangle> Triangles)
{
	const auto Lower = Shape.Lower();
	const auto Upper = Shape.Upper();
	if (!SupportsClimbing(Lower, Upper, Shape.Axis, Shape.Normal))
	{
		return {CandidateStatus::UnsupportedGeometry};
	}
	Point Facing{Shape.Normal.X, 0, Shape.Normal.Z};
	Facing = Facing * (1.f / std::sqrt(Dot(Facing, Facing)));
	auto LowerChoices = FindLandings(Lower, Facing, Nodes);
	auto UpperChoices = FindLandings(Upper, Facing, Nodes);
	if (LowerChoices.empty() || UpperChoices.empty())
	{
		return {LowerChoices.empty() ? CandidateStatus::MissingLower : CandidateStatus::MissingUpper};
	}
	const auto Blocked = [&](Point End, f32 Lift, Landing Item)
	{
		return !ClearLanding(Nodes[Item.Node].Position, End, Facing, Lift, Triangles);
	};
	std::erase_if(LowerChoices, [&](Landing Item) { return Blocked(Lower, 0.10f, Item); });
	std::erase_if(UpperChoices, [&](Landing Item) { return Blocked(Upper, 0.20f, Item); });
	if (LowerChoices.empty() || UpperChoices.empty())
	{
		return {LowerChoices.empty() ? CandidateStatus::BlockedLower : CandidateStatus::BlockedUpper};
	}
	f32 Best = std::numeric_limits<f32>::infinity();
	Candidate Result{CandidateStatus::Incompatible};
	for (const auto From : LowerChoices)
	{
		for (const auto To : UpperChoices)
		{
			const auto LowerNode = Nodes[From.Node].Position;
			const auto UpperNode = Nodes[To.Node].Position;
			if (From.Rank + To.Rank >= Best || !MatchesLandings(Lower, Upper, LowerNode, UpperNode))
			{
				continue;
			}
			Best = From.Rank + To.Rank;
			const auto Delta = Upper - Lower;
			Result = {CandidateStatus::Ready, From.Node, To.Node,
				std::sqrt(Dot(Delta, Delta)) + std::sqrt(From.Rank) + std::sqrt(To.Rank)};
		}
	}
	return Result;
}
}

SpawnStatus ReadSpawn(Bytes Data, std::vector<Ladder>& Ladders)
{
	Ladders.clear();
	std::vector<Ladder> Parsed;
	PacketReader Chunks(Data);
	while (!Chunks.Empty())
	{
		const auto Id = Chunks.Integer(4);
		const auto Size = Chunks.Integer(4);
		const auto Payload = Chunks.Take(Size);
		if (!Chunks.Good() || Size < 3 || Size > 16 * 1024)
		{
			return SpawnStatus::Invalid;
		}
		if (Id & 0x80000000u)
		{
			return SpawnStatus::Unsupported;
		}
		PacketReader Packet(Payload);
		const auto Message = Packet.Integer(2);
		const auto Section = Packet.String();
		if (!Packet.Good() || Message != 1 || Section.empty())
		{
			return SpawnStatus::Invalid;
		}
		if (Section == ClimbableSection)
		{
			const auto Status = ReadClimbable(Packet, Parsed);
			if (Status != SpawnStatus::Ready)
			{
				return Status;
			}
		}
	}
	Ladders.swap(Parsed);
	return SpawnStatus::Ready;
}

bool ReadWalkGrid(Bytes Data, std::vector<WalkNode>& Nodes)
{
	using namespace ArenaNavigation;
	Nodes.clear();
	const auto Grid = ReadGridIdentity(Data);
	if (!Grid)
	{
		return false;
	}
	const auto Version = ReadInteger(Data, 0);
	const u32 NodeSize = Version == 8 ? 21 : Version == 10 ? 23 : Version == 11 ? 24 : 25;
	const u32 LinkBits = Version <= 10 ? 23 : Version == 11 ? 25 : 26;
	const f32 Cell = ReadFloat(Data, 8);
	const f32 Height = ReadFloat(Data, 12);
	const auto Min = ReadPoint(Data, 16);
	const auto Max = ReadPoint(Data, 28);
	const u32 Rows = u32(std::floor((Max.Z - Min.Z) / Cell + 0.001f + 1.5f));
	const u32 Columns = u32(std::floor((Max.X - Min.X) / Cell + 0.001f + 1.5f));

	std::vector<WalkNode> Parsed(Grid->Nodes);
	u32 Previous = 0;
	for (u32 Index = 0; Index < Grid->Nodes; ++Index)
	{
		const auto Node = Data.subspan(GridHeaderSize + std::size_t(Index) * NodeSize, NodeSize);
		const u32 XZ = ReadInteger(Node, NodeSize - 5, 3);
		const u32 Y = ReadInteger(Node, NodeSize - 2, 2);
		const Point Position{Min.X + f32(XZ / Rows) * Cell, Min.Y + f32(Y) / 65535.f * Height,
			Min.Z + f32(XZ % Rows) * Cell};
		if (XZ >= Rows * Columns || XZ < Previous || !Finite(Position))
		{
			return false;
		}
		Previous = XZ;
		Parsed[Index].Position = Position;
		for (u32 Slot = 0; Slot < 4; ++Slot)
		{
			const u32 StartBit = Slot * LinkBits;
			const u32 To = (ReadInteger(Node, StartBit / 8) >> (StartBit % 8)) & ((1u << LinkBits) - 1);
			if (To < Grid->Nodes && To != Index)
			{
				Parsed[Index].CanLeave = true;
				Parsed[To].CanEnter = true;
			}
		}
	}
	Nodes.swap(Parsed);
	return true;
}

bool AppendCollision(Bytes Data, u32 ExpectedVersion, std::vector<Triangle>& Triangles)
{
	using ArenaNavigation::CollisionHeaderSize;
	if (Data.size() < CollisionHeaderSize || ReadInteger(Data, 0) != ExpectedVersion)
	{
		return false;
	}
	const u32 VertexCount = ReadInteger(Data, 4);
	const u32 FaceCount = ReadInteger(Data, 8);
	const u64 FacesOffset = CollisionHeaderSize + u64(VertexCount) * 12;
	if (FacesOffset + u64(FaceCount) * 16 != Data.size())
	{
		return false;
	}
	std::vector<Triangle> Parsed;
	Parsed.reserve(FaceCount);
	for (u32 Index = 0; Index < FaceCount; ++Index)
	{
		const auto Face = std::size_t(FacesOffset) + std::size_t(Index) * 16;
		Triangle Output;
		for (u32 Slot = 0; Slot < 3; ++Slot)
		{
			const auto Vertex = ReadInteger(Data, Face + Slot * 4);
			if (Vertex >= VertexCount)
			{
				return false;
			}
			Output.Points[Slot] = ReadPoint(Data, CollisionHeaderSize + std::size_t(Vertex) * 12);
			if (!Finite(Output.Points[Slot]))
			{
				return false;
			}
		}
		Parsed.push_back(Output);
	}
	Triangles.insert(Triangles.end(), Parsed.begin(), Parsed.end());
	return true;
}

std::vector<Candidate> FindCandidates(xr_span<const Ladder> Ladders, xr_span<const WalkNode> Nodes, xr_span<const Triangle> Triangles)
{
	std::map<xr_string_view, u32> ActiveNames;
	for (const auto& Ladder : Ladders)
	{
		if (Ladder.Deathmatch && !Ladder.Name.empty())
		{
			++ActiveNames[Ladder.Name];
		}
	}
	std::vector<Candidate> Candidates;
	Candidates.reserve(Ladders.size());
	for (const auto& Ladder : Ladders)
	{
		if (!Ladder.Deathmatch)
		{
			Candidates.push_back({CandidateStatus::Disabled});
		}
		else if (!Ladder.Name.empty() && ActiveNames.at(Ladder.Name) > 1)
		{
			Candidates.push_back({CandidateStatus::DuplicateName});
		}
		else if (Ladder.Name.empty() || !Ladder.Shape)
		{
			Candidates.push_back({CandidateStatus::InvalidShape});
		}
		else
		{
			Candidates.push_back(ValidateShape(*Ladder.Shape, Nodes, Triangles));
		}
	}
	return Candidates;
}

const char* Describe(CandidateStatus Status)
{
	switch (Status)
	{
	case CandidateStatus::Ready: return "validated static landing candidates";
	case CandidateStatus::Disabled: return "disabled in deathmatch";
	case CandidateStatus::DuplicateName: return "duplicate live ladder name";
	case CandidateStatus::InvalidShape: return "unsupported box or entity identity";
	case CandidateStatus::UnsupportedGeometry: return "outside the stalker climb profile";
	case CandidateStatus::MissingLower: return "no connected lower walk node on the landing layer";
	case CandidateStatus::MissingUpper: return "no connected upper walk node on the landing layer";
	case CandidateStatus::BlockedLower: return "lower landing blocked or unsupported";
	case CandidateStatus::BlockedUpper: return "upper landing blocked or unsupported";
	case CandidateStatus::Incompatible: return "landings do not form a compatible pair";
	}
	return "invalid ladder candidate";
}
}
