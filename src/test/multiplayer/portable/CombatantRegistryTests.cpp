#include "../../../xrGame/Multiplayer/CombatantRegistry.h"

#include <array>
#include <cstdio>
#include <limits>

namespace CombatantRegistryTests
{
using namespace Multiplayer;

constexpr WorldIdentity World{0x123456789abcdef0ull, 0};

static bool Expect(const char* Case, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Case);
	}
	return Passed;
}

static bool CheckSlotReuse()
{
	CombatantRegistry Registry(World);
	const auto First = Registry.AddParticipant();
	if (!Expect("the first combatant has a participant ID", First.has_value()))
	{
		return false;
	}
	const auto OldBody = Registry.BindBody(World, *First, 27);
	if (!Expect("the engine slot resolves to the first combatant", OldBody && Registry.Resolve(World, *OldBody) == First))
	{
		return false;
	}

	bool Passed = Expect("retiring a body invalidates its reference immediately",
		Registry.RetireBody(World, *OldBody) && !Registry.Resolve(World, *OldBody));
	Passed &= Expect("disconnect removes the original participant", Registry.RemoveParticipant(World.ServerSession, *First));
	const auto Replacement = Registry.AddParticipant();
	if (!Expect("the replacement gets a fresh participant ID", Replacement && Replacement != First))
	{
		return false;
	}
	const auto NewBody = Registry.BindBody(World, *Replacement, 27);
	if (!Expect("the replacement can use the same object slot", NewBody && Registry.Resolve(World, *NewBody) == Replacement))
	{
		return false;
	}

	Passed &= Expect("reusing the object slot changes its generation", NewBody->ObjectId == OldBody->ObjectId &&
		NewBody->Generation != OldBody->Generation);
	Passed &= Expect("the new participant can have the same numeric life ID", NewBody->LifeId == OldBody->LifeId);
	Passed &= Expect("a stale reference cannot resolve to the replacement", !Registry.Resolve(World, *OldBody));
	Passed &= Expect("late retirement or disconnect cannot remove the replacement", !Registry.RetireBody(World, *OldBody) &&
		!Registry.RemoveParticipant(World.ServerSession, *First) && Registry.Resolve(World, *NewBody) == Replacement);
	return Passed;
}

static bool CheckRespawnAndBots()
{
	CombatantRegistry Registry(World);
	const auto Human = Registry.AddParticipant();
	const auto Bot = Registry.AddParticipant();
	if (!Expect("a bot and a human can be registered before they have bodies", Human && Bot && Human != Bot))
	{
		return false;
	}

	const auto DeadBody = Registry.BindBody(World, *Human, 41);
	const auto BotBody = Registry.BindBody(World, *Bot, 42);
	if (!Expect("a bot can own a body without a connection", DeadBody && BotBody && Registry.Resolve(World, *BotBody) == Bot))
	{
		return false;
	}
	bool Passed = Expect("the human's old life ends", Registry.RetireBody(World, *DeadBody));
	const auto NewBody = Registry.BindBody(World, *Human, 44);
	if (!Expect("the human keeps the participant ID after respawn", NewBody && Registry.Resolve(World, *NewBody) == Human))
	{
		return false;
	}

	const EntityRef OldLife{NewBody->ObjectId, NewBody->Generation, DeadBody->LifeId};
	Passed &= Expect("respawn gets a fresh life ID even on a different engine slot", NewBody->LifeId != DeadBody->LifeId);
	Passed &= Expect("an old life cannot resolve even with the new slot and generation", !Registry.Resolve(World, OldLife));
	Passed &= Expect("ending one life preserves another active body", Registry.Resolve(World, *BotBody) == Bot);
	return Passed;
}

static bool CheckWorldAndSession()
{
	constexpr WorldIdentity NextRound{World.ServerSession, 4};
	constexpr WorldIdentity NextSession{World.ServerSession + 1, 0};
	CombatantRegistry Registry(World);
	const auto Participant = Registry.AddParticipant();
	const auto Other = Registry.AddParticipant();
	const auto OldBody = Participant ? Registry.BindBody(World, *Participant, 9) : std::nullopt;
	const auto OtherBody = Other ? Registry.BindBody(World, *Other, 10) : std::nullopt;
	if (!Expect("round reset fixture has multiple live bodies", OldBody && OtherBody))
	{
		return false;
	}

	bool Passed = Expect("a repeated epoch preserves the current world", !Registry.AdvanceWorld(World.WorldEpoch) &&
		Registry.Resolve(World, *OldBody) == Participant);
	Passed &= Expect("a future or foreign reference cannot change the active world", !Registry.Resolve(NextRound, *OldBody) &&
		!Registry.RetireBody(NextRound, *OldBody) && !Registry.Resolve(NextSession, *OldBody));
	Passed &= Expect("the round advance retires the previous body", Registry.AdvanceWorld(NextRound.WorldEpoch) &&
		!Registry.Resolve(World, *OldBody) && !Registry.Resolve(NextRound, *OldBody) &&
		!Registry.Resolve(World, *OtherBody) && !Registry.Resolve(NextRound, *OtherBody));
	Passed &= Expect("old-round work cannot bind a new life", !Registry.BindBody(World, *Participant, 9));

	const auto NewBody = Registry.BindBody(NextRound, *Participant, 9);
	if (!Expect("the round retains the participant and can reuse numeric body fields", NewBody && NewBody == OldBody &&
		Registry.Resolve(NextRound, *NewBody) == Participant))
	{
		return false;
	}
	Passed &= Expect("stale world operations fail even when the body tuple matches",
		!Registry.AdvanceWorld(World.WorldEpoch) && !Registry.Resolve(World, *OldBody) && !Registry.RetireBody(World, *OldBody) &&
		Registry.Resolve(NextRound, *NewBody) == Participant);

	CombatantRegistry Restarted(NextSession);
	const auto RestartedParticipant = Restarted.AddParticipant();
	const auto RestartedBody = RestartedParticipant ? Restarted.BindBody(NextSession, *RestartedParticipant, 9) : std::nullopt;
	Passed &= Expect("restart reuses numerals but rejects references from the old server", RestartedBody == OldBody &&
		Restarted.Resolve(NextSession, *OldBody) == RestartedParticipant && !Restarted.Resolve(World, *OldBody) &&
		!Restarted.RetireBody(World, *OldBody));

	Passed &= Expect("a previous session cannot remove a participant even when its ID matches", RestartedParticipant == Participant &&
		!Restarted.RemoveParticipant(World.ServerSession, *Participant) && Restarted.Resolve(NextSession, *OldBody) == RestartedParticipant);

	constexpr WorldIdentity LastRound{World.ServerSession, std::numeric_limits<u32>::max()};
	Passed &= Expect("the last round value is usable and cannot wrap", Registry.AdvanceWorld(LastRound.WorldEpoch) &&
		!Registry.AdvanceWorld(0));
	const auto LastBody = Registry.BindBody(LastRound, *Other, 10);
	Passed &= Expect("both participants survive round resets", LastBody && Registry.Resolve(LastRound, *LastBody) == Other &&
		Registry.RemoveParticipant(World.ServerSession, *Participant));
	return Passed;
}

static bool CheckBindingFailures()
{
	CombatantRegistry Unbound({0, 0});
	CombatantRegistry Registry(World);
	const auto First = Registry.AddParticipant();
	const auto Second = Registry.AddParticipant();
	const auto Active = First ? Registry.BindBody(World, *First, 0) : std::nullopt;
	if (!Expect("engine slot zero is valid", First && Second && Active && Registry.Resolve(World, *Active) == First))
	{
		return false;
	}

	bool Passed = Expect("an unassigned session cannot create participants or advance rounds", !Unbound.AddParticipant() && !Unbound.AdvanceWorld(1));
	Passed &= Expect("an unassigned or unknown participant cannot take an engine slot",
		!Registry.BindBody(World, {}, 33) && !Registry.BindBody(World, ParticipantId{9999}, 33));
	Passed &= Expect("the engine's invalid object slot cannot be assigned", !Registry.BindBody(World, *Second, 0xffff));
	Passed &= Expect("a live body cannot be replaced by binding again", !Registry.BindBody(World, *First, 0) &&
		!Registry.BindBody(World, *First, 33));
	Passed &= Expect("two participants cannot bind the same slot", !Registry.BindBody(World, *Second, 0));
	auto WrongSlot = *Active;
	WrongSlot.ObjectId = 33;
	Passed &= Expect("even a matching generation and life require the right slot", !Registry.Resolve(World, WrongSlot) &&
		!Registry.RetireBody(World, WrongSlot));
	Passed &= Expect("failed binds preserve the existing body", Registry.Resolve(World, *Active) == First);

	const auto HighestSlot = Registry.BindBody(World, *Second, 0xfffe);
	Passed &= Expect("failed binds spend neither a generation nor a life", HighestSlot && HighestSlot->Generation == Active->Generation + 1 &&
		HighestSlot->LifeId == Active->LifeId && Registry.Resolve(World, *HighestSlot) == Second);
	return Passed;
}

static bool CheckCapacityAndIdentityExhaustion()
{
	CombatantRegistry Registry(World);
	std::array<ParticipantId, 32> Participants{};
	for (auto& Participant : Participants)
	{
		const auto Added = Registry.AddParticipant();
		if (!Expect("the registry holds 32 combatants", Added.has_value()))
		{
			return false;
		}
		Participant = *Added;
	}
	bool Passed = Expect("the 33rd combatant is rejected", !Registry.AddParticipant());
	const auto Body = Registry.BindBody(World, Participants.front(), 51);
	if (!Expect("a full registry can still bind a body", Body.has_value()))
	{
		return false;
	}
	Passed &= Expect("disconnect atomically removes the participant and its active mapping", Registry.RemoveParticipant(World.ServerSession, Participants.front()) &&
		!Registry.Resolve(World, *Body) && !Registry.BindBody(World, Participants.front(), 52));
	const auto Added = Registry.AddParticipant();
	Passed &= Expect("capacity is returned without recycling a participant ID", Added && Added->Value == Participants.back().Value + 1 &&
		!Registry.AddParticipant());

	constexpr u32 Last = std::numeric_limits<u32>::max();
	Passed &= Expect("identity counters start at one and allow the largest nonzero value",
		Detail::NextCombatantIdentity(0) == 1u && Detail::NextCombatantIdentity(Last - 1) == Last);
	Passed &= Expect("identity counters exhaust instead of wrapping", !Detail::NextCombatantIdentity(Last));
	return Passed;
}

bool Run()
{
	bool Passed = CheckSlotReuse();
	Passed &= CheckRespawnAndBots();
	Passed &= CheckWorldAndSession();
	Passed &= CheckBindingFailures();
	Passed &= CheckCapacityAndIdentityExhaustion();
	if (!Passed)
	{
		return false;
	}
	std::puts("Combatant registry checks passed");
	return true;
}
}
