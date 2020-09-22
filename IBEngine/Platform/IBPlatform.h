#pragma once

#include <stdint.h>

#define DLL_API __declspec(dllexport)

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

    DLL_API WindowHandle createWindow(WindowDesc desc);
    DLL_API void destroyWindow(WindowHandle window);

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
    DLL_API bool consumeMessageQueue(PlatformMessage *message);
    DLL_API void sendQuitMessage();
} // namespace IB
