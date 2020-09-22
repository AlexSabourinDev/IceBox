#include "IBPlatform.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <assert.h>
#include <stdint.h>

namespace
{
    struct ActiveWindow
    {
        HWND WindowHandle = NULL;

        void (*OnCloseRequested)(void *) = nullptr;
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
		assert(classAtom != 0);

		RECT rect = { 0, 0, desc.Width, desc.Height };
		BOOL result = AdjustWindowRect(&rect, style, FALSE);
		assert(result == TRUE);

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
		assert(hwnd != NULL);

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
				break;
			}
		}
		assert(i < MaxActiveWindows);

		return IB::WindowHandle{ i };
	}
} // namespace

namespace IB
{
    WindowHandle createWindow(WindowDesc desc)
    {
		return createWindowWin32(desc, nullptr, WS_OVERLAPPEDWINDOW);
    }

    void destroyWindow(WindowHandle window)
    {
        DestroyWindow(ActiveWindows[window.value].WindowHandle);
        ActiveWindows[window.value] = {};
    }

    bool consumeMessageQueue(PlatformMessage *message)
    {
        MSG msg;

        bool hasMessage = PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE);
        if (hasMessage)
        {
            if (msg.message == WM_QUIT)
            {
                *message = PlatformMessage::Quit;
            }

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        return hasMessage;
    }

    void sendQuitMessage()
    {
        PostQuitMessage(0);
    }
} // namespace IB

// Bridge
extern "C"
{
	DLL_API void* IB_createWindow(void* parentWindowHandle, const char* name, int width, int height)
	{
		IB::WindowDesc desc = {};
		desc.Name = name;
		desc.Width = width;
		desc.Height = height;
		IB::WindowHandle handle = createWindowWin32(desc, reinterpret_cast<HWND>(parentWindowHandle), DS_CONTROL | WS_CHILD);
		return ActiveWindows[handle.value].WindowHandle;
	}

	DLL_API void IB_destroyWindow(void* windowHandle)
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
