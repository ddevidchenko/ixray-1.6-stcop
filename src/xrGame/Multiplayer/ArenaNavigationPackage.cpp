#ifdef XRGAME_EXPORTS
#include "StdAfx.h"
#endif
#include "ArenaNavigationPackage.h"
#include "StalkerNavigation.h"

#include <bit>

#include <openssl/sha.h>

namespace Multiplayer::ArenaNavigation
{
Digest Sha256(Bytes Data)
{
	xr_array<unsigned char, SHA256_DIGEST_LENGTH> Raw{};
	SHA256_CTX Context;
	SHA256_Init(&Context);
	SHA256_Update(&Context, Data.data(), Data.size());
	SHA256_Final(Raw.data(), &Context);
	constexpr char Hex[] = "0123456789abcdef";
	Digest Hash{};
	for (std::size_t Index = 0; Index < Raw.size(); ++Index)
	{
		Hash[Index * 2] = Hex[Raw[Index] >> 4];
		Hash[Index * 2 + 1] = Hex[Raw[Index] & 0xf];
	}
	return Hash;
}

namespace
{
template <class Integer>
Integer ReadLittleEndian(Bytes Data, std::size_t Offset)
{
	Integer Value = 0;
	for (std::size_t Index = 0; Index < sizeof(Integer); ++Index)
	{
		Value |= Integer(Data[Offset + Index]) << (Index * 8);
	}
	return Value;
}
}

std::optional<u32> ReadCollisionParts(Bytes Data)
{
	if (Data.size() < CollisionHeaderSize)
	{
		return {};
	}
	const u32 Version = ReadLittleEndian<u32>(Data, 0);
	if (Version == 4)
	{
		return 0;
	}
	if (Version != 5 || Data.size() != CollisionHeaderSize + sizeof(u32))
	{
		return {};
	}
	const u32 Count = ReadLittleEndian<u32>(Data, CollisionHeaderSize);
	if (Count == 0 || Count > MaxCollisionParts)
	{
		return {};
	}
	return Count;
}

std::optional<GridIdentity> ReadGridIdentity(Bytes Data)
{
	if (Data.size() < GridHeaderSize)
	{
		return {};
	}
	u32 NodeSize = 0;
	switch (ReadLittleEndian<u32>(Data, 0))
	{
	case 8: NodeSize = 21; break;
	case 10: NodeSize = 23; break;
	case 11: NodeSize = 24; break;
	case 13: NodeSize = 25; break;
	}
	const u32 Count = ReadLittleEndian<u32>(Data, 4);
	const auto ReadFloat = [&](std::size_t Offset) { return std::bit_cast<f32>(ReadLittleEndian<u32>(Data, Offset)); };
	const f32 CellSize = ReadFloat(8);
	if (!StalkerNavigation::ArenaGridFits(Data.size(), GridHeaderSize, Count, NodeSize, CellSize, ReadFloat(12)))
	{
		return {};
	}
	for (std::size_t Axis = 0; Axis < 3; ++Axis)
	{
		const f32 Minimum = ReadFloat(16 + Axis * sizeof(f32));
		const f32 Maximum = ReadFloat(28 + Axis * sizeof(f32));
		if (!std::isfinite(Minimum) || !std::isfinite(Maximum) || Maximum < Minimum)
		{
			return {};
		}
	}
	// The native constructor performs the extent subtraction in single precision.
	const f32 Rows = (ReadFloat(36) - ReadFloat(24)) / CellSize;
	const f32 Columns = (ReadFloat(28) - ReadFloat(16)) / CellSize;
	if (!std::isfinite(Rows) || !std::isfinite(Columns) || (f64{Rows} + 2.0) * (f64{Columns} + 2.0) >= (1u << 24))
	{
		return {};
	}
	return GridIdentity{{ReadLittleEndian<u64>(Data, 40), ReadLittleEndian<u64>(Data, 48)}, Count};
}

const char* Describe(Status Code)
{
	switch (Code)
	{
	case Status::Ready: return "ready";
	case Status::MissingPackage: return "navigation package missing";
	case Status::InvalidSize: return "navigation package size invalid";
	case Status::InvalidVersion: return "unsupported navigation package version";
	case Status::WrongMap: return "map name mismatch";
	case Status::WrongVersion: return "map version mismatch";
	case Status::WrongAgent: return "agent profile mismatch";
	case Status::InvalidPartCount: return "collision part count mismatch or invalid";
	case Status::InvalidManifest: return "invalid navigation manifest";
	case Status::MissingAsset: return "required navigation input missing";
	case Status::UnreadableAsset: return "navigation input unreadable or empty";
	case Status::StaleAsset: return "navigation input SHA-256 mismatch";
	case Status::InvalidPayload: return "traversal size or SHA-256 mismatch";
	case Status::InvalidLinks: return "invalid traversal records or level.ai GUID mismatch";
	}
	return "unknown navigation validation status";
}
}
