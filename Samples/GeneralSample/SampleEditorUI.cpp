#include <IBEngine/IBRenderer.h>
#include <IBEngine/IBRendererFrontend.h>
#include <IBEngine/IBSerialization.h>
#include <IBEngine/IBLogging.h>
#include <IBEngine/IBPlatform.h>

#include <imgui/imgui.h>

void sampleEditorUI()
{
    IB::WindowDesc winDesc = {};
    winDesc.Name = "Ice Box";
    winDesc.Width = 1024;
    winDesc.Height = 720;
    winDesc.OnWindowMessage = [](void *data, IB::WindowMessage message, IB::WindowMessagePlatformData const* platformData)
    {
        if (IB::imGuiPlatformMessage(platformData))
        {
            return true;
        }

        switch (message.Type)
        {
        case IB::WindowMessage::Close:
            IB::sendQuitMessage();
            return true;
        }

        return false;;
    };
    IB::Window::Handle window = IB::createWindow(winDesc);

    IB::RendererDesc rendererDesc = {};
    rendererDesc.Window = window;
    IB::initRenderer(rendererDesc);

    IB::MessageQueueState state = IB::MessageQueueState::Continue;
    while (state != IB::MessageQueueState::Quit)
    {
        state = IB::processMessageQueue();

        IB::newImGuiFrame();

        ImGui::Begin("Hello, world!");
        ImGui::Text("This is some useful text.");
        ImGui::End();

        ImGui::Render();

        IB::ViewDesc viewDesc = {};
        viewDesc.ImGuiDrawData = ImGui::GetDrawData();
        IB::drawView(viewDesc);
    }

    IB::killRenderer();
    IB::destroyWindow(window);
}
