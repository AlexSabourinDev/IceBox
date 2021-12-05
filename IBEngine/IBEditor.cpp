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
#include "IBGeometryToolkit.h"

#include <imgui/imgui.h>
#include <stdio.h>

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
            Int2 PreviousMousePos = {};
            Int2 MousePos = {};
            bool MouseButtonStates[Mouse::Button::Count];
            uint32_t MouseButtonFrameCounts[Mouse::Button::Count];

            bool KeyButtonStates[Key::Code::Count];
            uint32_t KeyButtonFrameCounts[Key::Code::Count];

            bool isButtonClicked(Mouse::Button button)
            {
                return MouseButtonStates[button] && MouseButtonFrameCounts[button] == 1;
            }

            bool isButtonPressed(Mouse::Button button)
            {
                return MouseButtonStates[button];
            }

            bool isButtonClicked(Key::Code key)
            {
                return KeyButtonStates[key] && KeyButtonFrameCounts[key] == 1;
            }

            bool isButtonPressed(Key::Code key)
            {
                return KeyButtonStates[key];
            }

            void newFrame()
            {
                for (uint32_t i = 0; i < Mouse::Button::Count; i++)
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

                for (uint32_t i = 0; i < Key::Code::Count; i++)
                {
                    if (KeyButtonStates[i])
                    {
                        KeyButtonFrameCounts[i]++;
                    }
                    else
                    {
                        KeyButtonFrameCounts[i] = 0;
                    }
                }
            }

            void endFrame()
            {
                PreviousMousePos = MousePos;
            }

            bool onWindowMessage(IB::WindowMessage message)
            {
                switch (message.Type)
                {
                case IB::WindowMessage::MouseClick:
                    MousePos.x = static_cast<int32_t>(message.Data.MouseClick.X);
                    MousePos.y = static_cast<int32_t>(message.Data.MouseClick.Y);
                    MouseButtonStates[message.Data.MouseClick.Button] = message.Data.MouseClick.State == Mouse::Pressed;
                    return true;
                case IB::WindowMessage::Key:
                    KeyButtonStates[message.Data.Key.Code] = message.Data.Key.State == Key::Pressed;
                    return true;
                case IB::WindowMessage::MouseMove:
                    MousePos.x = static_cast<int32_t>(message.Data.MouseMove.X);
                    MousePos.y = static_cast<int32_t>(message.Data.MouseMove.Y);
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
        bool EditorFocus = false;

        Float3 mouseToWorldPos(Mat4x4 const& invViewProj, Int2 mousePos, float depth = 0.0f)
        {
            float u = static_cast<float>(mousePos.x) / static_cast<float>(Editor::WindowWidth)*2.0f - 1.0f;
            float v = static_cast<float>(mousePos.y) / static_cast<float>(Editor::WindowHeight)*2.0f - 1.0f;

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
            camera.Aspect = static_cast<float>(WindowWidth) / static_cast<float>(WindowHeight);
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
            ImGeo::newFrame();
            ImGizmo::newFrame(CameraHandle);

            ImGeo::grid(ImGeo::state(), 100, 100); 

            static bool showEditorInspector = false;
            static bool showSettings = false;

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
                    ImGui::MenuItem("Editor Inspector", nullptr, &showEditorInspector);
                    ImGui::MenuItem("Editor Settings", nullptr, &showSettings);
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

            if (ImGui::BeginPopupModal("Save?", nullptr, ImGuiWindowFlags_NoResize))
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

                if (ImGui::BeginPopupModal("Load Cell?", nullptr, ImGuiWindowFlags_NoResize))
                {
                    if (ImGui::Button("OK", ImVec2(120, 0)))
                    {
                        if (*state->CellResource != InvalidResource)
                        {
                            SelectedEntity = InvalidEntity;
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

            struct EditorSettings
            {
                float CameraPanSpeed = 180.0f * 0.5f;
                float CameraSpeed = 0.25f;
                float CameraSlowSpeed = 0.05f;
            };

            static EditorSettings settings;
            if (showSettings)
            {
                if (ImGui::Begin("Editor Settings", &showSettings))
                {
                    if (ImGui::BeginTable("Inspector Table", 2))
                    {
                        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
                        ImGui::TableSetupColumn("Data", ImGuiTableColumnFlags_None);

                        ImGui::TableNextColumn();
                        ImGui::Text("Camera Pan Speed (Degrees)");

                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        ImGui::InputFloat("#CameraPanSpeed", &settings.CameraPanSpeed);

                        ImGui::TableNextColumn();
                        ImGui::Text("Camera Speed (m/s)");

                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        ImGui::InputFloat("#CameraSpeed", &settings.CameraSpeed);

                        ImGui::TableNextColumn();
                        ImGui::Text("Camera Slow  Speed (m/s)");

                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        ImGui::InputFloat("#CameraSlowSpeed", &settings.CameraSlowSpeed);

                        ImGui::EndTable();
                    }
                }
                ImGui::End();
            }

            // The order of events for click handling is:
            // - ImGui
            // - ImGizmo
            // - Editor render window functionality

            if(SelectedEntity != InvalidEntity)
            {
                Property::Handle transformProperty = getPropertyFromEntity(SelectedEntity, TransformPropertyType);
                if (transformProperty != InvalidProperty)
                {
                    Transform selectedTransform = getLocalTransform(transformProperty);

                    Property::Handle lineSweepProperty = getPropertyFromEntity(SelectedEntity, LineSweepPropertyType);
                    if (lineSweepProperty != InvalidProperty)
                    {
                        bool lineSweepChanged = false;

                        LineSweepDesc* sweep = getLineSweep(lineSweepProperty);
                        for (uint32_t i = 0; i < sweep->Points.count(); i++)
                        {
                            static ImGizmo::TransformGizmo transformState;

                            Transform worldTransform = applyTransform(selectedTransform, sweep->Points[i]);
                            lineSweepChanged |= ImGizmo::transform(&worldTransform, &transformState, i);
                            sweep->Points[i] = applyInverseTransform(selectedTransform, worldTransform);
                        }

                        if (lineSweepChanged)
                        {
                            Job::Handle job = updateLineSweepAsync(SelectedEntity, *sweep);
                            wait(job); // Currently just plain waiting on the job. Likely want to run in parallel to the editor code
                        }
                    }
                    else
                    {
                        static ImGizmo::TransformGizmo transformState;
                        ImGizmo::transform(&selectedTransform, &transformState);

                        setLocalTransform(transformProperty, selectedTransform);
                    }
                }
            }

            const bool canWindowConsumeInput = !ImGui::GetIO().WantCaptureMouse && !ImGizmo::hasFocusedGizmo();
            if (canWindowConsumeInput)
            {
                // Camera flycam
                EditorFocus = Input::isButtonPressed(Mouse::Right);
                if (Input::isButtonPressed(Mouse::Right))
                {
                    Property::Handle transformProperty = getPropertyFromEntity(CameraHandle, TransformPropertyType);
                    if (transformProperty != InvalidProperty)
                    {
                        Transform cameraTransform = getLocalTransform(transformProperty);

                        float const rotationSpeed = settings.CameraPanSpeed * DegToRad;
                        Float2 mouseDelta = static_cast<Float2>(Input::MousePos - Input::PreviousMousePos) / Float2{ static_cast<float>(WindowWidth), static_cast<float>(WindowHeight) };
                        mouseDelta = mouseDelta * 2.0f;
                        cameraTransform.Rotation = fromAxisAngle(Float3{ 0.0f, 0.0f, 1.0f }, -mouseDelta.x * rotationSpeed) * cameraTransform.Rotation * fromAxisAngle(Float3{1.0f, 0.0f, 0.0f}, -mouseDelta.y * rotationSpeed);

                        Float3 cameraDirection = {};
                        if (Input::isButtonPressed(Key::W))
                        {
                            cameraDirection.y = 1.0f;
                        }
                        else if (Input::isButtonPressed(Key::S))
                        {
                            cameraDirection.y = -1.0f;
                        }

                        if (Input::isButtonPressed(Key::D))
                        {
                            cameraDirection.x = 1.0f;
                        }
                        else if (Input::isButtonPressed(Key::A))
                        {
                            cameraDirection.x = -1.0f;
                        }

                        const float cameraDirectionLength = length(cameraDirection);
                        if (cameraDirectionLength > 0.0f)
                        {
                            cameraDirection = normalize(cameraDirection, cameraDirectionLength);
                            float const cameraSpeed = Input::isButtonPressed(Key::Shift) ? settings.CameraSlowSpeed : settings.CameraSpeed;

                            cameraTransform.Position += rotate(cameraTransform.Rotation, cameraDirection * cameraSpeed);
                        }

                        setLocalTransform(transformProperty, cameraTransform);
                    }
                }

                // Scene picking
                if (Input::isButtonClicked(Mouse::Left))
                {
                    CameraProperty::Handle cameraProperty = toCameraHandle(getPropertyFromEntity(CameraHandle, CameraPropertyType));
                    IB_ASSERT(cameraProperty != InvalidCameraProperty, "Camera handle should have a camera property!");

                    Transform cameraTransform = {};
                    Mat4x4 invViewProj = Mat4x4::identity();
                    if (cameraProperty != InvalidCameraProperty)
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

                        Float3 rayStartWorld = mouseToWorldPos(invViewProj, Input::MousePos, 0.0f);
                        Float3 rayEndWorld = mouseToWorldPos(invViewProj, Input::MousePos, 1.0f);

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

            }

            Input::endFrame();
            ImGizmo::endFrame();
            ImGeo::endFrame();
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
            {
                WindowWidth = message.Data.Resize.Width;
                WindowHeight = message.Data.Resize.Height;

                CameraProperty::Handle cameraProperty = toCameraHandle(getPropertyFromEntity(CameraHandle, CameraPropertyType));
                Camera camera = IB::getCamera(cameraProperty);
                camera.Aspect = static_cast<float>(WindowWidth) / static_cast<float>(WindowHeight);
                setCamera(cameraProperty, camera);
                return true;
            }
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
            constexpr float RotationCircleRadius = 1.0f;

            Mesh::Handle TranslationGizmoMesh = InvalidMesh;
            Mesh::Handle RotationGizmoMesh = InvalidMesh;

            Image::Handle GizmoImage = InvalidImage;
            Material::Handle GizmoMaterial = InvalidMaterial;

            Entity::Handle FrameCamera;

            void* FocusedGizmo = nullptr;
            uint32_t FocusedGizmoId = 0;

            bool isFocused(void *gizmo, uint32_t gizmoId)
            {
                bool isFocused = false;
                if (FocusedGizmo != nullptr)
                {
                    isFocused = FocusedGizmo == gizmo && FocusedGizmoId == gizmoId;
                }

                return isFocused;
            }

            bool canFocus(void* gizmo, uint32_t gizmoId)
            {
                bool canFocus = !ImGui::GetIO().WantCaptureMouse && !Editor::EditorFocus;
                if (canFocus)
                {
                    if (FocusedGizmo != nullptr)
                    {
                        canFocus = isFocused(gizmo, gizmoId);
                    }
                    else
                    {
                        canFocus = true; // We can focus if there's no focused gizmo
                    }
                }
                return canFocus;
            }

            void setFocus(void *gizmo, uint32_t gizmoId)
            {
                FocusedGizmo = gizmo;
                FocusedGizmoId = gizmoId;
            }
        }

        void init()
        {
            static Float4 constexpr gizmoColors[3] = {
                {1.0f, 0.0f, 0.0f, 0.75f},
                {0.0f, 1.0f, 0.0f, 0.75f},
                {0.0f, 0.0f, 1.0f, 0.75f} };

            launchJob([](Job::Handle /*currentJob*/)
            {
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
            }, NoJobType, RendererJobQueueIndex);

            launchJob([](Job::Handle /*currentJob*/)
            {
                // Mesh
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

                ImGeo::State state = {};
                for (uint16_t axis = 0; axis < 3; axis++)
                {
                    state.Vertices = vertices;
                    state.Indices = indices;

                    state.Transform = Transform{};
                    Float3 rotations[3] = 
                        {{0.0f, 0.0f, 0.0f},
                         {0.0f, 0.0f, Pi * 0.5f},
                         {0.0f, -Pi * 0.5f, 0.0f}};
                    state.Transform.Rotation = fromEulerAngles(rotations[axis]);

                    state.VertexColor = gizmoColors[axis];
                    ImGeo::tube(&state,
                        cylinderLength,
                        cylinderRadius,
                        cylinderSegments);

                    state.Transform.Position = Float3{};
                    state.Transform.Position[axis] = cylinderLength;
                    ImGeo::cone(&state,
                               coneHeight,
                               coneRadius,
                               coneSegmentCount);
                }

                MeshDesc meshDesc = {};
                meshDesc.Vertices.Data = state.Vertices;
                meshDesc.Vertices.Count = state.VertexCount;
                meshDesc.Indices.Data = state.Indices;
                meshDesc.Indices.Count = state.IndexCount;

                TranslationGizmoMesh = createMesh(meshDesc);

                return JobResult::Complete;
            }, NoJobType, RendererJobQueueIndex);

            // Rotation Gizmo
            launchJob([](Job::Handle /*currentJob*/)
            {
                constexpr uint32_t circleSegments = 180;
                constexpr uint32_t circleTriangleCount = circleSegments * 2;
                constexpr uint32_t circleVertexCount = circleSegments * 4;
                constexpr uint32_t circleIndexCount = circleTriangleCount * 3;
                constexpr float circleThickness = 0.15f;
                constexpr float circleInnerRadius = RotationCircleRadius - circleThickness;
                Vertex vertices[circleVertexCount * 3];
                uint16_t indices[circleIndexCount * 3];

                constexpr uint32_t axisIndices[3][2] = 
                {
                    {0, 1},
                    {1, 2},
                    {0, 2}
                };

                uint32_t vertexWritePos = 0;
                uint32_t indexWritePos = 0;
                for (uint32_t axis = 0; axis < 3; axis++)
                {
                    float deltaAngle = 2.0f * Pi / static_cast<float>(circleSegments);
                    for (uint32_t i = 0; i < circleSegments; i++)
                    {
                        float angle0 = deltaAngle * i;
                        float angle1 = deltaAngle * (i + 1);

                        Float3 quad[4] = {};
                        quad[0][axisIndices[axis][0]] = cosf(angle0) * circleInnerRadius;
                        quad[0][axisIndices[axis][1]] = sinf(angle0) * circleInnerRadius;

                        quad[1][axisIndices[axis][0]] = cosf(angle0) * RotationCircleRadius;
                        quad[1][axisIndices[axis][1]] = sinf(angle0) * RotationCircleRadius;

                        quad[2][axisIndices[axis][0]] = cosf(angle1) * RotationCircleRadius;
                        quad[2][axisIndices[axis][1]] = sinf(angle1) * RotationCircleRadius;

                        quad[3][axisIndices[axis][0]] = cosf(angle1) * circleInnerRadius;
                        quad[3][axisIndices[axis][1]] = sinf(angle1) * circleInnerRadius;

                        vertices[vertexWritePos + i * 4 + 0].Pos = Float4{ quad[0].x, quad[0].y, quad[0].z, 0.0f };
                        vertices[vertexWritePos + i * 4 + 1].Pos = Float4{ quad[1].x, quad[1].y, quad[1].z, 0.0f };
                        vertices[vertexWritePos + i * 4 + 2].Pos = Float4{ quad[2].x, quad[2].y, quad[2].z, 0.0f };
                        vertices[vertexWritePos + i * 4 + 3].Pos = Float4{ quad[3].x, quad[3].y, quad[3].z, 0.0f };
                        vertices[vertexWritePos + i * 4 + 0].Normal.w = bitcast<float>(packRGBA(gizmoColors[axis]));
                        vertices[vertexWritePos + i * 4 + 1].Normal.w = bitcast<float>(packRGBA(gizmoColors[axis]));
                        vertices[vertexWritePos + i * 4 + 2].Normal.w = bitcast<float>(packRGBA(gizmoColors[axis]));
                        vertices[vertexWritePos + i * 4 + 3].Normal.w = bitcast<float>(packRGBA(gizmoColors[axis]));
                    }

                    for (uint32_t i = 0; i < circleSegments; i++)
                    {
                        indices[indexWritePos + i * 6 + 0] = static_cast<uint16_t>(vertexWritePos + i * 4 + 0);
                        indices[indexWritePos + i * 6 + 1] = static_cast<uint16_t>(vertexWritePos + i * 4 + 1);
                        indices[indexWritePos + i * 6 + 2] = static_cast<uint16_t>(vertexWritePos + i * 4 + 2);
                        indices[indexWritePos + i * 6 + 3] = static_cast<uint16_t>(vertexWritePos + i * 4 + 0);
                        indices[indexWritePos + i * 6 + 4] = static_cast<uint16_t>(vertexWritePos + i * 4 + 2);
                        indices[indexWritePos + i * 6 + 5] = static_cast<uint16_t>(vertexWritePos + i * 4 + 3);
                    }

                    vertexWritePos += circleSegments * 4;
                    indexWritePos += circleSegments * 6;
                }

                MeshDesc meshDesc = {};
                meshDesc.Vertices.Data = vertices;
                meshDesc.Vertices.Count = circleVertexCount * 3;
                meshDesc.Indices.Data = indices;
                meshDesc.Indices.Count = circleIndexCount * 3;

                RotationGizmoMesh = createMesh(meshDesc);

                return JobResult::Complete;
            }, NoJobType, RendererJobQueueIndex);
        }

        void kill()
        {

        }

        void newFrame(Entity::Handle camera)
        {
            FrameCamera = camera;
        }

        void endFrame()
        {
        }

        bool hasFocusedGizmo()
        {
            return FocusedGizmo != nullptr;
        }

        bool translation(Float3 *position, Quaternion rotation, TranslationGizmo *state, uint32_t id)
        {
            bool translationChanged = false;

            Transform cameraTransform = {};
            Property::Handle transformProperty = getPropertyFromEntity(FrameCamera, TransformPropertyType);
            if (transformProperty != InvalidProperty)
            {
                cameraTransform = getLocalTransform(transformProperty);
            }

            CameraProperty::Handle cameraProperty = toCameraHandle(getPropertyFromEntity(FrameCamera, CameraPropertyType));
            IB_ASSERT(cameraProperty != InvalidCameraProperty, "Camera handle should have a camera property!");

            Mat4x4 invViewProj = Mat4x4::identity();
            if (cameraProperty != InvalidCameraProperty)
            {
                invViewProj = getInvViewProjection(cameraProperty, cameraTransform);
            }

            Float3 axies[3] =
            {
                rotate(rotation, Float3{1.0f, 0.0f, 0.0f}),
                rotate(rotation, Float3{0.0f, 1.0f, 0.0f}),
                rotate(rotation, Float3{0.0f, 0.0f, 1.0f}),
            };

            if (canFocus(state, id))
            {
                if (!Input::isButtonPressed(Mouse::Left))
                {
                    state->ActiveAxis = 3;

                    // Clear out our focus if we were active
                    if (isFocused(state, id))
                    {
                        setFocus(nullptr, 0);
                    }
                }
                else
                {
                    if (Input::isButtonClicked(Mouse::Left))
                    {
                        Float3 rayStart = Editor::mouseToWorldPos(invViewProj, Input::MousePos, 0.0f);
                        Float3 rayEnd = Editor::mouseToWorldPos(invViewProj, Input::MousePos, 1.0f);

                        for (uint32_t i = 0; i < 3; i++)
                        {
                            if (doesLineCylinderIntersect(rayStart, rayEnd, *position, *position + axies[i], 0.1f))
                            {
                                // Set our gizmo as the active gizmo
                                setFocus(state, id);

                                state->ActiveAxis = i;
                                break;
                            }
                        }
                    }
                    else if (isFocused(state, id))
                    {
                        Float3 previousPos = Editor::mouseToWorldPos(invViewProj, Input::PreviousMousePos) - cameraTransform.Position;
                        Float3 nextPos = Editor::mouseToWorldPos(invViewProj, Input::MousePos) - cameraTransform.Position;

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
                        previousPos = intersectRayPlane(axies[planeNormal], planeDistance, Float3{}, previousPos);
                        nextPos = intersectRayPlane(axies[planeNormal], planeDistance, Float3{}, nextPos);

                        // Project our offset along our active axis for our delta
                        *position += axies[state->ActiveAxis] * dot(nextPos - previousPos, axies[state->ActiveAxis]);
                        translationChanged = true;
                    }
                }
            }
            
            ImGeo::addDrawInstance(ImGeo::DrawData::Instance{TranslationGizmoMesh, GizmoMaterial, Transform{*position, rotation, 1.0f}}, ViewDesc::ForwardPass::DebugOverlay);
            return translationChanged;
        }

        bool rotation(Float3 position, Quaternion* rotation, RotationGizmo* state, uint32_t id)
        {
            bool rotationChanged = false;

            Transform cameraTransform = {};
            Property::Handle transformProperty = getPropertyFromEntity(FrameCamera, TransformPropertyType);
            if (transformProperty != InvalidProperty)
            {
                cameraTransform = getLocalTransform(transformProperty);
            }

            CameraProperty::Handle cameraProperty = toCameraHandle(getPropertyFromEntity(FrameCamera, CameraPropertyType));
            IB_ASSERT(cameraProperty != InvalidCameraProperty, "Camera handle should have a camera property!");

            Mat4x4 invViewProj = Mat4x4::identity();
            if (cameraProperty != InvalidCameraProperty)
            {
                invViewProj = getInvViewProjection(cameraProperty, cameraTransform);
            }

            Float3 axies[3] =
            {
                rotate(*rotation, Float3{0.0f, 0.0f, 1.0f}),
                rotate(*rotation, Float3{1.0f, 0.0f, 0.0f}),
                rotate(*rotation, Float3{0.0f, 1.0f, 0.0f}),
            };

            Float3 unrotatedAxies[3] =
            {
                Float3{0.0f, 0.0f, 1.0f},
                Float3{1.0f, 0.0f, 0.0f},
                Float3{0.0f, 1.0f, 0.0f},
            };

            if (canFocus(state, id))
            {
                if (!Input::isButtonPressed(Mouse::Left))
                {
                    state->ActiveAxis = 3;

                    // Clear out our focus if we were active
                    if (isFocused(state, id))
                    {
                        setFocus(nullptr, id);
                    }
                }
                else
                {
                    if (Input::isButtonClicked(Mouse::Left))
                    {
                        Float3 rayStart = Editor::mouseToWorldPos(invViewProj, Input::MousePos, 0.0f);
                        Float3 rayEnd = Editor::mouseToWorldPos(invViewProj, Input::MousePos, 1.0f);

                        struct PlaneIntersection
                        {
                            float Distance;
                            uint32_t Axis;
                        };

                        PlaneIntersection intersections[3];
                        uint32_t intersectionCount = 0;

                        for (uint32_t i = 0; i < 3; i++)
                        {
                            Float3 planeIntersection = intersectRayPlane(axies[i], dot(position, axies[i]), rayStart, normalize(rayEnd- rayStart));
                            if (distanceSqr(position, planeIntersection) < RotationCircleRadius * RotationCircleRadius)
                            {
                                intersections[intersectionCount++] = PlaneIntersection{distanceSqr(cameraTransform.Position, planeIntersection), i};
                            }
                        }

                        uint32_t closestIntersection = 0;
                        for (uint32_t i = 1; i < intersectionCount; i++)
                        {
                            if (intersections[i].Distance < intersections[closestIntersection].Distance)
                            {
                                closestIntersection = i;
                            }
                        }

                        if (intersectionCount > 0)
                        {
                            // Set our gizmo as the active gizmo
                            setFocus(state, id);

                            state->ActiveAxis = intersections[closestIntersection].Axis;
                        }
                    }
                    else if (isFocused(state, id))
                    {
                        Float3 previousPos = Editor::mouseToWorldPos(invViewProj, Input::PreviousMousePos) - cameraTransform.Position;
                        Float3 nextPos = Editor::mouseToWorldPos(invViewProj, Input::MousePos) - cameraTransform.Position;

                        Float3 positionViewSpace = position - cameraTransform.Position;
                        float planeDistance = dot(positionViewSpace, axies[state->ActiveAxis]);

                        // Cast a ray from our camera origin to our axis plane
                        Float3 previousPosLocal = intersectRayPlane(axies[state->ActiveAxis], planeDistance, Float3{}, previousPos) - positionViewSpace;
                        Float3 nextPosLocal = intersectRayPlane(axies[state->ActiveAxis], planeDistance, Float3{}, nextPos) - positionViewSpace;

                        float cosAngle = dot(normalize(previousPosLocal), normalize(nextPosLocal));
                        float rotationAngle = acosf(clamp(cosAngle, -1.0f, 1.0f));

                        // Only handle rotations up to 90 degrees. More than that and our object will snap which looks unnapealing.
                        if (rotationAngle < Pi * 0.5f)
                        {
                            float rotationDirection = sign(dot(cross(previousPosLocal, nextPosLocal), axies[state->ActiveAxis]));
                            *rotation = (*rotation) * fromAxisAngle(unrotatedAxies[state->ActiveAxis], rotationAngle * rotationDirection);
                            rotationChanged = true;
                        }
                    }
                }
            }

            ImGeo::addDrawInstance(ImGeo::DrawData::Instance{RotationGizmoMesh, GizmoMaterial, Transform{position, *rotation, 1.0f}}, ViewDesc::ForwardPass::DebugOverlay);
            
            return rotationChanged;
        }

        bool transform(Transform* transform, TransformGizmo* state, uint32_t id)
        {
            if (canFocus(&state->Translation, id) || canFocus(&state->Rotation, id))
            {
                if (Input::isButtonClicked(Key::Code::W))
                {
                    state->GizmoType = TransformGizmo::Type::Translation;
                }
                else if (Input::isButtonClicked(Key::Code::E))
                {
                    state->GizmoType = TransformGizmo::Type::Rotation;
                }
            }

            bool changed = false;
            switch (state->GizmoType)
            {
            case TransformGizmo::Type::Translation:
                changed = translation(&transform->Position, transform->Rotation, &state->Translation, id);
                break;
            case TransformGizmo::Type::Rotation:
                changed = rotation(transform->Position, &transform->Rotation, &state->Rotation, id);
                break;
            }
            return changed;
        }
    }

    namespace ImGeo
    {
        namespace
        {
            constexpr uint32_t FrameBufferCount = 2;
            constexpr uint32_t MaxDynamicVertexCount = 100000;
            constexpr uint32_t MaxDynamicIndexCount = 100000;
            Vertex DynamicVertices[FrameBufferCount][MaxDynamicVertexCount];
            uint16_t DynamicIndices[FrameBufferCount][MaxDynamicIndexCount];

            State FrameStates[FrameBufferCount];
            uint32_t ActiveFrame = 0;

            Mesh::Handle DynamicMesh;
            Material::Handle Material;
            Image::Handle Albedo;

            DrawData FrameDrawData;
        } // namespace

        State *state()
        {
            return &FrameStates[ActiveFrame];
        }

        DrawData const *getDrawData()
        {
            return &FrameDrawData;
        }

        void addDrawInstance(DrawData::Instance const &instance, ViewDesc::ForwardPass::Type type)
        {
            FrameDrawData.Instances[type].add(instance);
        }

        void init()
        {
            for (uint32_t i = 0; i < FrameBufferCount; i++)
            {
                FrameStates[i].Vertices = DynamicVertices[i];
                FrameStates[i].Indices = DynamicIndices[i];
            }

            launchJob([](Job::Handle /*currentJob*/)
                      {
                          {
                              uint8_t whiteImageTexels[] = {255, 255, 255, 255};

                              IB::ImageDesc whiteImageDesc = {};
                              whiteImageDesc.Format = IB::ImageFormat::RGBA8;
                              whiteImageDesc.Width = 1;
                              whiteImageDesc.Height = 1;
                              whiteImageDesc.Data = whiteImageTexels;
                              Albedo = IB::createImage(whiteImageDesc);
                          }

                          {
                              IB::ForwardDesc gizmoMatDesc = {};
                              gizmoMatDesc.AlbedoTint = IB::toRGBA(1.0f, 1.0f, 1.0f, 1.0f);
                              gizmoMatDesc.AlbedoImage = Albedo;
                              Material = IB::createMaterial(gizmoMatDesc);
                          }

                          {
                              Vertex dummyVertex = {};
                              uint16_t dummyIndices[3] = {};

                              MeshDesc meshDesc = {};
                              meshDesc.Vertices.Data = &dummyVertex;
                              meshDesc.Vertices.Count = 1;
                              meshDesc.Indices.Data = dummyIndices;
                              meshDesc.Indices.Count = 3;

                              DynamicMesh = createMesh(meshDesc);
                          }
                          return JobResult::Complete;
                      },
                      NoJobType, RendererJobQueueIndex);
        }

        void kill()
        {
            
        }

        void newFrame()
        {
            FrameStates[ActiveFrame].VertexCount = 0;
            FrameStates[ActiveFrame].IndexCount = 0;
            FrameStates[ActiveFrame].Transform = {};
            FrameStates[ActiveFrame].VertexColor = Float4{1.0f,1.0f,1.0f,1.0f};

            for (uint32_t i = 0; i < ViewDesc::ForwardPass::Count; i++)
            {
                FrameDrawData.Instances[i].clear();
            }


            FrameDrawData.Instances[ViewDesc::ForwardPass::Default].add(ImGeo::DrawData::Instance{DynamicMesh, Material, Transform{}});
        }

        void endFrame()
        {
            if (FrameStates[ActiveFrame].VertexCount > 0)
            {
                launchJob([frame = ActiveFrame](Job::Handle /*currentJob*/)
                          {
                              MeshDesc meshDesc = {};
                              meshDesc.Vertices.Data = FrameStates[frame].Vertices;
                              meshDesc.Vertices.Count = FrameStates[frame].VertexCount;
                              meshDesc.Indices.Data = FrameStates[frame].Indices;
                              meshDesc.Indices.Count = FrameStates[frame].IndexCount;

                              updateMesh(DynamicMesh, meshDesc);

                              return JobResult::Complete;
                          },
                          NoJobType, RendererJobQueueIndex);

                ActiveFrame = (ActiveFrame + 1) % FrameBufferCount;
            }
        }

        void tube(State *state, float length, float radius, uint16_t segmentCount)
        {
            float tubeAngleDelta = IB::Tao / static_cast<float>(segmentCount);
            for (uint32_t i = 0; i < segmentCount; i++)
            {
                Float3 start = {};
                Float3 end = {};

                float angle = tubeAngleDelta * i;
                start.x = 0.0f;
                end.x = length;
                end.y = start.y = sinf(angle) * radius;
                end.z = start.z = cosf(angle) * radius;

                state->Vertices[state->VertexCount + i * 2] = Vertex{Float4{transformPoint(state->Transform, start), 0.0f}};
                state->Vertices[state->VertexCount + i * 2].Normal.w = bitcast<float>(packRGBA(state->VertexColor));
                state->Vertices[state->VertexCount + i * 2 + 1] = Vertex{Float4{transformPoint(state->Transform, end), 0.0f}};
                state->Vertices[state->VertexCount + i * 2 + 1].Normal.w = bitcast<float>(packRGBA(state->VertexColor));
            }

            for (uint16_t i = 0; i < segmentCount; i++)
            {
                uint16_t startEdge = state->VertexCount + i * 2;
                uint16_t endEdge = state->VertexCount + (i + 1) * 2 % (segmentCount * 2);

                state->Indices[state->IndexCount + i * 6] = startEdge;
                state->Indices[state->IndexCount + i * 6 + 1] = startEdge + 1;
                state->Indices[state->IndexCount + i * 6 + 2] = endEdge + 1;
                state->Indices[state->IndexCount + i * 6 + 3] = endEdge + 1;
                state->Indices[state->IndexCount + i * 6 + 4] = endEdge;
                state->Indices[state->IndexCount + i * 6 + 5] = startEdge;
            }

            state->IndexCount += segmentCount * 6;
            state->VertexCount += segmentCount * 2;
        }

        void cone(State *state, float height, float radius, uint16_t segmentCount)
        {
            float coneAngleDelta = IB::Tao / static_cast<float>(segmentCount);
            for (uint32_t i = 0; i < segmentCount; i++)
            {
                float angle = coneAngleDelta * i;
                Float3 pos = {};
                pos.x = 0.0f;
                pos.y = sinf(angle) * radius;
                pos.z = cosf(angle) * radius;
                state->Vertices[state->VertexCount + i] = IB::Vertex{Float4{transformPoint(state->Transform, pos), 0.0f}};
                state->Vertices[state->VertexCount + i].Normal.w = bitcast<float>(packRGBA(state->VertexColor));
            }

            Float3 pos = {};
            state->Vertices[state->VertexCount + segmentCount] = Vertex{Float4{transformPoint(state->Transform, pos), 0.0f}};
            state->Vertices[state->VertexCount + segmentCount].Normal.w = bitcast<float>(packRGBA(state->VertexColor));

            pos.x = height;
            state->Vertices[state->VertexCount + segmentCount + 1] = Vertex{Float4{transformPoint(state->Transform, pos), 0.0f}};
            state->Vertices[state->VertexCount + segmentCount + 1].Normal.w = bitcast<float>(packRGBA(state->VertexColor));

            for (uint16_t i = 0; i < segmentCount; i++)
            {
                uint16_t centerVertex = state->VertexCount + segmentCount;
                uint16_t topVertex = state->VertexCount + segmentCount + 1;

                uint16_t startVertex = state->VertexCount + i;
                uint16_t endVertex = state->VertexCount + (i + 1) % segmentCount;

                state->Indices[state->IndexCount + i * 6] = startVertex;
                state->Indices[state->IndexCount + i * 6 + 1] = topVertex;
                state->Indices[state->IndexCount + i * 6 + 2] = endVertex;
                state->Indices[state->IndexCount + i * 6 + 3] = centerVertex;
                state->Indices[state->IndexCount + i * 6 + 4] = startVertex;
                state->Indices[state->IndexCount + i * 6 + 5] = endVertex;
            }

            state->VertexCount += segmentCount + 2;
            state->IndexCount += segmentCount * 6;
        }

        void grid(State* state, uint32_t width, uint32_t height)
        {
            constexpr float gridRadius = 0.03f;
            Transform const previousTransform = state->Transform;

            for (uint32_t y = 0; y <= height; y++)
            {
                Transform gridOffset = {};
                gridOffset.Position = Float3{
                    -static_cast<float>(width) * 0.5f,
                    -static_cast<float>(height) * 0.5f + static_cast<float>(y),
                    0.0f};

                state->Transform = applyTransform(gridOffset, previousTransform);
                tube(state, static_cast<float>(width), gridRadius, 4);
            }

            for (uint32_t x = 0; x <= width; x++)
            {
                Transform gridOffset = {};
                gridOffset.Rotation = fromEulerAngles(Float3{0.0f, 0.0f, Pi * 0.5f});
                gridOffset.Position = Float3{
                    -static_cast<float>(width) * 0.5f + static_cast<float>(x),
                    -static_cast<float>(height) * 0.5f,
                    0.0f};

                state->Transform = applyTransform(gridOffset, previousTransform);
                tube(state, static_cast<float>(height), gridRadius, 4);
            }
            state->Transform = previousTransform;
        }

    } // namespace ImGeo
}