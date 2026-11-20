#pragma once

#include "ArenaLadderAttachment.h"

#include <optional>

namespace Multiplayer::ArenaNavigation
{
constexpr auto PackageName = "level.navlinks";
constexpr xr_string_view SchemaTag = "arena_navlinks";
constexpr xr_string_view SchemaVersion = "2";
constexpr xr_string_view AgentProfile = "stalker-v1";
constexpr u32 MaxPackageBytes = 64 * 1024;
constexpr u32 MaxCollisionParts = 512;

using Bytes = xr_span<const u8>;
using Digest = xr_array<char, 64>;

inline Bytes AsBytes(xr_string_view Text)
{
	return {reinterpret_cast<const u8*>(Text.data()), Text.size()};
}

Digest Sha256(Bytes Data);

struct GridIdentity
{
	xr_array<u64, 2> Guid;
	u32 Nodes;
};

enum class AssetState { Missing, Unreadable, Present };

struct AssetDigest
{
	AssetState State = AssetState::Missing;
	Digest Hash{};
};

struct Identity
{
	xr_string_view Map;
	xr_string_view Version;
	xr_string_view Agent;
	GridIdentity Grid;
	u32 CollisionParts;
};

enum class Status
{
	Ready, MissingPackage, InvalidSize, InvalidVersion, WrongMap, WrongVersion, WrongAgent,
	InvalidPartCount, InvalidManifest, MissingAsset, UnreadableAsset, StaleAsset,
	InvalidPayload, InvalidLinks
};

struct Result
{
	Status Code;
	xr_array<char, 32> File{};
};

std::optional<u32> ReadCollisionParts(Bytes Data);
std::optional<GridIdentity> ReadGridIdentity(Bytes Data);
const char* Describe(Status Code);

constexpr u32 GridHeaderSize = 56;
constexpr u32 CollisionHeaderSize = 36;

inline bool IsIdentifier(xr_string_view Text, std::size_t Limit, bool Dots = false)
{
	return !Text.empty() && Text.size() <= Limit && std::all_of(Text.begin(), Text.end(), [&](char Ch)
	{
		return (Ch >= 'a' && Ch <= 'z') || (Ch >= 'A' && Ch <= 'Z') || (Ch >= '0' && Ch <= '9') ||
			Ch == '-' || Ch == '_' || (Dots && Ch == '.');
	});
}

inline bool IsDigest(xr_string_view Text)
{
	return Text.size() == Digest{}.size() && Text.find_first_not_of("0123456789abcdef") == xr_string_view::npos;
}

inline xr_array<char, 32> CollisionPartName(u32 Part)
{
	xr_array<char, 32> Name{};
	constexpr xr_string_view Prefix = "level_";
	constexpr xr_string_view Suffix = ".cform";
	std::copy(Prefix.begin(), Prefix.end(), Name.data());
	const auto End = std::to_chars(Name.data() + Prefix.size(), Name.data() + Name.size() - Suffix.size(), Part).ptr;
	std::copy(Suffix.begin(), Suffix.end(), End);
	return Name;
}

template <class Visit>
bool ForEachAsset(u32 CollisionParts, Visit File)
{
	if (!File("level.ai") || !File("level.cform"))
	{
		return false;
	}
	for (u32 Part = 0; Part < CollisionParts; ++Part)
	{
		if (!File(CollisionPartName(Part).data()))
		{
			return false;
		}
	}
	return File("level.game") && File("level.spawn") && File("gamemtl.xr");
}

inline Result Issue(Status Code, xr_string_view File = PackageName)
{
	Result Error{Code};
	std::copy_n(File.data(), std::min(File.size(), Error.File.size() - 1), Error.File.data());
	return Error;
}

// ReadAsset resolves a schema-defined name against the same asset view used by
// the map. Out is cleared on every failure. Record names borrow the package bytes.
template <class Records, class Lookup>
Result Validate(xr_string_view Package, const Identity& Expected, Records& Out, Lookup ReadAsset)
{
	using ArenaLadders::Detail::NextLine;
	using ArenaLadders::Detail::Number;
	using ArenaLadders::Detail::Token;

	Out.clear();
	if (Package.empty())
	{
		return Issue(Status::MissingPackage);
	}
	if (Package.size() > MaxPackageBytes)
	{
		return Issue(Status::InvalidSize);
	}

	auto Header = NextLine(Package);
	if (Token(Header) != SchemaTag || Token(Header) != SchemaVersion || !Token(Header).empty())
	{
		return Issue(Status::InvalidVersion);
	}
	auto MapLine = NextLine(Package);
	const auto MapTag = Token(MapLine);
	const auto Map = Token(MapLine);
	const auto Version = Token(MapLine);
	const auto Agent = Token(MapLine);
	if (MapTag != "map" || !IsIdentifier(Map, 63) || !IsIdentifier(Version, 31, true) ||
		!IsIdentifier(Agent, 31) || !Token(MapLine).empty())
	{
		return Issue(Status::InvalidManifest);
	}
	if (Map != Expected.Map)
	{
		return Issue(Status::WrongMap);
	}
	if (Version != Expected.Version)
	{
		return Issue(Status::WrongVersion);
	}
	if (Agent != Expected.Agent)
	{
		return Issue(Status::WrongAgent);
	}

	auto PartsLine = NextLine(Package);
	u32 Parts = 0;
	if (Token(PartsLine) != "parts" || !Number(Token(PartsLine), Parts) || !Token(PartsLine).empty() ||
		Parts > MaxCollisionParts || Parts != Expected.CollisionParts)
	{
		return Issue(Status::InvalidPartCount, "level.cform");
	}

	bool HasSpawn = false;
	Result Error{Status::Ready};
	const bool Matched = ForEachAsset(Parts, [&](xr_string_view Name)
	{
		auto Line = NextLine(Package);
		const auto ActualName = Token(Line);
		const auto Hash = Token(Line);
		const bool Absent = Name == "level.spawn" && Hash == "-";
		if (ActualName != Name || (!Absent && !IsDigest(Hash)) || !Token(Line).empty())
		{
			Error = Issue(Status::InvalidManifest, Name);
			return false;
		}
		const auto Asset = ReadAsset(Name);
		if (Asset.State == AssetState::Unreadable)
		{
			Error = Issue(Status::UnreadableAsset, Name);
			return false;
		}
		if (Absent && Asset.State == AssetState::Missing)
		{
			return true;
		}
		if (Asset.State == AssetState::Missing)
		{
			Error = Issue(Status::MissingAsset, Name);
			return false;
		}
		if (Hash != xr_string_view(Asset.Hash.data(), Asset.Hash.size()))
		{
			Error = Issue(Status::StaleAsset, Name);
			return false;
		}
		HasSpawn |= Name == "level.spawn";
		return true;
	});
	if (!Matched)
	{
		return Error;
	}

	auto LinksLine = NextLine(Package);
	u32 Size = 0;
	if (Token(LinksLine) != "links" || !Number(Token(LinksLine), Size) || Size == 0 ||
		Size > ArenaLadders::MaxAttachmentBytes || Size != Package.size())
	{
		return Issue(Status::InvalidPayload);
	}
	const auto Hash = Token(LinksLine);
	if (!IsDigest(Hash) || !Token(LinksLine).empty() || !std::ranges::equal(Hash, Sha256(AsBytes(Package))))
	{
		return Issue(Status::InvalidPayload);
	}

	Records Parsed;
	if (!ArenaLadders::ReadAttachment(Package, Expected.Grid.Guid, Expected.Grid.Nodes, Parsed))
	{
		return Issue(Status::InvalidLinks);
	}
	if (!Parsed.empty() && !HasSpawn)
	{
		return Issue(Status::MissingAsset, "level.spawn");
	}
	Out.swap(Parsed);
	return {Status::Ready};
}
}
