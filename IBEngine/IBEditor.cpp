#define _CRT_SECURE_NO_WARNINGS // TODO: Move the string functions to a header and handle the platform differences.

#include "IBEditor.h"
#include "IBPlatform.h"
#include "IBAsset.h"
#include "IBCollection.h"
#include "IBRenderer.h"
#include "IBRendererFrontend.h"
#include "IBEntity.h"
#include "IBJobs.h"

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
            ImGizmo::newFrame();

            if (ImGui::BeginMainMenuBar())
            {
                if (ImGui::BeginMenu("File"))
                {
                    if (*state->CellResource != InvalidResource)
                    {
                        if (ImGui::Button("Save"))
                        {
                            wait(Asset::saveResourceAsync(*state->CellResource));
                        }
                    }
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

    namespace ImGizmo
    {
        namespace
        {
            Mesh::Handle GizmoMesh = InvalidMesh;
            Image::Handle GizmoImage = InvalidImage;
            Material::Handle GizmoMaterial = InvalidMaterial;

            DrawData FrameDrawData;
        }

        void init()
        {
            launchJob([]()
            {
                // Mesh
                {
                    constexpr uint16_t cylinderSegments = 8;
                    constexpr float cylinderLength = 0.75f;
                    constexpr float cylinderRadius = 0.035f;

                    constexpr uint16_t cylinderVertexCount = cylinderSegments * 2;
                    constexpr uint16_t cylinderIndexCount = cylinderSegments * 6;

                    constexpr uint16_t coneSegmentCount = 8;
                    constexpr float coneHeight = 0.25f;
                    constexpr float coneRadius = 0.1f;

                    constexpr uint16_t coneVertexCount = coneSegmentCount + 2;
                    constexpr uint16_t coneIndexCount = coneSegmentCount * 3 + coneSegmentCount * 3;

                    constexpr uint16_t axisVertexCount = (cylinderVertexCount + coneVertexCount);
                    constexpr uint16_t axisIndexCount = (cylinderIndexCount + coneIndexCount);

                    Vertex vertices[axisVertexCount * 3];
                    uint16_t indices[axisIndexCount * 3];

                    for (uint16_t axis = 0; axis < 3; axis++)
                    {
                        float colors[3][4] = {
                            {1.0f, 0.0f, 0.0f, 1.0f},
                            {0.0f, 1.0f, 0.0f, 1.0f},
                            {0.0f, 0.0f, 1.0f, 1.0f} };

                        Vertex *axisVerts = vertices + axisVertexCount * axis;
                        uint16_t *axisIndices = indices + axisIndexCount * axis;

                        float cylinderAngleDelta = IB::Tao / static_cast<float>(cylinderSegments);
                        for (uint32_t i = 0; i < cylinderSegments; i++)
                        {
                            float start[3] = {};
                            float end[3] = {};

                            {
                                float angle = cylinderAngleDelta * i;
                                start[axis] = 0.0f;
                                end[axis] = cylinderLength;
                                end[(axis + 1) % 3] = start[(axis + 1) % 3] = sinf(angle) * cylinderRadius;
                                end[(axis + 2) % 3] = start[(axis + 2) % 3] = cosf(angle) * cylinderRadius;
                            }

                            axisVerts[i * 2] = IB::Vertex{ {start[0], start[1], start[2]} };
                            memcpy(&axisVerts[i * 2].Color, colors[axis], sizeof(float) * 4);
                            axisVerts[i * 2 + 1] = IB::Vertex{ {end[0], end[1], end[2]} };
                            memcpy(axisVerts[i * 2 + 1].Color, colors[axis], sizeof(float) * 4);
                        }

                        axisVerts += cylinderVertexCount;
                        float coneAngleDelta = IB::Tao / static_cast<float>(cylinderSegments);
                        for (uint32_t i = 0; i < coneSegmentCount; i++)
                        {
                            float angle = coneAngleDelta * i;
                            float pos[3];
                            pos[axis] = cylinderLength;
                            pos[(axis + 1) % 3] = sinf(angle) * coneRadius;
                            pos[(axis + 2) % 3] = cosf(angle) * coneRadius;
                            axisVerts[i] = IB::Vertex{ {pos[0], pos[1], pos[2]} };
                            memcpy(axisVerts[i].Color, colors[axis], sizeof(float) * 4);
                        }

                        float pos[3];
                        pos[axis] = cylinderLength;
                        pos[(axis + 1) % 3] = 0.0f;
                        pos[(axis + 2) % 3] = 0.0f;
                        axisVerts[coneSegmentCount] = Vertex{ {pos[0], pos[1], pos[2]} };
                        memcpy(axisVerts[coneSegmentCount].Color, colors[axis], sizeof(float) * 4);

                        pos[axis] = cylinderLength + coneHeight;
                        axisVerts[coneSegmentCount + 1] = Vertex{ {pos[0], pos[1], pos[2]} };
                        memcpy(axisVerts[coneSegmentCount + 1].Color, colors[axis], sizeof(float) * 4);

                        for (uint16_t i = 0; i < cylinderSegments; i++)
                        {
                            uint16_t startEdge = axisVertexCount * axis + i * 2;
                            uint16_t endEdge = axisVertexCount * axis + (i + 1) * 2 % cylinderVertexCount;

                            axisIndices[i * 6] = startEdge;
                            axisIndices[i * 6 + 1] = startEdge + 1;
                            axisIndices[i * 6 + 2] = endEdge + 1;
                            axisIndices[i * 6 + 3] = endEdge + 1;
                            axisIndices[i * 6 + 4] = endEdge;
                            axisIndices[i * 6 + 5] = startEdge;
                        }

                        axisIndices += cylinderIndexCount;
                        for (uint16_t i = 0; i < coneSegmentCount; i++)
                        {
                            uint16_t centerVertex = axisVertexCount * axis + cylinderVertexCount + coneSegmentCount;
                            uint16_t topVertex = axisVertexCount * axis + cylinderVertexCount + coneSegmentCount + 1;

                            uint16_t startVertex = axisVertexCount * axis + cylinderVertexCount + i;
                            uint16_t endVertex = axisVertexCount * axis + cylinderVertexCount + (i + 1) % coneSegmentCount;

                            axisIndices[i * 6] = startVertex;
                            axisIndices[i * 6 + 1] = topVertex;
                            axisIndices[i * 6 + 2] = endVertex;
                            axisIndices[i * 6 + 3] = centerVertex;
                            axisIndices[i * 6 + 4] = startVertex;
                            axisIndices[i * 6 + 5] = endVertex;
                        }
                    }

                    MeshDesc meshDesc = {};
                    meshDesc.Vertices.Data = vertices;
                    meshDesc.Vertices.Count = axisVertexCount * 3;
                    meshDesc.Indices.Data = indices;
                    meshDesc.Indices.Count = axisIndexCount * 3;

                    GizmoMesh = createMesh(meshDesc);
                }

                {
                    uint8_t whiteImageTexels[] = { 255, 255, 255, 255 };

                    IB::ImageDesc whiteImageDesc = {};
                    whiteImageDesc.Format = IB::ImageFormat::RGBA8;
                    whiteImageDesc.Width = 1;
                    whiteImageDesc.Height = 1;
                    whiteImageDesc.Data = whiteImageTexels;
                    GizmoImage = IB::createImage(whiteImageDesc);
                }

                {
                    IB::ForwardDesc gizmoMatDesc = {};
                    gizmoMatDesc.AlbedoTint = IB::toRGBA(1.0f, 1.0f, 1.0f, 1.0f);
                    gizmoMatDesc.AlbedoImage = GizmoImage;
                    GizmoMaterial = IB::createMaterial(gizmoMatDesc);
                }

                return JobResult::Complete;
            }, RendererJobQueueIndex);
        }

        void kill()
        {

        }

        void newFrame()
        {
            FrameDrawData.Instances.clear();
        }

        DrawData const* getDrawData()
        {
            return &FrameDrawData;
        }

        void translation(Float3* position, Quaternion rotation)
        {
            FrameDrawData.Instances.add(DrawData::Instance{ GizmoMesh, GizmoMaterial, Transform{*position, rotation, 1.0f} });
        }
    }
}