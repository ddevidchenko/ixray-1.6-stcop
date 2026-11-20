#include "../../../xrNetServer/Multiplayer/TransportEventRouter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

namespace TransportInboxTests
{
using namespace Multiplayer;

struct FixtureLock
{
	std::atomic_flag Held;
};

class FixtureGuard
{
public:
	explicit FixtureGuard(FixtureLock* Lock) noexcept : Lock(Lock)
	{
		while (Lock->Held.test_and_set(std::memory_order_acquire))
		{
			std::this_thread::yield();
		}
	}
	~FixtureGuard() { Lock->Held.clear(std::memory_order_release); }

private:
	FixtureLock* Lock;
};

using Inbox = TransportInbox<FixtureLock, FixtureGuard>;

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

static bool CheckOrderedDelivery()
{
	constexpr TransportPeer Before{41, 1};
	constexpr TransportPeer After{41, 2};
	constexpr u8 Expected[]{0x00, 0xa4, 0xff};
	xr_array<TransportEvent, 4> Storage;
	Inbox Events(Storage);
	bool Received = false;
	{
		std::jthread Worker([&]()
		{
			xr_array<u8, 3> Datagram{0x00, 0xa4, 0xff};
			Received = Events.PushConnected(Before) && Events.PushPacket(Before, Datagram) &&
				Events.PushDisconnected(Before, 17) && Events.PushConnected(After);
			Datagram.fill(0);
		});
	}
	const auto Connected = Events.Pop();
	const auto Packet = Events.Pop();
	const auto Disconnected = Events.Pop();
	const auto Reconnected = Events.Pop();
	bool Passed = Expect("connect, packet and disconnect are handed to the owner in receive order", Received &&
		Connected && *Connected && (*Connected)->Kind == TransportEventKind::Connected && (*Connected)->Peer == Before &&
		Packet && *Packet && (*Packet)->Kind == TransportEventKind::Packet && (*Packet)->Peer == Before &&
		Disconnected && *Disconnected && (*Disconnected)->Kind == TransportEventKind::Disconnected &&
		(*Disconnected)->Peer == Before && (*Disconnected)->Reason == 17);
	Passed &= Expect("packet owns original receive bytes and reconnect retains its own generation",
		Packet && *Packet && std::ranges::equal((*Packet)->GetPayload(), Expected) &&
		Reconnected && *Reconnected && (*Reconnected)->Peer == After && (*Reconnected)->Kind == TransportEventKind::Connected);

	const auto Empty = Events.Pop();
	Passed &= Expect("the owner can poll the drained inbox", Empty && !*Empty);
	return Passed;
}

static bool CheckLimitsAndReuse()
{
	constexpr TransportPeer Peer{41, 1};
	xr_array<TransportEvent, 2> Storage;
	Inbox Events(Storage);
	xr_array<u8, 1101> Datagram{};
	Datagram[1099] = 0xc3;
	bool Passed = Expect("invalid handle, generation and oversized datagrams never occupy storage",
		Events.PushConnected({0, 1}) == std::unexpected(TransportInboxError::InvalidPeer) &&
		Events.PushDisconnected({41, 0}, 5) == std::unexpected(TransportInboxError::InvalidPeer) &&
		Events.PushPacket(Peer, Datagram) == std::unexpected(TransportInboxError::PacketTooLarge));
	Passed &= Expect("the maximum complete datagram and a lifecycle event both fit",
		Events.PushPacket(Peer, xr_span(Datagram).first(1100)) && Events.PushConnected(Peer));

	const auto First = Events.Pop();
	Passed &= Expect("the next owner turn can retire one entry and reuse its slot",
		First && *First && (*First)->GetPayload().size() == 1100 && (*First)->GetPayload().back() == 0xc3 &&
		Events.PushDisconnected(Peer, 74));
	const auto Connected = Events.Pop();
	const auto Disconnected = Events.Pop();
	Passed &= Expect("ring reuse preserves chronological order and lifecycle metadata",
		Connected && *Connected && (*Connected)->Kind == TransportEventKind::Connected && (*Connected)->GetPayload().empty() &&
		Disconnected && *Disconnected && (*Disconnected)->Kind == TransportEventKind::Disconnected &&
		(*Disconnected)->Reason == 74 && (*Disconnected)->GetPayload().empty());
	Passed &= Expect("a zero-length datagram still carries its packet kind", Events.PushPacket(Peer, {}).has_value());
	const auto EmptyPacket = Events.Pop();
	Passed &= Expect("empty payload does not turn a packet into a lifecycle event",
		EmptyPacket && *EmptyPacket && (*EmptyPacket)->Kind == TransportEventKind::Packet && (*EmptyPacket)->GetPayload().empty());
	return Passed;
}

static bool CheckOverflow()
{
	constexpr TransportPeer Stale{41, 11};
	constexpr TransportPeer Current{41, 12};
	xr_array<TransportEvent, 1> Storage;
	Inbox Events(Storage);
	constexpr u8 Packet[]{1};

	bool Passed = Expect("overflow is reported even when the blocked event is needed for cleanup",
		Events.PushConnected(Stale) && Events.PushDisconnected(Stale, 99) == std::unexpected(TransportInboxError::Overflow));
	Passed &= Expect("owner sees terminal overflow before any stale event and new writes still fail",
		Events.Pop() == std::unexpected(TransportInboxError::Overflow) &&
		Events.PushPacket(Current, Packet) == std::unexpected(TransportInboxError::Overflow) &&
		Events.Pop() == std::unexpected(TransportInboxError::Overflow));
	Events.ResetAfterClose();
	const auto Clear = Events.Pop();
	Passed &= Expect("after endpoint cleanup the owner can reuse the buffer", Clear && !*Clear && Events.PushConnected(Current));
	const auto Fresh = Events.Pop();
	Passed &= Expect("holding the same raw handle never revives old work after reset", Fresh && *Fresh && (*Fresh)->Peer == Current);

	Inbox None({});
	Passed &= Expect("zero capacity reports overflow without touching storage",
		None.PushConnected(Current) == std::unexpected(TransportInboxError::Overflow) && None.Pop() == std::unexpected(TransportInboxError::Overflow));
	return Passed;
}

static bool CheckConcurrentDrain()
{
	constexpr TransportPeer Peer{41, 20};
	constexpr u8 Count = 32;
	xr_array<TransportEvent, Count> Storage;
	Inbox Events(Storage);
	bool Posted = true;
	std::jthread Worker([&]()
	{
		for (u8 Sequence = 0; Sequence < Count; ++Sequence)
		{
			Posted &= Events.PushPacket(Peer, xr_span(&Sequence, 1)).has_value();
		}
	});

	const auto Deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	u8 Read = 0;
	bool Passed = true;
	while (Read < Count && std::chrono::steady_clock::now() < Deadline)
	{
		const auto Next = Events.Pop();
		if (!Next)
		{
			return Expect("concurrent receipt must stay within its configured capacity", false);
		}
		if (!*Next)
		{
			std::this_thread::yield();
			continue;
		}
		const auto Payload = (*Next)->GetPayload();
		Passed &= Expect("the owner observes complete payloads in order while the worker posts",
			(*Next)->Kind == TransportEventKind::Packet && (*Next)->Peer == Peer && Payload.size() == 1 && Payload[0] == Read);
		++Read;
	}
	Worker.join();
	return Expect("the game owner drains every posted packet", Passed && Posted && Read == Count);
}

static bool CheckDrainBudget()
{
	constexpr TransportPeer Peer{11, 1};
	xr_array<TransportEvent, 3> Storage;
	Inbox Events(Storage);
	constexpr u8 Bytes[]{3};
	if (!Events.PushConnected(Peer) || !Events.PushPacket(Peer, Bytes))
	{
		return Expect("seed the drain budget fixture", false);
	}
	int Calls = 0;
	bool PostedClose = false;
	auto Consume = [&](const TransportEvent& Event) noexcept
	{
		++Calls;
		if (Event.Kind == TransportEventKind::Connected)
		{
			PostedClose = Events.PushDisconnected(Peer, 42).has_value();
		}
	};
	bool Passed = Expect("a zero work budget leaves all queued events untouched", Events.Drain(0, Consume) == std::size_t{0} && Calls == 0);
	Passed &= Expect("the callback runs outside the inbox guard and can post later work",
		Events.Drain(1, Consume) == std::size_t{1} && Calls == 1 && PostedClose);
	const auto Next = Events.Pop();
	Passed &= Expect("a budget of one leaves the next packet for the following turn",
		Next && *Next && (*Next)->Kind == TransportEventKind::Packet && (*Next)->GetPayload().size() == 1);
	Passed &= Expect("a later drain finds the posted cleanup then stops at empty",
		Events.Drain(4, Consume) == std::size_t{1} && Calls == 2);
	return Passed;
}

struct RoutedEvent
{
	TransportEventKind Kind;
	TransportPeer Peer;
	TransportEventDelivery Delivery = TransportEventDelivery::Deliver;
	u32 Reason = 0;
	u8 FirstByte = 0;
};

struct EventTrace
{
	xr_array<RoutedEvent, 16> Events{};
	std::size_t Count = 0;

	void operator()(const TransportEvent& Event, TransportEventDelivery Delivery) noexcept
	{
		if (Count < Events.size())
		{
			const auto Payload = Event.GetPayload();
			Events[Count++] = {Event.Kind, Event.Peer, Delivery, Event.Reason, Payload.empty() ? u8{0} : Payload.front()};
		}
	}

	bool Is(std::size_t Index, TransportEventKind Kind, TransportPeer Peer, u32 Reason = 0, u8 FirstByte = 0,
		TransportEventDelivery Delivery = TransportEventDelivery::Deliver) const noexcept
	{
		return Index < Count && Events[Index].Kind == Kind && Events[Index].Peer == Peer && Events[Index].Delivery == Delivery &&
			Events[Index].Reason == Reason && Events[Index].FirstByte == FirstByte;
	}
};

static bool CheckOwnerRoutingAndBudget()
{
	constexpr TransportPeer Previous{31, 4};
	constexpr TransportPeer Current{31, 8};
	constexpr u8 Data[]{0xac};
	xr_array<TransportEvent, 10> Slots;
	xr_array<TransportConnectionSlot, 2> Peers;
	Inbox Events(Slots);
	TransportEventRouter Owner(Events, Peers);
	EventTrace Trace;
	if (!Events.PushPacket(Previous, Data) || !Events.PushConnected(Previous) || !Events.PushConnected(Previous) ||
		!Events.PushPacket(Previous, Data) || !Events.PushDisconnected(Previous, 41) || !Events.PushPacket(Previous, Data) ||
		!Events.PushConnected(Previous) || !Events.PushConnected(Current) || !Events.PushDisconnected(Previous, 99) ||
		!Events.PushPacket(Current, Data))
	{
		return Expect("seed owner routing and stale-event fixture", false);
	}

	bool Passed = Expect("a zero budget leaves all events queued", Owner.Drain(0, Trace) == std::size_t{0} && Trace.Count == 0);
	Passed &= Expect("discarded events use the same budget as delivered events",
		Owner.Drain(2, Trace) == std::size_t{2} && Trace.Count == 1 && Trace.Is(0, TransportEventKind::Connected, Previous));
	Passed &= Expect("each turn preserves FIFO while suppressing stale packets and duplicate connect",
		Owner.Drain(5, Trace) == std::size_t{5} && Trace.Count == 3 &&
		Trace.Is(1, TransportEventKind::Packet, Previous, 0, Data[0]) && Trace.Is(2, TransportEventKind::Disconnected, Previous, 41));
	Passed &= Expect("old close cannot retire the new peer even when the native handle is identical",
		Owner.Drain(5, Trace) == std::size_t{3} && Trace.Count == 5 &&
		Trace.Is(3, TransportEventKind::Connected, Current) && Trace.Is(4, TransportEventKind::Packet, Current, 0, Data[0]));
	return Passed;
}

static bool CheckOutboundAttempt()
{
	constexpr TransportPeer Attempt{43, 12};
	constexpr TransportPeer Retry{43, 13};
	constexpr TransportPeer Unknown{44, 14};
	constexpr u8 Data[]{0xbb};
	xr_array<TransportEvent, 5> Slots;
	xr_array<TransportConnectionSlot, 1> Peers;
	Inbox Events(Slots);
	TransportEventRouter Owner(Events, Peers);
	EventTrace Trace;
	bool Passed = Expect("the loading owner can register its outbound attempt",
		Owner.RegisterOutbound(Attempt) == TransportRegistration::Tracked && Owner.RegisterOutbound(Attempt) == TransportRegistration::Stale);
	Passed &= Expect("seed pending packets and connection failure",
		Events.PushPacket(Attempt, Data) && Events.PushDisconnected(Unknown, 5) && Events.PushDisconnected(Attempt, 75) &&
		Events.PushDisconnected(Attempt, 75) && Events.PushConnected(Attempt));
	Passed &= Expect("a matching pre-connect close is delivered once and pending payloads are discarded",
		Owner.Drain(5, Trace) == std::size_t{5} && Trace.Count == 1 && Trace.Is(0, TransportEventKind::Disconnected, Attempt, 75));
	Passed &= Expect("retry can register the same raw handle with a newer generation", Owner.RegisterOutbound(Retry) == TransportRegistration::Tracked);
	Passed &= Expect("seed native connect and data for an outbound retry",
		Events.PushConnected(Retry) && Events.PushPacket(Retry, Data));
	Passed &= Expect("native connect enables data for the registered outbound retry",
		Owner.Drain(3, Trace) == std::size_t{2} && Trace.Count == 3 &&
		Trace.Is(1, TransportEventKind::Connected, Retry) && Trace.Is(2, TransportEventKind::Packet, Retry, 0, Data[0]));
	Passed &= Expect("outbound capacity rejects a fresh attempt before its events can arrive",
		Owner.RegisterOutbound(Unknown) == TransportRegistration::Full && Events.PushDisconnected(Retry, 76) &&
		Owner.Drain(1, Trace) == std::size_t{1} && Owner.RegisterOutbound(Unknown) == TransportRegistration::Stale);
	return Passed;
}

static bool CheckOutOfOrderOutboundConfirmations()
{
	constexpr TransportPeer First{41, 1};
	constexpr TransportPeer Second{42, 2};
	xr_array<TransportEvent, 2> Slots;
	xr_array<TransportConnectionSlot, 2> Peers;
	Inbox Events(Slots);
	TransportEventRouter Owner(Events, Peers);
	EventTrace Trace;
	bool Passed = Expect("two valid outbound generations can be tracked before either confirms",
		Owner.RegisterOutbound(First) == TransportRegistration::Tracked && Owner.RegisterOutbound(Second) == TransportRegistration::Tracked);
	Passed &= Expect("confirmations can arrive in native order for tracked attempts",
		Events.PushConnected(Second) && Events.PushConnected(First) && Owner.Drain(2, Trace) == std::size_t{2} &&
		Trace.Count == 2 && Trace.Is(0, TransportEventKind::Connected, Second) && Trace.Is(1, TransportEventKind::Connected, First));
	return Passed;
}

static bool CheckOwnerCapacity()
{
	constexpr TransportPeer First{61, 20};
	constexpr TransportPeer Rejected{62, 21};
	constexpr TransportPeer Later{62, 22};
	constexpr u8 Data[]{0x42};
	xr_array<TransportEvent, 7> Slots;
	xr_array<TransportConnectionSlot, 1> Peers;
	Inbox Events(Slots);
	TransportEventRouter Owner(Events, Peers);
	EventTrace Trace;
	bool Passed = Expect("seed capacity overflow and the next valid generation",
		Events.PushConnected(First) && Events.PushConnected(Rejected) && Events.PushPacket(Rejected, Data) &&
		Events.PushDisconnected(First, 88) && Events.PushConnected(Rejected) && Events.PushConnected(Later) && Events.PushPacket(Later, Data));
	Passed &= Expect("a full owner table asks to close only the newcomer and never revives its rejected generation",
		Owner.Drain(7, Trace) == std::size_t{7} && Trace.Count == 5 &&
		Trace.Is(0, TransportEventKind::Connected, First) &&
		Trace.Is(1, TransportEventKind::Connected, Rejected, 0, 0, TransportEventDelivery::RejectFull) &&
		Trace.Is(2, TransportEventKind::Disconnected, First, 88) && Trace.Is(3, TransportEventKind::Connected, Later) &&
		Trace.Is(4, TransportEventKind::Packet, Later, 0, Data[0]));
	return Passed;
}

static bool CheckEndpointFaultRecovery()
{
	constexpr TransportPeer Active{72, 30};
	constexpr TransportPeer Pending{73, 31};
	constexpr TransportPeer New{72, 32};
	constexpr TransportPeer Conflict{72, 33};
	constexpr TransportPeer AfterRecovery{73, 34};
	constexpr u8 Data[]{1};
	xr_array<TransportEvent, 2> Slots;
	xr_array<TransportConnectionSlot, 2> Peers;
	Inbox Events(Slots);
	TransportEventRouter Owner(Events, Peers);
	EventTrace Trace;
	bool Passed = Expect("seed active and pending owner peer state", Events.PushConnected(Active) &&
		Owner.Drain(1, Trace) == std::size_t{1} && Owner.RegisterOutbound(Pending) == TransportRegistration::Tracked);
	Passed &= Expect("fill the inbox and overflow a terminal event", Events.PushPacket(Active, Data) && Events.PushPacket(Active, Data) &&
		Events.PushDisconnected(Active, 6) == std::unexpected(TransportInboxError::Overflow));
	Passed &= Expect("inbox overflow stops delivery and requires endpoint cleanup",
		Owner.Drain(2, Trace) == std::unexpected(TransportRouteError::InboxOverflow) && Trace.Count == 1);

	xr_array<TransportPeer, 2> Retired{};
	std::size_t RetiredCount = 0;
	auto Retire = [&](TransportPeer Peer) noexcept
	{
		if (RetiredCount < Retired.size())
		{
			Retired[RetiredCount] = Peer;
		}
		++RetiredCount;
	};
	Owner.ResetAfterClose(Retire);
	Owner.ResetAfterClose(Retire);
	Passed &= Expect("cleanup retires both active and pending peers once",
		RetiredCount == 2 && std::ranges::find(Retired, Active) != Retired.end() && std::ranges::find(Retired, Pending) != Retired.end());
	Passed &= Expect("cleanup allows reuse with newer generations only", Events.PushConnected(Active) && Events.PushConnected(New) &&
		Owner.Drain(2, Trace) == std::size_t{2} && Trace.Count == 2 && Trace.Is(1, TransportEventKind::Connected, New));
	Passed &= Expect("a newer connect colliding with a live handle stops dispatch before following payloads",
		Events.PushConnected(Conflict) && Events.PushPacket(New, Data) &&
		Owner.Drain(2, Trace) == std::unexpected(TransportRouteError::HandleConflict) &&
		Owner.Drain(2, Trace) == std::unexpected(TransportRouteError::HandleConflict) && Trace.Count == 2);
	Passed &= Expect("an endpoint fault also blocks new outbound registrations",
		Owner.RegisterOutbound(AfterRecovery) == TransportRegistration::EndpointFailed);
	std::size_t ReapedAfterConflict = 0;
	TransportPeer ConflictRetired{};
	Owner.ResetAfterClose([&](TransportPeer Peer) noexcept
	{
		++ReapedAfterConflict;
		ConflictRetired = Peer;
	});
	Passed &= Expect("conflict cleanup retires the old live generation and discards unapplied work",
		ReapedAfterConflict == 1 && ConflictRetired == New && Owner.Drain(2, Trace) == std::size_t{0} &&
		Owner.RegisterOutbound(Conflict) == TransportRegistration::Stale &&
		Owner.RegisterOutbound(AfterRecovery) == TransportRegistration::Tracked);
	return Passed;
}

bool RunRouter()
{
	bool Passed = CheckOwnerRoutingAndBudget();
	Passed &= CheckOutboundAttempt();
	Passed &= CheckOutOfOrderOutboundConfirmations();
	Passed &= CheckOwnerCapacity();
	Passed &= CheckEndpointFaultRecovery();
	if (Passed)
	{
		std::puts("Transport event routing checks passed");
	}
	return Passed;
}

bool Run()
{
	bool Passed = CheckOrderedDelivery();
	Passed &= CheckLimitsAndReuse();
	Passed &= CheckOverflow();
	Passed &= CheckConcurrentDrain();
	Passed &= CheckDrainBudget();
	if (!Passed)
	{
		return false;
	}
	std::puts("Transport inbox checks passed");
	return true;
}
}
