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
#include <IBEngine/IBEditor.h>
#include <IBEngine/IBGeometryToolkit.h>

#include <imgui/imgui.h>

using namespace IB;
namespace
{
    // Global until we figure out state storage.
    Resource::Handle CellResource = {};
    bool EditorAlive = true;
    constexpr uint32_t PlatformJobQueue = 2;
    Window::Handle WindowHandle;

    bool primaryWindowHandler(void *data, WindowMessage message, WindowMessagePlatformData const *platformData)
    {
        if (imGuiPlatformMessage(platformData))
        {
            return true;
        }

        if (Editor::onWindowMessage(message))
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
} // namespace

namespace IB
{
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

        JobResult platformMessageQueueJob(Job::Handle /*currentJob*/)
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

        JobResult editorJob(Job::Handle /*currentJob*/)
        {
            Editor::FrameState state = { &CellResource };
            Editor::frame(&state);
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
            SubmittedViewGather = continueJob([viewDescriptor, renderFrameAllocator](Job::Handle /*currentJob*/) {
                Cell::Handle currentCell = InvalidCell;
                // Todo: needs a volatile load. Don't want the compiler to cache our load
                if (Asset::isResourceAssetAvailable(CellResource))
                {
                    currentCell = toCellHandle(Asset::getAssetFromResource(CellResource));
                }

                GatherCellViewDesc gatherDesc
                {
                    renderFrameAllocator,
                    Editor::getCamera(),
                    currentCell,
                    ImGui::GetDrawData(),
                    ImGizmo::getDrawData()
                };
                *viewDescriptor = gatherCellView(gatherDesc);

                return JobResult::Complete;
            },
                                              &SubmittedEditorHandle, 1);

            // Writes:
            // Backend Renderer state
            continueJob([viewDescriptor, renderFrameAllocator](Job::Handle /*currentJob*/) {
                drawCellView(renderFrameAllocator, *viewDescriptor);

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
        launchJob([platformEvent](Job::Handle /*currentJob*/) {
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
        Editor::init();
        ImGizmo::init();
        initGeometryToolkit();

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

        killGeometryToolkit();
        ImGizmo::kill();
        Editor::kill();
        JobGraph::kill();
        killEntitySystem();
        killRendererFrontend();
        Serialization::killSerialization();

        launchJob([platformEvent](Job::Handle /*currentJob*/) {
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
