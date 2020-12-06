#define _CRT_SECURE_NO_WARNINGS
#include <IBEngine/IBMath.h>
#include <IBEngine/IBSerialization.h>
#include <IBEngine/IBJobs.h>
#include <IBEngine/IBRendererFrontend.h>
#include <IBEngine/IBAllocator.h>
#include <IBEngine/IBLogging.h>
#include <IBEngine/IBAsset.h>
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

struct RendererProperty
{
    IB::Asset::ResourceHandle MeshResource;
};
RendererProperty RendererProperties[1024];
uint32_t ActiveRendererProps = 0;

class RendererPropertyAssetStreamer : public IB::Asset::IStreamer
{
public:
    IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
    {
        enum
        {
            LoadMesh = 0,
            Complete
        };

        if (context->State == LoadMesh)
        {
            context->Data = ActiveRendererProps++;

            char const *path;
            fromBinary(&context->Stream, &path);

            IB::JobHandle meshJob = IB::Asset::loadResourceAsync(path, IB::Asset::toFourCC("MESH"), &RendererProperties[context->Data].MeshResource);
            return IB::Asset::wait(&meshJob, 1, Complete);
        }
        else
        {
            return IB::Asset::complete({context->Data});
        }
    }

    void saveThreadSafe(IB::Asset::SaveContext *context) override
    {
        RendererProperty renderer = RendererProperties[context->Asset.Value];
        char const *resourcePath = IB::Asset::GetResourcePath(renderer.MeshResource);
        toBinary(context->Stream, resourcePath);
    }

    void unloadThreadSafe(IB::Asset::AssetHandle asset) override
    {
        IB::Asset::releaseResourceAsync(RendererProperties[asset.Value].MeshResource);
    }
};

class TransformPropertyAssetStreamer : public IB::Asset::IStreamer
{
public:
    IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
    {
        IB::Mat3x4 localTransform;
        fromBinary(&context->Stream, &localTransform);

        LocalTransforms[ActiveTransforms] = localTransform;
        WorldTransforms[ActiveTransforms] = localTransform;
        EntityMap[ActiveTransforms] = {static_cast<uint32_t>(context->ParentAsset.Value)};
        return IB::Asset::complete({ActiveTransforms++});
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

struct EntityProperty
{
    IB::Asset::FourCC Type;
    IB::Asset::AssetHandle Asset;
};

struct EntityAsset
{
    EntityID Id;
    EntityProperty *Properties;
    uint32_t PropertyCount;
};

EntityAsset EntityAssets[100];

class EntityAssetStreamer : public IB::Asset::IStreamer
{
public:
    IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
    {
        enum State
        {
            LoadProperties = 0,
            Log
        };

        if (context->State == LoadProperties)
        {
            fromBinary(&context->Stream, &EntityAssets[0].Id);
            fromBinary(&context->Stream, &EntityAssets[0].PropertyCount);

            EntityAssets[0].Properties = IB::allocateArray<EntityProperty>(EntityAssets[0].PropertyCount);

            IB_ASSERT(EntityAssets[0].PropertyCount <= IB::Asset::MaxDependencyCount, "Too many properties!");
            IB::JobHandle loadHandles[IB::Asset::MaxDependencyCount];
            uint32_t handleCount = 0;
            for (uint32_t i = 0; i < EntityAssets[0].PropertyCount; i++)
            {
                fromBinary(&context->Stream, &EntityAssets[0].Properties[i].Type);
                uint32_t offset;
                fromBinary(&context->Stream, &offset);
                loadHandles[handleCount++] = IB::Asset::loadSubAssetAsync(context->Stream, EntityAssets[0].Properties[i].Type, {EntityAssets[0].Id.Value}, &EntityAssets[0].Properties[i].Asset);
                advance(&context->Stream, offset);
            }

            context->Data = 0;
            return IB::Asset::wait(loadHandles, handleCount, Log);
        }
        else
        {
            IB_LOG(IB::LogLevel::Log, "Asset", "Entity Load Complete");
            return IB::Asset::complete({context->Data});
        }
    }

    void saveThreadSafe(IB::Asset::SaveContext *context) override
    {
        uint32_t entityIndex = static_cast<uint32_t>(context->Asset.Value);
        toBinary(context->Stream, EntityAssets[entityIndex].Id);
        toBinary(context->Stream, EntityAssets[entityIndex].PropertyCount);
        for (uint32_t i = 0; i < EntityAssets[entityIndex].PropertyCount; i++)
        {
            toBinary(context->Stream, EntityAssets[entityIndex].Properties[i].Type);

            uint32_t dummyWriteSize = 0;
            toBinary(context->Stream, dummyWriteSize);
            uint32_t writeStart = flush(context->Stream);
            IB::Asset::saveSubAssetThreadSafe(context->Stream, EntityAssets[entityIndex].Properties[i].Type, EntityAssets[entityIndex].Properties[i].Asset);
            uint32_t writeEnd = flush(context->Stream);

            // Write our written size right before our sub asset.
            uint32_t writeSize = writeEnd - writeStart;
            IB::writeToFile(context->Stream->File, &writeSize, sizeof(uint32_t), writeStart - sizeof(uint32_t));
        }
    }

    void unloadThreadSafe(IB::Asset::AssetHandle assetHandle) override
    {
        IB_ASSERT(EntityAssets[assetHandle.Value].PropertyCount > 0, "");
        IB::deallocateArray(EntityAssets[assetHandle.Value].Properties, EntityAssets[assetHandle.Value].PropertyCount);
    }
};

EntityAssetStreamer EntityStreamer;
RendererPropertyAssetStreamer RendererPropertyStreamer;
TransformPropertyAssetStreamer TransformPropertyStreamer;

int main()
{
    IB::WindowDesc winDesc = {};
    winDesc.Name = "Ice Box";
    winDesc.Width = 500;
    winDesc.Height = 500;
    winDesc.OnWindowMessage = [](void * /*data*/, IB::WindowMessage message) {
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
    IB::initRendererFrontend({&window});
    IB::Asset::addStreamer(IB::Asset::toFourCC("ENTT"), &EntityStreamer);
    IB::Asset::addStreamer(IB::Asset::toFourCC("TFRM"), &TransformPropertyStreamer);
    IB::Asset::addStreamer(IB::Asset::toFourCC("RNDR"), &RendererPropertyStreamer);

    auto waitOnJob = [](IB::JobHandle job) {
        IB::ThreadEvent threadEvent = IB::createThreadEvent();
        continueJob([threadEvent]() {
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
    IB::Asset::AssetHandle rendererPropertyAsset = {0};
    {
        IB::JobHandle meshJobHandle = IB::Asset::loadResourceAsync("Box.msh", IB::Asset::toFourCC("MESH"), &RendererProperties[0].MeshResource);
        waitOnJob(meshJobHandle);
    }

    // Create our transform property
    IB::Asset::AssetHandle transformPropertyAsset = {0};
    {
        LocalTransforms[0] = {};
    }

    // Create our entity asset
    IB::Asset::AssetHandle entityAsset = {0};
    {
        EntityProperty *properties = IB::allocateArray<EntityProperty>(2);
        properties[0] = {IB::Asset::toFourCC("RNDR"), rendererPropertyAsset};
        properties[1] = {IB::Asset::toFourCC("TFRM"), transformPropertyAsset};

        EntityAssets[0].Id = {0};
        EntityAssets[0].Properties = properties;
        EntityAssets[0].PropertyCount = 2;
    }

    // Save our resource
    {
        IB::Asset::ResourceHandle entityResource = IB::Asset::createResourceThreadSafe("TestEntity.entt", IB::Asset::toFourCC("ENTT"), entityAsset);
        IB::JobHandle saveJobHandle = saveResourceAsync(entityResource);

        waitOnJob(saveJobHandle);

        IB::Asset::ResourceHandle savedEntityResource;
        IB::JobHandle entityJobHandle = IB::Asset::loadResourceAsync("TestEntity.entt", IB::Asset::toFourCC("ENTT"), &savedEntityResource);
        waitOnJob(entityJobHandle);
        IB_ASSERT(savedEntityResource.Hash == entityResource.Hash, "Failed to load the same asset!");

        IB::Asset::releaseResourceAsync(entityResource);
        IB::Asset::releaseResourceAsync(savedEntityResource);
    }

    {
        IB::Asset::ResourceHandle entityResource;
        IB::JobHandle entityJobHandle = IB::Asset::loadResourceAsync("TestEntity.entt", IB::Asset::toFourCC("ENTT"), &entityResource);
        waitOnJob(entityJobHandle);
        IB::Asset::releaseResourceAsync(entityResource);
    }

    IB::killRendererFrontend();
    IB::destroyWindow(window);
    IB::killJobSystem();
    IB::Serialization::killSerialization();
}
