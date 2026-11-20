#include "../transport/TransportScenario.h"
#include "BaselineTransfer.h"
#include "CatchUpDespawn.h"

#include "../../../xrNetServer/Multiplayer/GnsCallbackPump.h"
#include "../../../xrNetServer/Multiplayer/GnsEndpoint.h"
#include "../../../xrNetServer/Multiplayer/GnsRuntime.h"
#include "../../../xrNetServer/Multiplayer/GnsReceive.h"
#include "../../../xrNetServer/Multiplayer/GnsReliableSend.h"
#include "../../../xrNetServer/Multiplayer/GnsSnapshotSend.h"
#include "../../../xrNetServer/Multiplayer/TransportEventRouter.h"

#include <steam/isteamnetworkingutils.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>

namespace GnsCallbackPumpTests
{
bool Run(ISteamNetworkingSockets& Sockets);
}

namespace GnsTransportTests
{
using namespace Multiplayer::Test;
using namespace std::chrono_literals;

// The fixture owns all runtime operations on its test thread.
struct FixtureLock
{
};

class FixtureGuard
{
public:
	explicit FixtureGuard(FixtureLock*) noexcept {}
};

using ProcessRuntime = Multiplayer::GnsRuntime<FixtureLock, FixtureGuard>;

static bool CheckSharedRuntime()
{
	ProcessRuntime Runtime;
	SteamNetworkingErrMsg Error{};
	auto Server = Runtime.AcquireServer(Error);
	auto Client = Runtime.AcquireClient("local fixture", Error);
	auto* Sockets = Server.Get();
	if (!Sockets || Client.Get() != Sockets)
	{
		std::fputs("server and client must share the first initialized GNS interface\n", stderr);
		return false;
	}

	auto Moved = std::move(Client);
	Server.Reset();
	if (Client.Get() || Moved.Get() != Sockets || SteamNetworkingSockets() != Sockets)
	{
		std::fputs("releasing the server or moving the client must preserve the remaining lease\n", stderr);
		return false;
	}
	Moved.Reset();
	if (SteamNetworkingSockets() != nullptr)
	{
		std::fputs("the final endpoint must release the GNS runtime\n", stderr);
		return false;
	}
	return true;
}

struct RuntimeFaults
{
	bool FailStart = false;
	bool HideInterface = false;
	int Starts = 0;
	int Stops = 0;
};

static RuntimeFaults g_RuntimeFaults;

static bool StartRuntime(const SteamNetworkingIdentity* Identity, SteamNetworkingErrMsg& Error)
{
	++g_RuntimeFaults.Starts;
	if (g_RuntimeFaults.FailStart)
	{
		std::snprintf(Error, sizeof(Error), "injected initialization failure");
		return false;
	}
	return GameNetworkingSockets_Init(Identity, Error);
}

static ISteamNetworkingSockets* GetRuntimeInterface()
{
	return g_RuntimeFaults.HideInterface ? nullptr : SteamNetworkingSockets();
}

static void StopRuntime()
{
	++g_RuntimeFaults.Stops;
	GameNetworkingSockets_Kill();
}

static bool CheckFailureAndRetry()
{
	g_RuntimeFaults = {};
	ProcessRuntime Runtime({StartRuntime, GetRuntimeInterface, StopRuntime});
	SteamNetworkingErrMsg Error{};

	const std::array<char, 256> LongIdentity = []
	{
		std::array<char, 256> Value{};
		Value.fill('x');
		Value.back() = 0;
		return Value;
	}();
	if (Runtime.AcquireClient(nullptr, Error).Get() || Runtime.AcquireClient(LongIdentity.data(), Error).Get() ||
		g_RuntimeFaults.Starts != 0 || g_RuntimeFaults.Stops != 0 || !Error[0])
	{
		std::fputs("invalid client identities must leave the runtime untouched\n", stderr);
		return false;
	}

	g_RuntimeFaults.FailStart = true;
	if (Runtime.AcquireServer(Error).Get() || g_RuntimeFaults.Starts != 1 || g_RuntimeFaults.Stops != 0 || !Error[0])
	{
		std::fputs("failed GNS initialization must leave a retryable runtime without calling shutdown\n", stderr);
		return false;
	}

	g_RuntimeFaults.FailStart = false;
	g_RuntimeFaults.HideInterface = true;
	if (Runtime.AcquireServer(Error).Get() || SteamNetworkingSockets() != nullptr ||
		g_RuntimeFaults.Starts != 2 || g_RuntimeFaults.Stops != 1 || !Error[0])
	{
		std::fputs("successful initialization without an interface must be unwound once\n", stderr);
		return false;
	}

	g_RuntimeFaults.HideInterface = false;
	auto Server = Runtime.AcquireServer(Error);
	auto Client = Runtime.AcquireClient("fixture", Error);
	if (!Server.Get() || Client.Get() != Server.Get() || g_RuntimeFaults.Starts != 3 || g_RuntimeFaults.Stops != 1)
	{
		std::fputs("a retry must initialize once and both leases must use that interface\n", stderr);
		return false;
	}
	Client = std::move(Server);
	if (!Client.Get() || Server.Get() || g_RuntimeFaults.Stops != 1 || SteamNetworkingSockets() != Client.Get())
	{
		std::fputs("moving onto an existing lease must release just one ownership share\n", stderr);
		return false;
	}
	Client.Reset();
	Client.Reset();
	if (g_RuntimeFaults.Stops != 2 || SteamNetworkingSockets() != nullptr)
	{
		std::fputs("reset after move assignment must shut down once\n", stderr);
		return false;
	}
	return true;
}

static bool CheckProcessIdentity()
{
	ProcessRuntime Runtime;
	SteamNetworkingErrMsg Error{};
	auto First = Runtime.AcquireClient("fixture one", Error);
	auto Same = Runtime.AcquireClient("fixture one", Error);
	auto Different = Runtime.AcquireClient("fixture two", Error);
	auto Server = Runtime.AcquireServer(Error);
	SteamNetworkingIdentity Identity{};
	if (!First.Get() || Same.Get() != First.Get() || Different.Get() || Server.Get() || !Error[0] ||
		!First.Get()->GetIdentity(&Identity) || !Identity.GetGenericString() ||
		std::strcmp(Identity.GetGenericString(), "fixture one") != 0)
	{
		std::fputs("an active remote client identity cannot change while the runtime is leased\n", stderr);
		return false;
	}

	Same.Reset();
	First.Reset();
	Server = Runtime.AcquireServer(Error);
	auto Local = Runtime.AcquireClient("fixture two", Error);
	if (!Server.Get() || Local.Get() != Server.Get() || !Local.Get()->GetIdentity(&Identity) || !Identity.IsLocalHost())
	{
		std::fputs("a fresh server runtime supplies the shared local identity to its client\n", stderr);
		return false;
	}

	Local.Reset();
	if (SteamNetworkingSockets() != Server.Get())
	{
		std::fputs("closing the client first must preserve the live server runtime\n", stderr);
		return false;
	}
	return true;
}

namespace EndpointTests
{
using namespace Multiplayer;

enum class Attempt
{
	None,
	Listener,
	PollGroup,
	Connection,
	Worker,
	WorkerThrows,
};

struct Resources
{
	Attempt Failure = Attempt::None;
	int Listeners = 0;
	int PollGroups = 0;
	int Connections = 0;
	int WorkerStarts = 0;
	bool ClosedEarly = false;
	std::atomic<bool> Started = false;
	std::atomic<bool> Exited = false;
};

static Resources g_Resources;

static void RunWorker(std::stop_token Stop, GnsEndpointView, void*) noexcept
{
	g_Resources.Started.store(true);
	while (!Stop.stop_requested())
	{
		std::this_thread::sleep_for(1ms);
	}
	g_Resources.Exited.store(true);
}

static const GnsEndpointApi FixtureApi{
	[](ISteamNetworkingSockets&, const SteamNetworkingIPAddr&, std::span<const SteamNetworkingConfigValue_t>)
	{
		if (g_Resources.Failure == Attempt::Listener)
		{
			return k_HSteamListenSocket_Invalid;
		}
		++g_Resources.Listeners;
		return HSteamListenSocket{1};
	},
	[](ISteamNetworkingSockets&)
	{
		if (g_Resources.Failure == Attempt::PollGroup)
		{
			return k_HSteamNetPollGroup_Invalid;
		}
		++g_Resources.PollGroups;
		return HSteamNetPollGroup{2};
	},
	[](ISteamNetworkingSockets&, const SteamNetworkingIPAddr&, std::span<const SteamNetworkingConfigValue_t>)
	{
		if (g_Resources.Failure == Attempt::Connection)
		{
			return k_HSteamNetConnection_Invalid;
		}
		++g_Resources.Connections;
		return HSteamNetConnection{3};
	},
	[](ISteamNetworkingSockets&, HSteamListenSocket) { --g_Resources.Listeners; },
	[](ISteamNetworkingSockets&, HSteamNetPollGroup) { --g_Resources.PollGroups; },
	[](ISteamNetworkingSockets&, HSteamNetConnection)
	{
		g_Resources.ClosedEarly = g_Resources.Started.load() && !g_Resources.Exited.load();
		--g_Resources.Connections;
	},
	[](GnsEndpointRun Run, GnsEndpointView View, void* Context)
	{
		++g_Resources.WorkerStarts;
		if (g_Resources.Failure == Attempt::Worker)
		{
			return std::jthread{};
		}
		if (g_Resources.Failure == Attempt::WorkerThrows)
		{
			throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
		}
		return std::jthread(Run, View, Context);
	},
};

static bool CheckEndpointLifecycle()
{
	ProcessRuntime Runtime;
	SteamNetworkingIPAddr Address{};
	SteamNetworkingErrMsg Error{};
	GnsEndpoint<ProcessRuntime::Lease> Endpoint(FixtureApi);
	if (Endpoint.OpenClient({}, Address, {}, RunWorker, nullptr) != std::unexpected(GnsEndpointError::RuntimeUnavailable))
	{
		std::fputs("an endpoint cannot open without owning a runtime lease\n", stderr);
		return false;
	}
	for (const auto Failure : {Attempt::Listener, Attempt::PollGroup, Attempt::Worker, Attempt::WorkerThrows})
	{
		g_Resources.Failure = Failure;
		const auto Opened = Endpoint.OpenServer(Runtime.AcquireServer(Error), Address, {}, RunWorker, nullptr);
		const GnsEndpointError Expected = Failure == Attempt::Listener ? GnsEndpointError::ListenerFailed :
			Failure == Attempt::PollGroup ? GnsEndpointError::PollGroupFailed : GnsEndpointError::WorkerFailed;
		if (Opened != std::unexpected(Expected) || Endpoint.Get().Sockets || g_Resources.Listeners != 0 ||
			g_Resources.PollGroups != 0 || SteamNetworkingSockets() != nullptr)
		{
			std::fputs("server startup failure must release each acquired resource and the runtime exactly once\n", stderr);
			return false;
		}
	}

	g_Resources.Failure = Attempt::Connection;
	const auto Failed = Endpoint.OpenClient(Runtime.AcquireClient("fixture", Error), Address, {}, RunWorker, nullptr);
	if (Failed != std::unexpected(GnsEndpointError::ConnectionFailed) || Endpoint.Get().Sockets ||
		g_Resources.Connections != 0 || SteamNetworkingSockets() != nullptr)
	{
		std::fputs("client connection failure must release its runtime lease\n", stderr);
		return false;
	}

	g_Resources.Failure = Attempt::PollGroup;
	const auto GroupFailed = Endpoint.OpenClient(Runtime.AcquireClient("fixture", Error), Address, {}, RunWorker, nullptr);
	if (GroupFailed != std::unexpected(GnsEndpointError::PollGroupFailed) || Endpoint.Get().Sockets ||
		g_Resources.Connections != 0 || g_Resources.PollGroups != 0 || SteamNetworkingSockets() != nullptr)
	{
		std::fputs("a failed client receive group must release the connection and its runtime lease\n", stderr);
		return false;
	}

	g_Resources.Failure = Attempt::None;
	if (!Endpoint.OpenClient(Runtime.AcquireClient("fixture", Error), Address, {}, RunWorker, nullptr))
	{
		std::fputs("endpoint must allow retry after creation failures\n", stderr);
		return false;
	}
	if (Endpoint.OpenClient(Runtime.AcquireClient("fixture", Error), Address, {}, RunWorker, nullptr) !=
		std::unexpected(GnsEndpointError::AlreadyOpen) || g_Resources.Connections != 1 || g_Resources.PollGroups != 1 || !Endpoint.Get().Sockets)
	{
		std::fputs("duplicate open must preserve the active endpoint\n", stderr);
		return false;
	}
	const auto Deadline = std::chrono::steady_clock::now() + 2s;
	while (!g_Resources.Started.load() && std::chrono::steady_clock::now() < Deadline)
	{
		std::this_thread::sleep_for(1ms);
	}
	Endpoint.Close();
	Endpoint.Close();
	if (g_Resources.ClosedEarly || !g_Resources.Started.load() || !g_Resources.Exited.load() ||
		g_Resources.Connections != 0 || g_Resources.PollGroups != 0 || Endpoint.Get().Sockets || SteamNetworkingSockets() != nullptr)
	{
		std::fputs("closing an active endpoint must join the worker before closing its connection\n", stderr);
		return false;
	}
	return true;
}
static bool CheckPolledEndpointLifecycle()
{
	ProcessRuntime Runtime;
	SteamNetworkingIPAddr Address{};
	SteamNetworkingErrMsg Error{};
	GnsEndpoint<ProcessRuntime::Lease> Server(FixtureApi);
	const int PreviousWorkerStarts = g_Resources.WorkerStarts;
	if (Server.OpenServer({}, Address, {}) != std::unexpected(GnsEndpointError::RuntimeUnavailable))
	{
		std::fputs("a polled endpoint still requires a runtime lease\n", stderr);
		return false;
	}

	g_Resources.Failure = Attempt::PollGroup;
	if (Server.OpenServer(Runtime.AcquireServer(Error), Address, {}) != std::unexpected(GnsEndpointError::PollGroupFailed) ||
		Server.Get().Sockets || g_Resources.Listeners != 0 || g_Resources.PollGroups != 0 || SteamNetworkingSockets())
	{
		std::fputs("a polled listener must unwind its native handle and lease when group creation fails\n", stderr);
		return false;
	}

	g_Resources.Failure = Attempt::WorkerThrows;
	if (!Server.OpenServer(Runtime.AcquireServer(Error), Address, {}))
	{
		std::fputs("opening an owner-polled server must not start a worker\n", stderr);
		return false;
	}
	const auto ServerView = Server.Get();
	{
		GnsEndpoint<ProcessRuntime::Lease> Client(FixtureApi);
		g_Resources.Failure = Attempt::Connection;
		if (Client.OpenClient(Runtime.AcquireClient("polled fixture", Error), Address, {}) != std::unexpected(GnsEndpointError::ConnectionFailed) ||
			Client.Get().Sockets || SteamNetworkingSockets() != ServerView.Sockets || g_Resources.Connections != 0)
		{
			std::fputs("a failed hosted client open must preserve the listener and release only the client lease\n", stderr);
			return false;
		}

		g_Resources.Failure = Attempt::PollGroup;
		if (!Client.OpenClient(Runtime.AcquireClient("polled fixture", Error), Address, {}) ||
			Client.Get().PollGroup != k_HSteamNetPollGroup_Invalid || Client.Get().Sockets != ServerView.Sockets ||
			g_Resources.PollGroups != 1 || g_Resources.Connections != 1)
		{
			std::fputs("a directly polled client must use its native connection without creating a group\n", stderr);
			return false;
		}
		const auto Connection = Client.Get().Connection;
		if (Client.OpenClient(Runtime.AcquireClient("polled fixture", Error), Address, {}) != std::unexpected(GnsEndpointError::AlreadyOpen) ||
			Client.Get().Connection != Connection || g_Resources.Connections != 1)
		{
			std::fputs("a duplicate owner-polled open must preserve the existing connection\n", stderr);
			return false;
		}

		Server.Close();
		Server.Close();
		if (Server.Get().Sockets || SteamNetworkingSockets() != Client.Get().Sockets ||
			g_Resources.Listeners != 0 || g_Resources.PollGroups != 0)
		{
			std::fputs("closing the polled server must release its native resources and preserve the hosted client lease\n", stderr);
			return false;
		}
	}
	if (SteamNetworkingSockets() || g_Resources.Connections != 0 || g_Resources.WorkerStarts != PreviousWorkerStarts)
	{
		std::fputs("the polled client destructor must close its connection and release the final lease without a worker\n", stderr);
		return false;
	}

	g_Resources.Failure = Attempt::None;
	return true;
}

struct InboxLock
{
	std::atomic_flag Held;
};

class InboxGuard
{
public:
	explicit InboxGuard(InboxLock* Lock) noexcept : Lock(Lock)
	{
		while (Lock->Held.test_and_set(std::memory_order_acquire))
		{
			std::this_thread::yield();
		}
	}
	~InboxGuard() { Lock->Held.clear(std::memory_order_release); }

private:
	InboxLock* Lock;
};

using EventInbox = TransportInbox<InboxLock, InboxGuard>;
using EventRouter = TransportEventRouter<InboxLock, InboxGuard>;
using CallbackPump = GnsCallbackPump<EventInbox>;

struct ReceiveFixture
{
	EventInbox& Inbox;
	std::atomic<bool> Failed = false;
};

static void RunReceive(std::stop_token Stop, GnsEndpointView View, void* Context) noexcept
{
	auto& Fixture = *static_cast<ReceiveFixture*>(Context);
	while (!Stop.stop_requested())
	{
		const auto Received = ReceiveGnsPacket(View, Fixture.Inbox);
		if (!Received)
		{
			Fixture.Failed.store(true);
			return;
		}
		if (!*Received)
		{
			std::this_thread::sleep_for(1ms);
		}
	}
}

template <class Predicate>
static bool WaitFor(Predicate&& Ready)
{
	const auto Deadline = std::chrono::steady_clock::now() + 2s;
	do
	{
		if (Ready())
		{
			return true;
		}
		std::this_thread::sleep_for(1ms);
	}
	while (std::chrono::steady_clock::now() < Deadline);
	return false;
}

static bool CheckPolledNativeEndpoints()
{
	ProcessRuntime Runtime;
	SteamNetworkingErrMsg Error{};
	GnsEndpoint<ProcessRuntime::Lease> Server;
	GnsEndpoint<ProcessRuntime::Lease> Client;
	SteamNetworkingIPAddr Address{};
	const auto Callback = GnsCallbackDispatch::CallbackOption();
	for (u16 Port = 48800; Port < 48832; ++Port)
	{
		Address.SetIPv4(0x7f000001u, Port);
		const auto Opened = Server.OpenServer(Runtime.AcquireServer(Error), Address, xr_span(&Callback, 1));
		if (Opened || Opened.error() != GnsEndpointError::ListenerFailed)
		{
			break;
		}
	}
	const auto Listen = Server.Get();
	if (!Listen.Sockets || !Listen.Sockets->GetListenSocketAddress(Listen.Listener, &Address) ||
		!Client.OpenClient(Runtime.AcquireClient("hosted fixture", Error), Address, {}))
	{
		std::fputs("could not open owner-polled native endpoints over one runtime\n", stderr);
		return false;
	}
	const auto Outbound = Client.Get();
	auto& Sockets = *Listen.Sockets;
	constexpr u8 BeforeAccept[]{0x3d, 0x0a};
	if (SubmitGnsSnapshot(Sockets, Outbound.Connection, BeforeAccept) != SnapshotSendAttempt::Dropped)
	{
		std::fputs("a no-delay snapshot must be dropped while the connection is still opening\n", stderr);
		return false;
	}

	struct Arrival
	{
		HSteamListenSocket Listener;
		HSteamNetConnection Pending = k_HSteamNetConnection_Invalid;
	} Peer{Listen.Listener};
	bool Accepted = false;
	const bool Connected = WaitFor([&]
	{
		GnsCallbackDispatch::Poll(Sockets, &Peer, [](void* Context, const SteamNetConnectionStatusChangedCallback_t& Event) noexcept
		{
			auto& Peer = *static_cast<Arrival*>(Context);
			if (Event.m_info.m_hListenSocket == Peer.Listener && Event.m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting)
			{
				Peer.Pending = Event.m_hConn;
			}
		});
		if (Peer.Pending != k_HSteamNetConnection_Invalid && !Accepted)
		{
			Accepted = Sockets.AcceptConnection(Peer.Pending) == k_EResultOK &&
				Sockets.SetConnectionPollGroup(Peer.Pending, Listen.PollGroup);
		}
		SteamNetConnectionInfo_t Info{};
		return Accepted && Sockets.GetConnectionInfo(Outbound.Connection, &Info) &&
			Info.m_eState == k_ESteamNetworkingConnectionState_Connected;
	});
	constexpr u8 ToClient[]{0x6a, 0, 0xc8};
	constexpr u8 ToServer[]{0xc3, 0, 0x19};
	if (!Connected || Outbound.Sockets != Listen.Sockets ||
		Sockets.SendMessageToConnection(Peer.Pending, ToClient, sizeof(ToClient), k_nSteamNetworkingSend_ReliableNoNagle, nullptr) != k_EResultOK ||
		Sockets.SendMessageToConnection(Outbound.Connection, ToServer, sizeof(ToServer), k_nSteamNetworkingSend_ReliableNoNagle, nullptr) != k_EResultOK)
	{
		std::fputs("the hosted polled connection must exchange data over the same GNS runtime\n", stderr);
		return false;
	}

	const auto ReceivedBytes = [](auto Receive, xr_span<const u8> Expected)
	{
		SteamNetworkingMessage_t* Raw = nullptr;
		if (!WaitFor([&] { return Receive(&Raw) == 1; }))
		{
			return false;
		}
		const auto Release = [](SteamNetworkingMessage_t* Message) noexcept { Message->Release(); };
		const std::unique_ptr<SteamNetworkingMessage_t, decltype(Release)> Message(Raw, Release);
		return Message && Message->m_cbSize == static_cast<int>(Expected.size()) &&
			std::memcmp(Message->m_pData, Expected.data(), Expected.size()) == 0;
	};
	if (!ReceivedBytes([&](auto** Message) { return Sockets.ReceiveMessagesOnConnection(Outbound.Connection, Message, 1); }, ToClient) ||
		!ReceivedBytes([&](auto** Message) { return Sockets.ReceiveMessagesOnPollGroup(Listen.PollGroup, Message, 1); }, ToServer))
	{
		std::fputs("owner polling must preserve packet bytes through the client connection and server receive group\n", stderr);
		return false;
	}

	Client.Close();
	if (SteamNetworkingSockets() != Listen.Sockets || !WaitFor([&]
	{
		SteamNetConnectionInfo_t Info{};
		return Sockets.GetConnectionInfo(Peer.Pending, &Info) && Info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer;
	}))
	{
		std::fputs("client-first close must keep the listener able to observe peer closure\n", stderr);
		return false;
	}
	Server.Close();
	if (SteamNetworkingSockets() || !Client.OpenClient(Runtime.AcquireClient("standalone fixture", Error), Address, {}))
	{
		std::fputs("after final close the runtime must reopen with a fresh standalone client identity\n", stderr);
		return false;
	}
	SteamNetworkingIdentity Identity{};
	const bool Standalone = Client.Get().Sockets->GetIdentity(&Identity) && Identity.GetGenericString() &&
		std::strcmp(Identity.GetGenericString(), "standalone fixture") == 0;
	Client.Close();
	return Standalone && SteamNetworkingSockets() == nullptr;
}

static std::optional<TransportEvent> NextEvent(EventRouter& Router)
{
	std::optional<TransportEvent> Event;
	const auto Drained = Router.Drain(1, [&](const TransportEvent& Next, TransportEventDelivery Delivery) noexcept
	{
		if (Delivery == TransportEventDelivery::Deliver)
		{
			Event = Next;
		}
	});
	return Drained ? Event : std::nullopt;
}

static bool CheckPump(CallbackPump& Pump, const CallbackPump::ServerRoute* Server, const CallbackPump::ClientRoute* Client)
{
	const auto Result = Pump.Poll(Server, Client);
	return Result && Result->Server == GnsEndpointEventStatus::Ok && Result->Client == GnsEndpointEventStatus::Ok;
}

static bool CheckClose(CallbackPump& Pump, const CallbackPump::ServerRoute* Server, const CallbackPump::ClientRoute* Client,
	EventRouter& Router, TransportPeer Peer, u32 Reason)
{
	std::optional<TransportEvent> Closed;
	bool Pumped = true;
	WaitFor([&]
	{
		Pumped = CheckPump(Pump, Server, Client);
		Closed = NextEvent(Router);
		return !Pumped || Closed.has_value();
	});
	return Pumped && Closed && Closed->Kind == TransportEventKind::Disconnected && Closed->Peer == Peer && Closed->Reason == Reason;
}

enum class CloseOrder
{
	ListenerFirst,
	ClientFirst,
	ServerPeerFirst,
};

static bool CheckRealEndpoints(CloseOrder Order)
{
	ProcessRuntime Runtime;
	CallbackPump Pump;
	SteamNetworkingErrMsg Error{};
	xr_array<TransportEvent, 3> ServerStorage;
	xr_array<TransportEvent, 3> ClientStorage;
	EventInbox ServerInbox(ServerStorage);
	EventInbox ClientInbox(ClientStorage);
	xr_array<TransportConnectionSlot, 1> ServerPeers;
	xr_array<TransportConnectionSlot, 1> ClientPeers;
	EventRouter ServerRouter(ServerInbox, ServerPeers);
	EventRouter ClientRouter(ClientInbox, ClientPeers);
	ReceiveFixture ServerReceive{ServerInbox};
	ReceiveFixture ClientReceive{ClientInbox};
	GnsEndpoint<ProcessRuntime::Lease> Server;
	GnsEndpoint<ProcessRuntime::Lease> Client;
	SteamNetworkingIPAddr Address{};
	const auto Callback = CallbackPump::CallbackOption();
	// GNS requires a nonzero direct-IP listen port.
	for (u16 Port = 48700; Port < 48732; ++Port)
	{
		Address.SetIPv4(0x7f000001u, Port);
		const auto Opened = Server.OpenServer(Runtime.AcquireServer(Error), Address, xr_span(&Callback, 1), RunReceive, &ServerReceive);
		if (Opened || Opened.error() != GnsEndpointError::ListenerFailed)
		{
			break;
		}
	}
	if (!Server.Get().Sockets)
	{
		std::fputs("could not start an owned GNS listener and poll group\n", stderr);
		return false;
	}

	const auto Listen = Server.Get();
	const auto Options = Pump.PrepareClient();
	if (!Options || !Listen.Sockets->GetListenSocketAddress(Listen.Listener, &Address))
	{
		std::fputs("could not prepare the hosted GNS client route\n", stderr);
		return false;
	}
	if (!Client.OpenClient(Runtime.AcquireClient("loopback", Error), Address, Options->Native, RunReceive, &ClientReceive))
	{
		std::fputs("could not connect an owned GNS client to the listener\n", stderr);
		return false;
	}

	const CallbackPump::ServerRoute ServerRoute{Server.Get(), ServerInbox};
	const CallbackPump::ClientRoute ClientRoute{Client.Get(), ClientInbox, Options->Generation};
	const TransportPeer Local{ClientRoute.Endpoint.Connection, static_cast<u64>(Options->Generation)};
	if (ClientRouter.RegisterOutbound(Local) != TransportRegistration::Tracked)
	{
		std::fputs("the loading owner must register the native outbound attempt before polling\n", stderr);
		return false;
	}
	std::optional<TransportEvent> Accepted;
	std::optional<TransportEvent> Connected;
	bool Pumped = true;
	WaitFor([&]
	{
		Pumped = CheckPump(Pump, &ServerRoute, &ClientRoute);
		if (!Accepted)
		{
			Accepted = NextEvent(ServerRouter);
		}
		if (!Connected)
		{
			Connected = NextEvent(ClientRouter);
		}
		return !Pumped || (Accepted && Connected);
	});
	if (!Pumped || !Accepted || !Connected || Accepted->Kind != TransportEventKind::Connected ||
		Connected->Kind != TransportEventKind::Connected || Connected->Peer != Local || Accepted->Peer.Generation == Local.Generation)
	{
		std::fputs("one process pump must publish both native connect events with distinct generations\n", stderr);
		return false;
	}

	const auto Remote = Accepted->Peer;
	auto& Sockets = *Listen.Sockets;
	constexpr u8 ToServer[]{0x41, 0, 0xa3};
	constexpr u8 ToClient[]{0x42, 0, 0xb4};
	if (Sockets.SendMessageToConnection(Local.Handle, ToServer, sizeof(ToServer), k_nSteamNetworkingSend_ReliableNoNagle, nullptr) != k_EResultOK ||
		Sockets.SendMessageToConnection(Remote.Handle, ToClient, sizeof(ToClient), k_nSteamNetworkingSend_ReliableNoNagle, nullptr) != k_EResultOK)
	{
		std::fputs("owned GNS endpoints could not exchange reliable messages\n", stderr);
		return false;
	}

	std::optional<TransportEvent> ServerPacket;
	std::optional<TransportEvent> ClientPacket;
	WaitFor([&]
	{
		if (!ServerPacket)
		{
			ServerPacket = NextEvent(ServerRouter);
		}
		if (!ClientPacket)
		{
			ClientPacket = NextEvent(ClientRouter);
		}
		return (ServerPacket && ClientPacket) || ServerReceive.Failed.load() || ClientReceive.Failed.load();
	});
	if (!ServerPacket || !ClientPacket || ServerPacket->Kind != TransportEventKind::Packet || ClientPacket->Kind != TransportEventKind::Packet ||
		ServerPacket->Peer != Remote || ClientPacket->Peer != Local || !std::ranges::equal(ServerPacket->GetPayload(), ToServer) ||
		!std::ranges::equal(ClientPacket->GetPayload(), ToClient))
	{
		std::fputs("both receive workers must publish owned bytes after their native connect events\n", stderr);
		return false;
	}

	if (Order == CloseOrder::ListenerFirst)
	{
		Server.Close();
		SteamNetConnectionInfo_t ClosedInfo{};
		if (SteamNetworkingSockets() != Listen.Sockets || Client.Get().Connection != Local.Handle ||
			Sockets.GetConnectionInfo(Remote.Handle, &ClosedInfo))
		{
			std::fputs("closing the listener must release accepted peers and preserve the client's lease\n", stderr);
			return false;
		}
	}
	else if (Order == CloseOrder::ClientFirst)
	{
		Client.Close();
		if (!CheckClose(Pump, &ServerRoute, nullptr, ServerRouter, Remote, k_ESteamNetConnectionEnd_App_Generic))
		{
			std::fputs("closing the client must publish the server peer's disconnection with its original generation and reason\n", stderr);
			return false;
		}
	}
	else
	{
		constexpr int Reason = k_ESteamNetConnectionEnd_App_Min + 42;
		if (!Sockets.CloseConnection(Remote.Handle, Reason, nullptr, false) ||
			!CheckClose(Pump, &ServerRoute, &ClientRoute, ClientRouter, Local, Reason))
		{
			std::fputs("closing the accepted server peer must deliver its reason to the client's inbox\n", stderr);
			return false;
		}
	}
	Client.Close();
	Server.Close();
	std::optional<TransportPeer> ServerRetired;
	std::optional<TransportPeer> ClientRetired;
	ServerRouter.ResetAfterClose([&](TransportPeer Peer) noexcept { ServerRetired = Peer; });
	ClientRouter.ResetAfterClose([&](TransportPeer Peer) noexcept { ClientRetired = Peer; });
	const std::optional<TransportPeer> ExpectedServer = Order == CloseOrder::ClientFirst ? std::nullopt : std::optional{Remote};
	const std::optional<TransportPeer> ExpectedClient = Order == CloseOrder::ServerPeerFirst ? std::nullopt : std::optional{Local};
	if (ServerRetired != ExpectedServer || ClientRetired != ExpectedClient)
	{
		std::fputs("owner cleanup must retire only the peers that did not receive a remote close\n", stderr);
		return false;
	}
	return SteamNetworkingSockets() == nullptr;
}

}

class GnsTransport
{
public:
	explicit GnsTransport(ProcessRuntime& Runtime)
	{
		SteamNetworkingErrMsg Error{};
		Lease = Runtime.AcquireClient("loopback fixture", Error);
		Sockets = Lease.Get();
		if (!Sockets)
		{
			std::fprintf(stderr, "GNS initialization failed: %s\n", Error);
		}
	}

	~GnsTransport()
	{
		Close(Peer::First);
		Close(Peer::Second);
	}

	GnsTransport(const GnsTransport&) = delete;
	GnsTransport& operator=(const GnsTransport&) = delete;

	TransportStatus Connect()
	{
		if (!Sockets || Handles[0] != k_HSteamNetConnection_Invalid || Handles[1] != k_HSteamNetConnection_Invalid)
		{
			return TransportStatus::Failed;
		}
		if (!Sockets->CreateSocketPair(&Handles[0], &Handles[1], true, nullptr, nullptr))
		{
			std::fputs("Could not create a GNS UDP loopback pair\n", stderr);
			return TransportStatus::Failed;
		}
		return TransportStatus::Success;
	}

	HSteamNetConnection GetHandle(Peer Side) const { return Handles[static_cast<std::size_t>(Side)]; }

	TransportStatus Send(Peer Sender, MessageView Message)
	{
		const auto Handle = GetHandle(Sender);
		if (Handle == k_HSteamNetConnection_Invalid)
		{
			return TransportStatus::Closed;
		}
		if (Message.size() > static_cast<std::size_t>(k_cbMaxSteamNetworkingSocketsMessageSizeSend))
		{
			return TransportStatus::Failed;
		}
		const auto Result = Sockets->SendMessageToConnection(Handle, Message.data(), static_cast<u32>(Message.size()), k_nSteamNetworkingSend_ReliableNoNagle, nullptr);
		if (Result != k_EResultOK)
		{
			std::fprintf(stderr, "GNS send failed with status %d\n", static_cast<int>(Result));
			return TransportStatus::Failed;
		}
		return TransportStatus::Success;
	}

	ReceivedMessage Receive(Peer Receiver, std::optional<bool> Reliable = true)
	{
		const auto Handle = Handles[static_cast<std::size_t>(Receiver)];
		if (Handle == k_HSteamNetConnection_Invalid)
		{
			return {TransportStatus::Closed, {}};
		}

		SteamNetworkingMessage_t* Raw = nullptr;
		const int Count = Sockets->ReceiveMessagesOnConnection(Handle, &Raw, 1);
		if (Count == 1)
		{
			const auto Release = [](SteamNetworkingMessage_t* Incoming) { Incoming->Release(); };
			const std::unique_ptr<SteamNetworkingMessage_t, decltype(Release)> Incoming(Raw, Release);
			const bool IsReliable = (Incoming->m_nFlags & k_nSteamNetworkingSend_Reliable) != 0;
			if (Reliable && IsReliable != *Reliable)
			{
				std::fputs("GNS loopback received an unexpected reliability flag\n", stderr);
				return {TransportStatus::Failed, {}};
			}
			std::vector<u8> Bytes(static_cast<std::size_t>(Incoming->m_cbSize));
			if (!Bytes.empty())
			{
				std::memcpy(Bytes.data(), Incoming->m_pData, Bytes.size());
			}
			return {TransportStatus::Success, std::move(Bytes)};
		}
		SteamNetConnectionInfo_t Info{};
		if (Count < 0 || !Sockets->GetConnectionInfo(Handle, &Info))
		{
			std::fputs("GNS receive or connection status query failed\n", stderr);
			return {TransportStatus::Failed, {}};
		}
		if (Info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer)
		{
			return {TransportStatus::Closed, {}};
		}
		if (Info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
		{
			std::fprintf(stderr, "GNS peer failed with reason %d: %s\n", Info.m_eEndReason, Info.m_szEndDebug);
			return {TransportStatus::Failed, {}};
		}
		return {TransportStatus::Empty, {}};
	}

	TransportStatus Close(Peer Side)
	{
		auto& Handle = Handles[static_cast<std::size_t>(Side)];
		if (Handle == k_HSteamNetConnection_Invalid)
		{
			return TransportStatus::Closed;
		}
		if (!Sockets->CloseConnection(Handle, 0, "loopback fixture finished", false))
		{
			std::fputs("GNS could not close owned connection\n", stderr);
			return TransportStatus::Failed;
		}
		Handle = k_HSteamNetConnection_Invalid;
		return TransportStatus::Success;
	}

private:
	std::array<HSteamNetConnection, 2> Handles{k_HSteamNetConnection_Invalid, k_HSteamNetConnection_Invalid};
	ISteamNetworkingSockets* Sockets = nullptr;
	ProcessRuntime::Lease Lease;
};

static ReceivedMessage AwaitReceive(GnsTransport& Link, Peer Receiver, std::optional<bool> Reliable = true)
{
	const auto Deadline = std::chrono::steady_clock::now() + 2s;
	do
	{
		auto Message = Link.Receive(Receiver, Reliable);
		if (Message.Status != TransportStatus::Empty)
		{
			return Message;
		}
		std::this_thread::sleep_for(1ms);
	}
	while (std::chrono::steady_clock::now() < Deadline);
	std::fprintf(stderr, "GNS receive deadline expired for peer %d\n", static_cast<int>(Receiver));
	return {TransportStatus::Empty, {}};
}

static bool CheckReliableSendPressure()
{
	using namespace Multiplayer;
	ProcessRuntime Runtime;
	GnsTransport Link(Runtime);
	if (!ExpectStatus("open a UDP loopback pair for reliable send pressure", Link.Connect(), TransportStatus::Success))
	{
		return false;
	}
	const auto Handle = Link.GetHandle(Peer::First);
	auto& Sockets = *SteamNetworkingSockets();
	auto& Utils = *SteamNetworkingUtils();
	const auto SetBufferLimit = [&](int Limit)
	{
		return Utils.SetConnectionConfigValueInt32(Handle, k_ESteamNetworkingConfig_SendBufferSize, Limit);
	};
	const auto Send = [&](xr_span<const u8> Bytes) noexcept { return SubmitGnsReliable(Sockets, Handle, Bytes); };
	constexpr u8 First[] = {0x7a, 0x00, 0x87};
	constexpr u8 Second[] = {0xc4, 0x09, 0xfe, 0x51};
	if (!SetBufferLimit(2))
	{
		std::fputs("could not set the GNS connection send buffer limit\n", stderr);
		return false;
	}
	if (Send(First) != ReliableSendAttempt::Backpressured)
	{
		std::fputs("the GNS buffer refusal must be classified as retryable pressure\n", stderr);
		return false;
	}

	xr_array<ReliableSendQueue::Slot, 2> Slots;
	ReliableSendQueue Queue(Slots, 7, 25);
	if (Queue.Enqueue(First, 100) != ReliableEnqueueResult::Queued ||
		Queue.Enqueue(Second, 101) != ReliableEnqueueResult::Queued ||
		Queue.SubmitNext(103, Send) != ReliableSendResult::Backpressured ||
		Queue.Inspect(103) != ReliableSendQueue::Status{2, 7, 3, ReliableSendFailure::None})
	{
		std::fputs("GNS pressure must leave both frames pending with their original ages\n", stderr);
		return false;
	}
	if (!SetBufferLimit(64) ||
		Queue.SubmitNext(104, Send) != ReliableSendResult::Submitted ||
		Queue.SubmitNext(105, Send) != ReliableSendResult::Submitted ||
		Queue.Inspect(105) != ReliableSendQueue::Status{0, 0, 0, ReliableSendFailure::None})
	{
		std::fputs("restoring GNS capacity must allow both frames to submit\n", stderr);
		return false;
	}
	const auto ReceivedFirst = AwaitReceive(Link, Peer::Second);
	const auto ReceivedSecond = AwaitReceive(Link, Peer::Second);
	if (ReceivedFirst.Status != TransportStatus::Success || !std::ranges::equal(ReceivedFirst.Bytes, First) ||
		ReceivedSecond.Status != TransportStatus::Success || !std::ranges::equal(ReceivedSecond.Bytes, Second))
	{
		std::fputs("the remote peer must receive the two distinct frames in order\n", stderr);
		return false;
	}

	if (Link.Close(Peer::First) != TransportStatus::Success || Queue.Enqueue(First, 106) != ReliableEnqueueResult::Queued ||
		Queue.SubmitNext(107, Send) != ReliableSendResult::CloseRequired ||
		Queue.Inspect(107) != ReliableSendQueue::Status{1, 3, 1, ReliableSendFailure::Transport})
	{
		std::fputs("a closed GNS handle must cause a terminal queue result\n", stderr);
		return false;
	}
	std::puts("GNS reliable send pressure checks passed");
	return true;
}

static bool CheckSnapshotSendPressure()
{
	using namespace Multiplayer;
	ProcessRuntime Runtime;
	GnsTransport Link(Runtime);
	if (!ExpectStatus("open a UDP loopback pair for snapshot send pressure", Link.Connect(), TransportStatus::Success))
	{
		return false;
	}
	const auto Handle = Link.GetHandle(Peer::First);
	auto& Sockets = *SteamNetworkingSockets();
	auto& Utils = *SteamNetworkingUtils();
	const auto Send = [&](xr_span<const u8> Bytes) noexcept { return SubmitGnsSnapshot(Sockets, Handle, Bytes); };
	constexpr JoinIdentity Identity{21, 4, 1};
	xr_array<Wire::CombatantSnapshot, 16> First;
	for (std::size_t Index = 0; Index < First.size(); ++Index)
	{
		Wire::HumanCombatantSnapshot Human{};
		Human.State.Participant = {static_cast<u32>(Index + 1)};
		Human.State.Body = {static_cast<u16>(Index), static_cast<u32>(Index + 1), 1};
		Human.State.BodyProfileId = 6;
		First[Index] = Human;
	}

	SnapshotSendQueue Queue(Identity, 50);
	constexpr Wire::SnapshotBatchHeader Initial{Identity, 1, 17, 23, 1000, 5};
	if (!Utils.SetConnectionConfigValueInt32(Handle, k_ESteamNetworkingConfig_SendBufferSize, 2) ||
		Queue.Stage(Initial, First, 10) != 2 || Queue.SubmitNext(11, Send) != SnapshotSendResult::Dropped ||
		Queue.Inspect(11) != SnapshotSendQueue::Status{1, 118, 1})
	{
		std::fputs("GNS capacity refusal must drop the attempted packet and preserve the unattempted tail\n", stderr);
		return false;
	}

	auto Survivor = std::get<Wire::HumanCombatantSnapshot>(First.back());
	Survivor.State.Position[0] = 3.0f;
	const Wire::CombatantSnapshot Latest = Survivor;
	constexpr Wire::SnapshotBatchHeader Replacement{Identity, 3, 18, 24, 1020, 6};
	if (Queue.Stage(Replacement, xr_span(&Latest, 1), 12) != 1 ||
		!Utils.SetConnectionConfigValueInt32(Handle, k_ESteamNetworkingConfig_SendBufferSize, 512) ||
		Queue.SubmitNext(13, Send) != SnapshotSendResult::Submitted || Queue.SubmitNext(13, Send) != SnapshotSendResult::Empty)
	{
		std::fputs("a new capture must replace the old tail and submit after capacity returns\n", stderr);
		return false;
	}
	const auto Message = AwaitReceive(Link, Peer::Second, false);
	const auto Received = Wire::CombatantSnapshotBatch::Decode(Message.Bytes);
	if (Message.Status != TransportStatus::Success || !Received || Received->GetHeader() != Replacement ||
		!std::ranges::equal(Received->GetRecords(), xr_span(&Latest, 1)))
	{
		std::fputs("the latest accepted capture must arrive as an unreliable datagram\n", stderr);
		return false;
	}

	auto Final = Replacement;
	++Final.ServerTick;
	++Final.MessageSequence;
	if (Queue.Stage(Final, xr_span(&Latest, 1), 14) != 1 || Link.Close(Peer::First) != TransportStatus::Success ||
		Queue.SubmitNext(15, Send) != SnapshotSendResult::CloseRequired)
	{
		std::fputs("a closed native connection must require retiring the queue\n", stderr);
		return false;
	}
	std::puts("GNS snapshot send pressure checks passed");
	return true;
}

static bool CheckOutgoingTrafficPressure()
{
	using namespace Multiplayer;
	ProcessRuntime Runtime;
	GnsTransport Link(Runtime);
	if (!ExpectStatus("open a UDP loopback pair for egress pacing", Link.Connect(), TransportStatus::Success))
	{
		return false;
	}
	const auto Handle = Link.GetHandle(Peer::First);
	auto& Sockets = *SteamNetworkingSockets();
	auto& Utils = *SteamNetworkingUtils();
	// This standalone test process uses lossless receive delay to hold the first bulk packet outstanding.
	if (!Utils.SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketLag_Recv, 300))
	{
		std::fputs("could not delay loopback acknowledgements\n", stderr);
		return false;
	}

	constexpr JoinIdentity Identity{4, 1, 2};
	xr_array<u8, Wire::MaxBaselineChunkBytes> Data;
	Data.fill(0x21);
	xr_array<u8, Wire::MaxMessageSize> BulkBytes;
	xr_array<u8, Wire::MaxMessageSize> NextBulk;
	const auto FirstSize = Wire::EncodeBaselineChunk({Identity, 20, 9, 0, Data}, BulkBytes);
	const auto NextSize = Wire::EncodeBaselineChunk({Identity, 22, 9, 1, Data}, NextBulk);
	if (!FirstSize || !NextSize)
	{
		std::fputs("failed to encode the baseline chunks for the pacing fixture\n", stderr);
		return false;
	}
	const auto FirstFrame = xr_span(BulkBytes).first(*FirstSize);
	const auto NextFrame = xr_span(NextBulk).first(*NextSize);
	const auto SendReliable = [&](xr_span<const u8> Bytes) noexcept { return SubmitGnsReliableFrame(Sockets, Handle, Bytes); };
	if (SendReliable(FirstFrame) != ReliableSendAttempt::Accepted || SendReliable(NextFrame) != ReliableSendAttempt::Backpressured)
	{
		std::fputs("one outstanding bulk frame must pace the next frame despite available GNS capacity\n", stderr);
		return false;
	}

	xr_array<u8, Wire::MaxMessageSize> ControlBytes;
	const auto ControlSize = Wire::EncodeCatchUpDespawn({Identity, 21, 9, 45, 1, 102, {2}, {3, 1, 1}}, ControlBytes);
	if (!ControlSize)
	{
		return false;
	}
	const auto Control = xr_span(ControlBytes).first(*ControlSize);
	constexpr u8 Snapshot[] = {0x14, 0xa9, 0x23};
	constexpr u8 Command[] = {0xdc, 0x19, 0x76};
	constexpr u8 Cosmetic[] = {0xc8, 0x88};
	xr_array<ReliableSendQueue::Slot, 1> Slots;
	ReliableSendQueue Queue(Slots, 1100, 20);
	if (Queue.Enqueue(Control, 4) != ReliableEnqueueResult::Queued || Queue.SubmitNext(5, SendReliable) != ReliableSendResult::Submitted ||
		SubmitGnsSnapshot(Sockets, Handle, Snapshot) != SnapshotSendAttempt::Accepted ||
		SubmitGnsOutgoing(Sockets, Handle, OutgoingTraffic::Command, Command) != GnsSendAttempt::Accepted ||
		SubmitGnsOutgoing(Sockets, Handle, OutgoingTraffic::Cosmetic, Cosmetic) != GnsSendAttempt::Deferred)
	{
		std::fputs("bulk must leave room for control and gameplay while cosmetics defer\n", stderr);
		return false;
	}

	bool ReceivedBulk = false;
	bool ReceivedControl = false;
	bool ReceivedSnapshot = false;
	bool ReceivedCommand = false;
	for (int Index = 0; Index < 4; ++Index)
	{
		const auto Message = AwaitReceive(Link, Peer::Second, std::nullopt);
		if (Message.Status != TransportStatus::Success)
		{
			return false;
		}
		if (!ReceivedBulk && std::ranges::equal(Message.Bytes, FirstFrame))
		{
			ReceivedBulk = true;
		}
		else if (ReceivedBulk && !ReceivedControl && std::ranges::equal(Message.Bytes, Control))
		{
			ReceivedControl = true;
		}
		else if (!ReceivedSnapshot && std::ranges::equal(Message.Bytes, Snapshot))
		{
			ReceivedSnapshot = true;
		}
		else if (!ReceivedCommand && std::ranges::equal(Message.Bytes, Command))
		{
			ReceivedCommand = true;
		}
		else
		{
			std::fputs("received unexpected traffic or reordered reliable control\n", stderr);
			return false;
		}
	}
	if (!ReceivedBulk || !ReceivedControl || !ReceivedSnapshot || !ReceivedCommand)
	{
		return false;
	}
	auto Outcome = ReliableSendAttempt::Backpressured;
	if (!EndpointTests::WaitFor([&]
		{
			Outcome = SendReliable(NextFrame);
			return Outcome != ReliableSendAttempt::Backpressured;
		}) ||
		Outcome != ReliableSendAttempt::Accepted)
	{
		std::fputs("acknowledgements must release the outstanding native bulk allowance\n", stderr);
		return false;
	}
	const auto Last = AwaitReceive(Link, Peer::Second);
	if (Last.Status != TransportStatus::Success || !std::ranges::equal(Last.Bytes, NextFrame))
	{
		std::fputs("the paced producer must deliver the resumed reliable frame once\n", stderr);
		return false;
	}

	std::puts("GNS outgoing traffic pressure checks passed");
	return true;
}
}

int main(int ArgumentCount, char** Arguments)
{
	using namespace GnsTransportTests;
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--reliable-send-pressure") == 0)
	{
		return CheckReliableSendPressure() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--snapshot-send-pressure") == 0)
	{
		return CheckSnapshotSendPressure() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount == 2 && std::strcmp(Arguments[1], "--outgoing-traffic-pressure") == 0)
	{
		return CheckOutgoingTrafficPressure() ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (ArgumentCount != 1)
	{
		std::fputs("Unexpected GNS test arguments\n", stderr);
		return EXIT_FAILURE;
	}
	bool Passed = CheckSharedRuntime();
	Passed &= CheckFailureAndRetry();
	Passed &= CheckProcessIdentity();
	{
		ProcessRuntime Runtime;
		SteamNetworkingErrMsg Error{};
		auto Lease = Runtime.AcquireServer(Error);
		Passed &= Lease.Get() && GnsCallbackPumpTests::Run(*Lease.Get());
	}
	Passed &= EndpointTests::CheckEndpointLifecycle();
	Passed &= EndpointTests::CheckPolledEndpointLifecycle();
	Passed &= EndpointTests::CheckPolledNativeEndpoints();
	Passed &= EndpointTests::CheckRealEndpoints(EndpointTests::CloseOrder::ListenerFirst);
	Passed &= EndpointTests::CheckRealEndpoints(EndpointTests::CloseOrder::ClientFirst);
	Passed &= EndpointTests::CheckRealEndpoints(EndpointTests::CloseOrder::ServerPeerFirst);
	if (!Passed)
	{
		return EXIT_FAILURE;
	}
	ProcessRuntime Runtime;
	{
		GnsTransport Link(Runtime);
		if (!ExpectStatus("open both handles for scope cleanup", Link.Connect(), TransportStatus::Success))
		{
			return EXIT_FAILURE;
		}
	}
	if (SteamNetworkingSockets() != nullptr)
	{
		std::fputs("GNS runtime survived open-pair scope cleanup\n", stderr);
		return EXIT_FAILURE;
	}

	for (int Cycle = 0; Cycle < 3; ++Cycle)
	{
		{
			SteamNetworkingErrMsg Error{};
			auto Server = Runtime.AcquireServer(Error);
			if (!Server.Get())
			{
				std::fprintf(stderr, "GNS runtime fixture could not start a server lease: %s\n", Error);
				return EXIT_FAILURE;
			}
			GnsTransport Link(Runtime);
			Server.Reset();
			if (!CheckTransportExchange(Link, [&Link](Peer Side) { return AwaitReceive(Link, Side); }))
			{
				return EXIT_FAILURE;
			}
			if (!ExpectStatus("release the closed peer's handle", Link.Close(Peer::Second), TransportStatus::Success))
			{
				return EXIT_FAILURE;
			}
		}
		if (SteamNetworkingSockets() != nullptr)
		{
			std::fputs("GNS runtime survived completed exchange\n", stderr);
			return EXIT_FAILURE;
		}
	}
	std::puts("GNS loopback exchange and close checks passed");
	return EXIT_SUCCESS;
}
