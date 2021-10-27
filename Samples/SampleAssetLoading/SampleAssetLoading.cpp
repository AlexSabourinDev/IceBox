#define _CRT_SECURE_NO_WARNINGS
#include <IBEngine/IBMath.h>
#include <IBEngine/IBSerialization.h>
#include <IBEngine/IBJobs.h>
#include <IBEngine/IBRendererFrontend.h>
#include <IBEngine/IBAllocator.h>
#include <IBEngine/IBLogging.h>
#include <IBEngine/IBAsset.h>
#include <IBEngine/IBEntity.h>
#include <IBEngine/IBPlatform.h>
#include <stddef.h>
#include <string.h>

struct EntityID
{
    uint32_t Value;
};

IB::Mat3x4 LocalTransforms[1024];
IB::Mat3x4 WorldTransforms[1024];
EntityID EntityMap[1024];
uint32_t ActiveTransforms = 0;

class TransformPropertyAssetStreamer : public IB::Asset::IStreamer
{
public:
    IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
    {
        // TODO: entity activation step

        IB::Mat3x4 localTransform;
        fromBinary(&context->Stream, &localTransform);

        LocalTransforms[ActiveTransforms] = localTransform;
        WorldTransforms[ActiveTransforms] = localTransform;
        return IB::Asset::complete(IB::Asset::toHandle(ActiveTransforms++));
    }

    void saveThreadSafe(IB::Asset::SaveContext *context) override
    {
        IB::Mat3x4 transform = LocalTransforms[IB::Asset::toValue(context->Asset)];
        toBinary(context->Stream, transform);
    }

    void unloadThreadSafe(IB::Asset::Handle) override
    {
        // Nothing to do here. (at least, not right now)
    }
};

TransformPropertyAssetStreamer TransformPropertyStreamer;

int main()
{
    IB::WindowDesc winDesc = {};
    winDesc.Name = "Ice Box";
    winDesc.Width = 500;
    winDesc.Height = 500;
    winDesc.OnWindowMessage = [](void * /*data*/, IB::WindowMessage message, IB::WindowMessagePlatformData const* platformData)
    {
        switch (message.Type)
        {
        case IB::WindowMessage::Close:
            IB::sendQuitMessage();
            return true;
        }

        return false;
    };
    IB::Window::Handle window = IB::createWindow(winDesc);

    IB::Serialization::initSerialization();
    IB::initJobSystem();
    IB::initRendererFrontend({ window });
    IB::initEntitySystem();
    IB::Asset::addStreamer(IB::TransformPropertyType, &TransformPropertyStreamer);

    auto waitOnJob = [](IB::Job::Handle job)
    {
        IB::ThreadEvent threadEvent = IB::createThreadEvent();
        IB::continueJob([threadEvent](IB::Job::Handle /*currentJob*/)
        {
            IB::signalThreadEvent(threadEvent);
            return IB::JobResult::Complete;
        },
            &job, 1);
        IB::waitOnThreadEvent(threadEvent);
        IB::destroyThreadEvent(threadEvent);
    };

    // Test double asset loading
    {
        IB::Resource::Handle meshAsset1;
        IB::Resource::Handle meshAsset2;

        IB::Asset::loadResourceAsync("Box.msh", { IB::MeshType }, &meshAsset1);
        IB::Job::Handle meshJob = IB::Asset::loadResourceAsync("Box.msh", { IB::MeshType }, &meshAsset2);
        waitOnJob(meshJob);

        IB_ASSERT(meshAsset1 == meshAsset2, "Loaded the same asset.");
        IB::Asset::releaseResourceAsync(meshAsset2);
        IB::Asset::releaseResourceAsync(meshAsset1);
    }

    // Create our renderer property
    IB::Property::Handle rendererPropertyAsset = IB::createRendererProperty("Box.msh", "Test.mat");

    // Create our transform property
    IB::Property::Handle transformPropertyAsset = IB::Property::toHandle(0);
    {
        LocalTransforms[0] = {};
    }

    // Create our entity asset
    IB::Entity::Handle entityAsset = IB::createEntity();
    {
        IB::addPropertyToEntity(entityAsset, IB::RendererPropertyType, rendererPropertyAsset);
        IB::addPropertyToEntity(entityAsset, IB::TransformPropertyType, transformPropertyAsset);
    }

    // Save our resource
    {
        IB::Resource::Handle entityResource = IB::Asset::createResourceThreadSafe("TestEntity.entt", IB::EntityType, IB::toAssetHandle(entityAsset));
        IB::Job::Handle saveJob = IB::Asset::saveResourceAsync(entityResource);

        waitOnJob(saveJob);

        IB::Resource::Handle savedEntityResource;
        IB::Job::Handle entityJob = IB::Asset::loadResourceAsync("TestEntity.entt", { IB::EntityType }, &savedEntityResource);
        waitOnJob(entityJob);
        IB_ASSERT(savedEntityResource == entityResource, "Failed to load the same asset!");

        IB::Asset::releaseResourceAsync(entityResource);
        entityJob = IB::Asset::releaseResourceAsync(savedEntityResource);
        waitOnJob(entityJob);
    }

    {
        IB::Resource::Handle entityResource;
        IB::Job::Handle entityJob = IB::Asset::loadResourceAsync("TestEntity.entt", { IB::EntityType }, &entityResource);
        waitOnJob(entityJob);
        entityJob = IB::Asset::releaseResourceAsync(entityResource);
        waitOnJob(entityJob);
    }

    IB::killEntitySystem();
    IB::killRendererFrontend();
    IB::destroyWindow(window);
    IB::killJobSystem();
    IB::Serialization::killSerialization();
}
