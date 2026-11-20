#pragma once

#include <steam/steamnetworkingsockets.h>

#include <cstddef>
#include <cstdio>
#include <utility>

namespace Multiplayer
{
struct GnsRuntimeApi
{
	decltype(&GameNetworkingSockets_Init) Start = GameNetworkingSockets_Init;
	decltype(&SteamNetworkingSockets) GetSockets = SteamNetworkingSockets;
	decltype(&GameNetworkingSockets_Kill) Stop = GameNetworkingSockets_Kill;
};

// The process owner outlives its endpoint leases. The guard serializes GNS start and shutdown.
template <class Lock, class Guard>
class GnsRuntime
{
public:
	class Lease
	{
	public:
		Lease() = default;
		~Lease() { Reset(); }
		Lease(const Lease&) = delete;
		Lease& operator=(const Lease&) = delete;
		Lease(Lease&& Other) noexcept;
		Lease& operator=(Lease&& Other) noexcept;

		[[nodiscard]] ISteamNetworkingSockets* Get() const noexcept { return Sockets; }
		void Reset() noexcept;

	private:
		friend class GnsRuntime;
		Lease(GnsRuntime& Owner, ISteamNetworkingSockets* Sockets) noexcept : Owner(&Owner), Sockets(Sockets) {}

		GnsRuntime* Owner = nullptr;
		ISteamNetworkingSockets* Sockets = nullptr;
	};

	explicit GnsRuntime(GnsRuntimeApi Api = {}) noexcept : Api(Api) {}
	GnsRuntime(const GnsRuntime&) = delete;
	GnsRuntime& operator=(const GnsRuntime&) = delete;

	[[nodiscard]] Lease AcquireServer(SteamNetworkingErrMsg& Error) noexcept
	{
		Guard Locked(&Section);
		SteamNetworkingIdentity Requested{};
		Requested.SetLocalHost();
		return Acquire(Requested, Error);
	}

	[[nodiscard]] Lease AcquireClient(const char* LegacyPassword, SteamNetworkingErrMsg& Error) noexcept
	{
		Guard Locked(&Section);
		if (Sockets && Identity.IsLocalHost())
		{
			return Acquire(Identity, Error);
		}

		SteamNetworkingIdentity Requested{};
		if (!LegacyPassword || !Requested.SetGenericString(LegacyPassword))
		{
			std::snprintf(Error, sizeof(Error), "Invalid legacy GNS client identity");
			return {};
		}
		return Acquire(Requested, Error);
	}

private:
	[[nodiscard]] Lease Acquire(const SteamNetworkingIdentity& Requested, SteamNetworkingErrMsg& Error) noexcept
	{
		Error[0] = '\0';
		if (Sockets)
		{
			if (Identity != Requested)
			{
				std::snprintf(Error, sizeof(Error), "GNS is already leased with a different process identity");
				return {};
			}
		}
		else
		{
			if (!Api.Start(&Requested, Error))
			{
				return {};
			}
			Sockets = Api.GetSockets();
			if (!Sockets)
			{
				Api.Stop();
				std::snprintf(Error, sizeof(Error), "GNS initialized without a sockets interface");
				return {};
			}
			Identity = Requested;
		}

		++Leases;
		return Lease(*this, Sockets);
	}

	void Release() noexcept
	{
		Guard Locked(&Section);
		if (--Leases == 0)
		{
			Api.Stop();
			Sockets = nullptr;
			Identity.Clear();
		}
	}

	Lock Section;
	const GnsRuntimeApi Api;
	SteamNetworkingIdentity Identity{};
	ISteamNetworkingSockets* Sockets = nullptr;
	std::size_t Leases = 0;
};

template <class Lock, class Guard>
GnsRuntime<Lock, Guard>::Lease::Lease(Lease&& Other) noexcept
	: Owner(std::exchange(Other.Owner, nullptr)), Sockets(std::exchange(Other.Sockets, nullptr))
{
}

template <class Lock, class Guard>
typename GnsRuntime<Lock, Guard>::Lease& GnsRuntime<Lock, Guard>::Lease::operator=(Lease&& Other) noexcept
{
	if (this != &Other)
	{
		Reset();
		Owner = std::exchange(Other.Owner, nullptr);
		Sockets = std::exchange(Other.Sockets, nullptr);
	}
	return *this;
}

template <class Lock, class Guard>
void GnsRuntime<Lock, Guard>::Lease::Reset() noexcept
{
	if (auto* Runtime = std::exchange(Owner, nullptr))
	{
		Sockets = nullptr;
		Runtime->Release();
	}
}
}
