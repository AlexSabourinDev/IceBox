#pragma once

#include <stdint.h>

namespace IB
{
	struct Window {};
	struct WindowDesc
	{
		char const* Name = nullptr;
		int32_t Width = 0;
		int32_t Height = 0;
	};

	enum class WindowState
	{
		Active,
		Closing,
	};

	Window* createWindow(WindowDesc desc);
	void destroyWindow(Window* window);
	WindowState tickWindow(Window* window);
} // namespace IB
