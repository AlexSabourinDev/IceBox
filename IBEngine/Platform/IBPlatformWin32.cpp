#include "../IBPlatform.h"
#include "../IBLogging.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <sysinfoapi.h>
#include <stdint.h>

namespace IB
{
    struct WindowMessagePlatformData
    {
        HWND Hwnd;
        UINT Msg;
        WPARAM WParam;
        LPARAM LParam;
    };
}

namespace
{
    template <typename T>
    T volatileLoad(T const *target)
    {
        return *static_cast<T volatile const *>(target);
    }

    struct ActiveWindow
    {
        HWND WindowHandle = NULL;

        void (*OnCloseRequested)(void *) = nullptr;
        bool (*OnWindowMessage)(void *data, IB::WindowMessage message, IB::WindowMessagePlatformData const*platformData) = nullptr;
        void *State = nullptr;
    };

    constexpr uint32_t MaxActiveWindows = 10;
    ActiveWindow ActiveWindows[MaxActiveWindows] = {};

    LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        const IB::WindowMessagePlatformData platformData = { hwnd, msg, wParam, lParam };

        ActiveWindow *activeWindow = nullptr;
        for (uint32_t i = 0; i < MaxActiveWindows; i++)
        {
            if (ActiveWindows[i].WindowHandle == hwnd)
            {
                activeWindow = &ActiveWindows[i];
                break;
            }
        }

        bool messageHandled = false;
        switch (msg)
        {
        case WM_CLOSE:
            if (activeWindow != nullptr && activeWindow->OnWindowMessage != nullptr)
            {
                IB::WindowMessage message;
                message.Type = IB::WindowMessage::Close;

                messageHandled = activeWindow->OnWindowMessage(activeWindow->State, message, &platformData);
            }
            break;
        case WM_SIZE:
            if (activeWindow != nullptr && activeWindow->OnWindowMessage != nullptr)
            {
                IB::WindowMessage message;
                message.Type = IB::WindowMessage::Resize;
                message.Data.Resize.Width = LOWORD(lParam);
                message.Data.Resize.Height = HIWORD(lParam);
                messageHandled = activeWindow->OnWindowMessage(activeWindow->State, message, &platformData);
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
            messageHandled = activeWindow->OnWindowMessage(activeWindow->State, message, &platformData);
        }
        break;
        case WM_MOUSEMOVE:
        {
            IB::WindowMessage message;
            message.Type = IB::WindowMessage::MouseMove;
            message.Data.MouseMove.X = LOWORD(lParam);
            message.Data.MouseMove.Y = HIWORD(lParam);
            messageHandled = activeWindow->OnWindowMessage(activeWindow->State, message, &platformData);
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
                        messageHandled = activeWindow->OnWindowMessage(activeWindow->State, message, &platformData);
                    }
                }
                else
                {
                    messageHandled = activeWindow->OnWindowMessage(activeWindow->State, message, &platformData);
                }
            }
        }
        break;
        default:
        {
            if (activeWindow != nullptr && activeWindow->OnWindowMessage != nullptr)
            {
                IB::WindowMessage message;
                message.Type = IB::WindowMessage::Unknown;
                messageHandled = activeWindow->OnWindowMessage(activeWindow->State, message, &platformData);
            }
        }
        break;
        }

        if (!messageHandled)
        {
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
        else
        {
            return TRUE;
        }
    }

    IB::Window::Handle createWindowWin32(IB::WindowDesc desc, HWND parentWindowHandle, DWORD style)
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

        return IB::Window::toHandle(i);
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
        void getWindowHandleAndInstance(Window::Handle handle, HWND *window, HINSTANCE *instance)
        {
            *instance = GetModuleHandle(NULL);
            *window = ActiveWindows[Window::toValue(handle)].WindowHandle;
        }

        void getPlatformMessageData(WindowMessagePlatformData const* platformData, HWND* hwnd, UINT* msg, WPARAM* wparam, LPARAM* lparam)
        {
            *hwnd = platformData->Hwnd;
            *msg = platformData->Msg;
            *wparam = platformData->WParam;
            *lparam = platformData->LParam;
        }
    } // namespace Win32

    Window::Handle createWindow(WindowDesc desc)
    {
        return createWindowWin32(desc, nullptr, WS_OVERLAPPEDWINDOW);
    }

    void destroyWindow(Window::Handle window)
    {
        DestroyWindow(ActiveWindows[Window::toValue(window)].WindowHandle);
        ActiveWindows[Window::toValue(window)] = {};
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
        LPVOID address = VirtualAlloc(NULL, memoryPageSize() * pageCount, MEM_RESERVE, PAGE_READONLY);
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

    void freeMemoryPages(void *pages)
    {
        IB_ASSERT(reinterpret_cast<uintptr_t>(pages) % memoryPageSize() == 0, "Memory must be aligned on a page size boundary!");

        BOOL result = VirtualFree(pages, 0, MEM_RELEASE);
        IB_ASSERT(result == TRUE, "Failed to release memory!");
    }

    void *mapLargeMemoryBlock(size_t size)
    {
        HANDLE fileMapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE | SEC_COMMIT, static_cast<DWORD>(size >> 32), static_cast<DWORD>(size & 0xFFFFFFFF), NULL);
        void *map = nullptr;

        for (uint32_t i = 0; i < MaxMemoryFileMappingCount; i++)
        {
            if (atomicCompareExchange(&ActiveMemoryFileMappings[i].MapHandle, nullptr, fileMapping) == nullptr)
            {
                map = MapViewOfFile(fileMapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
                ActiveMemoryFileMappings[i].Mapping = map;
                break;
            }
        }

        IB_ASSERT(map != nullptr, "Failed to create a file mapping!");
        return map;
    }

    void unmapLargeMemoryBlock(void *memory)
    {
        for (uint32_t i = 0; i < MaxMemoryFileMappingCount; i++)
        {
            if (volatileLoad(&ActiveMemoryFileMappings[i].Mapping) == memory)
            {
                HANDLE handle = ActiveMemoryFileMappings[i].MapHandle;
                // Make sure we clear our entry before we return our address
                // to the address list in UnmapViewOfFile.
                // If we don't, we can have a thread receive the same address,
                // and then it can get unmapped which will cause us to try to close our handle twice.
                ActiveMemoryFileMappings[i] = {};
                threadStoreStoreFence(); // make sure our store is completed before any stores done by UnmapViewOfFile

                UnmapViewOfFile(memory);
                CloseHandle(handle);
                break;
            }
        }
    }

    uint32_t atomicIncrement(uint32_t volatile *atomic)
    {
        return InterlockedIncrementNoFence(atomic);
    }

    uint32_t atomicDecrement(uint32_t volatile *atomic)
    {
        return InterlockedDecrementNoFence(atomic);
    }

    uint32_t atomicCompareExchange(uint32_t volatile *atomic, uint32_t compare, uint32_t exchange)
    {
        return InterlockedCompareExchangeNoFence(atomic, exchange, compare);
    }

    uint64_t atomicCompareExchange(uint64_t volatile *atomic, uint64_t compare, uint64_t exchange)
    {
        return InterlockedCompareExchangeNoFence64(reinterpret_cast<int64_t volatile *>(atomic), exchange, compare);
    }

    void *atomicCompareExchange(void *volatile *atomic, void *compare, void *exchange)
    {
        return InterlockedCompareExchangePointerNoFence(atomic, exchange, compare);
    }

    uint64_t atomicOr(uint64_t volatile *atomic, uint64_t orValue)
    {
        return InterlockedOr64NoFence(reinterpret_cast<int64_t volatile*>(atomic), orValue);
    }

    uint64_t atomicAnd(uint64_t volatile *atomic, uint64_t andValue)
    {
        return InterlockedAnd64NoFence(reinterpret_cast<int64_t volatile*>(atomic), andValue);
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

    void resetThreadEvent(ThreadEvent threadEvent)
    {
        BOOL result = ResetEvent(reinterpret_cast<HANDLE>(threadEvent.Value));
        IB_ASSERT(result, "Failed to reset our event!");
    }

    void threadStoreStoreFence()
    {
        // Assuminc x86-64 that already has store-store ordering
        // _mm_sfence();
    }

    void threadLoadLoadFence()
    {
        // Assuming x86-64 that already has load-load ordering
        // _mm_lfence();
    }

    void threadLoadStoreFence()
    {
        // Assuming x86-64 that already has load-store ordering
    }

    void threadStoreLoadFence()
    {
        _mm_mfence();
    }

    void threadAcquire()
    {
        threadLoadStoreFence();
        threadLoadLoadFence();
    }

    void threadRelease()
    {
        threadLoadStoreFence();
        threadStoreStoreFence();
    }

    void debugBreak()
    {
        DebugBreak();
    }

    File::Handle openFile(char const *filepath, uint32_t options)
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

        HANDLE fileHandle = CreateFile(filepath, access, FILE_SHARE_READ, NULL, open, FILE_ATTRIBUTE_NORMAL, NULL);
        if (fileHandle == INVALID_HANDLE_VALUE)
        {
            return File::toHandle(0);
        }

        return File::toHandle(reinterpret_cast<uintptr_t>(fileHandle));
    }

    void closeFile(File::Handle file)
    {
        CloseHandle(reinterpret_cast<HANDLE>(File::toValue(file)));
    }

    void *mapFile(File::Handle file)
    {
        HANDLE fileHandle = reinterpret_cast<HANDLE>(File::toValue(file));
        HANDLE fileMapping = CreateFileMapping(fileHandle, NULL, PAGE_READONLY, 0, 0, NULL);
        void *map = MapViewOfFile(fileMapping, FILE_MAP_READ, 0, 0, 0);

        for (uint32_t i = 0; i < MaxFileMappingCount; i++)
        {
            if (atomicCompareExchange(&ActiveFileMappings[i].MapHandle, nullptr, fileMapping) == nullptr)
            {
                ActiveFileMappings[i].FileHandle = fileHandle;
                ActiveFileMappings[i].Mapping = map;
                break;
            }
        }

        return map;
    }

    void unmapFile(File::Handle file)
    {
        for (uint32_t i = 0; i < MaxFileMappingCount; i++)
        {
            if (volatileLoad(&ActiveFileMappings[i].FileHandle) == reinterpret_cast<HANDLE>(File::toValue(file)))
            {
                void *mapping = ActiveFileMappings[i].Mapping;
                HANDLE handle = ActiveFileMappings[i].MapHandle;

                // Make sure our mapping is cleared before we return our data to the OS
                ActiveFileMappings[i] = {};
                threadStoreStoreFence(); // Make sure our write is globally visible before our unmap is visible

                UnmapViewOfFile(mapping);
                CloseHandle(handle);
                break;
            }
        }
    }

    void writeToFile(File::Handle file, void const *data, size_t size, uint32_t offset)
    {
        HANDLE fileHandle = reinterpret_cast<HANDLE>(File::toValue(file));

        SetFilePointer(fileHandle, offset, NULL, FILE_BEGIN);

        DWORD bytesWritten;
        BOOL result = WriteFile(fileHandle, data, static_cast<DWORD>(size), &bytesWritten, NULL);
        IB_ASSERT(result == TRUE, "Failed to write to file.");

        SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN);
    }

    void appendToFile(File::Handle file, void const *data, size_t size)
    {
        HANDLE fileHandle = reinterpret_cast<HANDLE>(File::toValue(file));

        SetFilePointer(fileHandle, 0, NULL, FILE_END);
        DWORD bytesWritten;
        BOOL result = WriteFile(fileHandle, data, static_cast<DWORD>(size), &bytesWritten, NULL);
        IB_ASSERT(result == TRUE, "Failed to write to file.");

        SetFilePointer(fileHandle, 0, NULL, FILE_BEGIN);
    }

    size_t fileSize(File::Handle file)
    {
        HANDLE fileHandle = reinterpret_cast<HANDLE>(File::toValue(file));

        DWORD high;
        DWORD low;
        low = GetFileSize(fileHandle, &high);
        return (static_cast<size_t>(high) << 32) | low;
    }

    bool doesFileExist(char const *filepath)
    {
        return GetFileAttributes(filepath) != INVALID_FILE_ATTRIBUTES;
    }

    bool isDirectory(char const *path)
    {
        return (GetFileAttributes(path) & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    void setWorkingDirectory(char const *path)
    {
        SetCurrentDirectory(path);
    }

    bool iterateDirectory(DirectoryIterator* iterator, FileInfo* fileInfo)
    {
        bool isDone = false;

        WIN32_FIND_DATA findData;
        if (iterator->State == 0)
        {
            HANDLE fileIterator = FindFirstFile(iterator->Path, &findData);
            iterator->State = reinterpret_cast<uintptr_t>(fileIterator);
            if (fileIterator == INVALID_HANDLE_VALUE)
            {
                isDone = true;
            }
        }
        else
        {
            if (FindNextFile(reinterpret_cast<HANDLE>(iterator->State), &findData) == FALSE)
            {
                FindClose(reinterpret_cast<HANDLE>(iterator->State));
                isDone = true;
            }
        }

        // Ignore our recursive directories.
        while(strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0)
        {
            if (FindNextFile(reinterpret_cast<HANDLE>(iterator->State), &findData) == FALSE)
            {
                FindClose(reinterpret_cast<HANDLE>(iterator->State));
                isDone = true;
            }
        }

        static_assert(MaxFilePath == MAX_PATH, "Maximum path values don't match.");
        memcpy(fileInfo->Path, findData.cFileName, MAX_PATH);
        fileInfo->IsDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        return !isDone;
    }

} // namespace IB
