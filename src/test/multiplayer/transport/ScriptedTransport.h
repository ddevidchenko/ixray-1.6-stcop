#pragma once

#include "TransportFixture.h"

#include <array>
#include <chrono>
#include <deque>
#include <initializer_list>
#include <map>

namespace Multiplayer::Test
{
enum class Fault
{
	Connect,
	Send,
	Receive,
	Close,
};

class ScriptedTransport
{
public:
	using Milliseconds = std::chrono::milliseconds;

	explicit ScriptedTransport(u64 BytesPerMillisecond = 0);
	// Each plan applies to one successful send. An empty plan drops the message.
	bool QueueDelivery(Peer Sender, std::initializer_list<Milliseconds> Delays);
	bool AdvanceTo(Milliseconds Time);
	void FailNext(Fault Operation);

	TransportStatus Connect();
	TransportStatus Send(Peer Sender, MessageView Message);
	ReceivedMessage Receive(Peer Receiver);
	TransportStatus Close(Peer Side);

private:
	struct Direction
	{
		Milliseconds BusyUntil{0};
		std::deque<std::vector<Milliseconds>> Plans;
		std::multimap<Milliseconds, std::vector<u8>> Pending;
	};

	bool ConsumeFailure(Fault Operation);

	const u64 BytesPerMillisecond;
	std::array<Direction, 2> Directions;
	std::array<bool, static_cast<std::size_t>(Fault::Close) + 1> Failures{};
	Milliseconds Now{0};
	bool Connected = false;
};
}
