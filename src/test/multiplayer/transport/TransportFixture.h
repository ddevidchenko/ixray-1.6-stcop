#pragma once

#include "../../../xrCore/ScalarTypes.h"

#include <span>
#include <vector>

namespace Multiplayer::Test
{
enum class Peer
{
	First,
	Second,
};

constexpr Peer OtherPeer(Peer Side)
{
	return Side == Peer::First ? Peer::Second : Peer::First;
}

enum class TransportStatus
{
	Success,
	Empty,
	Closed,
	Failed,
};

using MessageView = std::span<const u8>;

struct ReceivedMessage
{
	TransportStatus Status;
	std::vector<u8> Bytes;
};
}
