#pragma once

#include "WireEnvelope.h"
#include "../../xrCore/_stl_extensions_nonalloc.h"

#include <algorithm>
#include <expected>
#include <optional>
#include <type_traits>

namespace Multiplayer
{
struct TransportPeer
{
	u32 Handle = 0;
	u64 Generation = 0;

	bool operator==(const TransportPeer&) const = default;
};

enum class TransportEventKind
{
	Connected,
	Packet,
	Disconnected,
};

struct TransportEvent
{
	TransportEventKind Kind = TransportEventKind::Connected;
	TransportPeer Peer;
	u32 Reason = 0;

	[[nodiscard]] xr_span<const u8> GetPayload() const noexcept { return xr_span(Bytes).first(Size); }

private:
	template <class Lock, class Guard> friend class TransportInbox;

	xr_array<u8, Wire::MaxMessageSize> Bytes{};
	std::size_t Size = 0;
};

enum class TransportInboxError
{
	InvalidPeer,
	PacketTooLarge,
	Overflow,
};

// Producers supply a transport-owned generation. The owner keeps Storage exclusive and drains outside the queue lock.
template <class Lock, class Guard>
class TransportInbox
{
public:
	explicit TransportInbox(xr_span<TransportEvent> Storage) noexcept : Storage(Storage) {}
	TransportInbox(const TransportInbox&) = delete;
	TransportInbox& operator=(const TransportInbox&) = delete;

	[[nodiscard]] std::expected<void, TransportInboxError> PushConnected(TransportPeer Peer) noexcept
	{
		return Push(TransportEventKind::Connected, Peer, 0, {});
	}

	[[nodiscard]] std::expected<void, TransportInboxError> PushPacket(TransportPeer Peer, xr_span<const u8> Bytes) noexcept
	{
		return Push(TransportEventKind::Packet, Peer, 0, Bytes);
	}

	[[nodiscard]] std::expected<void, TransportInboxError> PushDisconnected(TransportPeer Peer, u32 Reason) noexcept
	{
		return Push(TransportEventKind::Disconnected, Peer, Reason, {});
	}

	[[nodiscard]] std::expected<std::optional<TransportEvent>, TransportInboxError> Pop() noexcept
	{
		Guard Locked(&Section);
		if (Overflowed)
		{
			return std::unexpected(TransportInboxError::Overflow);
		}
		if (Count == 0)
		{
			return std::nullopt;
		}

		TransportEvent Event = Storage[Head];
		Head = (Head + 1) % Storage.size();
		--Count;
		return Event;
	}

	template <class Consumer>
		requires std::is_nothrow_invocable_v<Consumer&, const TransportEvent&>
	[[nodiscard]] std::expected<std::size_t, TransportInboxError> Drain(std::size_t Budget, Consumer&& Consume) noexcept
	{
		for (std::size_t Index = 0; Index < Budget; ++Index)
		{
			const auto Event = Pop();
			if (!Event)
			{
				return std::unexpected(Event.error());
			}
			if (!*Event)
			{
				return Index;
			}
			Consume(**Event);
		}
		return Budget;
	}

	// The owner must close the endpoint and finish its peer cleanup before reusing the inbox.
	void ResetAfterClose() noexcept
	{
		Guard Locked(&Section);
		Head = 0;
		Count = 0;
		Overflowed = false;
	}

private:
	[[nodiscard]] std::expected<void, TransportInboxError> Push(TransportEventKind Kind, TransportPeer Peer, u32 Reason,
		xr_span<const u8> Bytes) noexcept
	{
		Guard Locked(&Section);
		if (Overflowed)
		{
			return std::unexpected(TransportInboxError::Overflow);
		}
		if (Peer.Handle == 0 || Peer.Generation == 0)
		{
			return std::unexpected(TransportInboxError::InvalidPeer);
		}
		if (Bytes.size() > Wire::MaxMessageSize)
		{
			return std::unexpected(TransportInboxError::PacketTooLarge);
		}
		if (Count == Storage.size())
		{
			Overflowed = true;
			Head = 0;
			Count = 0;
			return std::unexpected(TransportInboxError::Overflow);
		}

		auto& Event = Storage[(Head + Count) % Storage.size()];
		Event.Kind = Kind;
		Event.Peer = Peer;
		Event.Reason = Reason;
		std::ranges::copy(Bytes, Event.Bytes.begin());
		Event.Size = Bytes.size();
		++Count;
		return {};
	}

	Lock Section;
	const xr_span<TransportEvent> Storage;
	std::size_t Head = 0;
	std::size_t Count = 0;
	bool Overflowed = false;
};
}
