#include "../../../xrNetServer/Multiplayer/GnsCallbackPump.h"

#include <cstdio>
#include <thread>

namespace GnsCallbackPumpTests
{
using namespace Multiplayer;

struct OwnerLock {};
struct OwnerGuard
{
	explicit OwnerGuard(OwnerLock*) noexcept {}
};

using TestInbox = TransportInbox<OwnerLock, OwnerGuard>;
using CallbackPump = GnsCallbackPump<TestInbox>;

constexpr HSteamListenSocket Listener = 27;
constexpr HSteamNetPollGroup ServerGroup = 30;
constexpr HSteamNetPollGroup ClientGroup = 31;
constexpr HSteamNetConnection Peer = 1;
constexpr HSteamNetConnection OtherPeer = 2;
constexpr HSteamNetConnection Client = 3;

struct Fixture
{
	TestInbox& ServerEvents;
	TestInbox& ClientEvents;
	xr_array<SteamNetConnectionInfo_t, 4> Live{};
	xr_span<SteamNetConnectionStatusChangedCallback_t> Pending{};
	std::size_t Runs = 0;
	std::size_t Accepted = 0;
	std::size_t Closed = 0;
	bool GroupSucceeds = true;
	CallbackPump* Reenter = nullptr;
	std::optional<GnsCallbackPollError> NestedError{};
};

static thread_local Fixture* g_Fixture = nullptr;

static const GnsCallbackApi TestApi{
	[](ISteamNetworkingSockets&) noexcept
	{
		auto& State = *g_Fixture;
		++State.Runs;
		if (State.Reenter)
		{
			const auto Nested = State.Reenter->Poll(nullptr, nullptr);
			if (!Nested)
			{
				State.NestedError = Nested.error();
			}
		}
		const auto Callback = reinterpret_cast<FnSteamNetConnectionStatusChanged>(CallbackPump::CallbackOption().m_val.m_ptr);
		for (auto& Event : State.Pending)
		{
			Callback(&Event);
		}
	},
	[](ISteamNetworkingSockets&, HSteamNetConnection Handle, SteamNetConnectionInfo_t& Info) noexcept
	{
		if (Handle >= g_Fixture->Live.size())
		{
			return false;
		}
		Info = g_Fixture->Live[Handle];
		return Info.m_eState != k_ESteamNetworkingConnectionState_None;
	},
	[](ISteamNetworkingSockets&, HSteamNetConnection Handle, s64 Generation) noexcept
	{
		auto& State = *g_Fixture;
		++State.Accepted;
		State.Live[Handle].m_nUserData = Generation;
		State.Live[Handle].m_eState = k_ESteamNetworkingConnectionState_Connected;
		return true;
	},
	[](ISteamNetworkingSockets&, HSteamNetConnection Handle, HSteamNetPollGroup Group) noexcept
	{
		auto& State = *g_Fixture;
		if (!State.GroupSucceeds && Group == ServerGroup)
		{
			return false;
		}
		auto& Inbox = Group == ServerGroup ? State.ServerEvents : State.ClientEvents;
		constexpr u8 Packet[]{0xa5};
		return Inbox.PushPacket({Handle, static_cast<u64>(State.Live[Handle].m_nUserData)}, Packet).has_value();
	},
	[](ISteamNetworkingSockets&, HSteamNetConnection Handle) noexcept
	{
		++g_Fixture->Closed;
		g_Fixture->Live[Handle] = {};
	},
};

static bool Expect(const char* Failure, bool Condition)
{
	if (!Condition)
	{
		std::fprintf(stderr, "%s\n", Failure);
	}
	return Condition;
}

static SteamNetConnectionStatusChangedCallback_t Event(HSteamNetConnection Handle, HSteamListenSocket FromListener,
	ESteamNetworkingConnectionState State, s64 Generation, int Reason = 0)
{
	SteamNetConnectionStatusChangedCallback_t Event{};
	Event.m_hConn = Handle;
	Event.m_info.m_hListenSocket = FromListener;
	Event.m_info.m_eState = State;
	Event.m_info.m_nUserData = Generation;
	Event.m_info.m_eEndReason = Reason;
	return Event;
}

static bool Take(TestInbox& Inbox, TransportEventKind Kind, TransportPeer Peer, u32 Reason = 0)
{
	const auto Item = Inbox.Pop();
	return Item && *Item && (*Item)->Kind == Kind && (*Item)->Peer == Peer && (*Item)->Reason == Reason;
}

static bool Empty(TestInbox& Inbox)
{
	const auto Item = Inbox.Pop();
	return Item && !*Item;
}

static bool CheckHostedRouting(ISteamNetworkingSockets& Sockets)
{
	xr_array<TransportEvent, 3> ServerStorage;
	xr_array<TransportEvent, 3> ClientStorage;
	TestInbox ServerEvents(ServerStorage);
	TestInbox ClientEvents(ClientStorage);
	Fixture State{ServerEvents, ClientEvents};
	g_Fixture = &State;
	CallbackPump Pump(TestApi);
	const s64 Generation = Pump.PrepareClient().value_or(CallbackPump::ClientOptions{}).Generation;
	const CallbackPump::ServerRoute ServerRoute{{&Sockets, Listener, ServerGroup}, ServerEvents};
	const CallbackPump::ClientRoute ClientRoute{{&Sockets, {}, ClientGroup, Client}, ClientEvents, Generation};
	auto Incoming = Event(Peer, Listener, k_ESteamNetworkingConnectionState_Connecting, -1);
	auto Foreign = Event(OtherPeer, Listener + 1, k_ESteamNetworkingConnectionState_Connecting, -1);
	auto Outbound = Event(Client, {}, k_ESteamNetworkingConnectionState_Connected, Generation);
	State.Live[Peer] = Incoming.m_info;
	State.Live[OtherPeer] = Foreign.m_info;
	State.Live[Client] = Outbound.m_info;
	xr_array Initial{Incoming, Foreign, Outbound};
	State.Pending = Initial;
	State.Reenter = &Pump;
	const auto Batch = Pump.Poll(&ServerRoute, &ClientRoute);
	bool Passed = Expect("one owned pump dispatches both routes and refuses reentry",
		Batch && Batch->Server == GnsEndpointEventStatus::Ok && Batch->Client == GnsEndpointEventStatus::Ok &&
		State.Runs == 1 && State.NestedError == GnsCallbackPollError::AlreadyPolling);
	const TransportPeer ServerPeer{Peer, static_cast<u64>(State.Live[Peer].m_nUserData)};
	const TransportPeer LocalClient{Client, static_cast<u64>(Generation)};
	Passed &= Expect("the listener allocates a fresh generation and ignores a foreign listener",
		Generation > 0 && ServerPeer.Generation > LocalClient.Generation && State.Accepted == 1);
	Passed &= Expect("both connected events enter the inbox before their receive group can deliver a packet",
		Take(ServerEvents, TransportEventKind::Connected, ServerPeer) && Take(ServerEvents, TransportEventKind::Packet, ServerPeer) &&
		Take(ClientEvents, TransportEventKind::Connected, LocalClient) && Take(ClientEvents, TransportEventKind::Packet, LocalClient));

	const auto Stale = Event(Peer, Listener, k_ESteamNetworkingConnectionState_ClosedByPeer, Generation, 1001);
	xr_array Duplicate{Incoming, Outbound, Stale};
	State.Pending = Duplicate;
	State.Reenter = nullptr;
	Passed &= Expect("duplicate and stale callbacks cannot touch either current connection",
		Pump.Poll(&ServerRoute, &ClientRoute).has_value() && State.Accepted == 1 && State.Closed == 0 &&
		Empty(ServerEvents) && Empty(ClientEvents));

	auto Closed = Event(Peer, Listener, k_ESteamNetworkingConnectionState_ClosedByPeer, State.Live[Peer].m_nUserData, 1234);
	State.Live[Peer] = Closed.m_info;
	xr_array ClosedTwice{Closed, Closed};
	State.Pending = ClosedTwice;
	Passed &= Expect("the current inbound close retires the native handle and publishes its reason exactly once",
		Pump.Poll(&ServerRoute, &ClientRoute).has_value() && State.Closed == 1 &&
		Take(ServerEvents, TransportEventKind::Disconnected, ServerPeer, 1234) && Empty(ServerEvents));

	State.Live[Peer] = Incoming.m_info;
	const auto OutdatedClient = Event(Client, {}, k_ESteamNetworkingConnectionState_ClosedByPeer, Generation, 999);
	const s64 NextGeneration = Pump.PrepareClient().value_or(CallbackPump::ClientOptions{}).Generation;
	auto NextOutbound = Event(Client, {}, k_ESteamNetworkingConnectionState_Connected, NextGeneration);
	State.Live[Client] = NextOutbound.m_info;
	xr_array Reused{Closed, Incoming, Closed, OutdatedClient, NextOutbound};
	State.Pending = Reused;
	const CallbackPump::ClientRoute NextClient{ClientRoute.Endpoint, ClientEvents, NextGeneration};
	Passed &= Expect("old close callbacks cannot close new connections that reuse either raw handle",
		Pump.Poll(&ServerRoute, &NextClient).has_value() && State.Accepted == 2 && State.Closed == 1);
	const TransportPeer NextServer{Peer, static_cast<u64>(State.Live[Peer].m_nUserData)};
	const TransportPeer NextLocal{Client, static_cast<u64>(NextGeneration)};
	Passed &= Expect("reused handles publish only their new generations",
		NextServer.Generation > ServerPeer.Generation && NextLocal.Generation > LocalClient.Generation &&
		Take(ServerEvents, TransportEventKind::Connected, NextServer) && Take(ServerEvents, TransportEventKind::Packet, NextServer) && Empty(ServerEvents) &&
		Take(ClientEvents, TransportEventKind::Connected, NextLocal) && Take(ClientEvents, TransportEventKind::Packet, NextLocal) && Empty(ClientEvents));

	const CallbackPump::ClientRoute Aliased{NextClient.Endpoint, ServerEvents, NextGeneration};
	const auto Invalid = Pump.Poll(&ServerRoute, &Aliased);
	Passed &= Expect("hosted endpoints require separate inboxes for independent overflow recovery",
		!Invalid && Invalid.error() == GnsCallbackPollError::InvalidRoute && State.Runs == 4);

	std::optional<GnsCallbackPollError> ForeignPoll;
	std::jthread Caller([&]
	{
		const auto Result = Pump.Poll(nullptr, nullptr);
		if (!Result)
		{
			ForeignPoll = Result.error();
		}
	});
	Caller.join();
	Passed &= Expect("only the designated process owner can drive the pump",
		ForeignPoll == GnsCallbackPollError::WrongThread && State.Runs == 4);
	g_Fixture = nullptr;
	return Passed;
}

static bool CheckOverflowAndEarlyClose(ISteamNetworkingSockets& Sockets)
{
	xr_array<TransportEvent, 1> ServerStorage;
	xr_array<TransportEvent, 3> ClientStorage;
	TestInbox ServerEvents(ServerStorage);
	TestInbox ClientEvents(ClientStorage);
	Fixture State{ServerEvents, ClientEvents};
	g_Fixture = &State;
	CallbackPump Pump(TestApi);
	const s64 Generation = Pump.PrepareClient().value_or(CallbackPump::ClientOptions{}).Generation;
	const CallbackPump::ServerRoute ServerRoute{{&Sockets, Listener, ServerGroup}, ServerEvents};
	const CallbackPump::ClientRoute ClientRoute{{&Sockets, {}, ClientGroup, Client}, ClientEvents, Generation};
	bool Passed = Expect("fixture fills only the listener inbox", ServerEvents.PushConnected({14, 2}).has_value());
	auto Incoming = Event(Peer, Listener, k_ESteamNetworkingConnectionState_Connecting, -1);
	auto Other = Event(OtherPeer, Listener, k_ESteamNetworkingConnectionState_Connecting, -1);
	const auto ObsoleteConnect = Event(Client, {}, k_ESteamNetworkingConnectionState_Connected, Generation);
	auto DroppedClient = Event(Client, {}, k_ESteamNetworkingConnectionState_ProblemDetectedLocally, Generation, 5001);
	State.Live[Peer] = Incoming.m_info;
	State.Live[OtherPeer] = Other.m_info;
	State.Live[Client] = DroppedClient.m_info;
	xr_array Pending{Incoming, Other, ObsoleteConnect, DroppedClient, DroppedClient};
	State.Pending = Pending;
	const auto Batch = Pump.Poll(&ServerRoute, &ClientRoute);
	Passed &= Expect("queue overflow closes the undelivered peer and stops only the affected endpoint's callback delivery",
		Batch && Batch->Server == GnsEndpointEventStatus::InboxOverflow && Batch->Client == GnsEndpointEventStatus::Ok &&
		ServerEvents.Pop() == std::unexpected(TransportInboxError::Overflow) && State.Accepted == 1 && State.Closed == 1);
	Passed &= Expect("an outbound failure before connect still produces one terminal event for its attempt",
		Take(ClientEvents, TransportEventKind::Disconnected, {Client, static_cast<u64>(Generation)}, 5001) && Empty(ClientEvents));
	g_Fixture = nullptr;
	return Passed;
}

static bool CheckNativeFailureAndUnacceptedClose(ISteamNetworkingSockets& Sockets)
{
	xr_array<TransportEvent, 3> ServerStorage;
	xr_array<TransportEvent, 3> ClientStorage;
	TestInbox ServerEvents(ServerStorage);
	TestInbox ClientEvents(ClientStorage);
	Fixture State{ServerEvents, ClientEvents};
	g_Fixture = &State;
	CallbackPump Pump(TestApi);
	const s64 Generation = Pump.PrepareClient().value_or(CallbackPump::ClientOptions{}).Generation;
	const CallbackPump::ServerRoute ServerRoute{{&Sockets, Listener, ServerGroup}, ServerEvents};
	const CallbackPump::ClientRoute ClientRoute{{&Sockets, {}, ClientGroup, Client}, ClientEvents, Generation};
	auto Gone = Event(OtherPeer, Listener, k_ESteamNetworkingConnectionState_ClosedByPeer, -1);
	auto Incoming = Event(Peer, Listener, k_ESteamNetworkingConnectionState_Connecting, -1);
	auto Outbound = Event(Client, {}, k_ESteamNetworkingConnectionState_Connected, Generation);
	State.Live[OtherPeer] = Gone.m_info;
	State.Live[Peer] = Incoming.m_info;
	State.Live[Client] = Outbound.m_info;
	State.GroupSucceeds = false;
	xr_array Pending{Gone, Incoming, Outbound};
	State.Pending = Pending;
	const auto Batch = Pump.Poll(&ServerRoute, &ClientRoute);
	const TransportPeer LocalClient{Client, static_cast<u64>(Generation)};
	bool Passed = Expect("a receive group failure is terminal for its endpoint and preserves the other endpoint",
		Batch && Batch->Server == GnsEndpointEventStatus::ReceiveGroupFailed && Batch->Client == GnsEndpointEventStatus::Ok &&
		State.Closed == 2 && State.Accepted == 1 && Take(ClientEvents, TransportEventKind::Connected, LocalClient) &&
		Take(ClientEvents, TransportEventKind::Packet, LocalClient));
	const auto FirstServerEvent = ServerEvents.Pop();
	Passed &= Expect("a closed unaccepted peer never acquires an inbox identity",
		FirstServerEvent && *FirstServerEvent && (*FirstServerEvent)->Peer.Handle == Peer && Empty(ServerEvents));
	g_Fixture = nullptr;
	return Passed;
}

bool Run(ISteamNetworkingSockets& Sockets)
{
	bool Passed = CheckHostedRouting(Sockets);
	Passed &= CheckOverflowAndEarlyClose(Sockets);
	Passed &= CheckNativeFailureAndUnacceptedClose(Sockets);
	return Passed;
}
}
