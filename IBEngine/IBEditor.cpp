#define _CRT_SECURE_NO_WARNINGS // TODO: Move the string functions to a header and handle the platform differences.

#include "IBEditor.h"
#include "IBPlatform.h"
#include "IBAsset.h"
#include "IBCollection.h"
#include "IBRenderer.h"
#include "IBRendererFrontend.h"
#include "IBEntity.h"
#include "IBJobs.h"
#include "IBLogging.h"

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
    namespace ImGizmo
    {
        void newFrame(Entity::Handle camera);
        void endFrame();
        bool hasFocusedGizmo();
    }

    // Input
    namespace
    {
        namespace Input
        {
            uint32_t PreviousMouseX = 0;
            uint32_t PreviousMouseY = 0;
            uint32_t MouseX = 0;
            uint32_t MouseY = 0;
            bool MouseButtonStates[WindowMessage::Mouse::Button::Count];
            uint32_t MouseButtonFrameCounts[WindowMessage::Mouse::Button::Count];

            bool isButtonClicked(WindowMessage::Mouse::Button button)
            {
                return MouseButtonStates[button] && MouseButtonFrameCounts[button] == 1;
            }

            bool isButtonPressed(WindowMessage::Mouse::Button button)
            {
                return MouseButtonStates[button];
            }

            void newFrame()
            {
                for (uint32_t i = 0; i < WindowMessage::Mouse::Button::Count; i++)
                {
                    if (MouseButtonStates[i])
                    {
                        MouseButtonFrameCounts[i]++;
                    }
                    else
                    {
                        MouseButtonFrameCounts[i] = 0;
                    }
                }
            }

            void endFrame()
            {
                PreviousMouseX = MouseX;
                PreviousMouseY = MouseY;
            }

            bool onWindowMessage(IB::WindowMessage message)
            {
                switch (message.Type)
                {
                case IB::WindowMessage::MouseClick:
                    MouseX = message.Data.MouseClick.X;
                    MouseY = message.Data.MouseClick.Y;
                    MouseButtonStates[message.Data.MouseClick.Button] = message.Data.MouseClick.State == WindowMessage::Mouse::Pressed;
                    return true;
                case IB::WindowMessage::MouseMove:
                    MouseX = message.Data.MouseMove.X;
                    MouseY = message.Data.MouseMove.Y;
                    return true;
                }

                return false;
            }
        }
    }

    namespace Editor
    {
        Entity::Handle CameraHandle;
        uint32_t WindowWidth = 0;
        uint32_t WindowHeight = 0;
        Entity::Handle SelectedEntity;

        Float3 mouseToWorldPos(Mat4x4 const& invViewProj, uint32_t x, uint32_t y, float depth = 0.0f)
        {
            float u = static_cast<float>(x) / static_cast<float>(Editor::WindowWidth)*2.0f - 1.0f;
            float v = static_cast<float>(y) / static_cast<float>(Editor::WindowHeight)*2.0f - 1.0f;

            Float4 ndcRayStart = Float4{ u, v, depth, 1.0f };
            Float4 rayStart4 = mul(invViewProj, ndcRayStart);
            return Float3{ rayStart4.x, rayStart4.y, rayStart4.z } / rayStart4.w;
        };

        struct AssetBrowserState
        {
            FileInfo LoadResourceInfo;
        };

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

        void drawAssetEditor(Asset::FourCC type, Asset::Handle handle, void* parentEditorData)
        {
            for (uint32_t i = 0; i < AssetEditors.count(); i++)
            {
                if (AssetEditors[i].Type == type)
                {
                    AssetEditors[i].Callback(handle, parentEditorData);
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

        Entity::Handle getCamera() { return CameraHandle; }

        void init()
        {
            CameraHandle = createEntity("Editor Camera");
            Property::Handle transformProperty = createTransformProperty();
            addPropertyToEntity(CameraHandle, TransformPropertyType, transformProperty);
            setLocalTransform(transformProperty, { {0.0f, -5.0f, 0.0f}, Quaternion::identity(), 1.0f });

            Camera camera = {};
            Property::Handle cameraProperty = createCameraProperty(camera);
            addPropertyToEntity(CameraHandle, CameraPropertyType, cameraProperty);
        }

        void kill()
        {
            destroyEntity(CameraHandle);
        }

        void frame(Editor::FrameState* state)
        {
            newImGuiFrame();
            Input::newFrame();
            ImGizmo::newFrame(CameraHandle);

            static bool showEditorInspector = false;

            bool openSavePopup = false;
            if (ImGui::BeginMainMenuBar())
            {
                if (ImGui::BeginMenu("File"))
                {
                    if(ImGui::MenuItem("Save", "CTRL+S", false, *state->CellResource != InvalidResource))
                    {
                        openSavePopup = true;
                    }
                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("View"))
                {
                    ImGui::MenuItem("Toggle Editor Inspector", nullptr, &showEditorInspector);
                    ImGui::EndMenu();
                }

                ImGui::EndMainMenuBar();
            }

            // Only allow popup saving when we're not in the modal dialog
            if (!ImGui::IsPopupOpen("Save?"))
            {
                bool const saveShortcut = ImGui::IsKeyPressed('S', false) && (ImGui::GetIO().KeyMods & ImGuiKeyModFlags_Ctrl) != 0;
                if (openSavePopup || saveShortcut)
                {
                    ImGui::OpenPopup("Save?");
                }
            }

            if (ImGui::BeginPopupModal("Save?"))
            {
                if (ImGui::Button("Yes", ImVec2(120, 0)))
                {
                    wait(Asset::saveResourceAsync(*state->CellResource));
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SameLine();
                if (ImGui::Button("No", ImVec2(120, 0)))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
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

                        Asset::loadResourceAsync(assetBrowserState.LoadResourceInfo.Path, CellType,
                            [](void *data, Resource::Handle resource, Asset::ResourceLoad::State loadState)
                            {
                                if (loadState >= Asset::ResourceLoad::Loading)
                                {
                                    *reinterpret_cast<Resource::Handle *>(data) = resource;
                                }
                            }, state->CellResource);

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
                            SelectedEntity = entities[i];
                        }
                    }

                    if (ImGui::BeginPopupContextWindow())
                    {
                        if (ImGui::MenuItem("Create Entity"))
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
                if (SelectedEntity != InvalidEntity)
                {
                    drawAssetEditor(EntityType, toAssetHandle(SelectedEntity));
                }
            }
            ImGui::End();

            if (showEditorInspector)
            {
                if (ImGui::Begin("Editor Inspector", &showEditorInspector))
                {
                    drawAssetEditor(EntityType, toAssetHandle(CameraHandle));
                }
                ImGui::End();
            }

            // The order of events for click handling is:
            // - ImGui
            // - ImGizmo
            // - Editor picking
            if (Input::isButtonClicked(WindowMessage::Mouse::Left)
                && !ImGui::GetIO().WantCaptureMouse
                && !ImGizmo::hasFocusedGizmo())
            {
                CameraProperty::Handle cameraProperty = toCameraHandle(getPropertyFromEntity(CameraHandle, CameraPropertyType));
                IB_ASSERT(cameraProperty != InvalidProperty, "Camera handle should have a camera property!");

                Transform cameraTransform = {};
                Mat4x4 invViewProj = Mat4x4::identity();
                if (cameraProperty != InvalidProperty)
                {
                    Property::Handle transformProperty = getPropertyFromEntity(CameraHandle, TransformPropertyType);
                    if (transformProperty != InvalidProperty)
                    {
                        cameraTransform = getLocalTransform(transformProperty);
                    }

                    invViewProj = getInvViewProjection(cameraProperty, cameraTransform);
                }

                Cell::Handle currentCell = InvalidCell;
                if (Asset::isResourceAssetAvailable(*state->CellResource) && *state->CellResource != InvalidResource)
                {
                    currentCell = toCellHandle(Asset::getAssetFromResource(*state->CellResource));

                    Entity::Handle *entities;
                    uint32_t entityCount;
                    getEntityList(currentCell, &entities, &entityCount);

                    Float3 rayStartWorld = mouseToWorldPos(invViewProj, Input::MouseX, Input::MouseY, 0.0f);
                    Float3 rayEndWorld = mouseToWorldPos(invViewProj, Input::MouseX, Input::MouseY, 1.0f);

                    float closestDistance = FLT_MAX;
                    Entity::Handle closestEntity = InvalidEntity;

                    for (uint32_t e = 0; e < entityCount; e++)
                    {
                        Property::Handle rendererHandle = getPropertyFromEntity(entities[e], RendererPropertyType);
                        SimdTriangleSet* triangleSet;
                        uint32_t triangleCount;
                        if (tryGetTriangleSet(rendererHandle, &triangleSet, &triangleCount))
                        {
                            Transform entityToWorldTransform = {};
                            Property::Handle transformProperty = getPropertyFromEntity(entities[e], TransformPropertyType);
                            if (transformProperty != InvalidProperty)
                            {
                                entityToWorldTransform = getLocalTransform(transformProperty);
                            }

                            Float3 rayStartEntity = inverseTransformPoint(entityToWorldTransform, rayStartWorld);
                            Float3 rayEndEntity = inverseTransformPoint(entityToWorldTransform, rayEndWorld);

                            Float3 rayStartToEnd = rayEndEntity - rayStartEntity;
                            float rayLength = length(rayStartToEnd);
                            Float3 rayDir = rayStartToEnd / rayLength;

                            SimdFloat3 simdRayStart = splat(rayStartEntity);
                            SimdFloat3 simdRayDir = splat(rayDir);
                            SimdFloat simdRayLength = splat(rayLength);

                            for (uint32_t tr = 0; tr < triangleCount; tr += SimdLaneCount, triangleSet++)
                            {
                                uint32_t trianglesInSet = min(triangleCount - tr, SimdLaneCount);
                                SimdRayTriangleResult rayTriangleResult = rayTriangleIntersection(simdRayStart, simdRayDir, SimdZero, simdRayLength, *triangleSet, trianglesInSet);
                                for (uint32_t p = 0; p < rayTriangleResult.Count; p++)
                                {
                                    Float3 worldPoint = transformPoint(entityToWorldTransform, rayTriangleResult.Points[p]);
                                    float distance = distanceSqr(worldPoint, cameraTransform.Position);
                                    if (distance < closestDistance)
                                    {
                                        closestDistance = distance;
                                        closestEntity = entities[e];
                                    }
                                }
                            }
                        }
                    }

                    SelectedEntity = closestEntity;
                }
            }

            Input::endFrame();
            ImGizmo::endFrame();
            ImGui::Render();
        }

        bool isSelected(Entity::Handle entity)
        {
            return SelectedEntity == entity;
        }

        bool onWindowMessage(IB::WindowMessage message)
        {
            if (Input::onWindowMessage(message))
            {
                return true;
            }

            switch (message.Type)
            {
            case WindowMessage::Resize:
                WindowWidth = message.Data.Resize.Width;
                WindowHeight = message.Data.Resize.Height;
                return true;
            case WindowMessage::Create:
                WindowWidth = message.Data.Create.Width;
                WindowHeight = message.Data.Create.Height;
                return true;
            }

            return false;
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

            Entity::Handle FrameCamera;

            void* FocusedGizmo = nullptr;

            bool canFocus()
            {
                return !ImGui::GetIO().WantCaptureMouse;
            }
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

        void newFrame(Entity::Handle camera)
        {
            FrameDrawData.Instances.clear();
            FrameCamera = camera;
        }

        void endFrame()
        {
        }

        DrawData const* getDrawData()
        {
            return &FrameDrawData;
        }

        bool hasFocusedGizmo()
        {
            return FocusedGizmo != nullptr;
        }

        void translation(Float3* position, Quaternion rotation, TranslationGizmo* state)
        {
            Transform cameraTransform = {};
            Property::Handle transformProperty = getPropertyFromEntity(FrameCamera, TransformPropertyType);
            if (transformProperty != InvalidProperty)
            {
                cameraTransform = getLocalTransform(transformProperty);
            }

            CameraProperty::Handle cameraProperty = toCameraHandle(getPropertyFromEntity(FrameCamera, CameraPropertyType));
            IB_ASSERT(cameraProperty != InvalidProperty, "Camera handle should have a camera property!");

            Mat4x4 invViewProj = Mat4x4::identity();
            if (cameraProperty != InvalidProperty)
            {
                invViewProj = getInvViewProjection(cameraProperty, cameraTransform);
            }

            Float3 axies[3] =
            {
                rotate(rotation, Float3{1.0f, 0.0f, 0.0f}),
                rotate(rotation, Float3{0.0f, 1.0f, 0.0f}),
                rotate(rotation, Float3{0.0f, 0.0f, 1.0f}),
            };

            if (canFocus())
            {
                if (!Input::isButtonPressed(WindowMessage::Mouse::Left))
                {
                    state->ActiveAxis = 3;

                        // Clear out our focus if we were active
                        if (FocusedGizmo == state)
                        {
                            FocusedGizmo = nullptr;
                        }
                }
                else
                {
                    if (Input::isButtonClicked(WindowMessage::Mouse::Left))
                    {
                        Float3 rayStart = Editor::mouseToWorldPos(invViewProj, Input::MouseX, Input::MouseY, 0.0f);
                        Float3 rayEnd = Editor::mouseToWorldPos(invViewProj, Input::MouseX, Input::MouseY, 1.0f);

                        for (uint32_t i = 0; i < 3; i++)
                        {
                            if (doesLineCylinderIntersect(rayStart, rayEnd, *position, *position + axies[i], 0.1f))
                            {
                                // Set our gizmo as the active gizmo
                                FocusedGizmo = state;

                                state->ActiveAxis = i;
                                break;
                            }
                        }
                    }
                    else if (state->ActiveAxis < 3)
                    {
                        Float3 previousPos = Editor::mouseToWorldPos(invViewProj, Input::PreviousMouseX, Input::PreviousMouseY) - cameraTransform.Position;
                        Float3 nextPos = Editor::mouseToWorldPos(invViewProj, Input::MouseX, Input::MouseY) - cameraTransform.Position;

                        // Intersect our ray with the plane defined by our axies

                        // Given our current ray direction, pick our axis normal that is most parallel
                        // This will give us the most perpendicular plane
                        float angles[] =
                        {
                            fabsf(dot(nextPos, axies[(state->ActiveAxis + 1) % 3])),
                            fabsf(dot(nextPos, axies[(state->ActiveAxis + 2) % 3]))
                        };

                        uint32_t largest = angles[0] > angles[1] ? 0 : 1;
                        uint32_t planeNormal = (state->ActiveAxis + largest + 1) % 3;

                        float planeDistance = dot(*position - cameraTransform.Position, axies[planeNormal]);

                        // Cast a ray from our camera origin to our axis plane
                        previousPos = intersectRayPlane(axies[planeNormal], planeDistance, previousPos, Float3{});
                        nextPos = intersectRayPlane(axies[planeNormal], planeDistance, nextPos, Float3{});

                        // Project our offset along our active axis for our delta
                        *position += axies[state->ActiveAxis] * dot(nextPos - previousPos, axies[state->ActiveAxis]);
                    }
                }
            }
            

            FrameDrawData.Instances.add(DrawData::Instance{ GizmoMesh, GizmoMaterial, Transform{*position, rotation, 1.0f} });
        }
    }
}