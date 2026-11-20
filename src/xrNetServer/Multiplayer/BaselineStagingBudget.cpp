#include "BaselineStagingBudget.h"
#include "BaselineTransfer.h"

#include <utility>

namespace Multiplayer
{
BaselineStagingReservation::BaselineStagingReservation(BaselineStagingBudget& Owner, std::size_t Bytes) noexcept : Owner(&Owner), Bytes(Bytes)
{
}

BaselineStagingReservation::BaselineStagingReservation(BaselineStagingReservation&& Other) noexcept
	: Owner(std::exchange(Other.Owner, nullptr)), Bytes(Other.Bytes)
{
}

BaselineStagingReservation::~BaselineStagingReservation()
{
	if (Owner)
	{
		--Owner->ReservedBuffers;
		Owner->ReservedBytes -= Bytes;
	}
}

bool BaselineStagingReservation::Covers(std::size_t StorageSize) const noexcept
{
	return Owner && StorageSize <= Bytes;
}

BaselineStagingBudget::BaselineStagingBudget(std::size_t ByteCapacity, std::size_t BufferCapacity) noexcept
	: ByteCapacity(ByteCapacity), BufferCapacity(BufferCapacity)
{
}

std::expected<BaselineStagingReservation, BaselineStagingBudgetError> BaselineStagingBudget::TryReserve(std::size_t Bytes) noexcept
{
	if (Bytes > Wire::MaxBaselineBytes)
	{
		return std::unexpected(BaselineStagingBudgetError::BaselineTooLarge);
	}
	if (ReservedBuffers == BufferCapacity)
	{
		return std::unexpected(BaselineStagingBudgetError::BufferLimit);
	}
	if (Bytes > ByteCapacity - ReservedBytes)
	{
		return std::unexpected(BaselineStagingBudgetError::ByteLimit);
	}
	++ReservedBuffers;
	ReservedBytes += Bytes;
	return BaselineStagingReservation(*this, Bytes);
}
}
