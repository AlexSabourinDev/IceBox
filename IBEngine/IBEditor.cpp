#define _CRT_SECURE_NO_WARNINGS // TODO: Move the string functions to a header and handle the platform differences.

#include "IBEditor.h"
#include "IBPlatform.h"
#include "IBAsset.h"
#include "IBCollection.h"
#include "IBRenderer.h"
#include "IBEntity.h"

#include <imgui/imgui.h>

namespace
{
    bool hasExtension(char const *path, char const *extension)
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

        extensionIndex--; // Include period

        return extensionIndex > 0 && strcmp(path + extensionIndex, extension) == 0;
    }
}

namespace IB
{
    namespace Editor
    {
        struct AssetBrowserState
        {
            FileInfo LoadResourceInfo;
        };

        struct
        {
            struct
            {
                union
                {
                    Asset::Handle Asset = InvalidAsset;
                    Resource::Handle Resource;
                };
                bool IsResource = false;
                Asset::FourCC Type;
            } Selected;
        } InspectorState;

        struct AssetEditor
        {
            Asset::FourCC Type;
            AssetEditorCallback* Callback;
        };
        DynamicArray<AssetEditor> AssetEditors;

        void addAssetEditor(Asset::FourCC type, AssetEditorCallback* frame)
        {
            AssetEditors.add(type, frame);
        }

        void drawAssetEditor(Asset::FourCC type, Asset::Handle handle)
        {
            for (uint32_t i = 0; i < AssetEditors.count(); i++)
            {
                if (AssetEditors[i].Type == type)
                {
                    AssetEditors[i].Callback(handle);
                    break;
                }
            }
        }

        void assetBrowserFileTree(char const* rootPath, char const* folder, AssetBrowserState* assetBrowser)
        {
            char directoryPath[MaxFilePath] = { 0 };
            sprintf(directoryPath, "%s%s/", rootPath, folder);

            char searchPath[MaxFilePath] = { 0 };
            sprintf(searchPath, "%s*", directoryPath);

            DirectoryIterator directoryTree = { searchPath };
            FileInfo fileInfo;
            while (iterateDirectory(&directoryTree, &fileInfo))
            {
                if (fileInfo.IsDirectory)
                {
                    if (ImGui::TreeNode(fileInfo.Path))
                    {
                        assetBrowserFileTree(directoryPath, fileInfo.Path, assetBrowser);
                        ImGui::TreePop();
                    }
                }
                else
                {
                    ImGui::TreeNodeEx(fileInfo.Path, ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
                    if (ImGui::IsItemClicked() && hasExtension(fileInfo.Path, ".cell"))
                    {
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            assetBrowser->LoadResourceInfo = fileInfo;
                            ImGui::OpenPopup("Load Cell?");
                        }
                    }
                }
            }
        };

        void frame(Editor::FrameState* state)
        {
            newImGuiFrame();

            if (ImGui::BeginMainMenuBar())
            {
                if (ImGui::BeginMenu("File"))
                {
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Edit"))
                {
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            }

            if (ImGui::Begin("Asset Browser"))
            {
                static AssetBrowserState assetBrowserState = {};
                assetBrowserFileTree("../Assets/", "Compiled", &assetBrowserState);

                if (ImGui::BeginPopupModal("Load Cell?"))
                {
                    if (ImGui::Button("OK", ImVec2(120, 0)))
                    {
                        if (*state->CellResource != InvalidResource)
                        {
                            Asset::releaseResourceAsync(*state->CellResource);
                        }

                        Asset::loadResourceAsync(assetBrowserState.LoadResourceInfo.Path, Asset::toFourCC("CELL"),
                            [](void *data, Resource::Handle resource, Asset::ResourceLoad::State loadState)
                            {
                                if (loadState >= Asset::ResourceLoad::Loading)
                                {
                                    *reinterpret_cast<Resource::Handle *>(data) = resource;
                                }
                            }, state->CellResource);
                        InspectorState.Selected.Resource = *state->CellResource;
                        InspectorState.Selected.IsResource = true;
                        InspectorState.Selected.Type = Asset::toFourCC("CELL");

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
            }
            ImGui::End();

            if (ImGui::Begin("Cell"))
            {
                if (Asset::isResourceAssetAvailable(*state->CellResource))
                {
                    Cell::Handle currentCell = toCellHandle(Asset::getAssetFromResource(*state->CellResource));

                    Entity::Handle *entities;
                    uint32_t entityCount;
                    getEntityList(currentCell, &entities, &entityCount);

                    for (uint32_t i = 0; i < entityCount; i++)
                    {
                        ImGui::TreeNodeEx(getEntityName(entities[i]), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
                        if (ImGui::IsItemClicked())
                        {
                            InspectorState.Selected.Asset = toAssetHandle(entities[i]);
                            InspectorState.Selected.IsResource = false;
                            InspectorState.Selected.Type = Asset::toFourCC("ENTT");
                        }
                    }

                    if (ImGui::BeginPopupContextWindow())
                    {
                        if (ImGui::Button("Create Entity"))
                        {
                            addEntityToCell(currentCell, createEntity());
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }
                }
            }
            ImGui::End();

            if (ImGui::Begin("Inspector"))
            {
                Asset::Handle assetHandle = InvalidAsset;
                if (InspectorState.Selected.IsResource)
                {
                    if (Asset::isResourceAssetAvailable(InspectorState.Selected.Resource))
                    {
                        assetHandle = Asset::getAssetFromResource(InspectorState.Selected.Resource);
                    }
                }
                else
                {
                    assetHandle = InspectorState.Selected.Asset;
                }

                if (assetHandle != InvalidAsset)
                {
                    drawAssetEditor(InspectorState.Selected.Type, assetHandle);
                }
            }
            ImGui::End();

            ImGui::Render();
        }
    } // namespace Editor
}