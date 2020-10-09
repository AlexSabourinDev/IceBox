#pragma once

#include <stdint.h>
#include "../IBEngineAPI.h"

#ifdef _MSC_VER
#include <intrin.h>
#define IB_POPCOUNT(value) static_cast<uint8_t>(__popcnt64(value))
#endif // _MSC_VER

namespace IB
{
    // Windowing API
    struct WindowHandle
    {
        uintptr_t value;
    };
    struct WindowDesc
    {
        void (*OnCloseRequested)(void *) = nullptr;
        void *CallbackState = nullptr;

        char const *Name = nullptr;
        int32_t Width = 0;
        int32_t Height = 0;
    };

    IB_API WindowHandle createWindow(WindowDesc desc);
    IB_API void destroyWindow(WindowHandle window);

    // Messaging API
    // IceBox's platform abstraction is a message queue.
    // You can send messages to the queue and windows can also generate messages
    // The concept is very similar to Window's message queue, time will tell if it will map
    // to other platforms.
    enum class PlatformMessage
    {
        None,
        Quit
    };

    // returns whether or not there are more messages to consume.
    IB_API bool consumeMessageQueue(PlatformMessage *message);
    IB_API void sendQuitMessage();

    // Allocation API
    IB_API uint32_t memoryPageSize();
    IB_API void *reserveMemoryPages(uint32_t pageCount);
    // Commits reserved memory.
    IB_API void commitMemoryPages(void *pages, uint32_t pageCount);
    // Returns the page to reserve state.
    IB_API void decommitMemoryPages(void *pages, uint32_t pageCount);
    // Releases reserved memory.
    IB_API void freeMemoryPages(void *pages, uint32_t pageCount);

    // When requesting large blocks of memory, consider using a memory mapping
    IB_API void *mapLargeMemoryBlock(size_t size);
    IB_API void unmapLargeMemoryBlock(void *memory);

    // Atomic API
    struct AtomicU32
    {
        volatile uint32_t value;
    };

    IB_API uint32_t atomicIncrement(AtomicU32 *atomic);
    IB_API uint32_t atomicDecrement(AtomicU32 *atomic);

    IB_API void debugBreak();
} // namespace IB
