#pragma once

#include "../../xrCore/_stl_extensions_nonalloc.h"

#include <steam/steamnetworkingsockets.h>

#include <expected>
#include <limits>
#include <system_error>
#include <thread>
#include <utility>

namespace Multiplayer
{
struct GnsEndpointView
{
	ISteamNetworkingSockets* Sockets = nullptr;
	HSteamListenSocket Listener = k_HSteamListenSocket_Invalid;
	HSteamNetPollGroup PollGroup = k_HSteamNetPollGroup_Invalid;
	HSteamNetConnection Connection = k_HSteamNetConnection_Invalid;
};

using GnsEndpointRun = void (*)(std::stop_token, GnsEndpointView, void*) noexcept;

struct GnsEndpointApi
{
	HSteamListenSocket (*Listen)(ISteamNetworkingSockets&, const SteamNetworkingIPAddr&, xr_span<const SteamNetworkingConfigValue_t>) =
		[](ISteamNetworkingSockets& Sockets, const SteamNetworkingIPAddr& Address, xr_span<const SteamNetworkingConfigValue_t> Options)
	{
		return Sockets.CreateListenSocketIP(Address, static_cast<int>(Options.size()), Options.data());
	};

	HSteamNetPollGroup (*CreatePollGroup)(ISteamNetworkingSockets&) = [](ISteamNetworkingSockets& Sockets)
	{
		return Sockets.CreatePollGroup();
	};

	HSteamNetConnection (*Connect)(ISteamNetworkingSockets&, const SteamNetworkingIPAddr&, xr_span<const SteamNetworkingConfigValue_t>) =
		[](ISteamNetworkingSockets& Sockets, const SteamNetworkingIPAddr& Address, xr_span<const SteamNetworkingConfigValue_t> Options)
	{
		return Sockets.ConnectByIPAddress(Address, static_cast<int>(Options.size()), Options.data());
	};

	void (*CloseListener)(ISteamNetworkingSockets&, HSteamListenSocket) = [](ISteamNetworkingSockets& Sockets, HSteamListenSocket Listener)
	{
		Sockets.CloseListenSocket(Listener);
	};

	void (*DestroyPollGroup)(ISteamNetworkingSockets&, HSteamNetPollGroup) = [](ISteamNetworkingSockets& Sockets, HSteamNetPollGroup Group)
	{
		Sockets.DestroyPollGroup(Group);
	};

	void (*CloseConnection)(ISteamNetworkingSockets&, HSteamNetConnection) = [](ISteamNetworkingSockets& Sockets, HSteamNetConnection Connection)
	{
		Sockets.CloseConnection(Connection, 0, nullptr, false);
	};

	std::jthread (*StartWorker)(GnsEndpointRun, GnsEndpointView, void*) = [](GnsEndpointRun Run, GnsEndpointView View, void* Context)
	{
		return std::jthread(Run, View, Context);
	};
};

enum class GnsEndpointError
{
	AlreadyOpen,
	RuntimeUnavailable,
	InvalidOptions,
	ListenerFailed,
	PollGroupFailed,
	ConnectionFailed,
	WorkerFailed,
};

// The endpoint owner serializes open and close. The worker borrows its view until it returns on stop.
template <class RuntimeLease>
class GnsEndpoint
{
public:
	explicit GnsEndpoint(GnsEndpointApi Api = {}) noexcept : Api(Api) {}
	~GnsEndpoint() { Close(); }
	GnsEndpoint(const GnsEndpoint&) = delete;
	GnsEndpoint& operator=(const GnsEndpoint&) = delete;

	[[nodiscard]] std::expected<void, GnsEndpointError> OpenServer(RuntimeLease Acquired, const SteamNetworkingIPAddr& Address,
		xr_span<const SteamNetworkingConfigValue_t> Options, GnsEndpointRun Run, void* Context) noexcept
	{
		return Open(std::move(Acquired), Address, Options, Run, Context, true);
	}

	[[nodiscard]] std::expected<void, GnsEndpointError> OpenClient(RuntimeLease Acquired, const SteamNetworkingIPAddr& Address,
		xr_span<const SteamNetworkingConfigValue_t> Options, GnsEndpointRun Run, void* Context) noexcept
	{
		return Open(std::move(Acquired), Address, Options, Run, Context, false);
	}

	void Close() noexcept
	{
		if (Worker.joinable())
		{
			Worker.request_stop();
			Worker.join();
		}
		if (auto* Sockets = Handles.Sockets)
		{
			if (Handles.Connection != k_HSteamNetConnection_Invalid)
			{
				Api.CloseConnection(*Sockets, Handles.Connection);
			}
			if (Handles.PollGroup != k_HSteamNetPollGroup_Invalid)
			{
				Api.DestroyPollGroup(*Sockets, Handles.PollGroup);
			}
			if (Handles.Listener != k_HSteamListenSocket_Invalid)
			{
				Api.CloseListener(*Sockets, Handles.Listener);
			}
		}
		Handles = {};
		Lease.Reset();
	}

	[[nodiscard]] GnsEndpointView Get() const noexcept { return Handles; }

private:
	[[nodiscard]] std::expected<void, GnsEndpointError> Open(RuntimeLease Acquired, const SteamNetworkingIPAddr& Address,
		xr_span<const SteamNetworkingConfigValue_t> Options, GnsEndpointRun Run, void* Context, bool Server) noexcept
	{
		if (Handles.Sockets)
		{
			return std::unexpected(GnsEndpointError::AlreadyOpen);
		}
		auto* Sockets = Acquired.Get();
		if (!Sockets)
		{
			return std::unexpected(GnsEndpointError::RuntimeUnavailable);
		}
		if (Options.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
		{
			return std::unexpected(GnsEndpointError::InvalidOptions);
		}
		if (!Run)
		{
			return std::unexpected(GnsEndpointError::WorkerFailed);
		}

		Lease = std::move(Acquired);
		Handles.Sockets = Sockets;
		if (Server)
		{
			Handles.Listener = Api.Listen(*Sockets, Address, Options);
			if (Handles.Listener == k_HSteamListenSocket_Invalid)
			{
				return Fail(GnsEndpointError::ListenerFailed);
			}
		}
		else
		{
			Handles.Connection = Api.Connect(*Sockets, Address, Options);
			if (Handles.Connection == k_HSteamNetConnection_Invalid)
			{
				return Fail(GnsEndpointError::ConnectionFailed);
			}
		}

		Handles.PollGroup = Api.CreatePollGroup(*Sockets);
		if (Handles.PollGroup == k_HSteamNetPollGroup_Invalid)
		{
			return Fail(GnsEndpointError::PollGroupFailed);
		}

		try
		{
			Worker = Api.StartWorker(Run, Handles, Context);
		}
		catch (const std::system_error&)
		{
			return Fail(GnsEndpointError::WorkerFailed);
		}
		if (!Worker.joinable())
		{
			return Fail(GnsEndpointError::WorkerFailed);
		}
		return {};
	}

	[[nodiscard]] std::expected<void, GnsEndpointError> Fail(GnsEndpointError Error) noexcept
	{
		Close();
		return std::unexpected(Error);
	}

	const GnsEndpointApi Api;
	RuntimeLease Lease;
	GnsEndpointView Handles;
	std::jthread Worker;
};
}
