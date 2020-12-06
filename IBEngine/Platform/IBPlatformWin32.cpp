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
        void (*OnWindowMessage)(void *data, IB::WindowMessage message) = nullptr;
        void *State = nullptr;
    };

    constexpr uint32_t MaxActiveWindows = 10;
    ActiveWindow ActiveWindows[MaxActiveWindows] = {};

    LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
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
            if (activeWindow != nullptr && activeWindow->OnWindowMessage != nullptr)
            {
                IB::WindowMessage message;
                message.Type = IB::WindowMessage::Close;

                activeWindow->OnWindowMessage(activeWindow->State, message);
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
        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
        {
            IB::WindowMessage message;
            message.Type = IB::WindowMessage::MouseClick;

            message.Data.MouseClick.X = LOWORD(lParam);
            message.Data.MouseClick.Y = HIWORD(lParam);

            switch (msg)
            {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
                message.Data.MouseClick.Button = IB::WindowMessage::Mouse::Left;
                break;
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
                message.Data.MouseClick.Button = IB::WindowMessage::Mouse::Middle;
                break;
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
                message.Data.MouseClick.Button = IB::WindowMessage::Mouse::Right;
                break;
            }

            switch (msg)
            {
            case WM_LBUTTONUP:
            case WM_MBUTTONUP:
            case WM_RBUTTONUP:
                message.Data.MouseClick.State = IB::WindowMessage::Mouse::Released;
                break;
            case WM_LBUTTONDOWN:
            case WM_MBUTTONDOWN:
            case WM_RBUTTONDOWN:
                message.Data.MouseClick.State = IB::WindowMessage::Mouse::Pressed;
                break;
            }
            activeWindow->OnWindowMessage(activeWindow->State, message);
        }
        break;
        case WM_MOUSEMOVE:
        {
            IB::WindowMessage message;
            message.Type = IB::WindowMessage::MouseMove;
            message.Data.MouseMove.X = LOWORD(lParam);
            message.Data.MouseMove.Y = HIWORD(lParam);
            activeWindow->OnWindowMessage(activeWindow->State, message);
        }
        break;
        case WM_SYSKEYDOWN:
        case WM_KEYDOWN:
        case WM_SYSKEYUP:
        case WM_KEYUP:
        {
            if (activeWindow != nullptr && activeWindow->OnWindowMessage != nullptr)
            {
                IB::WindowMessage message;
                message.Type = IB::WindowMessage::Key;

                uint32_t conversionMapping[0xFF] = {};
                conversionMapping[VK_LEFT] = IB::WindowMessage::Key::Code::Left;
                conversionMapping[VK_RIGHT] = IB::WindowMessage::Key::Code::Right;
                conversionMapping[VK_UP] = IB::WindowMessage::Key::Code::Up;
                conversionMapping[VK_DOWN] = IB::WindowMessage::Key::Code::Down;
                conversionMapping[VK_SHIFT] = IB::WindowMessage::Key::Code::Shift;
                conversionMapping[VK_CONTROL] = IB::WindowMessage::Key::Code::Control;
                conversionMapping[VK_RETURN] = IB::WindowMessage::Key::Code::Return;
                conversionMapping[VK_SPACE] = IB::WindowMessage::Key::Code::Space;
                conversionMapping[VK_ESCAPE] = IB::WindowMessage::Key::Code::Escape;
                for (uint32_t i = 0; i < 10; i++)
                {
                    conversionMapping['0' + i] = IB::WindowMessage::Key::Code::Num0 + i;
                }

                for (uint32_t i = 0; i < 26; i++)
                {
                    conversionMapping['A' + i] = IB::WindowMessage::Key::Code::A + i;
                }

                message.Data.Key.Code = static_cast<IB::WindowMessage::Key::Code>(conversionMapping[wParam]);

                switch (msg)
                {
                case WM_SYSKEYDOWN:
                case WM_KEYDOWN:
                    message.Data.Key.State = IB::WindowMessage::Key::Pressed;
                    break;
                case WM_SYSKEYUP:
                case WM_KEYUP:
                default:
                    message.Data.Key.State = IB::WindowMessage::Key::Released;
                    break;
                }

                message.Data.Key.Alt = (msg == WM_SYSKEYDOWN);
                if (message.Data.Key.State == IB::WindowMessage::Key::Pressed)
                {
                    bool isFirstDown = (lParam & (1 << 30)) == 0; // 30th bit represents if the key was down before this message was sent.
                    if (isFirstDown)
                    {
                        activeWindow->OnWindowMessage(activeWindow->State, message);
                    }
                }
                else
                {
                    activeWindow->OnWindowMessage(activeWindow->State, message);
                }
            }
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
        wndClass.lpfnWndProc = wndProc;
        wndClass.hInstance = hinstance;
        wndClass.lpszClassName = desc.Name;
        wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
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
                ActiveWindows[i].OnWindowMessage = desc.OnWindowMessage;
                break;
            }
        }
        IB_ASSERT(i < MaxActiveWindows, "Failed to add our window to our list of windows.");

        return IB::WindowHandle{i};
    }

    struct ActiveFileMapping
    {
        HANDLE FileHandle = NULL;
        HANDLE MapHandle = NULL;
        void *Mapping = nullptr;
    };

    constexpr uint32_t MaxMemoryFileMappingCount = 1024;
    ActiveFileMapping ActiveMemoryFileMappings[MaxMemoryFileMappingCount];

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

        for (uint32_t i = 0; i < MaxMemoryFileMappingCount; i++)
        {
            if (ActiveMemoryFileMappings[i].MapHandle == NULL)
            {
                ActiveMemoryFileMappings[i].MapHandle = fileMapping;
                ActiveMemoryFileMappings[i].Mapping = map;
                break;
            }
        }

        return map;
    }

    void unmapLargeMemoryBlock(void *memory)
    {
        for (uint32_t i = 0; i < MaxMemoryFileMappingCount; i++)
        {
            if (ActiveMemoryFileMappings[i].Mapping == memory)
            {
                UnmapViewOfFile(memory);
                CloseHandle(ActiveMemoryFileMappings[i].MapHandle);
                ActiveMemoryFileMappings[i] = {};
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

    uint64_t atomicCompareExchange(AtomicU64 *atomic, uint64_t compare, uint64_t exchange)
    {
        return InterlockedCompareExchangeNoFence64(reinterpret_cast<int64_t volatile *>(&atomic->Value), exchange, compare);
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

    File openFile(char const *filepath, uint32_t options)
    {
        DWORD access = 0;
        if ((options & OpenFileOptions::Read) != 0)
        {
            access |= GENERIC_READ;
        }

        if ((options & OpenFileOptions::Write) != 0)
        {
            access |= GENERIC_WRITE;
        }

        DWORD open = OPEN_EXISTING;
        if ((options & OpenFileOptions::Overwrite) != 0)
        {
            open = CREATE_ALWAYS;
        }
        else if ((options & OpenFileOptions::Create) != 0)
        {
            open = OPEN_ALWAYS;
        }

        HANDLE fileHandle = CreateFile(filepath, access, 0, NULL, open, FILE_ATTRIBUTE_NORMAL, NULL);
        if (fileHandle == INVALID_HANDLE_VALUE)
        {
            return File{};
        }

        return File{reinterpret_cast<uintptr_t>(fileHandle)};
    }

    void closeFile(File file)
    {
        CloseHandle(reinterpret_cast<HANDLE>(file.Value));
    }

    void *mapFile(File file)
    {
        HANDLE fileHandle = reinterpret_cast<HANDLE>(file.Value);
        HANDLE fileMapping = CreateFileMapping(fileHandle, NULL, PAGE_READONLY, 0, 0, NULL);
        void *map = MapViewOfFile(fileMapping, FILE_MAP_READ, 0, 0, 0);

        for (uint32_t i = 0; i < MaxFileMappingCount; i++)
        {
            if (ActiveFileMappings[i].MapHandle == NULL)
            {
                ActiveFileMappings[i].MapHandle = fileMapping;
                ActiveFileMappings[i].FileHandle = fileHandle;
                ActiveFileMappings[i].Mapping = map;
                break;
            }
        }

        return map;
    }

    void unmapFile(File file)
    {
        for (uint32_t i = 0; i < MaxFileMappingCount; i++)
        {
            if (ActiveFileMappings[i].FileHandle == reinterpret_cast<HANDLE>(file.Value))
            {
                UnmapViewOfFile(ActiveFileMappings[i].Mapping);
                CloseHandle(ActiveFileMappings[i].MapHandle);
                ActiveFileMappings[i] = {};
                break;
            }
        }
    }

    void writeToFile(File file, void *data, size_t size)
    {
        HANDLE fileHandle = reinterpret_cast<HANDLE>(file.Value);
        DWORD bytesWritten;
        BOOL result = WriteFile(fileHandle, data, static_cast<DWORD>(size), &bytesWritten, NULL);
        IB_ASSERT(result == TRUE, "Failed to write to file.");
    }

    void appendToFile(File file, void *data, size_t size)
    {
        HANDLE fileHandle = reinterpret_cast<HANDLE>(file.Value);

        SetFilePointer(fileHandle, 0, NULL, FILE_END);
        DWORD bytesWritten;
        BOOL result = WriteFile(fileHandle, data, static_cast<DWORD>(size), &bytesWritten, NULL);
        IB_ASSERT(result == TRUE, "Failed to write to file.");

        SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN);
    }

    size_t fileSize(File file)
    {
        HANDLE fileHandle = reinterpret_cast<HANDLE>(file.Value);

        DWORD high;
        DWORD low;
        low = GetFileSize(fileHandle, &high);
        return (static_cast<size_t>(high) << 32) | low;
    }

    bool isDirectory(char const* path)
    {
        return (GetFileAttributes(path) & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    void setWorkingDirectory(char const* path)
    {
        SetCurrentDirectory(path);
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
