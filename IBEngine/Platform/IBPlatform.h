#pragma once

#include <stdint.h>
#include "../IBEngineAPI.h"

#ifdef _MSC_VER
#include <intrin.h>
#define IB_POPCOUNT(value) static_cast<uint8_t>(__popcnt64(value))
#endif // _MSC_VER

#if defined(_WIN32) || defined(_WIN64)
#define IB_WINDOWS
#endif // _WIN32 || _WIN64

namespace IB
{
    // Windowing API
    struct WindowMessage
    {
        enum
        {
            Resize,
        } Type;

        union
        {
            struct
            {
                uint32_t Width;
                uint32_t Height;
            } Resize;
        } Data;
    };

    struct WindowHandle
    {
        uintptr_t Value;
    };
    struct WindowDesc
    {
        void (*OnCloseRequested)(void *data) = nullptr;
        void(*OnWindowMessage)(void *data, WindowMessage message) = nullptr;
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
    IB_API void consumeMessageQueue(void (*consumerFunc)(void *data, PlatformMessage message), void *data);
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
        uint32_t volatile Value;
    };

    struct AtomicPtr
    {
        void* volatile Value;
    };

    IB_API uint32_t atomicIncrement(AtomicU32 *atomic);
    IB_API uint32_t atomicDecrement(AtomicU32 *atomic);
    IB_API uint32_t atomicCompareExchange(AtomicU32 *atomic, uint32_t compare, uint32_t exchange);
    IB_API void* atomicCompareExchange(AtomicPtr *atomic, void* compare, void* exchange);

    // Threading API
    struct ThreadHandle
    {
        uintptr_t Value;
    };

    using ThreadFunc = void(void *);
    IB_API uint32_t processorCount();
    IB_API ThreadHandle createThread(ThreadFunc *threadFunc, void *threadData);
    IB_API void destroyThread(ThreadHandle thread);
    IB_API void waitOnThreads(ThreadHandle* threads, uint32_t threadCount);

    struct ThreadEvent
    {
        uintptr_t Value;
    };

    IB_API ThreadEvent createThreadEvent();
    IB_API void destroyThreadEvent(ThreadEvent threadEvent);
    IB_API void signalThreadEvent(ThreadEvent threadEvent);
    IB_API void waitOnThreadEvent(ThreadEvent threadEvent);

    IB_API void threadStoreFence();

    IB_API void debugBreak();
} // namespace IB
