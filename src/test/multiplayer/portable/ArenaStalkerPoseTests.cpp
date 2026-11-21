#include "../../../xrGame/Multiplayer/ArenaStalkerPose.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace ArenaStalkerPoseTests
{
using namespace Multiplayer::ArenaStalkerPose;
using namespace Multiplayer::ArenaLadders;

bool Expect(bool Passed, const char* Description)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Description);
	}
	return Passed;
}

static bool Phases()
{
	const auto AllClips = [](xr_string_view) { return true; };
	const auto UpOnly = [](xr_string_view Name) { return Name == "cl_run_fwd_0"; };
	const auto Empty = [](xr_string_view) { return false; };
	auto Body = Measure(Phase::Mount, Direction::Up, 0.2f, 0.4f);
	bool Ok = Expect(Body.Action == Stage::MountUp && SelectClip(Body, AllClips).Name == "cl_idle_1",
		"Mount uses the stock ladder pose while feet approach the face.");
	Body = Measure(Phase::Climb, Direction::Up, 0.2f, 4.f);
	const auto Up = SelectClip(Body, AllClips);
	Ok &= Expect(Up.Name == "cl_run_fwd_0" && Up.Frame > 0.f && Up.Frame < 1.f &&
		Measure(Phase::Climb, Direction::Up, 0.2f, 4.f) == Body,
		"An ascending pose is sampled from actual travel and holds while the body stalls.");
	auto Down = Measure(Phase::Climb, Direction::Down, 0.2f, 4.f);
	const auto Reverse = SelectClip(Down, UpOnly);
	Ok &= Expect(SelectClip(Down, AllClips).Name == "cl_run_back_0" && Reverse.Name == Up.Name &&
		std::abs(Reverse.Frame + Up.Frame - 1.f) < 0.01f,
		"Absent descending animation falls back to reversed ascending samples.");
	Ok &= Expect(Measure(Phase::Mount, Direction::Down, -0.1f, 0.4f) == Pose{Stage::MountDown, 0} &&
		Measure(Phase::Mount, Direction::Down, 1.f, 0.4f) == Pose{Stage::MountDown, 65535},
		"Early and overshot mount samples are clamped to the transition.");
	Ok &= Expect(Measure(Phase::Dismount, Direction::Up, 0.f, 0.f) == Pose{Stage::ExitUp, 0} &&
		Measure(Phase::Climb, Direction::Up, 0.f, 4.f) == Pose{Stage::Up, 0},
		"Each phase starts at its own origin, including a coincident exit.");
	Body = Measure(Phase::Dismount, Direction::Down, 0.2f, 0.4f);
	Ok &= Expect(Body.Action == Stage::ExitDown && SelectClip(Body, AllClips).Name == "cr_idle_0_out" &&
		std::abs(SelectClip(Body, AllClips).Frame - 0.5f) < 0.01f,
		"Dismount transition is driven by physical progress toward the exit.");
	Body = Measure(Phase::Settle, Direction::Down, 0.f, 0.f);
	Ok &= Expect(Body.Action == Stage::SettleDown && SelectClip(Body, AllClips).Frame == 1.f &&
		SelectClip(Body, Empty).Name.empty(), "Settling holds the final pose with a safe missing-motion result.");
	Ok &= Expect(SampleTime(Body, SelectClip(Body, AllClips), 1.f, 0.25f) == 0.75f,
		"An exit held at full progress samples the last key, before native cyclic interpolation returns to the first key.");
	Ok &= Expect(SampleTime(Down, Reverse, 1.f, 0.25f) == Reverse.Frame,
		"Moving cycles sample the full duration including the space between the last and first keys.");
	for (const auto Terminal : {Phase::Failed, Phase::Complete, Phase::Idle})
	{
		Ok &= Expect(Measure(Terminal, Direction::Up, 2.f, 4.f) == Pose{},
			"Terminal and canceled traversals clear the pose.");
	}
	return Ok;
}

static bool Network()
{
	const Snapshot State{23, 950, 1.5f, 4.25f, -7.f, 2.8f, {Stage::Down, 377}};
	const auto Bytes = Encode(State);
	const auto Value = Decode(Bytes);
	bool Ok = Expect(Bytes.size() == WireSize && Value && Value->Life == 23 && Value->Tick == 950 &&
		Value->X == State.X && Value->Y == State.Y && Value->Z == State.Z &&
		std::abs(Value->Yaw - State.Yaw) < 0.001f && Value->Ladder == State.Ladder,
		"The periodic and spawn poses use the same fixed-size record.");
	const Wire Expected{1, 6, 0x79, 0x01, 23, 0, 0, 0, 0xb6, 0x03, 0, 0,
		0, 0, 0xc0, 0x3f, 0, 0, 0x88, 0x40, 0, 0, 0xe0, 0xc0, 0x33, 0x33, 0x33, 0x40};
	Ok &= Expect(Bytes == Expected, "The snapshot has stable little-endian wire bytes.");
	xr_array<u8, WireSize + 1> Overlong{};
	std::copy(Bytes.begin(), Bytes.end(), Overlong.begin());
	Ok &= Expect(!Decode(xr_span<const u8>(Bytes).first(Bytes.size() - 1)) && !Decode(Overlong),
		"Truncated and overlong pose records are discarded.");
	auto Invalid = Bytes;
	Invalid[0] = 0xfe;
	Ok &= Expect(!Decode(Invalid), "Unknown pose protocol versions are discarded.");
	Invalid = Bytes;
	Invalid[1] = 0xff;
	Ok &= Expect(!Decode(Invalid), "Unknown climb states are discarded.");
	auto Wrong = State;
	Wrong.Y = std::numeric_limits<f32>::infinity();
	Ok &= Expect(!Decode(Encode(Wrong)), "Nonfinite replicated coordinates are discarded.");

	Wrong = State;
	Wrong.Yaw = 4.f;
	Ok &= Expect(!Decode(Encode(Wrong)), "Facing outside its canonical range is discarded.");
	Wrong = State;
	Wrong.Ladder.Action = Stage::Ground;
	Ok &= Expect(!Decode(Encode(Wrong)), "A grounded snapshot cannot carry climb progress.");
	Wrong.Ladder = {Stage::SettleUp, 10};
	Ok &= Expect(!Decode(Encode(Wrong)), "A settling snapshot must hold the last exit frame.");

	Replica Client;
	Wrong = State;
	Wrong.Life = 0;
	Ok &= Expect(!Client.Start(Wrong) && !Client.Bound(), "A missing spawn identity cannot bind the stream.");
	Ok &= Expect(!Client.Accept(State), "Periodic state cannot attach to an unbound object.");
	Ok &= Expect(Client.Start(State) && Client.Sample(950) && Client.Sample(950)->Ladder == State.Ladder,
		"A late join starts with the authoritative phase sample.");
	auto Newer = State;
	Newer.Tick = 1050;
	Newer.Y = 5.25f;
	Newer.Yaw = -2.8f;
	Newer.Ladder.Progress = 529;
	Ok &= Expect(Client.Accept(Newer) && !Client.Accept(State) && !Client.Accept(Newer),
		"Only newer samples of the current spawn are accepted.");
	const auto Mid = Client.Sample(1000);
	Ok &= Expect(Mid && std::abs(Mid->Y - 4.75f) < 0.01f && std::abs(Mid->Yaw) > 3.f &&
		Mid->Ladder.Progress == 453, "Rendering interpolates movement, short-arc facing and unwrapped climb progress.");
	auto OtherLife = Newer;
	OtherLife.Life++;
	OtherLife.Tick++;
	Ok &= Expect(!Client.Accept(OtherLife), "A snapshot for a different life cannot attach.");
	auto Clear = Newer;
	Clear.Tick = 1100;
	Clear.Ladder = {};
	Ok &= Expect(Client.Accept(Clear) && Client.Sample(1099) && Client.Sample(1099)->Ladder.Action == Stage::Down &&
		Client.Sample(1100) && Client.Sample(1100)->Ladder.Action == Stage::Ground,
		"Terminal pose switches at the same sample boundary as presentation.");
	Ok &= Expect(!Client.Sample(3000), "A replica with stale data releases the climb animation.");
	Client.Clear();
	Ok &= Expect(!Client.Accept(Newer) && !Client.Sample(1100) && Client.Start(OtherLife) && !Client.Accept(Clear),
		"Death, reuse and map teardown require a fresh spawn binding.");
	auto Input = State;
	Ok &= Expect(Client.Start(Input), "A new replica can be bound after clearing the previous life.");
	for (u32 Offset = 1; Offset <= 20; ++Offset)
	{
		Input.Tick = State.Tick + Offset * 10;
		Input.Y = f32(Offset);
		Ok &= Expect(Client.Accept(Input), "The history accepts newer samples after its buffer fills.");
	}
	const auto Buffered = Client.Sample(State.Tick + 175);
	Ok &= Expect(Buffered && Buffered->Y == 17.5f && Client.Sample(State.Tick)->Y == 5.f,
		"The replica keeps enough history to interpolate older samples after rollover of its buffer.");
	auto BeforeWrap = State;
	BeforeWrap.Tick = u32(-4);
	auto AfterWrap = Newer;
	AfterWrap.Tick = 5;
	Ok &= Expect(Client.Start(BeforeWrap) && Client.Accept(AfterWrap) && Client.Sample(0),
		"Serial clock rollover preserves ordered snapshots.");
	return Ok;
}

bool Run()
{
	bool Ok = Phases();
	Ok &= Network();
	if (Ok)
	{
		std::puts("Arena stalker pose checks passed");
	}
	return Ok;
}
}
