#include "BaselineSend.h"
#include "BaselineStaging.h"

#include <algorithm>
#include <cstdio>

namespace BaselineSendTests
{
using namespace Multiplayer;
using namespace Multiplayer::Wire;
using enum BaselineSendResult;

constexpr JoinIdentity Receiver{0x1457, 0, 2};
constexpr BaselineManifest OneChunk{33, 96, 43, 4, 1, 1};

static bool Expect(const char* Step, bool Passed) noexcept
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

static bool CheckOrderAndRetry()
{
	constexpr u8 Source[] = {0x21, 0x01, 0, 0xfe};
	xr_array<ReliableSendQueue::Slot, 1> Slots;
	ReliableSendQueue Control(Slots, 32, 10);
	BaselineSend Transfer(Receiver, OneChunk, Source, 20);
	u32 Sequence = 0xffffffffu;
	int Attempts = 0;
	bool Passed = true;
	const auto Send = [&](xr_span<const u8> Datagram) noexcept
	{
		switch (Attempts++)
		{
			case 0:
			{
				const auto Begin = DecodeBaselineBegin(Datagram);
				Passed &= Expect("begin has the offered peer identity, sequence and manifest",
					Begin && Begin->Identity == Receiver && Begin->MessageSequence == 0xffffffffu && Begin->Manifest == OneChunk);
				return ReliableSendAttempt::Accepted;
			}
			case 1:
			case 2:
			{
				const auto Chunk = DecodeBaselineChunk(Datagram);
				Passed &= Expect("each attempt offers the same chunk and uses the available sequence",
					Chunk && Chunk->Identity == Receiver && Chunk->BaselineId == OneChunk.BaselineId &&
					Chunk->ChunkIndex == 0 && Chunk->MessageSequence == (Attempts == 2 ? 0u : 1u) &&
					std::ranges::equal(Chunk->Data, Source));
				return Attempts == 2 ? ReliableSendAttempt::Backpressured : ReliableSendAttempt::Accepted;
			}
			default:
			{
				const auto End = DecodeBaselineEnd(Datagram);
				Passed &= Expect("end follows the accepted chunk",
					End && End->Identity == Receiver && End->BaselineId == OneChunk.BaselineId && End->MessageSequence == 2);
				return ReliableSendAttempt::Accepted;
			}
		}
	};

	Passed &= Expect("first owner turn submits begin and advances the wrapping sequence",
		Transfer.SubmitNext(Control, Sequence, 1, Send) == Submitted && Attempts == 1 && Sequence == 0);
	Passed &= Expect("a refused chunk keeps its transfer cursor and consumes no sequence",
		Transfer.SubmitNext(Control, Sequence, 2, Send) == Deferred && Attempts == 2 && Sequence == 0);

	constexpr u8 EarlierControl[] = {0xab};
	const auto SubmitControl = [&](xr_span<const u8> Datagram) noexcept
	{
		Passed &= Expect("earlier control can progress after bulk refusal", std::ranges::equal(Datagram, EarlierControl));
		return ReliableSendAttempt::Accepted;
	};
	Passed &= Expect("pending control prevents any bulk attempt",
		Control.Enqueue(EarlierControl, 3) == ReliableEnqueueResult::Queued &&
		Transfer.SubmitNext(Control, Sequence, 3, Send) == Deferred && Attempts == 2);
	Passed &= Expect("draining earlier control frees the next turn", Control.SubmitNext(4, SubmitControl) == ReliableSendResult::Submitted);
	++Sequence;
	Passed &= Expect("the chunk can retry after other traffic takes its former sequence",
		Transfer.SubmitNext(Control, Sequence, 4, Send) == Submitted && Attempts == 3 && Sequence == 2 && !Transfer.IsComplete());
	Passed &= Expect("the exact End submission completes the transfer",
		Transfer.SubmitNext(Control, Sequence, 5, Send) == Submitted && Attempts == 4 && Sequence == 3 && Transfer.IsComplete());
	Passed &= Expect("completion remains stable with no more native attempts",
		Transfer.SubmitNext(Control, Sequence, 20, Send) == Complete && Attempts == 4 && Sequence == 3);
	return Passed;
}

static bool CheckReceiverHandoff()
{
	xr_array<u8, 1025> Source;
	for (std::size_t Index = 0; Index < Source.size(); ++Index)
	{
		Source[Index] = static_cast<u8>(Index % 251);
	}
	constexpr BaselineManifest Manifest{2, 10, 3, 1025, 12, 2};
	xr_array<u8, 1028> Storage;
	Storage.fill(0xc1);
	BaselineStagingBudget Budget(Storage.size(), 1);
	auto Reservation = Budget.TryReserve(Storage.size());
	if (!Expect("reserve the complete receiver staging space", Reservation.has_value()))
	{
		return false;
	}
	BaselineStaging Staging(Receiver, Storage, std::move(*Reservation));
	ReliableSendQueue Control({}, 0, 100);
	BaselineSend Transfer(Receiver, Manifest, Source, 200);
	u32 Sequence = 7;
	std::optional<StagedBaseline> Finished;
	bool Passed = true;
	const auto Send = [&](xr_span<const u8> Datagram) noexcept
	{
		const auto Envelope = DecodeEnvelope(Datagram);
		if (!Envelope || Envelope->Header.MessageSequence != Sequence)
		{
			return ReliableSendAttempt::Failed;
		}
		switch (Envelope->Header.Type)
		{
			case MessageType::BaselineBegin:
			{
				const auto Begin = DecodeBaselineBegin(Datagram);
				Passed &= Expect("start the bounded receiver with the declared manifest",
					Begin && Begin->Manifest == Manifest && Staging.Begin(*Begin).has_value());
				break;
			}
			case MessageType::BaselineChunk:
			{
				const auto Chunk = DecodeBaselineChunk(Datagram);
				Passed &= Expect("full chunk and tail have independent canonical offsets",
					Chunk && Chunk->ChunkIndex == Sequence - 8 && Chunk->Data.size() == (Sequence == 8 ? 1024 : 1) &&
					Staging.AddChunk(*Chunk).has_value());
				break;
			}
			case MessageType::BaselineEnd:
			{
				const auto End = DecodeBaselineEnd(Datagram);
				const auto Baseline = End ? Staging.Finish(*End) : std::unexpected(BaselineStagingError::InvalidState);
				Passed &= Expect("the receiver only finishes after the final submitted chunk", Baseline.has_value());
				if (Baseline)
				{
					Finished = *Baseline;
				}
				break;
			}
			default:
				return ReliableSendAttempt::Failed;
		}
		return ReliableSendAttempt::Accepted;
	};
	for (u64 Turn = 0; Turn < 4; ++Turn)
	{
		Passed &= Expect("each turn submits one frame", Transfer.SubmitNext(Control, Sequence, Turn, Send) == Submitted);
	}
	Passed &= Expect("the receiver owns the complete bytes without touching spare storage", Transfer.IsComplete() && Sequence == 11 &&
		Finished && std::ranges::equal(Finished->Data, Source) && Storage[1025] == 0xc1);
	return Passed;
}

static bool CheckEmptyAndFailures()
{
	ReliableSendQueue Idle({}, 0, 50);
	constexpr BaselineManifest Empty{0, 0, 0, 0, 0, 0};
	BaselineSend EmptyTransfer(Receiver, Empty, {}, 9);
	constexpr u8 Bytes[] = {7, 1, 0, 6};
	u32 Sequence = 100;
	int Attempts = 0;
	bool Passed = true;
	const auto Send = [&](xr_span<const u8> Frame) noexcept
	{
		const auto Envelope = DecodeEnvelope(Frame);
		const auto Expected = Attempts++ == 0 ? MessageType::BaselineBegin : MessageType::BaselineEnd;
		Passed &= Expect("an empty capture offers Begin and End with no chunk",
			Envelope && Envelope->Header.Type == Expected);
		return ReliableSendAttempt::Accepted;
	};
	Passed &= Expect("empty baseline has two wire frames", EmptyTransfer.SubmitNext(Idle, Sequence, 7, Send) == Submitted &&
		EmptyTransfer.SubmitNext(Idle, Sequence, 8, Send) == Submitted && EmptyTransfer.IsComplete() && Sequence == 102 && Attempts == 2);

	auto Invalid = OneChunk;
	++Invalid.ChunkCount;
	BaselineSend InvalidManifest(Receiver, Invalid, Bytes, 100);
	BaselineSend InvalidSize(Receiver, OneChunk, xr_span(Bytes).first(3), 100);
	BaselineSend Unbound({}, Empty, {}, 100);
	Passed &= Expect("invalid manifests, lengths and identities make no transport attempt",
		InvalidManifest.SubmitNext(Idle, Sequence, 1, Send) == InvalidSource &&
		InvalidSize.SubmitNext(Idle, Sequence, 1, Send) == InvalidSource &&
		Unbound.SubmitNext(Idle, Sequence, 1, Send) == InvalidSource && Attempts == 2 && Sequence == 102);

	xr_array<ReliableSendQueue::Slot, 1> Slots;
	ReliableSendQueue Control(Slots, 10, 30);
	BaselineSend Expired(Receiver, OneChunk, Bytes, 6);
	Passed &= Expect("pending control defers before the deadline and the exact deadline terminates the transfer",
		Control.Enqueue(Bytes, 1) == ReliableEnqueueResult::Queued && Expired.SubmitNext(Control, Sequence, 5, Send) == Deferred &&
		Expired.SubmitNext(Control, Sequence, 6, Send) == TimedOut && Expired.SubmitNext(Idle, Sequence, 7, Send) == TimedOut &&
		Attempts == 2 && Sequence == 102);

	BaselineSend Failed(Receiver, OneChunk, Bytes, 10);
	const auto Fail = [](xr_span<const u8>) noexcept { return ReliableSendAttempt::Failed; };
	Passed &= Expect("transport failure permanently stops the transfer without spending a sequence",
		Failed.SubmitNext(Idle, Sequence, 3, Fail) == TransportFailed && Failed.SubmitNext(Idle, Sequence, 4, Send) == TransportFailed &&
		Attempts == 2 && Sequence == 102 && !Failed.IsComplete());

	BaselineSend Stopped(Receiver, OneChunk, Bytes, 10);
	Passed &= Expect("a terminal control queue stops bulk without a native attempt",
		Control.Enqueue(Bytes, 5) == ReliableEnqueueResult::CloseRequired &&
		Stopped.SubmitNext(Control, Sequence, 5, Send) == ControlFailed &&
		Stopped.SubmitNext(Idle, Sequence, 6, Send) == ControlFailed && Attempts == 2 && Sequence == 102);
	return Passed;
}

bool Run()
{
	bool Passed = CheckOrderAndRetry();
	Passed &= CheckReceiverHandoff();
	Passed &= CheckEmptyAndFailures();
	if (Passed)
	{
		std::puts("Baseline send checks passed");
	}
	return Passed;
}
}
