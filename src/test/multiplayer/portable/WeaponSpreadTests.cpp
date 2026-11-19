#include "../../../xrGame/Weapons/WeaponSpread.h"
#include "../../../xrCore/ScalarTypes.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <span>

namespace WeaponSpreadTests
{
struct ScriptedRandom
{
	const char* CaseName;
	std::span<const float> Values;
	bool Positive;
	std::size_t UnitDraws = 0;
	std::size_t SignDraws = 0;
	std::size_t UnitDrawsAtSign = 0;

	float NextUnit()
	{
		if (UnitDraws == Values.size())
		{
			std::fprintf(stderr, "%s: spread sampler exceeded %zu scripted unit draws\n", CaseName, Values.size());
			std::exit(EXIT_FAILURE);
		}
		return Values[UnitDraws++];
	}

	bool NextPositiveSign()
	{
		++SignDraws;
		UnitDrawsAtSign = UnitDraws;
		return Positive;
	}
};

struct SpreadCase
{
	const char* Name;
	float Dispersion;
	std::span<const float> Draws;
	bool Positive;
	std::size_t UnitsBeforeSign;
	WeaponSpread::Angles Expected;
};

constexpr float Ordinary[] = {0.5f, 0.0f, 0.25f};
constexpr float Retry[] = {1.0f, 1.0f, 0.03125f, 0.5f, 0.25f, 0.0f, 0.75f};
constexpr float Large[] = {0.03125f, 0.0f, 1.0f};
constexpr float Upper[] = {1.0f, 0.5f, 1.0f};
constexpr float ZeroRejected[] = {0.0f, 0.25f, 0.5f, 0.0f, 0.25f};
constexpr float ZeroAccepted[] = {0.0f, 0.0f, 0.25f};
constexpr float AzimuthOnly[] = {0.75f};

constexpr SpreadCase Cases[] = {
	{"negative sign from source", 0.3f, Ordinary, false, 2, {-0.086915f, 0.785398f}},
	{"positive sign from source", 0.3f, Ordinary, true, 2, {0.086915f, 0.785398f}},
	{"retry two rejected magnitudes", 0.3f, Retry, true, 6, {0.173830f, 2.3561945f}},
	{"clamp positive inclination", 0.3f, Large, true, 2, {0.3f, 3.1415927f}},
	{"clamp negative inclination", 0.3f, Large, false, 2, {-0.3f, 3.1415927f}},
	{"upper unit endpoint", 0.3f, Upper, true, 2, {0.0f, 3.1415927f}},
	{"reject zero magnitude draw with positive acceptance draw", 0.3f, ZeroRejected, true, 4, {0.086915f, 0.785398f}},
	{"accept two zero draws at positive bound", 0.3f, ZeroAccepted, true, 2, {0.3f, 0.785398f}},
	{"accept two zero draws at negative bound", 0.3f, ZeroAccepted, false, 2, {-0.3f, 0.785398f}},
	{"zero dispersion draws only azimuth", 0.0f, AzimuthOnly, false, 0, {0.0f, 2.3561945f}},
	{"legacy negative dispersion lower comparison", -0.3f, Ordinary, true, 2, {0.3f, 0.785398f}},
	{"legacy negative dispersion upper comparison", -0.3f, Large, false, 2, {-0.3f, 3.1415927f}},
	{"endpoint clamp with legacy negative dispersion", -0.3f, ZeroAccepted, true, 2, {0.3f, 0.785398f}},
};

static bool CheckCase(const SpreadCase& Case)
{
	ScriptedRandom Source{Case.Name, Case.Draws, Case.Positive};
	const auto Actual = WeaponSpread::Sample(Case.Dispersion, Source);
	const std::size_t ExpectedSignDraws = Case.UnitsBeforeSign == 0 ? 0 : 1;
	if (Source.UnitDraws != Case.Draws.size() || Source.SignDraws != ExpectedSignDraws || Source.UnitDrawsAtSign != Case.UnitsBeforeSign)
	{
		std::fprintf(stderr, "%s: expected %zu unit draws and %zu sign draws after %zu units, got %zu, %zu and %zu\n",
			Case.Name, Case.Draws.size(), ExpectedSignDraws, Case.UnitsBeforeSign, Source.UnitDraws, Source.SignDraws, Source.UnitDrawsAtSign);
		return false;
	}

	constexpr float Tolerance = 0.000002f;
	if (std::abs(Actual.Inclination - Case.Expected.Inclination) <= Tolerance && std::abs(Actual.Azimuth - Case.Expected.Azimuth) <= Tolerance)
	{
		return true;
	}
	std::fprintf(stderr, "%s: expected inclination %.8g and azimuth %.8g, got %.8g and %.8g\n",
		Case.Name, Case.Expected.Inclination, Case.Expected.Azimuth, Actual.Inclination, Actual.Azimuth);
	return false;
}

class SeededRandom
{
public:
	explicit SeededRandom(u32 Seed) : Engine(Seed)
	{
	}

	float NextUnit()
	{
		return static_cast<float>(Engine() >> 8) * 0x1p-24f;
	}

	bool NextPositiveSign()
	{
		return (Engine() & 1u) != 0;
	}

private:
	std::mt19937 Engine;
};

struct SpreadEvent
{
	float Inclination;
	float Azimuth;

	bool operator==(const SpreadEvent&) const = default;
};

struct SpreadTrace
{
	std::array<SpreadEvent, 24> Shots;
	float CosmeticSamples = 0.0f;
};

static SpreadTrace RecordSpread(u32 Seed, bool DrawCosmetics)
{
	SeededRandom Simulation(Seed);
	SeededRandom Cosmetics(Seed);
	SpreadTrace Trace;
	for (std::size_t Shot = 0; Shot < Trace.Shots.size(); ++Shot)
	{
		if (DrawCosmetics)
		{
			for (std::size_t Draw = 0; Draw <= Shot % 5; ++Draw)
			{
				Trace.CosmeticSamples += Cosmetics.NextUnit();
			}
		}
		const auto Angles = WeaponSpread::Sample(Shot % 5 == 0 ? 0.0f : 0.3f, Simulation);
		Trace.Shots[Shot] = {Angles.Inclination, Angles.Azimuth};
	}
	return Trace;
}

static bool CheckSeededTrace()
{
	constexpr u32 Seed = 0x31823d45u;
	const auto Baseline = RecordSpread(Seed, false).Shots;
	if (Baseline != RecordSpread(Seed, false).Shots)
	{
		std::fputs("Same seed and shot inputs produced different spread traces\n", stderr);
		return false;
	}
	const auto WithCosmetics = RecordSpread(Seed, true);
	if (WithCosmetics.CosmeticSamples <= 0.0f || Baseline != WithCosmetics.Shots)
	{
		std::fputs("Expected cosmetic draws with an identical simulation spread trace\n", stderr);
		return false;
	}
	if (Baseline == RecordSpread(Seed + 1, false).Shots)
	{
		std::fputs("Changing the seed did not change the spread trace\n", stderr);
		return false;
	}
	return true;
}

bool Run()
{
	bool Passed = true;
	for (const SpreadCase& Case : Cases)
	{
		Passed &= CheckCase(Case);
	}
	Passed &= CheckSeededTrace();
	if (Passed)
	{
		std::puts("Weapon spread checks passed");
	}
	return Passed;
}
}
