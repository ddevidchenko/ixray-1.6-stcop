#include "OutgoingTraffic.h"

#include <cstdio>
#include <limits>

namespace OutgoingTrafficTests
{
using namespace Multiplayer;
using enum OutgoingTraffic;

static bool Expect(const char* Name, bool Passed) noexcept
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Name);
	}
	return Passed;
}

static bool CheckClassification()
{
	using enum Wire::MessageType;
	struct Case
	{
		Wire::MessageType Type;
		OutgoingTraffic Traffic;
	};
	constexpr Case Cases[] = {
		{ClientHello, Control}, {ServerWelcome, Control}, {AdmissionReject, Control},
		{ClientMapLoaded, Control}, {ReadyAcknowledgement, Control},
		{BaselineBegin, Control}, {BaselineChunk, Bulk}, {BaselineEnd, Control},
		{ServerJoinBarrier, Control}, {CatchUpStateEvent, Control},
		{SnapshotBatch, Snapshot}, {CombatantBarrierChunk, Bulk},
	};
	bool Passed = true;
	for (const auto& Item : Cases)
	{
		Passed &= Expect("each supported envelope tag has the selected traffic class", GetOutgoingTraffic(Item.Type) == Item.Traffic);
	}
	Passed &= Expect("an unknown tag receives no traffic class", !GetOutgoingTraffic(static_cast<Wire::MessageType>(0xff)));
	Passed &= Expect("only control and bulk are reliable", IsReliableTraffic(Control) && IsReliableTraffic(Bulk) &&
		!IsReliableTraffic(Command) && !IsReliableTraffic(Snapshot) && !IsReliableTraffic(Cosmetic));
	return Passed;
}

static bool CheckNativeBudget()
{
	using enum SendAllowance;
	bool Passed = Expect("one maximum reliable bulk packet fits in the native budget",
		CheckSendAllowance(Bulk, 1100, {}) == Allowed);
	Passed &= Expect("outstanding reliable bytes reserve the bulk allowance even after transmission",
		CheckSendAllowance(Bulk, 1100, {0, 0, 1}) == Deferred);
	Passed &= Expect("a bulk send charges its reliable framing allowance",
		CheckSendAllowance(Bulk, 100, {500, 250, 250}) == Allowed &&
		CheckSendAllowance(Bulk, 100, {500, 251, 250}) == Deferred);
	Passed &= Expect("a full bulk allowance leaves room for complete command and snapshot packets",
		CheckSendAllowance(Command, 1100, {1116, 0, 0}) == Allowed &&
		CheckSendAllowance(Snapshot, 1100, {0, 0, 1116}) == Allowed);
	Passed &= Expect("commands and snapshots share the same exact gameplay limit",
		CheckSendAllowance(Command, 1100, {1100, 1100, 1100}) == Allowed &&
		CheckSendAllowance(Snapshot, 1100, {1100, 1100, 1101}) == Deferred);
	Passed &= Expect("gameplay saturation leaves a reliable control reserve",
		CheckSendAllowance(Control, 1100, {2200, 2200, 0}) == Allowed);
	Passed &= Expect("the control budget includes pending and unacknowledged bytes",
		CheckSendAllowance(Control, 1100, {1100, 1100, 5484}) == Allowed &&
		CheckSendAllowance(Control, 1100, {1100, 1100, 5485}) == Deferred);
	Passed &= Expect("cosmetics use an empty native queue",
		CheckSendAllowance(Cosmetic, 1100, {}) == Allowed &&
		CheckSendAllowance(Cosmetic, 1, {0, 1, 0}) == Deferred &&
		CheckSendAllowance(Cosmetic, 1, {0, 0, 1}) == Deferred);
	Passed &= Expect("saturated native counters cannot overflow into available capacity",
		CheckSendAllowance(Control, 1, {std::numeric_limits<u32>::max(), std::numeric_limits<u32>::max(), 3}) == Deferred);
	Passed &= Expect("invalid sizes and traffic are rejected before native submission",
		CheckSendAllowance(Control, 0, {}) == Invalid && CheckSendAllowance(Snapshot, 1101, {}) == Invalid &&
		CheckSendAllowance(static_cast<OutgoingTraffic>(0xff), 1, {}) == Invalid);
	return Passed;
}

bool Run()
{
	bool Passed = CheckNativeBudget();
	Passed &= CheckClassification();
	if (Passed)
	{
		std::puts("Outgoing traffic checks passed");
	}
	return Passed;
}
}
