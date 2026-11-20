#pragma once

#include "TransportFixture.h"

#include <algorithm>
#include <cstdio>

namespace Multiplayer::Test
{
inline bool ExpectStatus(const char* Step, TransportStatus Actual, TransportStatus Expected)
{
	if (Actual == Expected)
	{
		return true;
	}
	std::fprintf(stderr, "%s: expected transport status %d, got %d\n", Step, static_cast<int>(Expected), static_cast<int>(Actual));
	return false;
}

inline bool ExpectMessage(const char* Step, const ReceivedMessage& Message, TransportStatus Status, MessageView Bytes = {})
{
	const bool StatusMatches = ExpectStatus(Step, Message.Status, Status);
	if (!std::ranges::equal(Message.Bytes, Bytes))
	{
		std::fprintf(stderr, "%s: payload differs (expected %zu bytes, received %zu)\n", Step, Bytes.size(), Message.Bytes.size());
		return false;
	}
	return StatusMatches;
}

// The two fixtures share this reliable binary exchange and close contract.
template <class Transport, class Await>
bool CheckTransportExchange(Transport& Link, Await&& AwaitReceive)
{
	constexpr u8 First[] = {0x00, 0x11, 0xff};
	constexpr u8 Second[] = {0x22, 0x00};
	constexpr u8 Reply[] = {0xaa, 0x80, 0x01};
	if (!ExpectStatus("connect peers", Link.Connect(), TransportStatus::Success))
	{
		return false;
	}

	bool Passed = ExpectStatus("send first payload", Link.Send(Peer::First, First), TransportStatus::Success);
	Passed &= ExpectStatus("send second payload", Link.Send(Peer::First, Second), TransportStatus::Success);
	Passed &= ExpectStatus("send reverse payload", Link.Send(Peer::Second, Reply), TransportStatus::Success);
	if (Passed)
	{
		Passed &= ExpectMessage("receive first payload", AwaitReceive(Peer::Second), TransportStatus::Success, First);
		Passed &= ExpectMessage("receive second payload", AwaitReceive(Peer::Second), TransportStatus::Success, Second);
		Passed &= ExpectMessage("receive reverse payload", AwaitReceive(Peer::First), TransportStatus::Success, Reply);
		Passed &= ExpectMessage("peer inbox drained", Link.Receive(Peer::Second), TransportStatus::Empty);
	}

	if (!ExpectStatus("close first peer", Link.Close(Peer::First), TransportStatus::Success))
	{
		return false;
	}
	Passed &= ExpectMessage("second peer observes close", AwaitReceive(Peer::Second), TransportStatus::Closed);
	Passed &= ExpectStatus("send on locally closed peer", Link.Send(Peer::First, First), TransportStatus::Closed);
	return Passed;
}
}
