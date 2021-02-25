#pragma once

#include <stdint.h>
#include "IBEngineAPI.h"

#ifdef _MSC_VER
#include <intrin.h>
#endif // _MSC_VER

#if defined(_WIN32) || defined(_WIN64)
#define IB_WINDOWS
#endif // _WIN32 || _WIN64

namespace IB
{
#ifdef _MSC_VER
    inline uint8_t popCount(uint64_t value)
    {
        return static_cast<uint8_t>(__popcnt64(value));
    }
#endif // _MSC_VER

    // Windowing API
    struct WindowMessage
    {
        struct Key
        {
            enum Code
            {
                Unknown = 0x00,
                Left = 0x01,
                Right = 0x02,
                Up = 0x03,
                Down = 0x04,
                Shift = 0x05,
                Control = 0x06,
                Escape = 0x07,
                Return = 0x0D,
                Space = ' ',
                Num0 = '0',
                Num9 = '9',
                A = 'A',
                Z = 'Z',
            };

            enum State
            {
                Pressed,
                Released,
            };
        };

        struct Mouse
        {
            enum Button
            {
                Left,
                Right,
                Middle
            };

            enum State
            {
                Pressed,
                Released
            };
        };

        enum
        {
            Resize,
            Close,
            Key,
            MouseClick,
            MouseMove,
        } Type;

        union {
            struct
            {
                uint32_t Width;
                uint32_t Height;
            } Resize;

            struct
            {
                Key::State State;
                Key::Code Code;
                bool Alt;
            } Key;

            struct
            {
                Mouse::Button Button;
                Mouse::State State;
                uint32_t X;
                uint32_t Y;
            } MouseClick;

            struct
            {
                uint32_t X;
                uint32_t Y;
            } MouseMove;
        } Data;
    };

    struct WindowHandle
    {
        uintptr_t Value;
    };
    struct WindowDesc
    {
        void (*OnWindowMessage)(void *data, WindowMessage message) = nullptr;
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
    IB_API void *mapLargeMemoryBlock(size_t size);   // Threadsafe
    IB_API void unmapLargeMemoryBlock(void *memory); // Threadsafe as long as you don't unmap the same memory block.

    // Atomic API

    IB_API uint32_t atomicIncrement(uint32_t volatile *atomic);
    IB_API uint32_t atomicDecrement(uint32_t volatile *atomic);
    IB_API uint32_t atomicCompareExchange(uint32_t volatile *atomic, uint32_t compare, uint32_t exchange);
    IB_API uint64_t atomicCompareExchange(uint64_t volatile *atomic, uint64_t compare, uint64_t exchange);
    IB_API void *atomicCompareExchange(void *volatile *atomic, void *compare, void *exchange);
    inline uint32_t atomicIncrement(uint32_t *atomic) { return atomicIncrement(static_cast<uint32_t volatile *>(atomic)); }
    inline uint32_t atomicDecrement(uint32_t *atomic) { return atomicDecrement(static_cast<uint32_t volatile *>(atomic)); }
    inline uint32_t atomicCompareExchange(uint32_t *atomic, uint32_t compare, uint32_t exchange) { return atomicCompareExchange(static_cast<uint32_t volatile *>(atomic), compare, exchange); }
    inline uint64_t atomicCompareExchange(uint64_t *atomic, uint64_t compare, uint64_t exchange) { return atomicCompareExchange(static_cast<uint64_t volatile *>(atomic), compare, exchange); }
    inline void *atomicCompareExchange(void **atomic, void *compare, void *exchange) { return atomicCompareExchange(static_cast<void *volatile *>(atomic), compare, exchange); }

    // Threading API
    struct ThreadHandle
    {
        uintptr_t Value;
    };

    using ThreadFunc = void(void *);
    IB_API uint32_t processorCount();
    IB_API ThreadHandle createThread(ThreadFunc *threadFunc, void *threadData);
    IB_API void destroyThread(ThreadHandle thread);
    IB_API void waitOnThreads(ThreadHandle *threads, uint32_t threadCount);

    struct ThreadEvent
    {
        uintptr_t Value;
    };

    IB_API ThreadEvent createThreadEvent();
    IB_API void destroyThreadEvent(ThreadEvent threadEvent);
    IB_API void signalThreadEvent(ThreadEvent threadEvent);
    IB_API void waitOnThreadEvent(ThreadEvent threadEvent);

    // Not the ideal place for these, but good enough for now
    template <typename T>
    T volatileLoad(T *value)
    {
        return *reinterpret_cast<T volatile *>(value);
    }

    template <typename T>
    void volatileStore(T *value, T set)
    {
        *reinterpret_cast<T volatile *>(value) = set;
    }

    IB_API void threadStoreStoreFence();
    IB_API void threadLoadLoadFence();
    IB_API void threadLoadStoreFence();
    IB_API void threadStoreLoadFence();
    IB_API void threadAcquire();
    IB_API void threadRelease();

    IB_API void debugBreak();

    // File API
    struct File
    {
        uintptr_t Value;
    };
    constexpr File InvalidFile = File{0};

    struct OpenFileOptions
    {
        enum
        {
            Read = 0x01,
            Write = 0x02,
            Create = 0x04,
            Overwrite = 0x08
        };
    };

    IB_API File openFile(char const *filepath, uint32_t options); // Threadsafe
    IB_API void closeFile(File file);                             // Threadsafe
    IB_API void *mapFile(File file);                              // Threadsafe
    IB_API void unmapFile(File file);                             // Threadsafe as long as you don't unmap the same file in another thread
    IB_API void writeToFile(File file, void const *data, size_t size, uint32_t offset = 0);
    IB_API void appendToFile(File file, void const *data, size_t size);
    IB_API size_t fileSize(File file);
    IB_API bool doesFileExist(char const *filepath);

    // File system
    IB_API bool isDirectory(char const *path);
    IB_API void setWorkingDirectory(char const *path);
} // namespace IB
