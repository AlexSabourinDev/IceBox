#include <IBEngine/Platform/IBPlatform.h>

int main()
{
	IB::WindowDesc winDesc = {};
	winDesc.Name = "Ice Box";
	winDesc.Width = 500;
	winDesc.Height = 150;
	winDesc.OnCloseRequested = [](void*) { IB::sendQuitMessage(); };
	IB::WindowHandle window = IB::createWindow(winDesc);

	IB::PlatformMessage message = IB::PlatformMessage::None;
	while (message != IB::PlatformMessage::Quit)
	{
		IB::consumeMessageQueue(&message);
	}

	IB::destroyWindow(window);
}
