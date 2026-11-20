#pragma once

#include "TransportInbox.h"

#include <utility>

namespace Multiplayer
{
class TransportConnectionSlot
{
	template <class Lock, class Guard> friend class TransportEventRouter;

	TransportPeer Peer{};
	bool Connected = false;
};

enum class TransportRegistration
{
	Tracked,
	Stale,
	Full,
	EndpointFailed,
};

enum class TransportEventDelivery
{
	Deliver,
	RejectFull,
};

enum class TransportRouteError
{
	InboxOverflow,
	HandleConflict,
};

// One serialized game owner binds the route table to its endpoint's inbox.
// Its storage and inbox must outlive the router and remain exclusive to that endpoint.
// Consumers can post to the inbox. Router operations stay serialized and cannot reenter it.
template <class Lock, class Guard>
class TransportEventRouter
{
public:
	TransportEventRouter(TransportInbox<Lock, Guard>& Inbox, xr_span<TransportConnectionSlot> Slots) noexcept : Inbox(Inbox), Slots(Slots)
	{
		std::ranges::fill(Slots, TransportConnectionSlot{});
	}

	TransportEventRouter(const TransportEventRouter&) = delete;
	TransportEventRouter& operator=(const TransportEventRouter&) = delete;

	// Register the native attempt before pumping callbacks so an early close can reach the loading owner.
	[[nodiscard]] TransportRegistration RegisterOutbound(TransportPeer Peer) noexcept
	{
		return Track(Peer, Find(Peer.Handle), false);
	}

	template <class Sink>
		requires std::is_nothrow_invocable_v<Sink&, const TransportEvent&, TransportEventDelivery>
	[[nodiscard]] std::expected<std::size_t, TransportRouteError> Drain(std::size_t Budget, Sink&& Consume) noexcept
	{
		if (Fault)
		{
			return std::unexpected(*Fault);
		}
		for (std::size_t Index = 0; Index < Budget; ++Index)
		{
			const auto Event = Inbox.Pop();
			if (!Event)
			{
				Fault = TransportRouteError::InboxOverflow;
				return std::unexpected(*Fault);
			}
			if (!*Event)
			{
				return Index;
			}
			const auto Delivery = Route(**Event);
			if (Fault)
			{
				return std::unexpected(*Fault);
			}
			if (Delivery)
			{
				Consume(**Event, *Delivery);
			}
		}
		return Budget;
	}

	// Close and stop native producers before calling this. Keep the transport's generation sequence across reopens.
	template <class Retire>
		requires std::is_nothrow_invocable_v<Retire&, TransportPeer>
	void ResetAfterClose(Retire&& RetirePeer) noexcept
	{
		for (auto& Slot : Slots)
		{
			const auto Retired = std::exchange(Slot, TransportConnectionSlot{}).Peer;
			if (Retired.Handle != 0)
			{
				RetirePeer(Retired);
			}
		}
		Inbox.ResetAfterClose();
		Fault.reset();
	}

private:
	[[nodiscard]] TransportConnectionSlot* Find(u32 Handle) noexcept
	{
		const auto Found = std::ranges::find_if(Slots, [Handle](const TransportConnectionSlot& Slot)
		{
			return Slot.Peer.Handle == Handle;
		});
		return Found == Slots.end() ? nullptr : &*Found;
	}

	[[nodiscard]] TransportRegistration Track(TransportPeer Peer, const TransportConnectionSlot* Existing, bool Connected) noexcept
	{
		if (Fault)
		{
			return TransportRegistration::EndpointFailed;
		}
		if (Peer.Handle == 0 || Peer.Generation <= HighestGeneration)
		{
			return TransportRegistration::Stale;
		}
		// Unsolicited connects for this endpoint are published in assignment order.
		HighestGeneration = Peer.Generation;
		if (Existing)
		{
			Fault = TransportRouteError::HandleConflict;
			return TransportRegistration::EndpointFailed;
		}
		if (auto* Slot = Find(0))
		{
			Slot->Peer = Peer;
			Slot->Connected = Connected;
			return TransportRegistration::Tracked;
		}
		return TransportRegistration::Full;
	}

	[[nodiscard]] std::optional<TransportEventDelivery> Route(const TransportEvent& Event) noexcept
	{
		auto* Slot = Find(Event.Peer.Handle);
		const bool Matches = Slot && Slot->Peer == Event.Peer;
		if (Event.Kind == TransportEventKind::Connected)
		{
			if (Matches)
			{
				if (std::exchange(Slot->Connected, true))
				{
					return {};
				}
				return TransportEventDelivery::Deliver;
			}
			const auto Registration = Track(Event.Peer, Slot, true);
			if (Registration == TransportRegistration::Tracked)
			{
				return TransportEventDelivery::Deliver;
			}
			if (Registration == TransportRegistration::Full)
			{
				return TransportEventDelivery::RejectFull;
			}
			return {};
		}

		if (!Matches || (Event.Kind == TransportEventKind::Packet && !Slot->Connected))
		{
			return {};
		}
		if (Event.Kind == TransportEventKind::Disconnected)
		{
			*Slot = {};
		}
		return TransportEventDelivery::Deliver;
	}

	TransportInbox<Lock, Guard>& Inbox;
	const xr_span<TransportConnectionSlot> Slots;
	u64 HighestGeneration = 0;
	std::optional<TransportRouteError> Fault;
};
}
