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
    Asset::FourCC const StartupConfigType = Asset::toFourCC("SCFG");

    struct StartupConfig
    {
        PathString StartupCellPath;
    };

    class StartupConfigAssetStreamer : public Asset::IStreamer
    {
    public:
        Asset::Handle create() override
        {
            StartupConfig *config = allocate<StartupConfig>();
            return Asset::toHandle(reinterpret_cast<uint64_t>(config));
        }
        Asset::LoadContinuation loadAsync(Asset::LoadContext *context) override
        {
            StartupConfig *startupConfig = reinterpret_cast<StartupConfig *>(Asset::toValue(context->LoadingAsset));
            fromBinary(&context->Stream, &startupConfig->StartupCellPath);
            return Asset::complete(Asset::toHandle(reinterpret_cast<uint64_t>(startupConfig)));
        }

        void saveThreadSafe(Asset::SaveContext *context) override
        {
            StartupConfig const *startupConfig = reinterpret_cast<StartupConfig const*>(Asset::toValue(context->Asset));

            toBinary(context->Stream, startupConfig->StartupCellPath);
        }

        void unloadThreadSafe(Asset::Handle asset) override
        {
            StartupConfig *startupConfig = reinterpret_cast<StartupConfig *>(Asset::toValue(asset));
            deallocate(startupConfig);
        }
    };

    StartupConfigAssetStreamer StartupConfigStreamer;
} // namespace

namespace IB
{
    namespace Editor
    {
        void startupConfigEditor(Asset::Handle asset, void *data)
        {
            bool *assetModified = reinterpret_cast<bool *>(data);

            StartupConfig *startupConfig = reinterpret_cast<StartupConfig *>(Asset::toValue(asset));
            if (ImGui::BeginTable("Inspector Table", 2))
            {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("Data", ImGuiTableColumnFlags_None);

                ImGui::TableNextColumn();
                ImGui::Text("Startup Cell");

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);

                *assetModified |= resourcePathCombo("#Cell", CellType, &startupConfig->StartupCellPath);

                ImGui::EndTable();
            }
        }
    } // namespace Editor
}

namespace
{
    bool EditorEnabled = true;

    // Global until we figure out state storage.
    Resource::Handle CellResource = InvalidResource;
    bool AppAlive = true;
    uint32_t constexpr PlatformJobQueue = 0;
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
    struct Resources
    {
        enum
        {
            TransformBit = 1 << 0,
            RendererFrontendBit = 1 << 1,
            RendererBackendBit = 1 << 2,
            ViewGatherBit = 1 << 3,
            EditorBit = 1 << 4,
            ImGuiBackendBit = 1 << 5, // Windowing + backend logic
        };
    };

    uint32_t QueuedFrameCount = 0;
    uint32_t CommitedFrameCount = 0;
    uint32_t constexpr MaxPendingFrameCount = 2;
    IB::Thread::Event PendingFrameEvent;

    StackAllocator RenderFrameAllocators[MaxPendingFrameCount];

    Resource::Handle StartupConfigResource;
    void init()
    {
        PendingFrameEvent = IB::createThreadEvent();
        for (uint32_t i = 0; i < MaxPendingFrameCount; i++)
        {
            uint32_t constexpr maxStackFrameSize = 1024 * 1024;
            uint32_t constexpr maxAlignment = 16;
            RenderFrameAllocators[i] = createStackAllocator(memoryAllocate(maxStackFrameSize, maxAlignment), maxStackFrameSize);
        }

        Asset::addStreamer(StartupConfigType, &StartupConfigStreamer);

        Editor::addAssetEditor(StartupConfigType, &Editor::startupConfigEditor);
        Editor::addLooseResourceType(StartupConfigType, "StartupConfig", ".scfg", true);

        if (!EditorEnabled)
        {
            Job::Handle loadJob = Asset::loadResourceAsync("Snake.scfg", StartupConfigType, &StartupConfigResource);
            continueJob([](Job::Handle)
                        {
                            Asset::Handle asset = Asset::getAssetFromResource(StartupConfigResource);
                            StartupConfig const *config = reinterpret_cast<StartupConfig const *>(Asset::toValue(asset));

                            loadResourceAsync(config->StartupCellPath.String, CellType, &CellResource);

                            Asset::releaseResourceAsync(StartupConfigResource);
                            return JobResult::Complete;
                        },
                        &loadJob, 1);
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
            AppAlive = false;
        }

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

        StackAllocator *renderFrameAllocator = &RenderFrameAllocators[QueuedFrameCount % MaxPendingFrameCount];
        reset(renderFrameAllocator);

        ViewDesc *viewDescriptor = allocate<ViewDesc>(renderFrameAllocator);


        // Handles all window messages. Might interact with editor state
        Job::Handle platformJob = reserveJob(&platformMessageQueueJob, NoJobType, PlatformJobQueue);
        JobGraph::addJob(platformJob, Resources::EditorBit, 
            Resources::EditorBit | Resources::RendererFrontendBit | Resources::ImGuiBackendBit | Resources::RendererBackendBit);

        if (EditorEnabled)
        {
            // Writes:
            // All frontend state (potentially)
            Job::Handle editorJobHandle = reserveJob(&editorJob);
            JobGraph::addJob(editorJobHandle, UINT32_MAX, UINT32_MAX);
        }

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

            GatherCellViewDesc gatherDesc
            {
                renderFrameAllocator,
                Editor::getCamera(),
                currentCell,
                ImGui::GetDrawData(),
                ImGeo::getDrawData()
            };
            *viewDescriptor = gatherCellView(gatherDesc);

            return JobResult::Complete;
        });

        JobGraph::addJob(viewGather,
                         Resources::TransformBit | Resources::RendererFrontendBit,
            Resources::ViewGatherBit);

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
        },
                                    RendererJobType, RendererJobQueueIndex);
        JobGraph::addJob(viewDraw, Resources::ViewGatherBit | Resources::ImGuiBackendBit, Resources::RendererBackendBit);
    }

    void flushQueuedFrames()
    {
        while (volatileLoad(&CommitedFrameCount) != QueuedFrameCount)
        {
        }
    }
} // namespace IB

int main()
{
    initJobSystem();
    Thread::Event platformEvent = createThreadEvent();

    {
        launchJob([platformEvent](Job::Handle /*currentJob*/) {
            WindowDesc winDesc = {};
            winDesc.Name = "Snake";
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
        initRendererFrontend({ WindowHandle });
        initEntitySystem();
        JobGraph::init();
        Editor::init();
        ImGeo::init();
        ImGizmo::init();
        initGeometryToolkit();

        init();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    }

    {
        while (AppAlive)
        {
            frame();
        }

        flushQueuedFrames();
    }

    {
        Job::Handle releaseJob = Asset::releaseResourceAsync(CellResource);
        wait(releaseJob);

        kill();

        killGeometryToolkit();
        ImGizmo::kill();
        ImGeo::kill();
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
                  NoJobType, PlatformJobQueue);

        waitOnThreadEvent(platformEvent);
        destroyThreadEvent(platformEvent);
        killJobSystem();
    }
}
