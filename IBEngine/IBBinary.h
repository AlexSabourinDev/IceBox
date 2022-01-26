#pragma once
// Header for bit utils

#include <stdint.h>

namespace IB
{
#ifdef _MSC_VER
    inline uint8_t popCount(uint64_t value)
    {
        return static_cast<uint8_t>(__popcnt64(value));
    }
#endif // _MSC_VER

    namespace Bitset
    {
        uint64_t constexpr NoSlot = 0xFFFFFFFFFFFFFFFF;

        // Assumes a uint64_t buffer
        uint64_t constexpr bufferSize(uint64_t bufferMaxCount);

        inline uint64_t firstClearedBitIndex(uint64_t value);
        inline uint64_t findClearedSlot(uint64_t *memory, uint64_t bitCount);
        inline void setSlot(uint64_t *memoryIter, uint64_t index);
        inline void clearSlot(uint64_t *memoryIter, uint64_t index);

        // Implementation
        uint64_t constexpr bufferSize(uint64_t bufferMaxCount)
        {
            return bufferMaxCount / (sizeof(uint64_t) * 8) +
                   ((bufferMaxCount % (sizeof(uint64_t) * 8) != 0) ? 1 : 0);
        }

        inline uint64_t firstClearedBitIndex(uint64_t value)
        {
            // Mask out all our set bits and the first non-set bit
            // (0010 + 1) ^ 0010 = 0011 ^ 0010 = 0001
            // (0011 + 1) ^ 0011 = 0100 ^ 0011 = 0111
            // (1011 + 1) ^ 1011 = 1100 ^ 1011 = 0111
            // (0111 + 1) ^ 0111 = 1000 ^ 0111 = 1111
            value = value ^ (value + 1);

            // At this point, the number of bits set is equal to the index of our first cleared bit + 1
            // Calculate the number of set bits and subtract 1 to get it's index

            return popCount(value) - 1;
        }

        inline uint64_t findClearedSlot(uint64_t *memory, uint64_t bitCount)
        {
            uint64_t freeSlot = NoSlot;

            // TODO: handle alignment
            uint64_t *memoryIter = reinterpret_cast<uint64_t *>(memory);
            for (; static_cast<int32_t>(bitCount) > 0; memoryIter++, bitCount -= 64)
            {
                uint64_t value = *memoryIter;
                uint64_t testBitMask = ~(bitCount < 64 ? (0xFFFFFFFFFFFFFFFF << (bitCount % 64)) : 0); // Create a bitmask of all the bits we want to test
                if ((value & testBitMask) != testBitMask)                                              // If any of our bits were cleared
                {
                    uint64_t bitIndex = firstClearedBitIndex(value);

                    ptrdiff_t chunkIndex = (memoryIter - reinterpret_cast<uint64_t *>(memory));
                    freeSlot = chunkIndex * 64 + bitIndex;
                    break;
                }
            }

            return freeSlot;
        }

        inline void setSlot(uint64_t *memoryIter, uint64_t index)
        {
            memoryIter = memoryIter + index / 64;
            *memoryIter |= 1ull << (index % 64);
        }

        inline void clearSlot(uint64_t *memoryIter, uint64_t index)
        {
            memoryIter = memoryIter + index / 64;
            *memoryIter &= ~(1ull << (index % 64));
        }
    } // namespace Bitset
} // namespace IB
