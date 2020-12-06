#define _CRT_SECURE_NO_WARNINGS
#include <IBEngine/IBMath.h>
#include <IBEngine/IBSerialization.h>
#include <IBEngine/IBJobs.h>
#include <IBEngine/IBRendererFrontend.h>
#include <IBEngine/IBAllocator.h>
#include <IBEngine/IBLogging.h>
#include <stddef.h>
#include <string.h>
#include <Windows.h>

namespace IB
{
    namespace Asset
    {
        struct LoadContext
        {
            Serialization::MemoryStream Stream;
            JobHandle Handle;
            uint64_t ParentAsset = 0;
            uint32_t State = 0;
        };

        struct LoadResult
        {
            enum
            {
                Advance,
                Complete
            };

            JobHandle Dependencies[32];
            uint32_t DependencyCount = 0;
            uint32_t NextState;
            uint32_t Continuation = Complete;
        };

        LoadResult wait(JobHandle* dependencies, uint32_t dependencyCount, uint32_t nextState)
        {
            LoadResult result = {};
            memcpy(result.Dependencies, dependencies, sizeof(JobHandle) * dependencyCount);
            result.DependencyCount = dependencyCount;
            result.NextState = nextState;
            result.Continuation = LoadResult::Advance;
            return result;
        }

        LoadResult complete()
        {
            return {};
        }

        using LoadFunc = LoadResult(LoadContext* context);

        struct Loader
        {
            LoadFunc* Load;
            uint32_t Type = 0;
        };
        constexpr uint32_t MaxLoaderCount = 100;
        Loader Loaders[MaxLoaderCount];

        void addLoader(uint32_t type, LoadFunc load)
        {
            for (uint32_t i = 0; i < MaxLoaderCount; i++)
            {
                if (Loaders[i].Type == 0)
                {
                    Loaders[i].Type = type;
                    Loaders[i].Load = load;
                    break;
                }
            }
        }

        JobResult load(Loader* loader, LoadContext* context)
        {
            LoadResult result = loader->Load(context);
            if (result.Continuation == LoadResult::Advance)
            {
                context->State = result.NextState;

                IB_ASSERT(result.DependencyCount > 0, "If we want to advance, we need dependencies.");
                // Resignal our job once our dependencies are complete.
                continueJob(context->Handle, result.Dependencies, result.DependencyCount);
                return JobResult::Sleep;
            }
            else
            {
                return JobResult::Complete;
            }
        }

        JobHandle loadAsync(Serialization::MemoryStream stream, uint32_t type, uint64_t parentAsset)
        {
            LoadContext* context = allocate<LoadContext>( stream, parentAsset );
            for (uint32_t i = 0; i < MaxLoaderCount; i++)
            {
                if (Loaders[i].Type == type)
                {
                    context->Handle = reserveJob([context, loader = &Loaders[i]]()
                    {
                        JobResult result = load(loader, context);
                        if (result == JobResult::Complete)
                        {
                            deallocate(context);
                        }
                        return result;
                    });
                    launchJob(context->Handle);
                }
            }
            return context->Handle;
        }

        IB::JobHandle loadAsync(char const *assetPath, uint32_t type, uint64_t parentAsset)
        {
            struct LoadFileData
            {
                File File;
                void *Memory;
                LoadContext Context;
            };

            Loader* loader = nullptr;
            for (uint32_t i = 0; i < MaxLoaderCount; i++)
            {
                if (Loaders[i].Type == type)
                {
                    loader = &Loaders[i];
                    break;
                }
            }

            LoadFileData* loadFileData = allocate<LoadFileData>();
            JobHandle fileJob = launchJob([assetPath, parentAsset, loadFileData]()
            {
                loadFileData->File = openFile(assetPath, OpenFileOptions::Read);
                loadFileData->Memory = mapFile(loadFileData->File);
                loadFileData->Context.Stream = { reinterpret_cast<uint8_t *>(loadFileData->Memory) };
                loadFileData->Context.ParentAsset = parentAsset;
                return JobResult::Complete;
            });

            loadFileData->Context.Handle = reserveJob([loadFileData, loader]()
            {
                JobResult result = load(loader, &loadFileData->Context);
                if (result == JobResult::Complete)
                {
                    deallocate(loadFileData);
                }
                return result;
            });
            continueJob(loadFileData->Context.Handle, &fileJob, 1);

            return loadFileData->Context.Handle;
        }
    }
}

struct TransformProperty
{
    IB::Mat3x4 Local;
};

struct EntityID
{
    uint32_t Value;
};

uint32_t EntityTable[1024];

IB::Mat3x4 LocalTransforms[1024];
IB::Mat3x4 WorldTransforms[1024];
EntityID EntityMap[1024];
uint32_t ActiveTransforms = 0;

IB::Asset::LoadResult onMeshAssetLoad(IB::Asset::LoadContext* context)
{
    printf("mesh\n");
    IB::MeshAsset mesh;
    fromBinary(&context->Stream, &mesh);
    return IB::Asset::complete();
}

IB::Asset::LoadResult onRendererPropertyLoad(IB::Asset::LoadContext* context)
{
    printf("renderer\n");
    enum
    {
        LoadMesh = 0,
        Complete
    };

    if (context->State == LoadMesh)
    {
        uint32_t stringSize;
        fromBinary(&context->Stream, &stringSize);

        char const* path = reinterpret_cast<char const*>(fromBinary(&context->Stream, stringSize));

        IB::JobHandle meshJob = IB::Asset::loadAsync(path, 'MESH', context->ParentAsset);
        return IB::Asset::wait(&meshJob, 1, Complete);
    }
    else
    {
        return IB::Asset::complete();
    }
}

IB::Asset::LoadResult onTransformPropertyLoad(IB::Asset::LoadContext *context)
{
    printf("transform\n");
    TransformProperty asset = {};
    fromBinary(&context->Stream, &asset);

    LocalTransforms[ActiveTransforms] = asset.Local;
    WorldTransforms[ActiveTransforms] = asset.Local;
    EntityMap[ActiveTransforms] = {static_cast<uint32_t>(context->ParentAsset)};
    ActiveTransforms++;
    return IB::Asset::complete();
}

IB::Asset::LoadResult onEntityLoad(IB::Asset::LoadContext *context)
{
    printf("entity\n");
    enum State
    {
        LoadProperties = 0,
        Log
    };

    if (context->State == LoadProperties)
    {
        EntityID entity;
        fromBinary(&context->Stream, &entity);
        EntityTable[0] = entity.Value;

        uint32_t propertyCount;
        fromBinary(&context->Stream, &propertyCount);

        IB::JobHandle loadHandles[10];
        uint32_t handleCount = 0;
        for (uint32_t i = 0; i < propertyCount; i++)
        {
            uint32_t type;
            fromBinary(&context->Stream, &type);
            uint32_t offset;
            fromBinary(&context->Stream, &offset);
            loadHandles[handleCount++] = IB::Asset::loadAsync(context->Stream, type, entity.Value);
            advance(&context->Stream, offset);
        }

        return IB::Asset::wait(loadHandles, handleCount, Log);
    }
    else
    {
        IB_LOG(IB::LogLevel::Log, "Asset", "Entity Load Complete");
        return IB::Asset::complete();
    }
}

int main()
{
    IB::initJobSystem();
    IB::Asset::addLoader('ENTT', &onEntityLoad);
    IB::Asset::addLoader('TFRM', &onTransformPropertyLoad);
    IB::Asset::addLoader('RNDR', &onRendererPropertyLoad);
    IB::Asset::addLoader('MESH', &onMeshAssetLoad);

#pragma pack(1)
    struct EntityAssetFile
    {
        EntityID Entity;
        uint32_t PropertyCount = 2;
        uint32_t TFRM = 'TFRM';
        uint32_t NextOffset1 = sizeof(TransformProperty);
        TransformProperty Transform;
        uint32_t RNDR = 'RNDR';
        uint32_t NextOffset2 = sizeof(uint32_t) + 29;
        uint32_t StringSize = 29;
        char Path[29] = "../Assets/Compiled/Box.c.msh";
    };

    EntityAssetFile asset;
    asset.Entity = {5};
    asset.Transform = {{{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}}}};
    IB::Serialization::MemoryStream stream = {reinterpret_cast<uint8_t *>(&asset)};

    IB::ThreadEvent threadEvent = IB::createThreadEvent();
    IB::JobHandle assetLoad = IB::Asset::loadAsync(stream, 'ENTT', 0);
    continueJob([threadEvent]()
    {
        IB::signalThreadEvent(threadEvent);
        return IB::JobResult::Complete;
    }, &assetLoad, 1);

    IB::waitOnThreadEvent(threadEvent);
    IB::destroyThreadEvent(threadEvent);
    IB::killJobSystem();
}
