#include <IBEngine/IBRenderer.h>
#include <IBEngine/IBRendererFrontend.h>
#include <IBEngine/IBSerialization.h>
#include <IBEngine/IBLogging.h>
#include <IBEngine/IBPlatform.h>

#include <imgui/imgui.h>

using namespace IB;
namespace
{
    bool primaryWindowHandler(void *data, WindowMessage message, WindowMessagePlatformData const *platformData)
    {
        if (imGuiPlatformMessage(platformData))
        {
            return true;
        }

        switch (message.Type)
        {
        case WindowMessage::Close:
            sendQuitMessage();
            return true;
        }

        return false;
    }
}

int main()
{
    // Create our primary window
    WindowDesc winDesc = {};
    winDesc.Name = "EngineCat";
    winDesc.Width = 1024;
    winDesc.Height = 720;
    winDesc.OnWindowMessage = &primaryWindowHandler;
    WindowHandle window = createWindow(winDesc);

    Serialization::initSerialization();
    initJobSystem();
    initRendererFrontend({ &window });
    initEntitySystem();

    Asset::ResourceHandle cellResource;
    Asset::loadResourceAsync("TestCell.cell", Asset::toFourCC("CELL"), &cellResource);

    bool alive = true;
    while (alive)
    {
        consumeMessageQueue([](void *data, PlatformMessage message) {
            if (message == PlatformMessage::Quit)
            {
                *reinterpret_cast<bool *>(data) = true;
            }
        },
                                &alive);

        newImGuiFrame();

        if (ImGui::BeginMainMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Undo", "CTRL+Z")) {}
                if (ImGui::MenuItem("Redo", "CTRL+Y", false, false)) {}  // Disabled item
                ImGui::Separator();
                if (ImGui::MenuItem("Cut", "CTRL+X")) {}
                if (ImGui::MenuItem("Copy", "CTRL+C")) {}
                if (ImGui::MenuItem("Paste", "CTRL+V")) {}
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        ImGui::Render();

        CellHandle currentCell = InvalidCell;
        // Todo: needs a volatile load. Don't want the compiler to cache our load
        if (Asset::isResourceAssetAvailable(cellResource))
        {
            currentCell = toCellHandle(Asset::getAssetFromResource(cellResource));
        }

        drawCellAsync(currentCell, ImGui::GetDrawData());
    }

    killEntitySystem();
    killRendererFrontend();
    destroyWindow(window);
    killJobSystem();
    Serialization::killSerialization();
}
