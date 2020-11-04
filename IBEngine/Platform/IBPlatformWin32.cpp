#include "IBPlatform.h"

#include "../IBLogging.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <sysinfoapi.h>
#include <stdint.h>

namespace
{
    struct ActiveWindow
    {
        HWND WindowHandle = NULL;

        void (*OnCloseRequested)(void *) = nullptr;
        void(*OnWindowMessage)(void *data, IB::WindowMessage message) = nullptr;
        void *State = nullptr;
    };

    constexpr uint32_t MaxActiveWindows = 10;
    ActiveWindow ActiveWindows[MaxActiveWindows] = {};

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        ActiveWindow *activeWindow = nullptr;
        for (uint32_t i = 0; i < MaxActiveWindows; i++)
        {
            if (ActiveWindows[i].WindowHandle == hwnd)
            {
                activeWindow = &ActiveWindows[i];
                break;
            }
        }

        switch (msg)
        {
        case WM_CLOSE:
            if (activeWindow != nullptr && activeWindow->OnCloseRequested != nullptr)
            {
                activeWindow->OnCloseRequested(activeWindow->State);
            }
            break;
        case WM_SIZE:
            if (activeWindow != nullptr && activeWindow->OnWindowMessage != nullptr)
            {
                IB::WindowMessage message;
                message.Type = IB::WindowMessage::Resize;
                message.Data.Resize.Width = LOWORD(lParam);
                message.Data.Resize.Height = HIWORD(lParam);
                activeWindow->OnWindowMessage(activeWindow->State, message);
            }
            break;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        return 0;
    }

    IB::WindowHandle createWindowWin32(IB::WindowDesc desc, HWND parentWindowHandle, DWORD style)
    {
        HINSTANCE hinstance = GetModuleHandle(NULL);

        WNDCLASS wndClass = {};
        wndClass.lpfnWndProc = WndProc;
        wndClass.hInstance = hinstance;
        wndClass.lpszClassName = desc.Name;
        ATOM classAtom = RegisterClass(&wndClass);
        IB_ASSERT(classAtom != 0, "Failed to register window class.");

        RECT rect = {0, 0, desc.Width, desc.Height};
        BOOL result = AdjustWindowRect(&rect, style, FALSE);
        IB_ASSERT(result == TRUE, "Failed to adjust our window's rect.");

        HWND hwnd = CreateWindowEx(
            0,
            desc.Name,
            desc.Name,
            style,
            CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
            parentWindowHandle,
            NULL,
            hinstance,
            NULL);
        IB_ASSERT(hwnd != NULL, "Failed to create our window");

        result = ShowWindow(hwnd, SW_SHOWNORMAL);
        UpdateWindow(hwnd);

        uint32_t i = 0;
        for (; i < MaxActiveWindows; i++)
        {
            if (ActiveWindows[i].WindowHandle == NULL)
            {
                ActiveWindows[i].WindowHandle = hwnd;
                ActiveWindows[i].State = desc.CallbackState;
                ActiveWindows[i].OnCloseRequested = desc.OnCloseRequested;
                ActiveWindows[i].OnWindowMessage = desc.OnWindowMessage;
                break;
            }
        }
        IB_ASSERT(i < MaxActiveWindows, "Failed to add our window to our list of windows.");

        return IB::WindowHandle{i};
    }

    struct ActiveFileMapping
    {
        HANDLE Handle = NULL;
        void *Mapping = nullptr;
    };

    constexpr uint32_t MaxFileMappingCount = 1024;
    ActiveFileMapping ActiveFileMappings[MaxFileMappingCount];

    struct ActiveThread
    {
        IB::ThreadFunc *Func = nullptr;
        void *Data = nullptr;
        HANDLE Thread = NULL;
    };
    constexpr uint32_t MaxThreadCount = 1024;
    ActiveThread ActiveThreads[MaxThreadCount];

    DWORD WINAPI ThreadProc(LPVOID data)
    {
        ActiveThread *activeThread = reinterpret_cast<ActiveThread *>(data);
        activeThread->Func(activeThread->Data);
        return 0;
    }

    constexpr uint32_t MaxEventCount = 1024;
    HANDLE ActiveEvents[MaxEventCount];
} // namespace

namespace IB
{
    namespace Win32
    {
        // Must be externed for access
        void getWindowHandleAndInstance(WindowHandle handle, HWND *window, HINSTANCE *instance)
        {
            *instance = GetModuleHandle(NULL);
            *window = ActiveWindows[handle.Value].WindowHandle;
        }
    } // namespace Win32

    WindowHandle createWindow(WindowDesc desc)
    {
        return createWindowWin32(desc, nullptr, WS_OVERLAPPEDWINDOW);
    }

    void destroyWindow(WindowHandle window)
    {
        DestroyWindow(ActiveWindows[window.Value].WindowHandle);
        ActiveWindows[window.Value] = {};
    }

    void consumeMessageQueue(void (*consumerFunc)(void *data, PlatformMessage message), void *data)
    {
        MSG msg;

        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                consumerFunc(data, PlatformMessage::Quit);
            }

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    void sendQuitMessage()
    {
        PostQuitMessage(0);
    }

    uint32_t memoryPageSize()
    {
        SYSTEM_INFO systemInfo;
        GetSystemInfo(&systemInfo);

        return systemInfo.dwPageSize;
    }

    void *reserveMemoryPages(uint32_t pageCount)
    {
        LPVOID address = VirtualAlloc(NULL, memoryPageSize() * pageCount, MEM_RESERVE, PAGE_NOACCESS);
        IB_ASSERT(address != NULL, "Failed to allocate block!");
        return address;
    }

    void commitMemoryPages(void *pages, uint32_t pageCount)
    {
        IB_ASSERT(reinterpret_cast<uintptr_t>(pages) % memoryPageSize() == 0, "Memory must be aligned on a page size boundary!");

        VirtualAlloc(pages, memoryPageSize() * pageCount, MEM_COMMIT, PAGE_READWRITE);
    }

    void decommitMemoryPages(void *pages, uint32_t pageCount)
    {
        IB_ASSERT(reinterpret_cast<uintptr_t>(pages) % memoryPageSize() == 0, "Memory must be aligned on a page size boundary!");

        BOOL result = VirtualFree(pages, memoryPageSize() * pageCount, MEM_DECOMMIT);
        IB_ASSERT(result == TRUE, "Failed to free memory!");
    }

    void freeMemoryPages(void *pages, uint32_t pageCount)
    {
        IB_ASSERT(reinterpret_cast<uintptr_t>(pages) % memoryPageSize() == 0, "Memory must be aligned on a page size boundary!");

        BOOL result = VirtualFree(pages, memoryPageSize() * pageCount, MEM_RELEASE);
        IB_ASSERT(result == TRUE, "Failed to release memory!");
    }

    void *mapLargeMemoryBlock(size_t size)
    {
        HANDLE fileMapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE | SEC_COMMIT, static_cast<DWORD>(size >> 32), static_cast<DWORD>(size & 0xFFFFFFFF), NULL);
        void *map = MapViewOfFile(fileMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);

        for (uint32_t i = 0; i < MaxFileMappingCount; i++)
        {
            if (ActiveFileMappings[i].Handle == NULL)
            {
                ActiveFileMappings[i].Handle = fileMapping;
                ActiveFileMappings[i].Mapping = map;
                break;
            }
        }

        return map;
    }

    void unmapLargeMemoryBlock(void *memory)
    {
        for (uint32_t i = 0; i < MaxFileMappingCount; i++)
        {
            if (ActiveFileMappings[i].Mapping == memory)
            {
                UnmapViewOfFile(memory);
                CloseHandle(ActiveFileMappings[i].Handle);
                ActiveFileMappings[i] = {};
                break;
            }
        }
    }

    uint32_t atomicIncrement(AtomicU32 *atomic)
    {
        return InterlockedIncrementNoFence(&atomic->Value);
    }

    uint32_t atomicDecrement(AtomicU32 *atomic)
    {
        return InterlockedDecrementNoFence(&atomic->Value);
    }

    uint32_t atomicCompareExchange(AtomicU32 *atomic, uint32_t compare, uint32_t exchange)
    {
        return InterlockedCompareExchangeNoFence(&atomic->Value, exchange, compare);
    }

    void *atomicCompareExchange(AtomicPtr *atomic, void *compare, void *exchange)
    {
        return InterlockedCompareExchangePointerNoFence(&atomic->Value, exchange, compare);
    }

    uint32_t processorCount()
    {
        SYSTEM_INFO systemInfo;
        GetSystemInfo(&systemInfo);

        return systemInfo.dwNumberOfProcessors;
    }

    ThreadHandle createThread(ThreadFunc *threadFunc, void *threadData)
    {
        uintptr_t index = 0;
        for (index; index < MaxThreadCount; index++)
        {
            // TODO: Not threadsafe, is that ok?
            if (ActiveThreads[index].Thread == NULL)
            {
                ActiveThreads[index].Func = threadFunc;
                ActiveThreads[index].Data = threadData;
                ActiveThreads[index].Thread = CreateThread(NULL, 0, &ThreadProc, &ActiveThreads[index], 0, NULL);
                IB_ASSERT(ActiveThreads[index].Thread != NULL, "Failed to create thread.");
                break;
            }
        }

        IB_ASSERT(index != MaxThreadCount, "Failed to create thread.");
        return ThreadHandle{index};
    }

    void destroyThread(ThreadHandle thread)
    {
        CloseHandle(ActiveThreads[thread.Value].Thread);
        ActiveThreads[thread.Value] = {};
    }

    IB_API void waitOnThreads(ThreadHandle *threads, uint32_t threadCount)
    {
        HANDLE threadHandles[MaxThreadCount];
        for (uint32_t i = 0; i < threadCount; i++)
        {
            threadHandles[i] = ActiveThreads[threads[i].Value].Thread;
        }

        DWORD result = WaitForMultipleObjects(threadCount, threadHandles, TRUE, INFINITE);
        IB_ASSERT(result != WAIT_FAILED, "Failed to wait on our threads!");
    }

    ThreadEvent createThreadEvent()
    {
        return ThreadEvent{reinterpret_cast<uintptr_t>(CreateEvent(NULL, FALSE, FALSE, NULL))};
    }

    void destroyThreadEvent(ThreadEvent threadEvent)
    {
        CloseHandle(reinterpret_cast<HANDLE>(threadEvent.Value));
    }

    void signalThreadEvent(ThreadEvent threadEvent)
    {
        BOOL result = SetEvent(reinterpret_cast<HANDLE>(threadEvent.Value));
        IB_ASSERT(result, "Failed to set our event!");
    }

    void waitOnThreadEvent(ThreadEvent threadEvent)
    {
        DWORD result = WaitForSingleObject(reinterpret_cast<HANDLE>(threadEvent.Value), INFINITE);
        IB_ASSERT(result != WAIT_FAILED, "Failed to wait on our event!");
    }

    void threadStoreFence()
    {
        _mm_sfence();
    }

    void debugBreak()
    {
        DebugBreak();
    }

} // namespace IB

// Bridge
extern "C"
{
    IB_API void *IB_createWindow(void *parentWindowHandle, const char *name, int width, int height)
    {
        IB::WindowDesc desc = {};
        desc.Name = name;
        desc.Width = width;
        desc.Height = height;
        IB::WindowHandle handle = createWindowWin32(desc, reinterpret_cast<HWND>(parentWindowHandle), DS_CONTROL | WS_CHILD);
        return ActiveWindows[handle.Value].WindowHandle;
    }

    IB_API void IB_destroyWindow(void *windowHandle)
    {
        DestroyWindow(reinterpret_cast<HWND>(windowHandle));

        for (uint32_t i = 0; i < MaxActiveWindows; i++)
        {
            if (ActiveWindows[i].WindowHandle == windowHandle)
            {
                ActiveWindows[i] = {};
                break;
            }
        }
    }
}
