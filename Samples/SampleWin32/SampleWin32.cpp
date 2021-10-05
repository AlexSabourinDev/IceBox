#include <IBEngine/IBPlatform.h>
#include <IBEngine/IBLogging.h>

int main()
{
    IB::WindowDesc winDesc = {};
    winDesc.Name = "Ice Box";
    winDesc.Width = 500;
    winDesc.Height = 150;
    winDesc.OnWindowMessage = [](void *, IB::WindowMessage message, IB::WindowMessagePlatformData const* platformData)
    {
        if (message.Type == IB::WindowMessage::Close)
        {
            IB::sendQuitMessage();
            return true;
        }
        else if (message.Type == IB::WindowMessage::Key)
        {
            if (message.Data.Key.Code != IB::Key::Unknown)
            {
                if (message.Data.Key.Alt)
                {
                    char logMessage[8] = {'A','l','t','-'};
                    logMessage[4] = message.Data.Key.Code;
                    logMessage[5] = ' ';
                    logMessage[6] = message.Data.Key.State == IB::Key::Pressed ? 'v' : '^';
                    IB_LOG(IB::LogLevel::Log, "Sample", logMessage);
                }
                else
                {
                    char logMessage[4] = {};
                    logMessage[0] = message.Data.Key.Code;
                    logMessage[1] = ' ';
                    logMessage[2] = message.Data.Key.State == IB::Key::Pressed ? 'v' : '^';
                    IB_LOG(IB::LogLevel::Log, "Sample", logMessage);
                }
            }
            return true;
        }
        else if (message.Type == IB::WindowMessage::MouseClick)
        {
            if (message.Data.MouseClick.State == IB::Mouse::Pressed)
            {
                IB_LOG(IB::LogLevel::Log, "Sample", "Mouse Down");
            }
            else
            {
                IB_LOG(IB::LogLevel::Log, "Sample", "Mouse Up");
            }

            switch (message.Data.MouseClick.Button)
            {
            case IB::Mouse::Left:
                IB_LOG(IB::LogLevel::Log, "Sample", "Mouse Left");
                break;
            case IB::Mouse::Right:
                IB_LOG(IB::LogLevel::Log, "Sample", "Mouse Right");
                break;
            case IB::Mouse::Middle:
                IB_LOG(IB::LogLevel::Log, "Sample", "Mouse Middle");
                break;
            }

            return true;
        }
        else if (message.Type == IB::WindowMessage::MouseMove)
        {
            IB_LOG(IB::LogLevel::Log, "Sample", "Mouse Move");
            return true;
        }

        return false;
    };

    IB::Window::Handle window = IB::createWindow(winDesc);
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
