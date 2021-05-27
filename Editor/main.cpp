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

    // TODO: Find a home for these types of functions
    int32_t findLastExtensionIndex(char const* path)
    {
        uint32_t length = static_cast<uint32_t>(strlen(path));
        int32_t extensionIndex = length;
        for (; extensionIndex > 0; extensionIndex--)
        {
            if (path[extensionIndex - 1] == '.')
            {
                break;
            }
        }

        return extensionIndex-1; // Include period
    }
}

int main()
{
    // Create our primary window
    WindowDesc winDesc = {};
    winDesc.Name = "EngineCat";
    winDesc.Width = 4096;
    winDesc.Height = 4096;
    winDesc.OnWindowMessage = &primaryWindowHandler;
    WindowHandle window = createWindow(winDesc);

    Serialization::initSerialization();
    initJobSystem();
    initRendererFrontend({ &window });
    initEntitySystem();

    Asset::ResourceHandle cellResource = {};

    bool alive = true;
    while (alive)
    {
        consumeMessageQueue([](void *data, PlatformMessage message) {
            if (message == PlatformMessage::Quit)
            {
                *reinterpret_cast<bool *>(data) = false;
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

        FileInfo loadingCell;
        if (ImGui::Begin("Asset Browser"))
        {
            DirectoryIterator directoryTree = { "../Assets/Compiled/*" };
            FileInfo fileInfo;
            while (iterateDirectory(&directoryTree, &fileInfo))
            {
                if (fileInfo.IsDirectory)
                {
                    if (ImGui::TreeNode(fileInfo.Path))
                    {
                        ImGui::TreePop();
                    }
                }
                else
                {
                    if (ImGui::Button(fileInfo.Path))
                    {
                        int32_t extensionIndex = findLastExtensionIndex(fileInfo.Path);
                        if (extensionIndex > 0 && strcmp(fileInfo.Path + extensionIndex, ".cell") == 0)
                        {
                            loadingCell = fileInfo;
                            ImGui::OpenPopup("Load Cell?");
                        }
                    }
                }
            }
        }

        if (ImGui::BeginPopupModal("Load Cell?"))
        {
            if (ImGui::Button("OK", ImVec2(120, 0)))
            {
                Asset::loadResourceAsync(loadingCell.Path, Asset::toFourCC("CELL"), &cellResource);
                ImGui::CloseCurrentPopup();
            }

            ImGui::SetItemDefaultFocus();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::End();

        ImGui::Render();

        CellHandle currentCell = InvalidCell;
        // Todo: needs a volatile load. Don't want the compiler to cache our load
        if (Asset::isResourceAssetAvailable(cellResource))
        {
            currentCell = toCellHandle(Asset::getAssetFromResource(cellResource));
        }

        drawCellAsync(currentCell, ImGui::GetDrawData());
    }

    Asset::releaseResourceAsync(cellResource);
    killEntitySystem();
    killRendererFrontend();
    destroyWindow(window);
    killJobSystem();
    Serialization::killSerialization();
}
