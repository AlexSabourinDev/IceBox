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
#include <stdio.h>

using namespace IB;
namespace
{
    // Global until we figure out state storage.
    Resource::Handle CellResource = {};
    bool GameAlive = true;
    uint32_t constexpr PlatformJobQueue = 0;
    Window::Handle WindowHandle;

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
} // namespace

namespace IB
{
    namespace JobGraph
    {
        struct Resources
        {
            enum
            {
                Transform = 0,
                RendererFrontend,
                RendererBackend,
                ViewGather,
                Count
            };

            enum
            {
                TransformBit = 1 << Transform,
                RendererFrontentBit = 1 << RendererFrontend,
                RendererBackendBit = 1 << RendererBackend,
                ViewGatherBit = 1 << ViewGather
            };
        };
        Job::Handle LastWriters[Resources::Count];

        void addJob(Job::Handle job, uint32_t resourceReads, uint32_t resourceWrites)
        {
            // Writes are considered reads as well
            uint32_t dependencyBits = resourceReads | resourceWrites;
            Job::Handle dependencies[Resources::Count];
            uint32_t dependencyCount = 0;

            for (uint32_t i = 0; i < Resources::Count; i++)
            {
                if ((dependencyBits & (1 << i)) != 0)
                {
                    dependencies[dependencyCount++] = LastWriters[i];
                }

                if ((resourceWrites & (1 << i)) != 0)
                {
                    LastWriters[i] = job;
                }
            }

            continueJob(job, dependencies, dependencyCount);
        }

        uint32_t QueuedFrameCount = 0;
        uint32_t CommitedFrameCount = 0;
        uint32_t constexpr MaxPendingFrameCount = 2;
        IB::Thread::Event PendingFrameEvent;

        StackAllocator RenderFrameAllocators[MaxPendingFrameCount];


        void init()
        {
            PendingFrameEvent = IB::createThreadEvent();
            for (uint32_t i = 0; i < MaxPendingFrameCount; i++)
            {
                uint32_t constexpr maxStackFrameSize = 1024 * 1024;
                uint32_t constexpr maxAlignment = 16;
                RenderFrameAllocators[i] = createStackAllocator(memoryAllocate(maxStackFrameSize, maxAlignment), maxStackFrameSize);
            }

            for (uint32_t i = 0; i < Resources::Count; i++)
            {
                LastWriters[i] = InvalidJob;
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
            if (processMessageQueue() == MessageQueueState::Quit)
            {
                GameAlive = false;
            }

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

            StackAllocator *renderFrameAllocator = &RenderFrameAllocators[QueuedFrameCount % MaxPendingFrameCount];
            reset(renderFrameAllocator);

            ViewDesc *viewDescriptor = allocate<ViewDesc>(renderFrameAllocator);

            // Handles all window messages.
            Job::Handle platformJob = reserveJob(&platformMessageQueueJob, NoJobType, PlatformJobQueue);
            addJob(platformJob, 0, 0);

            // Reads:
            // Frontend Renderer state
            // Transform state
            Job::Handle viewGather = reserveJob([viewDescriptor, renderFrameAllocator](Job::Handle /*currentJob*/)
                                              {
                                                  Cell::Handle currentCell = InvalidCell;
                                                  // Todo: needs a volatile load. Don't want the compiler to cache our load
                                                  if (Asset::isResourceAssetAvailable(CellResource))
                                                  {
                                                      currentCell = toCellHandle(Asset::getAssetFromResource(CellResource));
                                                  }

                                                  GatherCellViewDesc gatherDesc{
                                                      renderFrameAllocator,
                                                      InvalidEntity,
                                                      currentCell,
                                                      ImGui::GetDrawData(),
                                                      ImGeo::getDrawData()};
                                                  *viewDescriptor = gatherCellView(gatherDesc);

                                                  return JobResult::Complete;
                                              });
            addJob(viewGather, Resources::TransformBit | Resources::RendererFrontendBit, Resources::ViewGatherBit);

            // Writes:
            // Backend Renderer state
            Job::Handle viewDraw = reserveJob([viewDescriptor, renderFrameAllocator](Job::Handle /*currentJob*/)
                                        {
                                            drawCellView(renderFrameAllocator, *viewDescriptor);

                                            uint32_t previousPendingFrameCount = volatileLoad(&QueuedFrameCount) - CommitedFrameCount;
                                            CommitedFrameCount++;
                                            threadRelease(); // Assure that our write is visible before we signale our thread event.
                                            if (previousPendingFrameCount == MaxPendingFrameCount)
                                            {
                                                signalThreadEvent(PendingFrameEvent);
                                            }

                                            return JobResult::Complete;
                                        }, NoJobType, RendererJobQueueIndex);
            addJob(viewDraw, Resources::ViewGather, Resources::RendererBackendBit);
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
    Thread::Event platformEvent = createThreadEvent();

    {
        launchJob([platformEvent](Job::Handle /*currentJob*/)
                  {
                      WindowDesc winDesc = {};
                      winDesc.Name = "EngineCat";
                      winDesc.Width = 1024;
                      winDesc.Height = 720;
                      winDesc.OnWindowMessage = &primaryWindowHandler;
                      WindowHandle = createWindow(winDesc);

                      signalThreadEvent(platformEvent);
                      return JobResult::Complete;
                  },
                  NoJobType, PlatformJobQueue);

        waitOnThreadEvent(platformEvent);

        Serialization::initSerialization();
        initRendererFrontend({WindowHandle});
        initEntitySystem();
        JobGraph::init();
        ImGeo::init();
        ImGizmo::init();
        initGeometryToolkit();

        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    }

    {
        while (GameAlive)
        {
            JobGraph::frame();
        }

        JobGraph::flushQueuedFrames();
    }

    {
        Job::Handle releaseJob = Asset::releaseResourceAsync(CellResource);
        wait(releaseJob);

        killGeometryToolkit();
        ImGizmo::kill();
        ImGeo::kill();
        JobGraph::kill();
        killEntitySystem();
        killRendererFrontend();
        Serialization::killSerialization();

        launchJob([platformEvent](Job::Handle /*currentJob*/)
                  {
                      destroyWindow(WindowHandle);
                      signalThreadEvent(platformEvent);
                      return JobResult::Complete;
                  },
                  NoJobType, PlatformJobQueue);

        waitOnThreadEvent(platformEvent);
        destroyThreadEvent(platformEvent);
        killJobSystem();
    }
}
