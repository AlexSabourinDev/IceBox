#define _CRT_SECURE_NO_WARNINGS
#include "IBAsset.h"
#include "IBAllocator.h"
#include "IBLogging.h"
#include "IBJobs.h"
#include "IBPlatform.h"

#include <string.h>

namespace IB
{
    namespace Resource
    {
        namespace
        {
            struct Data
            {
                IB::Asset::FourCC Type = {};
                uint32_t PathHash = 0;
                IB::Job::Handle LoadingJob = {};
                IB::Asset::Handle Asset = IB::Asset::InvalidAsset;
                IB::File::Handle File = {};
                char Path[IB::MaxFilePath] = {};
            };
        }
    }
}

namespace
{
    constexpr char AssetPath[] = "../Assets/Compiled";

    char const *fileExtension(char const *filepath)
    {
        uint32_t filepathLength = static_cast<uint32_t>(strlen(filepath));
        int32_t extensionIndex;
        for (extensionIndex = filepathLength - 1; extensionIndex >= 0; extensionIndex--)
        {
            if (filepath[extensionIndex] == '.') // includes period
            {
                break;
            }
        }

        if (extensionIndex < 0)
        {
            return filepath + filepathLength; // Simply return the pointer to our null terminator
        }

        return filepath + extensionIndex;
    }

    // http://www.cse.yorku.ca/~oz/hash.html by Dan Bernstein
    uint32_t hash(char const *path)
    {
        uint32_t hash = 5381;
        for (; *path != 0; path++)
        {
            hash = hash * 33 + *path;
        }

        return hash;
    }

    struct StreamerData
    {
        IB::Asset::IStreamer *Streamer = nullptr;
        IB::Asset::FourCC Type = IB::Asset::InvalidFourCC;
    };

    constexpr uint32_t MaxStreamerCount = 100;
    StreamerData Streamers[MaxStreamerCount];

    struct LoadResult
    {
        IB::JobResult Result;
        IB::Asset::Handle Asset;
    };

    struct ResourceEntry
    {
        uint32_t RefCount;
        IB::Resource::Data *Resource;
    };

    constexpr uint32_t MaxTableEntries = 1024 * 1024;
    ResourceEntry ResourceHashTable[MaxTableEntries];

    IB::Asset::IStreamer *getStreamer(IB::Asset::FourCC type)
    {
        for (uint32_t i = 0; i < MaxStreamerCount; i++)
        {
            if (Streamers[i].Type == type)
            {
                return Streamers[i].Streamer;
            }
        }

        return nullptr;
    }

    LoadResult load(IB::Asset::IStreamer *streamer, IB::Asset::LoadContext *context)
    {
        IB::Asset::LoadContinuation result = streamer->loadAsync(context);
        if (result.Continuation == IB::Asset::LoadContinuation::Advance)
        {
            IB::volatileStore(&context->State, result.Data.Advance.NextState);
            IB::threadStoreStoreFence(); // Make sure our state value write is visible before our job is written to the job pool.

            IB_ASSERT(result.Data.Advance.DependencyCount > 0, "If we want to advance, we need dependencies.");
            // Resignal our job once our dependencies are complete.
            IB::continueJob(context->Handle, result.Data.Advance.Dependencies, result.Data.Advance.DependencyCount); // Job could be signaled before we return
            return {IB::JobResult::Sleep};
        }
        else
        {
            return LoadResult{IB::JobResult::Complete, result.Data.Complete.Handle};
        }
    }

    IB::Job::Handle loadBinaryAsync(IB::Resource::Data *resource, IB::Asset::FourCC type, IB::Asset::OnResourceLoad *onResourceLoad, void *data)
    {
        IB::Asset::IStreamer *streamer = getStreamer(type);
        IB_ASSERT(streamer != nullptr, "Failed to find streamer!");

        IB::Asset::LoadContext *loadContext = IB::allocate<IB::Asset::LoadContext>();
        IB::Job::Handle fileJob = IB::launchJob([resource, loadContext]() {
            char fullPath[IB::MaxFilePath] = {};
            sprintf(fullPath, "%s/%s", AssetPath, resource->Path);

            resource->File = IB::openFile(fullPath, IB::OpenFileOptions::Read);
            loadContext->Stream = {reinterpret_cast<uint8_t *>(IB::mapFile(resource->File))};
            IB_ASSERT(loadContext->Stream.Memory != nullptr, "Failed to map file!");
            fromBinary(&loadContext->Stream, &loadContext->SaveVersion);
            return IB::JobResult::Complete;
        });

        loadContext->Handle = IB::reserveJob([loadContext, streamer, onResourceLoad, data, resource]() {
            LoadResult loadResult = load(streamer, loadContext);
            if (loadResult.Result == IB::JobResult::Complete)
            {
                resource->Asset = loadResult.Asset;

                onResourceLoad(data, IB::Resource::toHandle(resource->PathHash), IB::Asset::ResourceLoad::Available);
                IB::deallocate(loadContext);
            }
            return loadResult.Result;
        });
        IB::continueJob(loadContext->Handle, &fileJob, 1);

        return loadContext->Handle;
    }

} // namespace

namespace IB
{
    namespace Asset
    {
        LoadContinuation wait(Job::Handle *dependencies, uint32_t dependencyCount, uint32_t nextState)
        {
            LoadContinuation result = {};
            memcpy(result.Data.Advance.Dependencies, dependencies, sizeof(Job::Handle) * dependencyCount);
            result.Data.Advance.DependencyCount = dependencyCount;
            result.Data.Advance.NextState = nextState;
            result.Continuation = LoadContinuation::Advance;
            return result;
        }

        LoadContinuation complete(Asset::Handle handle)
        {
            LoadContinuation result = {};
            result.Data.Complete.Handle = handle;
            result.Continuation = LoadContinuation::Complete;
            return result;
        }

        void IStreamer::saveThreadSafe(SaveContext *context)
        {
            (void)context;
            IB_ASSERT(false, "Loader does not support saving this asset");
        }

        void addStreamer(FourCC type, IStreamer *streamer)
        {
            for (uint32_t i = 0; i < MaxStreamerCount; i++)
            {
                if (Streamers[i].Type == InvalidFourCC)
                {
                    Streamers[i].Type = type;
                    Streamers[i].Streamer = streamer;
                    break;
                }
            }
        }

        Resource::Handle createResourceThreadSafe(char const *assetPath, FourCC type, Asset::Handle asset)
        {
            uint32_t pathHash = hash(assetPath);
            uint32_t hashTableIndex = pathHash % MaxTableEntries;

            bool newAssetEntry = atomicIncrement(&ResourceHashTable[hashTableIndex].RefCount) == 1;
            IB_ASSERT(newAssetEntry, "createResource should only be called on an asset that does not exist!");

            IB::Resource::Data *resource = allocate<IB::Resource::Data>();
            resource->PathHash = hash(assetPath);
            resource->Type = type;
            resource->Asset = asset;

            IB_ASSERT(strlen(assetPath) < IB::MaxFilePath - 1, "Path is too long!");
            strcpy(resource->Path, assetPath);

            // Assure that the writes to our resource are visible
            // before we write our resource to the table
            threadRelease();
            volatileStore(&ResourceHashTable[hashTableIndex].Resource, resource);

            return Resource::toHandle(pathHash);
        }

        Job::Handle loadSubAssetAsync(LoadContext const* parentContext, FourCC type, Asset::Handle parentAsset, OnSubAssetLoad *onSubAssetLoad, void *data)
        {
            LoadContext *context = allocate<LoadContext>(parentContext->Stream, parentAsset);
            context->SaveVersion = parentContext->SaveVersion;
            IB::Asset::IStreamer *streamer = getStreamer(type);
            if (streamer != nullptr)
            {
                context->Handle = reserveJob([context, streamer, onSubAssetLoad, data]() {
                    LoadResult loadResult = load(streamer, context);
                    if (loadResult.Result == JobResult::Complete)
                    {
                        onSubAssetLoad(data, loadResult.Asset);
                        deallocate(context);
                    }
                    return loadResult.Result;
                });
                launchJob(context->Handle);
            }
            return context->Handle;
        }

        Job::Handle loadResourceAsync(char const *assetPath, FourCC type, OnResourceLoad *onResourceLoad, void *data)
        {
            uint32_t pathHash = hash(assetPath);
            uint32_t hashTableIndex = pathHash % MaxTableEntries;

            bool newAssetEntry = atomicIncrement(&ResourceHashTable[hashTableIndex].RefCount) == 1; // Assure that we keep a ref count for this entry to assure that no one unloads the asset from under us.
            // Assure that we don't load our asset pointer before we've guaranteed our ref count
            threadAcquire();

            Job::Handle requestHandle = {};
            if (!newAssetEntry)
            {
                // We could be asking for our asset to be loaded twice before our first load is complete.
                // Simply wait for it to be complete.
                while (volatileLoad(&ResourceHashTable[hashTableIndex].Resource) == nullptr)
                {
                }
                // Assure we don't move our resource reads/writes above this point.
                // Our reads are dependent on the volatileLoad above however, perhaps we can avoid the acquire due to a data dependency?
                threadAcquire();

                IB::Resource::Data *resource = volatileLoad(&ResourceHashTable[hashTableIndex].Resource);

                // If our data loads are reordered above the branch,
                // and our loaded asset data has not been written yet
                // We'll simply wait until the loading job is visible
                // and submit a "continue" job to be run after our loading job

                if (resource->Asset != InvalidAsset)
                {
                    onResourceLoad(data, Resource::toHandle(pathHash), ResourceLoad::Available);
                }
                else
                {
                    requestHandle = continueJob([resource, onResourceLoad, data, pathHash]() {
                        IB_ASSERT(resource->Asset != InvalidAsset, "No asset handle loaded!");
                        onResourceLoad(data, Resource::toHandle(pathHash), ResourceLoad::Available);
                        return JobResult::Complete;
                    },
                                                &resource->LoadingJob, 1);
                }
            }
            else // New resource, request load
            {
                IB::Resource::Data *resource = allocate<IB::Resource::Data>();
                resource->PathHash = pathHash;
                resource->Type = type;

                IB_ASSERT(strlen(assetPath) < IB::MaxFilePath - 1, "Path is too long!");
                strcpy(resource->Path, assetPath);

                requestHandle = loadBinaryAsync(resource, type, onResourceLoad, data);
                resource->LoadingJob = requestHandle;

                // Assure that the writes to our resource are visible
                // before we write our resource to the table
                threadRelease();
                volatileStore(&ResourceHashTable[hashTableIndex].Resource, resource);
                onResourceLoad(data, Resource::toHandle( pathHash ), ResourceLoad::Loading);
            }

            return requestHandle;
        }

        Job::Handle releaseResourceAsync(Resource::Handle resourceHandle)
        {
            uint32_t mappingIndex = Resource::toValue(resourceHandle) % MaxTableEntries;
            // We could be asking to release our resource before our resource has been assigned to the resource table.
            while (volatileLoad(&ResourceHashTable[mappingIndex].Resource) == nullptr)
            {
            }
            // Assure we don't move our resource reads/writes past this point.
            // Our reads are dependent on the volatileLoad above however, perhaps we can avoid the acquire due to a data dependency?
            threadAcquire();

            // Load and copy our data before we do our decrement.
            IB::Resource::Data *resource = volatileLoad(&ResourceHashTable[mappingIndex].Resource);

            // Make sure our asset pointer load is completed before our ref count store is complete
            // If it isn't, we can potentially load someone else's data
            // Example:
            // Thread 1: Decrement ref count, 0
            // Thread 2: Increment ref count, 1
            // Thread 2: Allocate data and add to table
            // Thread 1: Load asset from table (Oops! This is now Thread 2's data!)
            //
            // Instead, we want:
            // Thread 1: Load asset from table (guaranteed our data)
            // Thread 1: Decrement ref count, 0
            // Thread 2: Increment ref count, 1
            // Thread 2: Allocate data and add to table
            threadLoadStoreFence();

            Job::Handle job = {};
            if (atomicDecrement(&ResourceHashTable[mappingIndex].RefCount) == 0)
            {
                auto onUnload = [resource]() {
                    getStreamer(resource->Type)->unloadThreadSafe(resource->Asset);
                    unmapFile(resource->File);
                    closeFile(resource->File);
                    deallocate(resource);
                    return JobResult::Complete;
                };

                // At this point, we own the loaded asset. We're free to do what we want with it's data
                if (volatileLoad(&resource->Asset) == InvalidAsset) // Not loaded yet
                {
                    job = continueJob(onUnload, &resource->LoadingJob, 1);
                }
                else
                {
                    job = launchJob(onUnload);
                }
            }

            return job;
        }

        void unloadSubAssetThreadSafe(Asset::Handle asset, FourCC type)
        {
            getStreamer(type)->unloadThreadSafe(asset);
        }

        Job::Handle saveResourceAsync(Resource::Handle resourceHandle)
        {
            return launchJob([resourceHandle]() {
                uint32_t hashTableIndex = Resource::toValue(resourceHandle) % MaxTableEntries;

                atomicIncrement(&ResourceHashTable[hashTableIndex].RefCount); // Keep a reference count as we save, we don't want to dump our asset

                IB_ASSERT(volatileLoad(&ResourceHashTable[hashTableIndex].Resource) != nullptr, "Asset should be in our table!");
                IB::Resource::Data *resource = volatileLoad(&ResourceHashTable[hashTableIndex].Resource);
                // Assure we don't move our resource reads/writes past this point.
                // Our reads are dependent on the volatileLoad above however, perhaps we can avoid the acquire due to a data dependency?
                threadAcquire();

                char fullPath[IB::MaxFilePath] = {};
                sprintf(fullPath, "%s/%s", AssetPath, resource->Path);

                IB::File::Handle file = IB::openFile(fullPath, OpenFileOptions::Create | OpenFileOptions::Overwrite | OpenFileOptions::Write);

                Serialization::FileStream fileStream = {file};
                Serialization::toBinary(&fileStream, Serialization::Version);

                SaveContext saveContext = {&fileStream, resource->Asset};
                getStreamer(resource->Type)->saveThreadSafe(&saveContext);
                flush(&fileStream);

                // Once we're done saving to our file, assure that we close our write access to it.
                IB::closeFile(file);

                releaseResourceAsync(resourceHandle); // Go through our release flow to assure that we do the appropriate cleanup if we're the last reference.

                return JobResult::Complete;
            });
        }

        void saveSubAssetThreadSafe(Serialization::FileStream *stream, FourCC type, Asset::Handle asset)
        {
            SaveContext saveContext = {stream, asset};
            getStreamer(type)->saveThreadSafe(&saveContext);
        }

        Asset::Handle getAssetFromResource(Resource::Handle resourceHandle)
        {
            uint32_t hashTableIndex = Resource::toValue(resourceHandle) % MaxTableEntries;
            IB_ASSERT(ResourceHashTable[hashTableIndex].RefCount > 0, "Resource is not loaded!");

            return ResourceHashTable[hashTableIndex].Resource->Asset;
        }

        bool isResourceAssetAvailable(Resource::Handle resourceHandle)
        {
            if (resourceHandle != InvalidResource)
            {
                uint32_t hashTableIndex = Resource::toValue(resourceHandle) % MaxTableEntries;
                IB_ASSERT(ResourceHashTable[hashTableIndex].RefCount > 0, "Resource is not loaded!");

                return ResourceHashTable[hashTableIndex].Resource->Asset != InvalidAsset;
            }
            else
            {
                return false;
            }
        }

        char const *getResourcePath(Resource::Handle resourceHandle)
        {
            uint32_t hashTableIndex = Resource::toValue(resourceHandle) % MaxTableEntries;
            IB_ASSERT(ResourceHashTable[hashTableIndex].RefCount > 0, "Resource is not loaded!");

            IB::Resource::Data *resource = volatileLoad(&ResourceHashTable[hashTableIndex].Resource);
            // Assure we don't move our resource reads/writes past this point.
            // Our reads are dependent on the volatileLoad above however, perhaps we can avoid the acquire due to a data dependency?
            threadAcquire();

            return resource->Path;
        }

    } // namespace Asset
} // namespace IB
