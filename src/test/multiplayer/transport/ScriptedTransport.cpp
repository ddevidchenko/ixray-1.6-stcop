#include "ScriptedTransport.h"

#include <algorithm>
#include <utility>

namespace Multiplayer::Test
{
ScriptedTransport::ScriptedTransport(u64 Rate) : BytesPerMillisecond(Rate)
{
}

bool ScriptedTransport::QueueDelivery(Peer Sender, std::initializer_list<Milliseconds> Delays)
{
	if (std::ranges::any_of(Delays, [](Milliseconds Delay) { return Delay < Milliseconds::zero(); }))
	{
		return false;
	}
	Directions[static_cast<std::size_t>(Sender)].Plans.emplace_back(Delays);
	return true;
}

bool ScriptedTransport::AdvanceTo(Milliseconds Time)
{
	if (Time < Now)
	{
		return false;
	}
	Now = Time;
	return true;
}

void ScriptedTransport::FailNext(Fault Operation)
{
	Failures[static_cast<std::size_t>(Operation)] = true;
}

bool ScriptedTransport::ConsumeFailure(Fault Operation)
{
	return std::exchange(Failures[static_cast<std::size_t>(Operation)], false);
}

TransportStatus ScriptedTransport::Connect()
{
	if (Connected || ConsumeFailure(Fault::Connect))
	{
		return TransportStatus::Failed;
	}
	Connected = true;
	return TransportStatus::Success;
}

TransportStatus ScriptedTransport::Send(Peer Sender, MessageView Message)
{
	if (!Connected)
	{
		return TransportStatus::Closed;
	}
	if (ConsumeFailure(Fault::Send))
	{
		return TransportStatus::Failed;
	}

	Direction& Link = Directions[static_cast<std::size_t>(Sender)];
	const Milliseconds TransferStart = std::max(Now, Link.BusyUntil);
	const u64 Size = Message.size();
	u64 TransferDuration = 0;
	if (BytesPerMillisecond != 0)
	{
		TransferDuration = Size / BytesPerMillisecond + (Size % BytesPerMillisecond != 0);
	}
	if (TransferDuration > static_cast<u64>((Milliseconds::max() - TransferStart).count()))
	{
		return TransportStatus::Failed;
	}

	const Milliseconds TransferEnd = TransferStart + Milliseconds(TransferDuration);
	constexpr Milliseconds Immediate[] = {Milliseconds::zero()};
	const std::span<const Milliseconds> Delays = Link.Plans.empty() ? std::span<const Milliseconds>(Immediate) : Link.Plans.front();
	const Milliseconds DelayLimit = Milliseconds::max() - TransferEnd;
	if (std::ranges::any_of(Delays, [DelayLimit](Milliseconds Delay) { return Delay > DelayLimit; }))
	{
		return TransportStatus::Failed;
	}

	// All copies share a transfer. Even a dropped message occupies the link for that transfer.
	for (const Milliseconds Delay : Delays)
	{
		Link.Pending.emplace(TransferEnd + Delay, std::vector<u8>(Message.begin(), Message.end()));
	}
	if (!Link.Plans.empty())
	{
		Link.Plans.pop_front();
	}
	Link.BusyUntil = TransferEnd;
	return TransportStatus::Success;
}

ReceivedMessage ScriptedTransport::Receive(Peer Receiver)
{
	if (!Connected)
	{
		return {TransportStatus::Closed, {}};
	}
	if (ConsumeFailure(Fault::Receive))
	{
		return {TransportStatus::Failed, {}};
	}

	auto& Pending = Directions[static_cast<std::size_t>(OtherPeer(Receiver))].Pending;
	if (Pending.empty() || Pending.begin()->first > Now)
	{
		return {TransportStatus::Empty, {}};
	}
	auto Next = Pending.begin();
	ReceivedMessage Result{TransportStatus::Success, std::move(Next->second)};
	Pending.erase(Next);
	return Result;
}

TransportStatus ScriptedTransport::Close(Peer)
{
	if (!Connected)
	{
		return TransportStatus::Closed;
	}
	if (ConsumeFailure(Fault::Close))
	{
		return TransportStatus::Failed;
	}
	for (Direction& Link : Directions)
	{
		Link.Pending.clear();
		Link.Plans.clear();
		Link.BusyUntil = Now;
	}
	Connected = false;
	return TransportStatus::Success;
}
}
