#include "../transport/ScriptedTransport.h"
#include "../transport/TransportScenario.h"

#include <cstdio>

namespace ScriptedTransportTests
{
using namespace Multiplayer::Test;
using namespace std::chrono_literals;

constexpr u8 First[] = {0x11, 0x00, 0xff};
constexpr u8 Second[] = {0x22, 0x00, 0x80};
constexpr u8 Third[] = {0x33};

static bool CheckDeadlines()
{
	ScriptedTransport Link;
	bool Passed = ExpectStatus("connect", Link.Connect(), TransportStatus::Success);
	Passed &= Link.QueueDelivery(Peer::First, {10ms});
	Passed &= Link.QueueDelivery(Peer::First, {5ms});
	Passed &= ExpectStatus("queue first send", Link.Send(Peer::First, First), TransportStatus::Success);
	Passed &= ExpectStatus("queue second send", Link.Send(Peer::First, Second), TransportStatus::Success);

	Passed &= Link.AdvanceTo(4ms);
	Passed &= ExpectMessage("before either deadline", Link.Receive(Peer::Second), TransportStatus::Empty);
	Passed &= Link.AdvanceTo(5ms);
	Passed &= ExpectMessage("second send overtakes first at its deadline", Link.Receive(Peer::Second), TransportStatus::Success, Second);
	Passed &= ExpectMessage("first send is still pending", Link.Receive(Peer::Second), TransportStatus::Empty);
	Passed &= Link.AdvanceTo(10ms);
	Passed &= ExpectMessage("first send arrives at its deadline", Link.Receive(Peer::Second), TransportStatus::Success, First);
	Passed &= ExpectMessage("first send delivered once", Link.Receive(Peer::Second), TransportStatus::Empty);
	return Passed;
}

static bool CheckLossAndCopies()
{
	ScriptedTransport Link;
	bool Passed = ExpectStatus("connect loss and copies fixture", Link.Connect(), TransportStatus::Success);
	Passed &= Link.AdvanceTo(100ms);
	Passed &= Link.QueueDelivery(Peer::First, {});
	Passed &= Link.QueueDelivery(Peer::First, {7ms, 2ms});
	Passed &= Link.QueueDelivery(Peer::First, {2ms});
	Passed &= ExpectStatus("drop has a successful send", Link.Send(Peer::First, First), TransportStatus::Success);

	u8 Mutable[] = {0x22, 0x00, 0x80};
	Passed &= ExpectStatus("schedule two copies", Link.Send(Peer::First, Mutable), TransportStatus::Success);
	Mutable[0] = 0x99;
	Passed &= ExpectStatus("schedule another send at same time", Link.Send(Peer::First, Third), TransportStatus::Success);
	Passed &= ExpectStatus("send reverse traffic without script", Link.Send(Peer::Second, First), TransportStatus::Success);
	Passed &= ExpectMessage("reverse direction has its own delivery script", Link.Receive(Peer::First), TransportStatus::Success, First);

	Passed &= Link.AdvanceTo(101ms);
	Passed &= ExpectMessage("no copy before relative delay", Link.Receive(Peer::Second), TransportStatus::Empty);
	Passed &= Link.AdvanceTo(102ms);
	Passed &= ExpectMessage("first copy owns its bytes", Link.Receive(Peer::Second), TransportStatus::Success, Second);
	Passed &= ExpectMessage("equal timestamps preserve enqueue order", Link.Receive(Peer::Second), TransportStatus::Success, Third);
	Passed &= ExpectMessage("late copy remains queued", Link.Receive(Peer::Second), TransportStatus::Empty);
	Passed &= Link.AdvanceTo(107ms);
	Passed &= ExpectMessage("late duplicate arrives", Link.Receive(Peer::Second), TransportStatus::Success, Second);
	Passed &= ExpectMessage("no drop or extra duplicate arrives", Link.Receive(Peer::Second), TransportStatus::Empty);
	return Passed;
}

static bool CheckBandwidth()
{
	ScriptedTransport Link(2);
	bool Passed = ExpectStatus("connect rate-limited peers", Link.Connect(), TransportStatus::Success);
	Passed &= Link.QueueDelivery(Peer::First, {});
	Passed &= Link.QueueDelivery(Peer::First, {3ms});
	Passed &= ExpectStatus("serialize dropped three-byte message", Link.Send(Peer::First, First), TransportStatus::Success);
	Passed &= ExpectStatus("serialize delayed three-byte message", Link.Send(Peer::First, Second), TransportStatus::Success);
	Passed &= ExpectStatus("serialize next one-byte message", Link.Send(Peer::First, Third), TransportStatus::Success);
	Passed &= ExpectStatus("serialize reverse one-byte message", Link.Send(Peer::Second, Third), TransportStatus::Success);
	Passed &= ExpectMessage("reverse message takes one millisecond", Link.Receive(Peer::First), TransportStatus::Empty);
	Passed &= Link.AdvanceTo(1ms);
	Passed &= ExpectMessage("reverse capacity is independent", Link.Receive(Peer::First), TransportStatus::Success, Third);
	Passed &= Link.AdvanceTo(4ms);
	Passed &= ExpectMessage("a drop still consumes capacity and send costs round up", Link.Receive(Peer::Second), TransportStatus::Empty);
	Passed &= Link.AdvanceTo(5ms);
	Passed &= ExpectMessage("next message arrives before earlier delayed send", Link.Receive(Peer::Second), TransportStatus::Success, Third);
	Passed &= Link.AdvanceTo(7ms);
	Passed &= ExpectMessage("propagation delay starts after serialization", Link.Receive(Peer::Second), TransportStatus::Success, Second);
	Passed &= ExpectMessage("rate-limited loss stays dropped", Link.Receive(Peer::Second), TransportStatus::Empty);

	Passed &= Link.AdvanceTo(100ms);
	Passed &= ExpectStatus("send after idle", Link.Send(Peer::First, First), TransportStatus::Success);
	Passed &= Link.AdvanceTo(101ms);
	Passed &= ExpectMessage("idle time does not bypass serialization", Link.Receive(Peer::Second), TransportStatus::Empty);
	Passed &= Link.AdvanceTo(102ms);
	Passed &= ExpectMessage("bandwidth resumes from send time", Link.Receive(Peer::Second), TransportStatus::Success, First);
	return Passed;
}

static bool CheckFailures()
{
	ScriptedTransport Link;
	Link.FailNext(Fault::Connect);
	bool Passed = ExpectStatus("inject connect failure", Link.Connect(), TransportStatus::Failed);
	Passed &= ExpectStatus("connect failure leaves link closed", Link.Send(Peer::First, First), TransportStatus::Closed);
	Passed &= ExpectStatus("retry connect", Link.Connect(), TransportStatus::Success);
	Passed &= Link.QueueDelivery(Peer::First, {6ms});
	Link.FailNext(Fault::Send);
	Passed &= ExpectStatus("inject send failure", Link.Send(Peer::First, First), TransportStatus::Failed);
	Passed &= ExpectStatus("retry send with same delivery plan", Link.Send(Peer::First, Second), TransportStatus::Success);
	Passed &= ExpectStatus("double connect preserves the open link", Link.Connect(), TransportStatus::Failed);
	Passed &= Link.AdvanceTo(5ms);
	Passed &= ExpectMessage("failed send did not consume plan", Link.Receive(Peer::Second), TransportStatus::Empty);
	Passed &= Link.AdvanceTo(6ms);
	Link.FailNext(Fault::Receive);
	Passed &= ExpectMessage("inject receive failure", Link.Receive(Peer::Second), TransportStatus::Failed);
	Passed &= ExpectMessage("failed receive leaves message pending", Link.Receive(Peer::Second), TransportStatus::Success, Second);
	Passed &= ExpectMessage("failed send did not queue payload", Link.Receive(Peer::Second), TransportStatus::Empty);

	Link.FailNext(Fault::Close);
	Passed &= ExpectStatus("inject close failure", Link.Close(Peer::First), TransportStatus::Failed);
	Passed &= ExpectStatus("link remains usable after close failure", Link.Send(Peer::First, First), TransportStatus::Success);
	Passed &= Link.QueueDelivery(Peer::First, {99ms});
	Passed &= ExpectStatus("retry close", Link.Close(Peer::First), TransportStatus::Success);
	Passed &= ExpectMessage("remote observes close", Link.Receive(Peer::Second), TransportStatus::Closed);
	Passed &= ExpectStatus("already closed", Link.Close(Peer::First), TransportStatus::Closed);
	Passed &= ExpectStatus("reuse fixture after close", Link.Connect(), TransportStatus::Success);
	Passed &= ExpectMessage("closed link discards pending payloads", Link.Receive(Peer::Second), TransportStatus::Empty);
	Passed &= ExpectStatus("new connection sends normally", Link.Send(Peer::First, Third), TransportStatus::Success);
	Passed &= ExpectMessage("closed link discards unused plans", Link.Receive(Peer::Second), TransportStatus::Success, Third);
	return Passed;
}

static bool CheckTimeValidation()
{
	ScriptedTransport Link(1);
	bool Passed = ExpectStatus("connect timestamp fixture", Link.Connect(), TransportStatus::Success);
	const bool AcceptedNegativeTime = Link.AdvanceTo(-1ms);
	const bool AcceptedNegativeDelay = Link.QueueDelivery(Peer::First, {-1ms});
	if (AcceptedNegativeTime || AcceptedNegativeDelay)
	{
		std::fputs("Accepted negative time or delay\n", stderr);
		Passed = false;
	}
	const auto Latest = ScriptedTransport::Milliseconds::max();
	Passed &= Link.AdvanceTo(Latest - 1ms);
	if (Link.AdvanceTo(Latest - 2ms))
	{
		std::fputs("Allowed virtual time to move backward\n", stderr);
		Passed = false;
	}
	Passed &= Link.QueueDelivery(Peer::First, {0ms});
	Passed &= ExpectStatus("overflow does not publish message", Link.Send(Peer::First, First), TransportStatus::Failed);
	Passed &= ExpectStatus("one byte still fits at the time limit", Link.Send(Peer::First, Third), TransportStatus::Success);
	Passed &= ExpectMessage("time limit message is not yet ready", Link.Receive(Peer::Second), TransportStatus::Empty);
	Passed &= Link.AdvanceTo(Latest);
	Passed &= ExpectMessage("time limit message arrives exactly once", Link.Receive(Peer::Second), TransportStatus::Success, Third);
	Passed &= ExpectMessage("no overflowed message arrives", Link.Receive(Peer::Second), TransportStatus::Empty);

	ScriptedTransport Copies;
	Passed &= ExpectStatus("connect overflow fixture", Copies.Connect(), TransportStatus::Success);
	Passed &= Copies.AdvanceTo(Latest - 1ms);
	Passed &= Copies.QueueDelivery(Peer::First, {0ms, 2ms});
	Passed &= ExpectStatus("one overflowing copy fails the send", Copies.Send(Peer::First, First), TransportStatus::Failed);
	Passed &= ExpectMessage("overflow leaves no partially scheduled copies", Copies.Receive(Peer::Second), TransportStatus::Empty);
	return Passed;
}

bool Run()
{
	bool Passed = CheckDeadlines();
	Passed &= CheckLossAndCopies();
	Passed &= CheckBandwidth();
	Passed &= CheckFailures();
	Passed &= CheckTimeValidation();
	ScriptedTransport Link;
	Passed &= CheckTransportExchange(Link, [&Link](Peer Receiver) { return Link.Receive(Receiver); });
	if (Passed)
	{
		std::puts("Scripted transport checks passed");
	}
	return Passed;
}
}
