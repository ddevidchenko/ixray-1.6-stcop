#include "../../../xrGame/Multiplayer/ArenaNavigationPackage.h"

#include <array>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace ArenaNavigationPackageTests
{
using namespace Multiplayer::ArenaNavigation;
using Multiplayer::ArenaLadders::Record;

static bool Expect(const char* Case, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Case);
	}
	return Passed;
}

struct Fixture
{
	Identity Expected{"arena", "1.0", AgentProfile, {{0x42, 0xab}, 8}, 2};
	std::map<xr_string_view, std::string> Effective{
		{"level.ai", "grid"}, {"level.cform", "descriptor"}, {"level_0.cform", "lower"}, {"level_1.cform", "upper"},
		{"level.game", "respawns"}, {"level.spawn", "clmbl#0"}, {"gamemtl.xr", "materials"}
	};
	std::vector<Record> Links;
	std::string Payload = "arena_ladders 1 42 ab\nclmbl#0 up 2 6 4\n";

	std::string Package() const
	{
		std::string Text = "arena_navlinks 2\nmap arena 1.0 stalker-v1\nparts 2\n";
		for (const auto Name : {"level.ai", "level.cform", "level_0.cform", "level_1.cform", "level.game", "level.spawn", "gamemtl.xr"})
		{
			const auto File = Effective.find(Name);
			Text += Name;
			Text += ' ';
			if (File == Effective.end())
			{
				Text += '-';
			}
			else
			{
				const auto Hash = Sha256(AsBytes(File->second));
				Text.append(Hash.data(), Hash.size());
			}
			Text += '\n';
		}
		Text += "links " + std::to_string(Payload.size()) + " ";
		const auto Hash = Sha256(AsBytes(Payload));
		Text.append(Hash.data(), Hash.size());
		return Text + '\n' + Payload;
	}

	Result Open(xr_string_view Package)
	{
		return Validate(Package, Expected, Links, [&](xr_string_view Name)
		{
			const auto File = Effective.find(Name);
			if (File == Effective.end())
			{
				return AssetDigest{};
			}
			if (File->second.empty())
			{
				return AssetDigest{AssetState::Unreadable};
			}
			return AssetDigest{AssetState::Present, Sha256(AsBytes(File->second))};
		});
	}
};

static bool AssetIdentity()
{
	Fixture Map;
	const auto Package = Map.Package();
	bool Ok = Expect("same effective map, profile, grid and assets publish the authored link",
		Map.Open(Package).Code == Status::Ready && Map.Links.size() == 1 &&
		Map.Links[0].Name == "clmbl#0" && Map.Links[0].Entry == 2 && Map.Links[0].Exit == 6);
	for (const auto Name : {"level.ai", "level.cform", "level_0.cform", "level_1.cform", "level.game", "level.spawn", "gamemtl.xr"})
	{
		Map.Effective[Name].push_back('!');
		const auto Error = Map.Open(Package);
		Ok &= Expect(Name, Error.Code == Status::StaleAsset && xr_string_view(Error.File.data()) == Name && Map.Links.empty());
		Map.Effective[Name].pop_back();
	}
	Map.Effective.erase("level.ai");
	const auto Missing = Map.Open(Package);
	Ok &= Expect("missing required VFS bytes identify level.ai", Missing.Code == Status::MissingAsset &&
		xr_string_view(Missing.File.data()) == "level.ai" && Map.Links.empty());
	Map.Effective["level.ai"] = "";
	Ok &= Expect("unreadable is distinct from an absent file", Map.Open(Package).Code == Status::UnreadableAsset);
	Map.Effective["level.ai"] = "grid";

	Map.Expected.Map = "arena_variant";
	Ok &= Expect("package for another map is rejected with the same bytes", Map.Open(Package).Code == Status::WrongMap);
	Map.Expected.Map = "arena";
	Map.Expected.Version = "2.0";
	Ok &= Expect("map version must also match", Map.Open(Package).Code == Status::WrongVersion);
	Map.Expected.Version = "1.0";
	Map.Expected.Agent = "stalker-v2";
	Ok &= Expect("changed agent capability profile invalidates package", Map.Open(Package).Code == Status::WrongAgent);
	Map.Expected.Agent = AgentProfile;
	Map.Expected.Grid.Guid[1] = 0xff;
	Ok &= Expect("same node IDs cannot attach to another grid GUID", Map.Open(Package).Code == Status::InvalidLinks);
	Map.Expected.Grid.Guid[1] = 0xab;
	Map.Expected.CollisionParts = 1;
	Ok &= Expect("split layout must match the effective descriptor", Map.Open(Package).Code == Status::InvalidPartCount);

	Map.Expected.CollisionParts = 2;
	Map.Effective.erase("level.spawn");
	const auto MissingSpawn = Map.Package();
	Ok &= Expect("typed ladder records need the spawn asset", Map.Open(MissingSpawn).Code == Status::MissingAsset);
	Map.Payload = "arena_ladders 1 42 ab\n";
	const auto WalkOnly = Map.Package();
	Ok &= Expect("walk-only maps explicitly declaring absent level.spawn are valid",
		Map.Open(WalkOnly).Code == Status::Ready && Map.Links.empty());
	Map.Effective["level.spawn"] = "spawn added by overlay";
	Ok &= Expect("unexpected VFS spawn override changes a walk-only bake", Map.Open(WalkOnly).Code == Status::StaleAsset);
	return Ok;
}

static bool Corruption()
{
	Fixture Map;
	const auto Valid = Map.Package();
	const auto Mutate = [&](xr_string_view Before, xr_string_view After)
	{
		auto Text = Valid;
		Text.replace(Text.find(Before), Before.size(), After);
		return Text;
	};
	bool Ok = Expect("bot-enabled load requires a package", Map.Open({}).Code == Status::MissingPackage);
	Ok &= Expect("version 1 source is distinct from the packaged format",
		Map.Open(Map.Payload).Code == Status::InvalidVersion);
	Ok &= Expect("oversized packages are rejected before processing", Map.Open(std::string(MaxPackageBytes + 1, 'a')).Code == Status::InvalidSize);
	Ok &= Expect("out-of-range collision part count cannot drive allocations",
		Map.Open(Mutate("parts 2", "parts 4294967295")).Code == Status::InvalidPartCount);
	Ok &= Expect("manifest cannot redirect file resolution", Map.Open(Mutate("level_0.cform", "../level.cform")).Code == Status::InvalidManifest);
	Ok &= Expect("duplicate manifest entries fail in schema order", Map.Open(Mutate("level_1.cform", "level_0.cform")).Code == Status::InvalidManifest);
	Ok &= Expect("truncated or appended payload bytes fail even after a valid package",
		Map.Open(Valid).Code == Status::Ready && Map.Open(xr_string_view(Valid).substr(0, Valid.size() - 1)).Code == Status::InvalidPayload &&
		Map.Links.empty() && Map.Open(Valid + "x").Code == Status::InvalidPayload);
	Ok &= Expect("payload length overflow fails before consuming the following bytes",
		Map.Open(Mutate("links " + std::to_string(Map.Payload.size()), "links 4294967296")).Code == Status::InvalidPayload);
	Ok &= Expect("required assets cannot be declared absent",
		Map.Open(Mutate("level.game " + std::string(Sha256(AsBytes("respawns")).data(), 64), "level.game -")).Code ==
			Status::InvalidManifest);
	Ok &= Expect("same-length payload mutation fails its digest",
		Map.Open(Mutate("up 2 6", "up 3 6")).Code == Status::InvalidPayload);
	Map.Payload = "arena_ladders 1 42 ab\nclmbl#0 up 2 8 4\n";
	const auto InvalidNode = Map.Package();
	Ok &= Expect("matching payload hash cannot authorize invalid native node IDs", Map.Open(InvalidNode).Code == Status::InvalidLinks);
	return Ok;
}

static bool NativeMetadata()
{
	xr_array<u8, 40> Descriptor{};
	Descriptor[0] = 4;
	bool Ok = Expect("vanilla collision has no sibling parts", ReadCollisionParts(xr_span(Descriptor).first(36)) == 0);
	Descriptor[0] = 5;
	Descriptor[36] = 2;
	Ok &= Expect("split layout reads its little-endian count", ReadCollisionParts(Descriptor) == 2);
	Ok &= Expect("truncated split descriptor cannot expose parts", !ReadCollisionParts(xr_span(Descriptor).first(39)));
	Descriptor[37] = 4;
	Ok &= Expect("split count is bounded", !ReadCollisionParts(Descriptor));

	xr_array<u8, 56 + 24> Grid{};
	Grid[0] = 11;
	Grid[4] = 1;
	Grid[14] = Grid[10] = 0x80;
	Grid[15] = Grid[11] = 0x3f;
	Grid[40] = 0x42;
	Grid[48] = 0xab;
	const auto Identity = ReadGridIdentity(Grid);
	Ok &= Expect("native grid header exposes real node count and GUID", Identity && Identity->Nodes == 1 &&
		Identity->Guid == xr_array<u64, 2>{0x42, 0xab});
	Grid[0] = 12;
	Ok &= Expect("unknown native level.ai versions are rejected", !ReadGridIdentity(Grid));
	Grid[0] = 11;
	Grid[4] = 2;
	Ok &= Expect("a native node count cannot exceed the asset size", !ReadGridIdentity(Grid));
	const auto Hash = Sha256(AsBytes("abc"));
	Ok &= Expect("SHA-256 matches standard tooling",
		xr_string_view(Hash.data(), Hash.size()) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	return Ok;
}

bool Run()
{
	const bool Identity = AssetIdentity();
	const bool Package = Corruption();
	const bool Native = NativeMetadata();
	if (Identity && Package && Native)
	{
		std::puts("Arena navigation package checks passed");
		return true;
	}
	return false;
}
}
