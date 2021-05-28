#include <IBEngine/IBRenderer.h>
#include <IBEngine/IBRendererFrontend.h>
#include <IBEngine/IBSerialization.h>
#include <IBEngine/IBLogging.h>
#include <IBEngine/IBPlatform.h>
#include <IBEngine/IBAllocator.h>

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

    // Global until we figure out state storage.
    Asset::ResourceHandle CellResource = {};
    bool EditorAlive = true;
    constexpr uint32_t PlatformJobQueue = 2;
    WindowHandle Window;
}

namespace IB
{
    namespace Editor
    {
        FileInfo LoadingCell;
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
                                LoadingCell = fileInfo;
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
                    Asset::loadResourceAsync(LoadingCell.Path, Asset::toFourCC("CELL"), &CellResource);
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
        }
    }

    namespace JobGraph
    {
        uint32_t QueuedFrameCount = 0;
        uint32_t CommitedFrameCount = 0;
        constexpr uint32_t MaxPendingFrameCount = 2;
        IB::ThreadEvent PendingFrameEvent;

        StackAllocator RenderFrameAllocators[MaxPendingFrameCount];

        JobHandle SubmittedEditorHandle = InvalidJob;
        JobHandle SubmittedViewGather = InvalidJob;

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
            consumeMessageQueue([](void *, PlatformMessage message)
            {
                if (message == PlatformMessage::Quit)
                {
                    EditorAlive = false;
                }
            }, nullptr);
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
            JobHandle platformJob;
            if (SubmittedEditorHandle.Value == InvalidJob.Value)
            {
                platformJob = launchJob(&platformMessageQueueJob, PlatformJobQueue);
            }
            else
            {
                platformJob = continueJob(&platformMessageQueueJob, &SubmittedEditorHandle, 1, PlatformJobQueue);
            }

            // Writes:
            // All frontend state (potentially)
            JobHandle editorDependencies[2] = { platformJob, SubmittedViewGather };
            // Wait on our previous view gather if present.
            uint32_t dependencyCount = SubmittedViewGather.Value != InvalidJob.Value ? 2 : 1;
            SubmittedEditorHandle = continueJob(&editorJob, editorDependencies, dependencyCount);

            StackAllocator* renderFrameAllocator = &RenderFrameAllocators[QueuedFrameCount % MaxPendingFrameCount];
            reset(renderFrameAllocator);

            ViewDesc* viewDescriptor = allocate<ViewDesc>(renderFrameAllocator);

            // Reads:
            // Frontend Renderer state
            // Transform state
            SubmittedViewGather = continueJob([viewDescriptor, renderFrameAllocator]()
            {
                CellHandle currentCell = InvalidCell;
                // Todo: needs a volatile load. Don't want the compiler to cache our load
                if (Asset::isResourceAssetAvailable(CellResource))
                {
                    currentCell = toCellHandle(Asset::getAssetFromResource(CellResource));
                }

                *viewDescriptor = gatherCellView(currentCell, ImGui::GetDrawData(), renderFrameAllocator);

                return JobResult::Complete;
            }, &SubmittedEditorHandle, 1);

            // Writes:
            // Backend Renderer state
            continueJob([viewDescriptor]()
            {
                drawCellView(*viewDescriptor);

                uint32_t previousPendingFrameCount = volatileLoad(&QueuedFrameCount) - CommitedFrameCount;
                CommitedFrameCount++;
                threadRelease(); // Assure that our write is visible before we signale our thread event.
                if (previousPendingFrameCount == MaxPendingFrameCount)
                {
                    signalThreadEvent(PendingFrameEvent);
                }

                return JobResult::Complete;
            }, &SubmittedViewGather, 1, RendererJobQueueIndex);
        }

        void flushQueuedFrames()
        {
            while (volatileLoad(&CommitedFrameCount) != QueuedFrameCount)
            {
            }
        }
    }
}

int main()
{
    initJobSystem();
    ThreadEvent platformEvent = createThreadEvent();

    {
        launchJob([platformEvent]()
        {
            WindowDesc winDesc = {};
            winDesc.Name = "EngineCat";
            winDesc.Width = 4096;
            winDesc.Height = 4096;
            winDesc.OnWindowMessage = &primaryWindowHandler;
            Window = createWindow(winDesc);

            signalThreadEvent(platformEvent);
            return JobResult::Complete;
        }, PlatformJobQueue);

        waitOnThreadEvent(platformEvent);

        Serialization::initSerialization();
        initRendererFrontend({ &Window });
        initEntitySystem();
        JobGraph::init();
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

        launchJob([platformEvent]()
        {
            destroyWindow(Window);
            signalThreadEvent(platformEvent);
            return JobResult::Complete;
        }, PlatformJobQueue);

        waitOnThreadEvent(platformEvent);
        destroyThreadEvent(platformEvent);
        killJobSystem();
    }
}
