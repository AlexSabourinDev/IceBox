#pragma once

#include <stdint.h>

namespace IB
{
	// Windowing API
	struct Window {};
	struct WindowDesc
	{
		void(*OnCloseRequested)(void*) = nullptr;
		void* CallbackState = nullptr;

		char const* Name = nullptr;
		int32_t Width = 0;
		int32_t Height = 0;
	};

	Window* createWindow(WindowDesc desc);
	void destroyWindow(Window* window);

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
	bool consumeMessageQueue(PlatformMessage* message);
	void sendQuitMessage();
} // namespace IB
