#pragma once

#include <cstddef>
#include <expected>

namespace Multiplayer
{
enum class BaselineStagingBudgetError
{
	BaselineTooLarge,
	BufferLimit,
	ByteLimit,
};

class BaselineStagingBudget;

class BaselineStagingReservation
{
public:
	~BaselineStagingReservation();
	BaselineStagingReservation(const BaselineStagingReservation&) = delete;
	BaselineStagingReservation(BaselineStagingReservation&& Other) noexcept;
	BaselineStagingReservation& operator=(const BaselineStagingReservation&) = delete;

private:
	friend class BaselineStagingBudget;
	friend class BaselineStaging;

	BaselineStagingReservation(BaselineStagingBudget& Owner, std::size_t Bytes) noexcept;
	[[nodiscard]] bool Covers(std::size_t StorageSize) const noexcept;

	BaselineStagingBudget* Owner;
	const std::size_t Bytes;
};

// The owner serializes reservations and releases across receivers. The budget outlives its reservations.
class BaselineStagingBudget
{
public:
	BaselineStagingBudget(std::size_t ByteCapacity, std::size_t BufferCapacity) noexcept;
	BaselineStagingBudget(const BaselineStagingBudget&) = delete;
	BaselineStagingBudget& operator=(const BaselineStagingBudget&) = delete;

	// Reserve before taking backing storage. Empty buffers still consume one place.
	[[nodiscard]] std::expected<BaselineStagingReservation, BaselineStagingBudgetError> TryReserve(std::size_t Bytes) noexcept;

private:
	friend class BaselineStagingReservation;

	const std::size_t ByteCapacity;
	const std::size_t BufferCapacity;
	std::size_t ReservedBytes = 0;
	std::size_t ReservedBuffers = 0;
};
}
