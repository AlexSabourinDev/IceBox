#define _CRT_SECURE_NO_WARNINGS

#include <IBEngine/IBAsset.h>
#include <IBEngine/IBEntity.h>
#include <IBEngine/IBRenderer.h>
#include <IBEngine/IBRendererFrontend.h>
#include <IBEngine/IBSerialization.h>
#include <IBEngine/IBLogging.h>
#include <IBEngine/IBPlatform.h>
#include <IBEngine/IBAllocator.h>
#include <IBEngine/IBCollection.h>
#include <IBEngine/IBJobs.h>

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

    // Global until we figure out state storage.
    Resource::Handle CellResource = {};
    bool EditorAlive = true;
    constexpr uint32_t PlatformJobQueue = 2;
    Window::Handle WindowHandle;
} // namespace

namespace IB
{
    namespace Editor
    {
        struct AssetBrowserState
        {
            FileInfo LoadCellInfo;
        };

        struct
        {
            Asset::Handle SelectedAsset;
            Asset::FourCC SelectedAssetType;
        } InspectorState;

        struct AssetEditor
        {
            using FrameCallback = void(Asset::Handle asset);
            Asset::FourCC Type;
            FrameCallback* Frame;
        };
        DynamicArray<AssetEditor> AssetEditors;

        void addAssetEditor(Asset::FourCC type, AssetEditor::FrameCallback* frame)
        {
            AssetEditors.add(type, frame);
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
                    if (ImGui::IsItemClicked())
                    {
                        if (hasExtension(fileInfo.Path, ".cell"))
                        {
                            assetBrowser->LoadCellInfo = fileInfo;
                            ImGui::OpenPopup("Load Cell?");
                        }
                    }
                }
            }
        };

        void frame()
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
                        Asset::loadResourceAsync(assetBrowserState.LoadCellInfo.Path, Asset::toFourCC("CELL"), &CellResource);
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
                if (Asset::isResourceAssetAvailable(CellResource))
                {
                    Cell::Handle currentCell = toCellHandle(Asset::getAssetFromResource(CellResource));

                    Entity::Handle *entities;
                    uint32_t entityCount;
                    getEntityList(currentCell, &entities, &entityCount);

                    for (uint32_t i = 0; i < entityCount; i++)
                    {
                        ImGui::TreeNodeEx(getEntityName(entities[i]), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen);
                        if (ImGui::IsItemClicked())
                        {
                            InspectorState.SelectedAsset = toAssetHandle(entities[i]);
                            InspectorState.SelectedAssetType = Asset::toFourCC("ENTT");
                        }
                    }
                }
            }
            ImGui::End();

            if (ImGui::Begin("Inspector"))
            {
                if (InspectorState.SelectedAsset != Asset::InvalidAsset)
                {
                    for (uint32_t i = 0; i < AssetEditors.count(); i++)
                    {
                        if (AssetEditors[i].Type == InspectorState.SelectedAssetType)
                        {
                            AssetEditors[i].Frame(InspectorState.SelectedAsset);
                            break;
                        }
                    }
                }
            }
            ImGui::End();

            ImGui::Render();
        }
    } // namespace Editor

    namespace JobGraph
    {
        uint32_t QueuedFrameCount = 0;
        uint32_t CommitedFrameCount = 0;
        constexpr uint32_t MaxPendingFrameCount = 2;
        IB::ThreadEvent PendingFrameEvent;

        StackAllocator RenderFrameAllocators[MaxPendingFrameCount];

        Job::Handle SubmittedEditorHandle = InvalidJob;
        Job::Handle SubmittedViewGather = InvalidJob;

        void init()
        {
            PendingFrameEvent = IB::createThreadEvent();
            for (uint32_t i = 0; i < MaxPendingFrameCount; i++)
            {
                constexpr uint32_t maxStackFrameSize = 1024 * 1024;
                constexpr uint32_t maxAlignment = 16;
                RenderFrameAllocators[i] = createStackAllocator(memoryAllocate(maxStackFrameSize, maxAlignment), maxStackFrameSize);
            }
        }

        void kill()
        {
            for (uint32_t i = 0; i < MaxPendingFrameCount; i++)
            {
                memoryFree(RenderFrameAllocators[i].MemoryHead);
            }
            IB::destroyThreadEvent(PendingFrameEvent);
        }

        JobResult platformMessageQueueJob()
        {
            consumeMessageQueue([](void *, PlatformMessage message) {
                if (message == PlatformMessage::Quit)
                {
                    EditorAlive = false;
                }
            },
                                nullptr);
            return JobResult::Complete;
        }

        JobResult editorJob()
        {
            Editor::frame();
            return JobResult::Complete;
        }

        void frame()
        {
            if (QueuedFrameCount - volatileLoad(&CommitedFrameCount) == MaxPendingFrameCount)
            {
                waitOnThreadEvent(PendingFrameEvent);
            }
            IB_ASSERT(QueuedFrameCount - volatileLoad(&CommitedFrameCount) < MaxPendingFrameCount, "We haven't finished submitting our last frame!");

            QueuedFrameCount++;
            // Make sure our thread event is reset here.
            // Since we could have signaled our event and never waited on it, it would still be set.
            // This can happen if we:
            // - Main thread submits 2 frames
            // - Main thread gets preempted
            // - Submission thread commits a frame
            // - Submission thread sees that the event should be signaled and signals it
            // - Main thread resumes, never waits on the event because the submission thread advanced the submission count.
            resetThreadEvent(PendingFrameEvent);

            // Handles all window messages. Might interact with editor state
            Job::Handle platformJob;
            if (SubmittedEditorHandle == InvalidJob)
            {
                platformJob = launchJob(&platformMessageQueueJob, PlatformJobQueue);
            }
            else
            {
                platformJob = continueJob(&platformMessageQueueJob, &SubmittedEditorHandle, 1, PlatformJobQueue);
            }

            // Writes:
            // All frontend state (potentially)
            Job::Handle editorDependencies[2] = {platformJob, SubmittedViewGather};
            // Wait on our previous view gather if present.
            uint32_t dependencyCount = SubmittedViewGather != InvalidJob ? 2 : 1;
            SubmittedEditorHandle = continueJob(&editorJob, editorDependencies, dependencyCount);

            StackAllocator *renderFrameAllocator = &RenderFrameAllocators[QueuedFrameCount % MaxPendingFrameCount];
            reset(renderFrameAllocator);

            ViewDesc *viewDescriptor = allocate<ViewDesc>(renderFrameAllocator);

            // Reads:
            // Frontend Renderer state
            // Transform state
            SubmittedViewGather = continueJob([viewDescriptor, renderFrameAllocator]() {
                Cell::Handle currentCell = InvalidCell;
                // Todo: needs a volatile load. Don't want the compiler to cache our load
                if (Asset::isResourceAssetAvailable(CellResource))
                {
                    currentCell = toCellHandle(Asset::getAssetFromResource(CellResource));
                }

                *viewDescriptor = gatherCellView(currentCell, ImGui::GetDrawData(), renderFrameAllocator);

                return JobResult::Complete;
            },
                                              &SubmittedEditorHandle, 1);

            // Writes:
            // Backend Renderer state
            continueJob([viewDescriptor]() {
                drawCellView(*viewDescriptor);

                uint32_t previousPendingFrameCount = volatileLoad(&QueuedFrameCount) - CommitedFrameCount;
                CommitedFrameCount++;
                threadRelease(); // Assure that our write is visible before we signale our thread event.
                if (previousPendingFrameCount == MaxPendingFrameCount)
                {
                    signalThreadEvent(PendingFrameEvent);
                }

                return JobResult::Complete;
            },
                        &SubmittedViewGather, 1, RendererJobQueueIndex);
        }

        void flushQueuedFrames()
        {
            while (volatileLoad(&CommitedFrameCount) != QueuedFrameCount)
            {
            }
        }
    } // namespace JobGraph
} // namespace IB

int main()
{
    initJobSystem();
    ThreadEvent platformEvent = createThreadEvent();

    {
        launchJob([platformEvent]() {
            WindowDesc winDesc = {};
            winDesc.Name = "EngineCat";
            winDesc.Width = 1024;
            winDesc.Height = 720;
            winDesc.OnWindowMessage = &primaryWindowHandler;
            WindowHandle = createWindow(winDesc);

            signalThreadEvent(platformEvent);
            return JobResult::Complete;
        },
                  PlatformJobQueue);

        waitOnThreadEvent(platformEvent);

        Serialization::initSerialization();
        initRendererFrontend({ WindowHandle });
        initEntitySystem();
        JobGraph::init();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    }

    {
        while (EditorAlive)
        {
            JobGraph::frame();
        }

        JobGraph::flushQueuedFrames();
    }

    {
        Asset::releaseResourceAsync(CellResource);

        JobGraph::kill();
        killEntitySystem();
        killRendererFrontend();
        Serialization::killSerialization();

        launchJob([platformEvent]() {
            destroyWindow(WindowHandle);
            signalThreadEvent(platformEvent);
            return JobResult::Complete;
        },
                  PlatformJobQueue);

        waitOnThreadEvent(platformEvent);
        destroyThreadEvent(platformEvent);
        killJobSystem();
    }
}
