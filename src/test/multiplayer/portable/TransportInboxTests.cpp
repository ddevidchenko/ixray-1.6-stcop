#include "../../../xrNetServer/Multiplayer/TransportInbox.h"

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
