#pragma once

#include <steam/steamnetworkingsockets.h>

namespace Multiplayer
{
// GNS invokes a bare function pointer synchronously inside RunCallbacks.
class GnsCallbackDispatch
{
public:
	using ReceiveStatus = void (*)(void*, const SteamNetConnectionStatusChangedCallback_t&) noexcept;
	using RunCallbacks = void (*)(ISteamNetworkingSockets&) noexcept;

	[[nodiscard]] static SteamNetworkingConfigValue_t CallbackOption() noexcept
	{
		SteamNetworkingConfigValue_t Option{};
		Option.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, reinterpret_cast<void*>(&Forward));
		return Option;
	}

	[[nodiscard]] static bool IsPolling() noexcept { return g_Active != nullptr; }

	static void Poll(ISteamNetworkingSockets& Sockets, void* Context, ReceiveStatus Receive,
		RunCallbacks Run = [](ISteamNetworkingSockets& Sockets) noexcept { Sockets.RunCallbacks(); }) noexcept
	{
		if (IsPolling())
		{
			return;
		}
		const Sink Route{Context, Receive};
		g_Active = &Route;
		Run(Sockets);
		g_Active = nullptr;
	}

private:
	struct Sink
	{
		void* Context;
		ReceiveStatus Receive;
	};

	static void Forward(SteamNetConnectionStatusChangedCallback_t* Event) noexcept
	{
		if (g_Active && Event && Event->m_hConn != k_HSteamNetConnection_Invalid)
		{
			g_Active->Receive(g_Active->Context, *Event);
		}
	}

	inline static thread_local const Sink* g_Active = nullptr;
};

[[nodiscard]] inline bool IsGnsPeerClosed(ESteamNetworkingConnectionState State) noexcept
{
	return State == k_ESteamNetworkingConnectionState_ClosedByPeer || State == k_ESteamNetworkingConnectionState_ProblemDetectedLocally;
}
}
