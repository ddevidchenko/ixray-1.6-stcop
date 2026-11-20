#include "../transport/TransportScenario.h"

#include "../../../xrNetServer/Multiplayer/GnsEndpoint.h"
#include "../../../xrNetServer/Multiplayer/GnsRuntime.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <system_error>
#include <thread>
#include <utility>

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

	g_Resources.Failure = Attempt::None;
	if (!Endpoint.OpenClient(Runtime.AcquireClient("fixture", Error), Address, {}, RunWorker, nullptr))
	{
		std::fputs("endpoint must allow retry after creation failures\n", stderr);
		return false;
	}
	if (Endpoint.OpenClient(Runtime.AcquireClient("fixture", Error), Address, {}, RunWorker, nullptr) !=
		std::unexpected(GnsEndpointError::AlreadyOpen) || g_Resources.Connections != 1 || !Endpoint.Get().Sockets)
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
		g_Resources.Connections != 0 || Endpoint.Get().Sockets || SteamNetworkingSockets() != nullptr)
	{
		std::fputs("closing an active endpoint must join the worker before closing its connection\n", stderr);
		return false;
	}
	return true;
}
struct AcceptedPeer
{
	GnsEndpointView Server;
	HSteamNetConnection Connection = k_HSteamNetConnection_Invalid;
	bool Failed = false;
};

static thread_local AcceptedPeer* g_AcceptedPeer = nullptr;

static void AcceptPeer(SteamNetConnectionStatusChangedCallback_t* Event)
{
	auto* Target = g_AcceptedPeer;
	if (!Target || Event->m_info.m_hListenSocket != Target->Server.Listener ||
		Event->m_info.m_eState != k_ESteamNetworkingConnectionState_Connecting)
	{
		return;
	}
	auto& Sockets = *Target->Server.Sockets;
	if (Sockets.AcceptConnection(Event->m_hConn) != k_EResultOK ||
		!Sockets.SetConnectionPollGroup(Event->m_hConn, Target->Server.PollGroup))
	{
		Target->Failed = true;
		return;
	}
	Target->Connection = Event->m_hConn;
}

static void RunUntilStopped(std::stop_token Stop, GnsEndpointView, void*) noexcept
{
	while (!Stop.stop_requested())
	{
		std::this_thread::sleep_for(1ms);
	}
}

static bool CheckRealEndpoints()
{
	ProcessRuntime Runtime;
	SteamNetworkingErrMsg Error{};
	GnsEndpoint<ProcessRuntime::Lease> Server;
	GnsEndpoint<ProcessRuntime::Lease> Client;
	SteamNetworkingIPAddr Address{};
	SteamNetworkingConfigValue_t Callback;
	Callback.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, reinterpret_cast<void*>(&AcceptPeer));
	// GNS requires a nonzero direct-IP listen port.
	for (u16 Port = 48700; Port < 48732; ++Port)
	{
		Address.SetIPv4(0x7f000001u, Port);
		const auto Opened = Server.OpenServer(Runtime.AcquireServer(Error), Address, std::span(&Callback, 1), RunUntilStopped, nullptr);
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
	if (!Listen.Sockets->GetListenSocketAddress(Listen.Listener, &Address) ||
		!Client.OpenClient(Runtime.AcquireClient("loopback", Error), Address, {}, RunUntilStopped, nullptr))
	{
		std::fputs("could not connect an owned GNS client to the listener\n", stderr);
		return false;
	}
	AcceptedPeer Peer{Listen};
	const auto Connection = Client.Get().Connection;
	const auto Deadline = std::chrono::steady_clock::now() + 2s;
	bool Connected = false;
	do
	{
		g_AcceptedPeer = &Peer;
		Listen.Sockets->RunCallbacks();
		g_AcceptedPeer = nullptr;
		SteamNetConnectionInfo_t Info{};
		Connected = Peer.Connection != k_HSteamNetConnection_Invalid && Listen.Sockets->GetConnectionInfo(Connection, &Info) &&
			Info.m_eState == k_ESteamNetworkingConnectionState_Connected;
		if (Connected || Peer.Failed)
		{
			break;
		}
		std::this_thread::sleep_for(1ms);
	}
	while (std::chrono::steady_clock::now() < Deadline);
	constexpr u8 Message[]{0x41, 0, 0xa3};
	if (!Connected || Listen.Sockets->SendMessageToConnection(Connection, Message, sizeof(Message),
			k_nSteamNetworkingSend_ReliableNoNagle, nullptr) != k_EResultOK)
	{
		std::fputs("owned GNS endpoints could not exchange a reliable message\n", stderr);
		return false;
	}

	SteamNetworkingMessage_t* Raw = nullptr;
	const auto ReceiveDeadline = std::chrono::steady_clock::now() + 2s;
	while (Listen.Sockets->ReceiveMessagesOnPollGroup(Listen.PollGroup, &Raw, 1) == 0 &&
		std::chrono::steady_clock::now() < ReceiveDeadline)
	{
		std::this_thread::sleep_for(1ms);
	}
	const auto Release = [](SteamNetworkingMessage_t* Packet) { Packet->Release(); };
	std::unique_ptr<SteamNetworkingMessage_t, decltype(Release)> Packet(Raw, Release);
	if (!Packet || Packet->m_conn != Peer.Connection || Packet->m_cbSize != sizeof(Message) ||
		std::memcmp(Packet->m_pData, Message, sizeof(Message)) != 0)
	{
		std::fputs("owned poll group did not receive the exact reliable message\n", stderr);
		return false;
	}

	Packet.reset();
	Server.Close();
	SteamNetConnectionInfo_t ClosedInfo{};
	if (SteamNetworkingSockets() != Listen.Sockets || Client.Get().Connection != Connection ||
		Listen.Sockets->GetConnectionInfo(Peer.Connection, &ClosedInfo))
	{
		std::fputs("closing the listener releases accepted peers and preserves the client's runtime lease\n", stderr);
		return false;
	}
	Client.Close();
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

	TransportStatus Send(Peer Sender, MessageView Message)
	{
		const auto Handle = Handles[static_cast<std::size_t>(Sender)];
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

	ReceivedMessage Receive(Peer Receiver)
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

static ReceivedMessage AwaitReceive(GnsTransport& Link, Peer Receiver)
{
	const auto Deadline = std::chrono::steady_clock::now() + 2s;
	do
	{
		auto Message = Link.Receive(Receiver);
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
}

int main()
{
	using namespace GnsTransportTests;
	bool Passed = CheckSharedRuntime();
	Passed &= CheckFailureAndRetry();
	Passed &= CheckProcessIdentity();
	Passed &= EndpointTests::CheckEndpointLifecycle();
	Passed &= EndpointTests::CheckRealEndpoints();
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
