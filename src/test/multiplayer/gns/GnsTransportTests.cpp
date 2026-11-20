#include "../transport/TransportScenario.h"

#include <steam/steamnetworkingsockets.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <utility>

namespace GnsTransportTests
{
using namespace Multiplayer::Test;
using namespace std::chrono_literals;

class GnsTransport
{
public:
	GnsTransport()
	{
		SteamNetworkingErrMsg Error{};
		Initialized = GameNetworkingSockets_Init(nullptr, Error);
		if (Initialized)
		{
			Sockets = SteamNetworkingSockets();
		}
		else
		{
			std::fprintf(stderr, "GNS initialization failed: %s\n", Error);
		}
	}

	~GnsTransport()
	{
		Close(Peer::First);
		Close(Peer::Second);
		if (Initialized)
		{
			GameNetworkingSockets_Kill();
		}
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
	bool Initialized = false;
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
	{
		GnsTransport Link;
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
			GnsTransport Link;
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
