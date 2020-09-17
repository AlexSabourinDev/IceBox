#include "IBPlatform.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <assert.h>

namespace
{
	LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		switch (msg)
		{
		case WM_CLOSE:
			DestroyWindow(hwnd);
			break;
		case WM_DESTROY:
			PostQuitMessage(0);
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

		return reinterpret_cast<Window*>(hwnd);
	}

	void destroyWindow(Window* window)
	{
		DestroyWindow(reinterpret_cast<HWND>(window));
	}

	WindowState tickWindow(Window* /*window*/)
	{
		WindowState state = WindowState::Active;

		MSG msg;
		while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
			{
				state = WindowState::Closing;
			}
			else
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}

		return state;
	}
}
