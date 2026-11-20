#include "../../../xrGame/Multiplayer/CombatantReplicas.h"

#include <cstdio>
#include <limits>
#include <memory>

namespace CombatantReplicasTests
{
using namespace Multiplayer;

constexpr WorldIdentity World{0x123456789abcdef0ull, 0};
constexpr ParticipantId Participant{71};
constexpr EntityRef Body{29, 11, 4};

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

static bool CheckRetirement()
{
	auto History = std::make_unique<RetiredCombatantGenerations>();
	CombatantReplicas Replicas(World, *History);
	if (!Expect("a checked lifecycle source can register a body", Replicas.RegisterBody(World, Participant, Body) &&
		Replicas.Classify(World, Participant, Body) == ReplicaBodyState::Current))
	{
		return false;
	}

	bool Passed = Expect("the exact body can despawn", Replicas.Despawn(World, Participant, Body));
	Passed &= Expect("a late snapshot for a despawned body is stale",
		Replicas.Classify(World, Participant, Body) == ReplicaBodyState::Stale);
	Passed &= Expect("a replayed creation cannot revive the retired body", !Replicas.RegisterBody(World, Participant, Body) &&
		Replicas.Classify(World, Participant, Body) == ReplicaBodyState::Stale);
	constexpr EntityRef Replacement{Body.ObjectId, Body.Generation + 2, 1};
	constexpr ParticipantId Other{72};
	Passed &= Expect("a new authoritative body can reuse the slot", Replicas.RegisterBody(World, Other, Replacement));
	Passed &= Expect("a delayed despawn cannot remove the replacement", !Replicas.Despawn(World, Participant, Body) &&
		Replicas.Classify(World, Participant, Body) == ReplicaBodyState::Stale &&
		Replicas.Classify(World, Other, Replacement) == ReplicaBodyState::Current);
	return Passed;
}

static bool CheckActiveConflicts()
{
	auto History = std::make_unique<RetiredCombatantGenerations>();
	History->fill(0xffffffffu);
	CombatantReplicas Replicas(World, *History);
	constexpr ParticipantId Other{72};
	constexpr EntityRef DifferentSlot{30, 13, 5};
	constexpr EntityRef Newer{Body.ObjectId, Body.Generation + 1, Body.LifeId + 1};
	constexpr EntityRef OldUnseen{Body.ObjectId, Body.Generation - 1, Body.LifeId - 1};
	constexpr EntityRef WrongLife{Body.ObjectId, Body.Generation, Body.LifeId - 1};
	bool Passed = Expect("unused or reused storage starts with unknown slots",
		Replicas.Classify(World, Participant, Body) == ReplicaBodyState::Unknown && !Replicas.Despawn(World, Participant, Body));
	Passed &= Expect("exact trusted registration is idempotent", Replicas.RegisterBody(World, Participant, Body) &&
		Replicas.RegisterBody(World, Participant, Body));
	Passed &= Expect("the same slot and generation still require the exact participant and life",
		Replicas.Classify(World, Other, Body) == ReplicaBodyState::Conflict &&
		Replicas.Classify(World, Participant, WrongLife) == ReplicaBodyState::Conflict &&
		!Replicas.Despawn(World, Other, Body) && !Replicas.Despawn(World, Participant, WrongLife));
	Passed &= Expect("a newer snapshot for an occupied slot waits for lifecycle data",
		Replicas.Classify(World, Participant, Newer) == ReplicaBodyState::Unknown &&
		!Replicas.RegisterBody(World, Participant, Newer) && !Replicas.Despawn(World, Participant, Newer));
	Passed &= Expect("a lower generation is stale even when the earlier body was unseen",
		Replicas.Classify(World, Other, OldUnseen) == ReplicaBodyState::Stale);
	Passed &= Expect("the same participant cannot register two live bodies",
		!Replicas.RegisterBody(World, Participant, DifferentSlot) && Replicas.RegisterBody(World, Other, DifferentSlot));
	Passed &= Expect("rejected operations leave the original body intact", Replicas.Classify(World, Participant, Body) == ReplicaBodyState::Current);
	return Passed;
}

static bool CheckWorldLifetime()
{
	auto History = std::make_unique<RetiredCombatantGenerations>();
	CombatantReplicas Replicas(World, *History);
	constexpr WorldIdentity NewWorld{World.ServerSession, 2};
	constexpr WorldIdentity DifferentSession{World.ServerSession + 1, World.WorldEpoch};
	constexpr EntityRef Live{30, 10, 1};
	constexpr ParticipantId Other{72};

	if (!Expect("the fixture has one retired and one still-live identity",
		Replicas.RegisterBody(World, Participant, Body) && Replicas.Despawn(World, Participant, Body) &&
		Replicas.RegisterBody(World, Other, Live)))
	{
		return false;
	}
	bool Passed = Expect("a future world or different server cannot alter the replicas",
		Replicas.Classify(NewWorld, Other, Live) == ReplicaBodyState::Invalid &&
		!Replicas.Despawn(DifferentSession, Other, Live) && !Replicas.RegisterBody(NewWorld, Participant, Body));
	Passed &= Expect("invalid world advances leave current state available",
		!Replicas.AdvanceWorld(World.WorldEpoch) && Replicas.Classify(World, Other, Live) == ReplicaBodyState::Current);
	Passed &= Expect("new worlds clear the active bodies and retirement history",
		Replicas.AdvanceWorld(NewWorld.WorldEpoch) &&
		Replicas.Classify(NewWorld, Participant, Body) == ReplicaBodyState::Unknown &&
		Replicas.Classify(NewWorld, Other, Live) == ReplicaBodyState::Unknown);
	Passed &= Expect("the same body values from an old epoch cannot target the new world",
		Replicas.RegisterBody(NewWorld, Participant, Body) && !Replicas.Despawn(World, Participant, Body) &&
		Replicas.Classify(World, Participant, Body) == ReplicaBodyState::Invalid &&
		Replicas.Classify(NewWorld, Participant, Body) == ReplicaBodyState::Current);
	Passed &= Expect("round epoch exhaustion cannot wrap", Replicas.AdvanceWorld(std::numeric_limits<u32>::max()) && !Replicas.AdvanceWorld(0));
	return Passed;
}

static bool CheckBounds()
{
	auto History = std::make_unique<RetiredCombatantGenerations>();
	CombatantReplicas Replicas(World, *History);
	constexpr EntityRef Invalid[] = {{0xffff, 1, 1}, {0, 0, 1}, {0, 1, 0}};
	bool Passed = Expect("an unassigned participant fails classification", Replicas.Classify(World, {}, Body) == ReplicaBodyState::Invalid &&
		!Replicas.RegisterBody(World, {}, Body));
	for (const auto& Reference : Invalid)
	{
		Passed &= Expect("invalid identity fields never address replica storage",
			Replicas.Classify(World, Participant, Reference) == ReplicaBodyState::Invalid &&
			!Replicas.RegisterBody(World, Participant, Reference) && !Replicas.Despawn(World, Participant, Reference));
	}
	for (u16 Slot = 0; Slot < 32; ++Slot)
	{
		Passed &= Expect("different slots may be registered out of generation order",
			Replicas.RegisterBody(World, ParticipantId{32u - Slot}, EntityRef{Slot, 32u - Slot, 1}));
	}
	constexpr EntityRef Highest{0xfffe, std::numeric_limits<u32>::max(), std::numeric_limits<u32>::max()};
	Passed &= Expect("capacity is limited to the 32 active bodies", !Replicas.RegisterBody(World, Participant, Highest));
	Passed &= Expect("retirement releases capacity and the highest slot is valid",
		Replicas.RegisterBody(World, {32}, {0, 32, 1}) && Replicas.Despawn(World, {32}, {0, 32, 1}) &&
		Replicas.RegisterBody(World, Participant, Highest) && Replicas.Despawn(World, Participant, Highest) &&
		Replicas.Classify(World, Participant, Highest) == ReplicaBodyState::Stale &&
		!Replicas.RegisterBody(World, Participant, Highest));
	Passed &= Expect("retirement history remains local to the slot", Replicas.Classify(World, {32}, {0, 32, 1}) == ReplicaBodyState::Stale &&
		Replicas.Classify(World, {1}, {31, 1, 1}) == ReplicaBodyState::Current);
	return Passed;
}

static bool CheckRetirementHistory()
{
	auto History = std::make_unique<RetiredCombatantGenerations>();
	CombatantReplicas Replicas(World, *History);
	for (u16 Slot = 0; Slot < 300; ++Slot)
	{
		const EntityRef Reference{Slot, Slot + 100u, Slot + 1u};
		if (!Expect("each retirement makes room for another active body", Replicas.RegisterBody(World, Participant, Reference) &&
			Replicas.Despawn(World, Participant, Reference)))
		{
			return false;
		}
	}

	constexpr EntityRef First{0, 100, 1};
	constexpr EntityRef Older{0, 99, 1};
	constexpr EntityRef Last{299, 399, 300};
	constexpr EntityRef Newer{299, 400, 301};
	return Expect("retirement history survives churn across hundreds of slots",
		Replicas.Classify(World, Participant, First) == ReplicaBodyState::Stale &&
		Replicas.Classify(World, Participant, Older) == ReplicaBodyState::Stale &&
		Replicas.Classify(World, Participant, Last) == ReplicaBodyState::Stale &&
		Replicas.Classify(World, Participant, Newer) == ReplicaBodyState::Unknown &&
		!Replicas.RegisterBody(World, Participant, First));
}

bool Run()
{
	bool Passed = CheckRetirement();
	Passed &= CheckActiveConflicts();
	Passed &= CheckWorldLifetime();
	Passed &= CheckBounds();
	Passed &= CheckRetirementHistory();
	if (!Passed)
	{
		return false;
	}
	std::puts("Combatant replica checks passed");
	return true;
}
}
