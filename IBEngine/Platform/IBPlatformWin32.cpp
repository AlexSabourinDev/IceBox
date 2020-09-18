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

		void(*OnCloseRequested)(void*) = nullptr;
		void* State = nullptr;
	};

	constexpr uint32_t MaxActiveWindows = 10;
	ActiveWindow ActiveWindows[MaxActiveWindows] = {};

	LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		ActiveWindow* activeWindow = nullptr;
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
}

namespace IB
{
	Window* createWindow(WindowDesc desc)
	{
		HINSTANCE hinstance = GetModuleHandle(NULL);

		WNDCLASS wndClass = {};
		wndClass.lpfnWndProc = WndProc;
		wndClass.hInstance = hinstance;
		wndClass.lpszClassName = desc.Name;
		ATOM classAtom = RegisterClass(&wndClass);
		assert(classAtom != 0);

		RECT rect = { 0, 0, desc.Width, desc.Height };
		BOOL result = AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
		assert(result == TRUE);

		HWND hwnd = CreateWindowEx(
			0,
			desc.Name,
			desc.Name,
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
			NULL,
			NULL,
			hinstance,
			NULL
		);
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

		return reinterpret_cast<Window*>(hwnd);
	}

	void destroyWindow(Window* window)
	{
		DestroyWindow(reinterpret_cast<HWND>(window));
	}

	bool consumeMessageQueue(PlatformMessage* message)
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
}
