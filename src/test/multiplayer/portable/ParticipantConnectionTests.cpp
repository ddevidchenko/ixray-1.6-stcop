#include "../../../xrGame/Multiplayer/CombatantRegistry.h"

#include <cstdio>
#include <limits>

namespace ParticipantConnectionTests
{
using namespace Multiplayer;

constexpr WorldIdentity World{0x123456789abcdef0ull, 5};
constexpr u32 Handle = 42;
constexpr AdmittedConnection FirstConnection{World.ServerSession, 17, Handle};
constexpr AdmittedConnection ReplacementConnection{World.ServerSession, 24, Handle};
constexpr JoinIdentity FirstMessage{World.ServerSession, World.WorldEpoch, FirstConnection.ConnectionEpoch};
constexpr JoinIdentity ReplacementMessage{World.ServerSession, World.WorldEpoch, ReplacementConnection.ConnectionEpoch};

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

static bool CheckRecycledHandle()
{
	CombatantRegistry Registry(World);
	const auto First = Registry.AddAdmittedParticipant(FirstConnection);
	if (!Expect("the first admission reaches its participant", First && Registry.ResolveConnection(FirstConnection, FirstMessage) == First))
	{
		return false;
	}

	const auto OldBody = Registry.BindBody(World, *First, 13);
	if (!Expect("the first participant has a body", OldBody.has_value()))
	{
		return false;
	}
	bool Passed = Expect("disconnect retires the original participant and body", Registry.Disconnect(FirstConnection) == First &&
		!Registry.Resolve(World, *OldBody) && !Registry.BindBody(World, *First, 14));
	const auto Replacement = Registry.AddAdmittedParticipant(ReplacementConnection);
	if (!Expect("a new participant binds the recycled handle", Replacement && Replacement != First &&
		Registry.ResolveConnection(ReplacementConnection, ReplacementMessage) == Replacement))
	{
		return false;
	}

	Passed &= Expect("an old queued packet cannot resolve to the new participant",
		!Registry.ResolveConnection(FirstConnection, FirstMessage));
	const auto NewBody = Registry.BindBody(World, *Replacement, 13);
	Passed &= Expect("the replacement can use the released object slot", NewBody.has_value());
	Passed &= Expect("an old disconnect cannot retire the new participant", !Registry.Disconnect(FirstConnection));
	Passed &= Expect("the new admission and body survive stale traffic",
		NewBody && Registry.ResolveConnection(ReplacementConnection, ReplacementMessage) == Replacement &&
		Registry.Resolve(World, *NewBody) == Replacement);
	return Passed;
}

static bool CheckReceiverAndMessage()
{
	CombatantRegistry Registry(World);
	constexpr AdmittedConnection OtherConnection{World.ServerSession, 18, 43};
	const auto First = Registry.AddAdmittedParticipant(FirstConnection);
	const auto Other = Registry.AddAdmittedParticipant(OtherConnection);
	if (!Expect("two admissions can be live together", First && Other && First != Other))
	{
		return false;
	}

	constexpr JoinIdentity InvalidMessages[] = {
		{},
		{World.ServerSession + 1, World.WorldEpoch, FirstConnection.ConnectionEpoch},
		{World.ServerSession, World.WorldEpoch + 1, FirstConnection.ConnectionEpoch},
		{World.ServerSession, World.WorldEpoch - 1, FirstConnection.ConnectionEpoch},
		{World.ServerSession, World.WorldEpoch, OtherConnection.ConnectionEpoch},
		{World.ServerSession, World.WorldEpoch, FirstConnection.ConnectionEpoch - 1},
	};
	bool Passed = true;
	for (const auto& Message : InvalidMessages)
	{
		Passed &= Expect("an exact local receive stamp still requires a matching packet identity", !Registry.ResolveConnection(FirstConnection, Message));
	}

	constexpr AdmittedConnection InvalidReceivers[] = {
		{},
		{World.ServerSession + 1, FirstConnection.ConnectionEpoch, Handle},
		{World.ServerSession, FirstConnection.ConnectionEpoch - 1, Handle},
		{World.ServerSession, FirstConnection.ConnectionEpoch, OtherConnection.Handle},
		OtherConnection,
	};
	for (const auto& Receiver : InvalidReceivers)
	{
		Passed &= Expect("a matching packet identity still requires the original receiving admission", !Registry.ResolveConnection(Receiver, FirstMessage));
	}
	Passed &= Expect("the matching peer retains its identity", Registry.ResolveConnection(FirstConnection, FirstMessage) == First);
	return Passed;
}

static bool CheckBindingFailures()
{
	CombatantRegistry Unbound({0, 0});
	CombatantRegistry Registry(World);
	const auto Human = Registry.AddAdmittedParticipant(FirstConnection);
	const auto Bot = Registry.AddParticipant();
	const auto BotBody = Bot ? Registry.BindBody(World, *Bot, 10) : std::nullopt;
	if (!Expect("a bot can coexist with a connected participant", Human && BotBody))
	{
		return false;
	}

	constexpr AdmittedConnection NextConnection{World.ServerSession, 18, 43};
	constexpr JoinIdentity NextMessage{World.ServerSession, World.WorldEpoch, NextConnection.ConnectionEpoch};
	bool Passed = Expect("a live handle cannot be reassigned", !Registry.AddAdmittedParticipant({World.ServerSession, NextConnection.ConnectionEpoch, Handle}) &&
		!Registry.AddAdmittedParticipant(FirstConnection));
	Passed &= Expect("a binding must use the current session and assigned handle and epoch",
		!Registry.AddAdmittedParticipant({World.ServerSession + 1, NextConnection.ConnectionEpoch, NextConnection.Handle}) &&
		!Registry.AddAdmittedParticipant({World.ServerSession, NextConnection.ConnectionEpoch, 0}) &&
		!Registry.AddAdmittedParticipant({World.ServerSession, 0, NextConnection.Handle}) &&
		!Unbound.AddAdmittedParticipant({0, NextConnection.ConnectionEpoch, NextConnection.Handle}));
	const auto Joined = Registry.AddAdmittedParticipant(NextConnection);
	Passed &= Expect("valid admission gets fresh identity while the bot and its body remain registered", Joined && Joined != Bot && Joined != Human &&
		Registry.ResolveConnection(NextConnection, NextMessage) == Joined && Registry.Resolve(World, *BotBody) == Bot);

	Passed &= Expect("owner removal clears only the selected admission", Registry.RemoveParticipant(World.ServerSession, *Human) &&
		!Registry.ResolveConnection(FirstConnection, FirstMessage) && !Registry.Disconnect(FirstConnection) &&
		Registry.ResolveConnection(NextConnection, NextMessage) == Joined && Registry.Resolve(World, *BotBody) == Bot);
	return Passed;
}

static bool CheckCapacity()
{
	CombatantRegistry Registry(World);
	const auto Human = Registry.AddAdmittedParticipant(FirstConnection);
	if (!Expect("the first admission holds one place", Human.has_value()))
	{
		return false;
	}
	for (u32 Count = 1; Count < 32; ++Count)
	{
		if (!Expect("connected participants share capacity with socket-free participants", Registry.AddParticipant().has_value()))
		{
			return false;
		}
	}

	bool Passed = Expect("a full registry rejects an admission and keeps the current occupant",
		!Registry.AddAdmittedParticipant({World.ServerSession, ReplacementConnection.ConnectionEpoch, Handle + 1}) &&
		Registry.ResolveConnection(FirstConnection, FirstMessage) == Human);
	Passed &= Expect("disconnect returns a place", Registry.Disconnect(FirstConnection) == Human);
	const auto NewHuman = Registry.AddAdmittedParticipant(ReplacementConnection);
	Passed &= Expect("the failed admission did not spend its epoch or a participant ID", NewHuman &&
		NewHuman->Value == Human->Value + 32 && Registry.ResolveConnection(ReplacementConnection, ReplacementMessage) == NewHuman);
	return Passed;
}

static bool CheckRoundLifetime()
{
	constexpr WorldIdentity NextRound{World.ServerSession, World.WorldEpoch + 1};
	constexpr JoinIdentity NextRoundMessage{NextRound.ServerSession, NextRound.WorldEpoch, FirstConnection.ConnectionEpoch};
	CombatantRegistry Registry(World);
	const auto Participant = Registry.AddAdmittedParticipant(FirstConnection);
	if (!Expect("round fixture contains an admitted participant", Participant.has_value()))
	{
		return false;
	}
	const auto Body = Registry.BindBody(World, *Participant, 22);
	if (!Expect("round fixture contains an active body", Body.has_value()))
	{
		return false;
	}

	bool Passed = Expect("a round advance preserves the admission through a body reset", Registry.AdvanceWorld(NextRound.WorldEpoch) &&
		!Registry.Resolve(World, *Body) && Registry.ResolveConnection(FirstConnection, NextRoundMessage) == Participant);
	Passed &= Expect("queued packets still need the new world epoch", !Registry.ResolveConnection(FirstConnection, FirstMessage));
	const auto NewBody = Registry.BindBody(NextRound, *Participant, 22);
	Passed &= Expect("a disconnect captured before the round change can clean up the new body", NewBody &&
		Registry.Disconnect(FirstConnection) == Participant && !Registry.Resolve(NextRound, *NewBody) &&
		!Registry.ResolveConnection(FirstConnection, NextRoundMessage) && !Registry.Disconnect(FirstConnection));
	return Passed;
}

static bool CheckAdmissionEpochLifetime()
{
	CombatantRegistry Registry(World);
	constexpr auto LastEpoch = std::numeric_limits<u32>::max();
	constexpr AdmittedConnection LastConnection{World.ServerSession, LastEpoch, Handle};
	const auto First = Registry.AddAdmittedParticipant(LastConnection);
	if (!Expect("the final nonzero admission epoch is usable", First.has_value()))
	{
		return false;
	}

	bool Passed = Expect("disconnect releases capacity and round advance succeeds", Registry.Disconnect(LastConnection) == First &&
		Registry.AdvanceWorld(World.WorldEpoch + 1));
	Passed &= Expect("round change and handle reuse cannot reopen the admission epoch range",
		!Registry.AddAdmittedParticipant(FirstConnection) && !Registry.AddAdmittedParticipant(LastConnection) &&
		!Registry.AddAdmittedParticipant({World.ServerSession, LastEpoch, Handle + 1}));

	constexpr WorldIdentity NewSession{World.ServerSession + 1, World.WorldEpoch};
	constexpr AdmittedConnection NewConnection{NewSession.ServerSession, LastEpoch, Handle};
	constexpr JoinIdentity NewMessage{NewSession.ServerSession, NewSession.WorldEpoch, LastEpoch};
	CombatantRegistry Restarted(NewSession);
	const auto NewParticipant = Restarted.AddAdmittedParticipant(NewConnection);
	Passed &= Expect("a new session can use the same numeric epoch and handle", NewParticipant == First &&
		Restarted.ResolveConnection(NewConnection, NewMessage) == NewParticipant);
	Passed &= Expect("an old session cannot disconnect the new occupant or submit on its behalf", !Restarted.Disconnect(LastConnection) &&
		!Restarted.ResolveConnection(LastConnection, NewMessage) && Restarted.ResolveConnection(NewConnection, NewMessage) == NewParticipant);
	return Passed;
}

bool Run()
{
	bool Passed = CheckRecycledHandle();
	Passed &= CheckReceiverAndMessage();
	Passed &= CheckBindingFailures();
	Passed &= CheckCapacity();
	Passed &= CheckRoundLifetime();
	Passed &= CheckAdmissionEpochLifetime();
	if (!Passed)
	{
		return false;
	}
	std::puts("Participant connection checks passed");
	return true;
}
}
