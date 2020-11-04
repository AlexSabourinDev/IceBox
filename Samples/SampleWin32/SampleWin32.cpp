#include <IBEngine/Platform/IBPlatform.h>
#include <IBEngine/IBLogging.h>

int main()
{
    IB::WindowDesc winDesc = {};
    winDesc.Name = "Ice Box";
    winDesc.Width = 500;
    winDesc.Height = 150;
    winDesc.OnCloseRequested = [](void *) { IB::sendQuitMessage(); };
    IB::WindowHandle window = IB::createWindow(winDesc);
    IB_LOG(IB::LogLevel::Log, "Sample", "Window created!");

    IB::PlatformMessage message = IB::PlatformMessage::None;
    while (message != IB::PlatformMessage::Quit)
    {
        IB::consumeMessageQueue([](void *data, IB::PlatformMessage message)
        {
            *reinterpret_cast<IB::PlatformMessage*>(data) = message;
        }, &message);
    }

    IB::destroyWindow(window);
    IB_LOG(IB::LogLevel::Log, "Sample", "Window destroyed!");
}
