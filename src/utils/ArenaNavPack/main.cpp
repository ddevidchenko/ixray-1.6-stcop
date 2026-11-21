#include "LadderBake.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
namespace fs = std::filesystem;

using namespace Multiplayer::ArenaNavigation;
using Multiplayer::ArenaLadders::Record;
namespace LadderBake = Multiplayer::ArenaLadders::Bake;
using AssetMap = std::map<std::string, AssetDigest, std::less<>>;

std::vector<u8> ReadFile(const fs::path& Path, std::size_t Limit = std::numeric_limits<std::size_t>::max())
{
	std::ifstream Input(Path, std::ios::binary | std::ios::ate);
	const auto Size = Input.tellg();
	if (Size <= 0 || static_cast<u64>(Size) > Limit || Size > std::numeric_limits<std::streamsize>::max())
	{
		throw std::runtime_error("missing, empty or oversized input: " + Path.string());
	}
	std::vector<u8> Data(static_cast<std::size_t>(Size));
	Input.seekg(0);
	if (!Input.read(reinterpret_cast<char*>(Data.data()), std::streamsize(Size)))
	{
		throw std::runtime_error("unable to read input: " + Path.string());
	}
	return Data;
}

void ExtractLadders(std::ostream& Output, const fs::path& Directory, Bytes GridBytes, Bytes Collision, u32 Parts,
	AssetMap& Assets)
{
	const auto Spawn = ReadFile(Directory / "level.spawn");
	Assets.emplace("level.spawn", AssetDigest{AssetState::Present, Sha256(Spawn)});
	std::vector<LadderBake::Ladder> Ladders;
	const auto Spawns = LadderBake::ReadSpawn(Spawn, Ladders);
	if (Spawns != LadderBake::SpawnStatus::Ready)
	{
		throw std::runtime_error(Spawns == LadderBake::SpawnStatus::Unsupported ?
			"level.spawn: unsupported chunk or climbable spawn format" : "level.spawn: invalid chunk or climbable packet");
	}
	std::vector<LadderBake::WalkNode> Nodes;
	if (!LadderBake::ReadWalkGrid(GridBytes, Nodes))
	{
		throw std::runtime_error("level.ai: invalid native node data for ladder extraction");
	}
	std::vector<LadderBake::Triangle> Triangles;
	if (Parts == 0 && !LadderBake::AppendCollision(Collision, 4, Triangles))
	{
		throw std::runtime_error("level.cform: invalid static collision triangles");
	}
	for (u32 Part = 0; Part < Parts; ++Part)
	{
		const auto Name = CollisionPartName(Part);
		const auto Bytes = ReadFile(Directory / Name.data());
		if (!LadderBake::AppendCollision(Bytes, 6, Triangles))
		{
			throw std::runtime_error(std::string(Name.data()) + ": invalid static collision triangles");
		}
		Assets.emplace(Name.data(), AssetDigest{AssetState::Present, Sha256(Bytes)});
	}

	const auto Candidates = LadderBake::FindCandidates(Ladders, Nodes, Triangles);
	u32 Links = 0;
	for (std::size_t Index = 0; Index < Candidates.size(); ++Index)
	{
		const auto& Candidate = Candidates[Index];
		const auto& Ladder = Ladders[Index];
		std::cerr << (Ladder.Name.empty() ? "(invalid name)" : Ladder.Name) << ": " << LadderBake::Describe(Candidate.Status);
		if (!Ladder.Material.empty() && std::all_of(Ladder.Material.begin(), Ladder.Material.end(),
			[](char Ch) { return Ch >= 32 && Ch <= 126; }))
		{
			std::cerr << " material=" << Ladder.Material;
		}
		if (Ladder.Shape)
		{
			const auto Lower = Ladder.Shape->Lower();
			const auto Upper = Ladder.Shape->Upper();
			std::cerr << " bottom=(" << Lower.X << ',' << Lower.Y << ',' << Lower.Z <<
				") top=(" << Upper.X << ',' << Upper.Y << ',' << Upper.Z << ')';
		}
		if (Candidate.Status == LadderBake::CandidateStatus::Ready)
		{
			std::cerr << " nodes=" << Candidate.Lower << ',' << Candidate.Upper;
			Links += 2;
			if (Links > Multiplayer::ArenaLadders::MaxLinks)
			{
				std::cerr << '\n';
				throw std::runtime_error("level.spawn: automatic ladder links exceed the package limit");
			}
			Output << Ladder.Name << " up " << Candidate.Lower << ' ' << Candidate.Upper << ' ' << Candidate.Cost << '\n';
			Output << Ladder.Name << " down " << Candidate.Upper << ' ' << Candidate.Lower << ' ' << Candidate.Cost << '\n';
		}
		std::cerr << '\n';
	}
	std::cerr << "ladder candidates=" << Candidates.size() << " directed links=" << Links << '\n';
}

void WritePackage(const fs::path& Directory, const fs::path& Materials, xr_string_view Map,
	xr_string_view Version, const fs::path* LadderSource, bool AutoLadders)
{
	if (!IsIdentifier(Map, 63) || !IsIdentifier(Version, 31, true))
	{
		throw std::runtime_error("invalid map name or version");
	}
	const auto Destination = Directory / PackageName;
	if (fs::exists(Destination))
	{
		throw std::runtime_error("navigation package already exists: " + Destination.string());
	}

	AssetMap Assets;
	const auto GridBytes = ReadFile(Directory / "level.ai");
	Assets.emplace("level.ai", AssetDigest{AssetState::Present, Sha256(GridBytes)});
	const auto Grid = ReadGridIdentity(GridBytes);
	if (!Grid)
	{
		throw std::runtime_error("invalid native level.ai header or node count");
	}
	const auto Collision = ReadFile(Directory / "level.cform");
	Assets.emplace("level.cform", AssetDigest{AssetState::Present, Sha256(Collision)});
	const auto Parts = ReadCollisionParts(Collision);
	if (!Parts)
	{
		throw std::runtime_error("invalid level.cform collision part count or descriptor");
	}

	std::string Payload;
	if (LadderSource)
	{
		const auto Data = ReadFile(*LadderSource, Multiplayer::ArenaLadders::MaxAttachmentBytes);
		Payload.assign(reinterpret_cast<const char*>(Data.data()), Data.size());
	}
	else
	{
		std::ostringstream DefaultLinks;
		DefaultLinks << "arena_ladders 1 " << std::hex << Grid->Guid[0] << ' ' << Grid->Guid[1] << '\n';
		DefaultLinks << std::dec;
		if (AutoLadders)
		{
			ExtractLadders(DefaultLinks, Directory, GridBytes, Collision, *Parts, Assets);
		}
		Payload = DefaultLinks.str();
	}

	std::ostringstream Package;
	Package << SchemaTag << ' ' << SchemaVersion << '\n';
	Package << "map " << Map << ' ' << Version << ' ' << AgentProfile << '\n';
	Package << "parts " << *Parts << '\n';
	ForEachAsset(*Parts, [&](xr_string_view Name)
	{
		const auto Path = Name == "gamemtl.xr" ? Materials : Directory / Name;
		auto Match = Assets.find(Name);
		if (Match == Assets.end())
		{
			AssetDigest Asset;
			if (Name != "level.spawn" || fs::exists(Path))
			{
				Asset = {AssetState::Present, Sha256(ReadFile(Path))};
			}
			Match = Assets.emplace(Name, Asset).first;
		}
		Package << Name << ' ';
		if (Match->second.State == AssetState::Missing)
		{
			Package << '-';
		}
		else
		{
			const auto& Hash = Match->second.Hash;
			Package.write(Hash.data(), Hash.size());
		}
		Package << '\n';
		return true;
	});

	const auto Hash = Sha256(AsBytes(Payload));
	Package << "links " << Payload.size() << ' ';
	Package.write(Hash.data(), Hash.size());
	Package << '\n' << Payload;
	const auto Text = Package.str();
	std::vector<Record> Records;
	const auto Checked = Validate(Text, {Map, Version, AgentProfile, *Grid, *Parts}, Records,
		[&](xr_string_view Name) { return Assets.find(Name)->second; });
	if (Checked.Code != Status::Ready)
	{
		throw std::runtime_error(std::string(Checked.File.data()) + ": " + Describe(Checked.Code));
	}

	std::ofstream Output(Destination, std::ios::binary | std::ios::noreplace);
	Output.write(Text.data(), std::streamsize(Text.size()));
	Output.close();
	if (!Output)
	{
		throw std::runtime_error("could not write navigation package: " + Destination.string());
	}
	const auto& MaterialHash = Assets.at("gamemtl.xr").Hash;
	std::cout.write(MaterialHash.data(), MaterialHash.size());
	std::cout << '\n';
}
}

int main(int ArgumentCount, char** Arguments)
{
	if (ArgumentCount != 5 && ArgumentCount != 6)
	{
		std::cerr << "Usage: ArenaNavPack <staged-map-directory> <gamemtl.xr> <map> <version> [ladder-source | --extract-ladders]\n";
		return 1;
	}
	try
	{
		const xr_string_view Option = ArgumentCount == 6 ? Arguments[5] : "";
		const bool AutoLadders = Option == "--extract-ladders";
		const fs::path Source = AutoLadders ? xr_string_view{} : Option;
		WritePackage(Arguments[1], Arguments[2], Arguments[3], Arguments[4], Source.empty() ? nullptr : &Source, AutoLadders);
		return 0;
	}
	catch (const std::exception& Error)
	{
		std::cerr << Error.what() << '\n';
		return 1;
	}
}
