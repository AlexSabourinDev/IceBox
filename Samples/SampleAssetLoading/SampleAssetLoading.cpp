#define _CRT_SECURE_NO_WARNINGS
#include <IBEngine/IBMath.h>
#include <IBEngine/IBSerialization.h>
#include <IBEngine/IBJobs.h>
#include <IBEngine/IBRendererFrontend.h>
#include <IBEngine/IBAllocator.h>
#include <IBEngine/IBLogging.h>
#include <IBEngine/IBAsset.h>
#include <IBEngine/IBEntity.h>
#include <stddef.h>
#include <string.h>
#include <Windows.h>

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
        IB::Mat3x4 localTransform;
        fromBinary(&context->Stream, &localTransform);

        LocalTransforms[ActiveTransforms] = localTransform;
        WorldTransforms[ActiveTransforms] = localTransform;
        EntityMap[ActiveTransforms] = { static_cast<uint32_t>(context->ParentAsset.Value) };
        return IB::Asset::complete({ ActiveTransforms++ });
    }

    void saveThreadSafe(IB::Asset::SaveContext *context) override
    {
        IB::Mat3x4 transform = LocalTransforms[context->Asset.Value];
        toBinary(context->Stream, transform);
    }

    void unloadThreadSafe(IB::Asset::AssetHandle) override
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
    winDesc.OnWindowMessage = [](void * /*data*/, IB::WindowMessage message)
    {
        switch (message.Type)
        {
        case IB::WindowMessage::Close:
            IB::sendQuitMessage();
            break;
        }
    };
    IB::WindowHandle window = IB::createWindow(winDesc);

    IB::Serialization::initSerialization();
    IB::initJobSystem();
    IB::initRendererFrontend({ &window });
    IB::initEntitySystem();
    IB::Asset::addStreamer(IB::Asset::toFourCC("TFRM"), &TransformPropertyStreamer);

    auto waitOnJob = [](IB::JobHandle job)
    {
        IB::ThreadEvent threadEvent = IB::createThreadEvent();
        continueJob([threadEvent]()
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
        IB::Asset::ResourceHandle meshAsset1;
        IB::Asset::ResourceHandle meshAsset2;

        IB::Asset::loadResourceAsync("Box.msh", IB::Asset::toFourCC("MESH"), &meshAsset1);
        IB::JobHandle meshJobHandle = IB::Asset::loadResourceAsync("Box.msh", IB::Asset::toFourCC("MESH"), &meshAsset2);
        waitOnJob(meshJobHandle);

        IB_ASSERT(meshAsset1.Hash == meshAsset2.Hash, "Loaded the same asset.");
        IB::Asset::releaseResourceAsync(meshAsset2);
        IB::Asset::releaseResourceAsync(meshAsset1);
    }

    // Create our renderer property
    IB::PropertyHandle rendererPropertyAsset = { 0 };
    {
        IB::Asset::ResourceHandle meshHandle;
        IB::JobHandle meshJobHandle = IB::Asset::loadResourceAsync("Box.msh", IB::Asset::toFourCC("MESH"), &meshHandle);
        waitOnJob(meshJobHandle);
        rendererPropertyAsset = IB::createRendererProperty(meshHandle);
    }

    // Create our transform property
    IB::PropertyHandle transformPropertyAsset = { 0 };
    {
        LocalTransforms[0] = {};
    }

    // Create our entity asset
    IB::EntityHandle entityAsset = IB::createEntity();
    {
        IB::addPropertyToEntity(entityAsset, IB::Asset::toFourCC("RNDR"), rendererPropertyAsset);
        IB::addPropertyToEntity(entityAsset, IB::Asset::toFourCC("TFRM"), transformPropertyAsset);
    }

    // Save our resource
    {
        IB::Asset::ResourceHandle entityResource = IB::Asset::createResourceThreadSafe("TestEntity.entt", IB::Asset::toFourCC("ENTT"), IB::toAssetHandle(entityAsset));
        IB::JobHandle saveJobHandle = saveResourceAsync(entityResource);

        waitOnJob(saveJobHandle);

        IB::Asset::ResourceHandle savedEntityResource;
        IB::JobHandle entityJobHandle = IB::Asset::loadResourceAsync("TestEntity.entt", IB::Asset::toFourCC("ENTT"), &savedEntityResource);
        waitOnJob(entityJobHandle);
        IB_ASSERT(savedEntityResource.Hash == entityResource.Hash, "Failed to load the same asset!");

        IB::Asset::releaseResourceAsync(entityResource);
        entityJobHandle = IB::Asset::releaseResourceAsync(savedEntityResource);
        waitOnJob(entityJobHandle);
    }

    {
        IB::Asset::ResourceHandle entityResource;
        IB::JobHandle entityJobHandle = IB::Asset::loadResourceAsync("TestEntity.entt", IB::Asset::toFourCC("ENTT"), &entityResource);
        waitOnJob(entityJobHandle);
        entityJobHandle = IB::Asset::releaseResourceAsync(entityResource);
        waitOnJob(entityJobHandle);
    }

    IB::killEntitySystem();
    IB::killRendererFrontend();
    IB::destroyWindow(window);
    IB::killJobSystem();
    IB::Serialization::killSerialization();
}
