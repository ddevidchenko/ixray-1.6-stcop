#include "BaselineStaging.h"

#include <array>
#include <cstdio>
#include <limits>
#include <optional>
#include <utility>

namespace BaselineStagingBudgetTests
{
using namespace Multiplayer;

constexpr JoinIdentity Identity{0x8877665544332211ull, 37, 11};
constexpr u32 Id = 0x12345678u;
constexpr Wire::BaselineBegin Declaration{Identity, 9, {Id, 10, 3, 3, 1, 1}};
constexpr Wire::BaselineEnd End{Identity, 11, Id};
constexpr u8 Data[] = {0x33, 0x00, 0xfe};

static bool Expect(const char* Step, bool Passed)
{
	if (!Passed)
	{
		std::fprintf(stderr, "%s\n", Step);
	}
	return Passed;
}

static bool ExpectError(const char* Step, const std::expected<BaselineStagingReservation, BaselineStagingBudgetError>& Result, BaselineStagingBudgetError Error)
{
	if (!Result && Result.error() == Error)
	{
		return true;
	}
	std::fprintf(stderr, "%s: expected %d, got %d\n", Step, static_cast<int>(Error), Result ? -1 : static_cast<int>(Result.error()));
	return false;
}

static bool CheckByteBudget()
{
	BaselineStagingBudget Budget(10, 4);
	const auto First = Budget.TryReserve(6);
	bool Passed = true;
	{
		const auto Second = Budget.TryReserve(4);
		Passed &= Expect("different buffers can reach the exact shared byte capacity", First && Second);
		for (int Attempt = 0; Attempt < 8; ++Attempt)
		{
			if (!ExpectError("a further byte is rejected while both buffers are held", Budget.TryReserve(1), BaselineStagingBudgetError::ByteLimit))
			{
				Passed = false;
				break;
			}
		}
		BaselineStagingBudget Independent(1, 1);
		Passed &= Expect("different owners have independent budgets", Independent.TryReserve(1).has_value());
	}
	Passed &= ExpectError("releasing one reservation restores only its byte count", Budget.TryReserve(5), BaselineStagingBudgetError::ByteLimit);
	const auto Reused = Budget.TryReserve(4);
	Passed &= Expect("rejected attempts left the released bytes available", Reused.has_value());
	return Passed;
}

static bool CheckBufferBudget()
{
	BaselineStagingBudget Budget(10, 2);
	const auto First = Budget.TryReserve(1);
	bool Passed = true;
	{
		const auto Empty = Budget.TryReserve(0);
		Passed &= Expect("a zero-byte baseline still reserves one buffer", First && Empty);
		Passed &= ExpectError("buffer count can be full with byte capacity left", Budget.TryReserve(1), BaselineStagingBudgetError::BufferLimit);
	}
	const auto Reused = Budget.TryReserve(1);
	Passed &= Expect("abandoning a reservation returns its buffer count", Reused.has_value());
	Passed &= ExpectError("only one buffer became available", Budget.TryReserve(1), BaselineStagingBudgetError::BufferLimit);

	BaselineStagingBudget Disabled(10, 0);
	Passed &= ExpectError("zero buffer capacity rejects every reservation", Disabled.TryReserve(0), BaselineStagingBudgetError::BufferLimit);
	BaselineStagingBudget EmptyOnly(0, 1);
	Passed &= ExpectError("zero byte capacity rejects nonempty buffers", EmptyOnly.TryReserve(1), BaselineStagingBudgetError::ByteLimit);
	Passed &= Expect("zero byte capacity permits an empty buffer within the count limit", EmptyOnly.TryReserve(0).has_value());
	return Passed;
}

static bool CheckPerBaselineLimit()
{
	BaselineStagingBudget Budget(std::numeric_limits<std::size_t>::max(), 1);
	bool Passed = ExpectError("one baseline cannot reserve over four MiB", Budget.TryReserve(4194305), BaselineStagingBudgetError::BaselineTooLarge);
	Passed &= ExpectError("a huge request is rejected before byte arithmetic", Budget.TryReserve(std::numeric_limits<std::size_t>::max()),
		BaselineStagingBudgetError::BaselineTooLarge);
	Passed &= Expect("per-baseline maximum still fits after rejected requests", Budget.TryReserve(4194304).has_value());
	return Passed;
}

static bool CheckMove()
{
	BaselineStagingBudget Budget(3, 2);
	std::optional<BaselineStagingReservation> Retained;
	{
		auto Original = Budget.TryReserve(3);
		if (!Expect("move fixture reserves its buffer", Original.has_value()))
		{
			return false;
		}
		Retained.emplace(std::move(*Original));
	}
	bool Passed = ExpectError("destroying moved-from source preserves the charge", Budget.TryReserve(1), BaselineStagingBudgetError::ByteLimit);
	const auto Empty = Budget.TryReserve(0);
	Passed &= Expect("moved reservation leaves exactly one count for another buffer", Empty.has_value());
	Passed &= ExpectError("moves preserve buffer count", Budget.TryReserve(0), BaselineStagingBudgetError::BufferLimit);
	Retained.reset();
	const auto Replacement = Budget.TryReserve(3);
	Passed &= Expect("destroying the moved-to reservation returns both charges", Replacement.has_value());
	Passed &= ExpectError("the surviving empty reservation still occupies one count", Budget.TryReserve(0), BaselineStagingBudgetError::BufferLimit);
	return Passed;
}

static bool CheckReceiverOwnership()
{
	BaselineStagingBudget Budget(4, 2);
	auto Lease = Budget.TryReserve(4);
	if (!Expect("receiver obtains a reservation before staging", Lease.has_value()))
	{
		return false;
	}
	bool Passed = true;
	{
		std::array<u8, 3> Storage{};
		BaselineStaging Receiver(Identity, Storage, std::move(*Lease));
		Passed &= Expect("a buffer covered by a larger reservation receives a baseline", Receiver.Begin(Declaration).has_value() &&
			Receiver.AddChunk({Identity, 10, Id, 0, Data}).has_value());
		Passed &= ExpectError("active staging charges the reserved buffer", Budget.TryReserve(1), BaselineStagingBudgetError::ByteLimit);
		const auto Baseline = Receiver.Finish(End);
		Passed &= Expect("completion leaves a borrowed view of reserved storage", Baseline && Baseline->Data.data() == Storage.data() && Baseline->Data.size() == 3);
		Passed &= ExpectError("completed storage remains charged while the view is valid", Budget.TryReserve(1), BaselineStagingBudgetError::ByteLimit);
		{
			const auto Empty = Budget.TryReserve(0);
			Passed &= Expect("another empty buffer fits beside the completed one", Empty.has_value());
			Passed &= ExpectError("completed storage still counts as one buffer", Budget.TryReserve(0), BaselineStagingBudgetError::BufferLimit);
		}
		Passed &= Expect("same-map reset keeps the buffer available to the receiver", Receiver.AdvanceWorld(Identity.WorldEpoch + 1));
		Passed &= ExpectError("round advance retains the reservation for reuse", Budget.TryReserve(1), BaselineStagingBudgetError::ByteLimit);
		auto Next = Declaration;
		++Next.Identity.WorldEpoch;
		Passed &= Expect("next world can stage within the same reservation", Receiver.Begin(Next).has_value());
	}
	const auto Reused = Budget.TryReserve(4);
	Passed &= Expect("destroying the receiver releases its held capacity", Reused.has_value());
	return Passed;
}

static bool CheckStorageBinding()
{
	BaselineStagingBudget Budget(2, 1);
	auto Lease = Budget.TryReserve(2);
	if (!Expect("storage binding fixture reserves two bytes", Lease.has_value()))
	{
		return false;
	}
	std::array<u8, 3> Storage{0xcc, 0xcc, 0xcc};
	BaselineStaging Uncovered(Identity, Storage, std::move(*Lease));
	const auto Larger = Uncovered.Begin({Identity, 9, {Id, 10, 3, 1, 1, 1}});
	bool Passed = Expect("only a fully reserved buffer can stage data, even under a small manifest", !Larger && Larger.error() == BaselineStagingError::InsufficientStorage);
	BaselineStaging Unreserved(Identity, {}, std::move(*Lease));
	const auto Empty = Unreserved.Begin({Identity, 9, {Id, 10, 3, 0, 0, 0}});
	Passed &= Expect("a moved-from reservation cannot stage an empty baseline", !Empty && Empty.error() == BaselineStagingError::InsufficientStorage);
	Passed &= Expect("invalid bindings leave backing bytes intact", Storage == std::array<u8, 3>{0xcc, 0xcc, 0xcc});
	Passed &= ExpectError("a failed begin keeps its reservation until the owner destroys the receiver", Budget.TryReserve(0), BaselineStagingBudgetError::BufferLimit);
	return Passed;
}

bool Run()
{
	bool Passed = CheckByteBudget();
	Passed &= CheckBufferBudget();
	Passed &= CheckPerBaselineLimit();
	Passed &= CheckMove();
	Passed &= CheckReceiverOwnership();
	Passed &= CheckStorageBinding();
	if (!Passed)
	{
		return false;
	}
	std::puts("Baseline staging budget checks passed");
	return true;
}
}
