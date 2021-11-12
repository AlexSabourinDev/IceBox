#define _CRT_SECURE_NO_WARNINGS
#include "IBAsset.h"
#include "IBAllocator.h"
#include "IBLogging.h"
#include "IBJobs.h"
#include "IBPlatform.h"

#include <string.h>
#include <stdio.h>

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
                IB::Asset::Handle Asset = IB::InvalidAsset;
                IB::File::Handle File = {};
                char Path[IB::MaxFilePath] = {};
            };
        }
    }
}

namespace IB
{
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
            IB::Asset::FourCC Type = IB::InvalidFourCC;
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

                // Resignal our job once our dependencies are complete.
                IB::continueJob(context->Handle, result.Data.Advance.Dependencies, result.Data.Advance.DependencyCount); // Job could be signaled before we return
                return { IB::JobResult::Sleep };
            }
            else
            {
                return LoadResult{ IB::JobResult::Complete, result.Data.Complete.Handle };
            }
        }

        IB::Job::Handle loadBinaryAsync(IB::Resource::Data *resource, IB::Asset::FourCC type, IB::Asset::OnResourceLoad *onResourceLoad, void *data)
        {
            IB::Asset::IStreamer *streamer = getStreamer(type);
            IB_ASSERT(streamer != nullptr, "Failed to find streamer!");

            IB::Asset::LoadContext *loadContext = IB::allocate<IB::Asset::LoadContext>();
            loadContext->LoadingAsset = streamer->create();

            IB::Job::Handle fileJob = IB::launchJob([resource, loadContext](Job::Handle /*currentJob*/)
            {
                char fullPath[IB::MaxFilePath] = {};
                sprintf(fullPath, "%s/%s", AssetPath, resource->Path);

                resource->File = IB::openFile(fullPath, IB::OpenFileOptions::Read);
                if (resource->File != IB::InvalidFile)
                {
                    loadContext->Stream = { reinterpret_cast<uint8_t *>(IB::mapFile(resource->File)) };
                    IB_ASSERT(loadContext->Stream.Memory != nullptr, "Failed to map file!");
                    fromBinary(&loadContext->Stream, &loadContext->SaveVersion);
                }
                return IB::JobResult::Complete;
            });

            loadContext->Handle = IB::reserveJob([loadContext, streamer, onResourceLoad, data, resource](Job::Handle /*currentJob*/)
            {
                if (resource->File == IB::InvalidFile) // Invalid files get marked as available with no asset.
                {
                    onResourceLoad(data, IB::Resource::toHandle(resource->PathHash), IB::Asset::ResourceLoad::NotExisting);
                    IB::deallocate(loadContext);
                    return JobResult::Complete;
                }

                LoadResult loadResult = load(streamer, loadContext);
                if (loadResult.Result == IB::JobResult::Complete)
                {
                    resource->Asset = loadResult.Asset;

                    onResourceLoad(data, IB::Resource::toHandle(resource->PathHash), IB::Asset::ResourceLoad::Available);

                    // If our file doesn't need to be memory mapped, we can let go of our file handle.
                    if (!streamer->memoryMapped())
                    {
                        unmapFile(resource->File);
                        closeFile(resource->File);
                        resource->File = InvalidFile;
                    }

                    IB::deallocate(loadContext);
                }
                return loadResult.Result;
            });
            IB::continueJob(loadContext->Handle, &fileJob, 1);

            return loadContext->Handle;
        }

        struct LoadAssetDesc
        {
            Serialization::ReadStream Stream;
            uint32_t SaveVersion;
            Asset::FourCC Type = InvalidFourCC;
            Asset::Handle LoadingAsset = InvalidAsset;
            Asset::Handle BaseAsset = InvalidAsset;
            Asset::OnSubAssetLoad *OnSubAssetLoad = nullptr;
            void *OnSubAssetLoadData = nullptr;
        };

        Job::Handle loadAssetAsync(LoadAssetDesc desc)
        {
            Asset::IStreamer *streamer = getStreamer(desc.Type);
            Job::Handle jobHandle = {};
            if (streamer != nullptr)
            {
                Asset::LoadContext loadContext = {};
                loadContext.SaveVersion = desc.SaveVersion;
                loadContext.LoadingAsset = desc.LoadingAsset;
                loadContext.BaseAsset = desc.BaseAsset;
                loadContext.Stream = desc.Stream;

                jobHandle = launchJob(
                    [loadContext, streamer, onSubAssetLoad = desc.OnSubAssetLoad, data = desc.OnSubAssetLoadData](Job::Handle currentJob) mutable
                    {
                        loadContext.Handle = currentJob;
                        LoadResult loadResult = load(streamer, &loadContext);
                        if (loadResult.Result == JobResult::Complete)
                        {
                            onSubAssetLoad(data, loadResult.Asset);
                        }
                        return loadResult.Result;
                    });
            }
            return jobHandle;
        }

        Job::Handle loadAssetInPlaceAsync(Serialization::ReadStream stream, Asset::FourCC type, Asset::Handle loadingAsset, Asset::Handle baseAsset, Asset::OnSubAssetLoad *onSubAssetLoad, void *data)
        {
            LoadAssetDesc desc =
            {
                stream,
                Serialization::Version, // In-place loading does not have any versioning.
                type,
                loadingAsset,
                baseAsset,
                onSubAssetLoad,
                data
            };
            return loadAssetAsync(desc);
        }
    } // namespace
} // IB

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

        void setResourceAsset(Resource::Handle resourceHandle, Asset::Handle asset)
        {
            uint32_t hashTableIndex = Resource::toValue(resourceHandle) % MaxTableEntries;
            IB_ASSERT(ResourceHashTable[hashTableIndex].RefCount > 0, "Resource is not loaded!");

            ResourceHashTable[hashTableIndex].Resource->Asset = asset;
        }

        IB_API Asset::Handle createAsset(FourCC type)
        {
            return getStreamer(type)->create();
        }

        Job::Handle loadSubAssetAsync(LoadSubAssetDesc desc, OnSubAssetLoad *onSubAssetLoad, void *data)
        {
            LoadAssetDesc loadAssetDesc =
            {
                desc.ParentContext->Stream,
                desc.ParentContext->SaveVersion,
                desc.Type,
                desc.LoadingAsset,
                desc.BaseAsset,
                onSubAssetLoad,
                data
            };
            return loadAssetAsync(loadAssetDesc);
        }

        Job::Handle loadResourceAsync(char const *assetPath, FourCC type, OnResourceLoad *onResourceLoad, void *data)
        {
            if (assetPath[0] == '\0')
            {
                onResourceLoad(data, InvalidResource, ResourceLoad::NotExisting);
                return InvalidJob;
            }

            uint32_t pathHash = hash(assetPath);
            uint32_t hashTableIndex = pathHash % MaxTableEntries;

            bool newAssetEntry = atomicIncrement(&ResourceHashTable[hashTableIndex].RefCount) == 1; // Assure that we keep a ref count for this entry to assure that no one unloads the asset from under us.
            // Assure that we don't load our asset pointer before we've guaranteed our ref count
            threadAcquire();

            Job::Handle requestHandle = InvalidJob;
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
                    onResourceLoad(data, Resource::toHandle(pathHash), ResourceLoad::Loading);

                    requestHandle = continueJob([resource, onResourceLoad, data, pathHash](Job::Handle /*currentJob*/) {
                        if (resource->Asset != InvalidAsset)
                        {
                            onResourceLoad(data, Resource::toHandle(pathHash), ResourceLoad::Available);
                        }
                        else
                        {
                            onResourceLoad(data, Resource::toHandle(pathHash), ResourceLoad::NotExisting);
                        }
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

        void acquireResource(Resource::Handle resource)
        {
            if (resource != InvalidResource)
            {
                uint32_t mappingIndex = Resource::toValue(resource) % MaxTableEntries;
                bool newAssetEntry = atomicIncrement(&ResourceHashTable[mappingIndex].RefCount) == 1;
                IB_ASSERT(!newAssetEntry, "Acquiring a resource must mean that we already had our resource loaded and a ref count.");
            }
        }

        Job::Handle releaseResourceAsync(Resource::Handle resourceHandle)
        {
            if (resourceHandle == InvalidResource)
            {
                return InvalidJob;
            }

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

            Job::Handle job = InvalidJob;
            if (atomicDecrement(&ResourceHashTable[mappingIndex].RefCount) == 0)
            {
                auto onUnload = [resource](Job::Handle /*currentJob*/) {

                    if (resource->Asset != InvalidAsset)
                    {
                        getStreamer(resource->Type)->unloadThreadSafe(resource->Asset);
                    }

                    if (resource->File != InvalidFile)
                    {
                        unmapFile(resource->File);
                        closeFile(resource->File);
                    }
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

        void unloadAssetThreadSafe(FourCC type, Asset::Handle asset)
        {
            getStreamer(type)->unloadThreadSafe(asset);
        }

        Job::Handle saveResourceAsync(Resource::Handle resourceHandle)
        {
            return launchJob([resourceHandle](Job::Handle /*currentJob*/) {
                uint32_t hashTableIndex = Resource::toValue(resourceHandle) % MaxTableEntries;

                atomicIncrement(&ResourceHashTable[hashTableIndex].RefCount); // Keep a reference count as we save, we don't want to dump our asset

                IB_ASSERT(volatileLoad(&ResourceHashTable[hashTableIndex].Resource) != nullptr, "Asset should be in our table!");
                IB::Resource::Data *resource = volatileLoad(&ResourceHashTable[hashTableIndex].Resource);
                // Assure we don't move our resource reads/writes past this point.
                // Our reads are dependent on the volatileLoad above however, perhaps we can avoid the acquire due to a data dependency?
                threadAcquire();

                char fullPath[IB::MaxFilePath] = {};
                sprintf(fullPath, "%s/%s", AssetPath, resource->Path);

                Serialization::WriteStream writeStream = {};
                Serialization::toBinary(&writeStream, Serialization::Version);

                SaveContext saveContext = {&writeStream, resource->Asset};
                getStreamer(resource->Type)->saveThreadSafe(&saveContext);

                IB::File::Handle file = IB::openFile(fullPath, OpenFileOptions::Create | OpenFileOptions::Overwrite | OpenFileOptions::Write);
                IB::writeToFile(file, writeStream.Buffer, writeStream.BufferCursor);
                IB::closeFile(file);

                releaseResourceAsync(resourceHandle); // Go through our release flow to assure that we do the appropriate cleanup if we're the last reference.

                return JobResult::Complete;
            });
        }

        void saveAssetThreadSafe(Serialization::WriteStream *stream, FourCC type, Asset::Handle asset, Asset::Handle baseAsset)
        {
            SaveContext saveContext = {stream, asset, baseAsset};
            getStreamer(type)->saveThreadSafe(&saveContext);
        }

        Asset::Handle cloneAssetThreadSafe(Asset::Handle asset, FourCC type)
        {
            Asset::IStreamer* streamer = getStreamer(type);

            Serialization::WriteStream writeStream;
            Asset::saveAssetThreadSafe(&writeStream, type, asset);

            Asset::Handle newAsset = streamer->create();
            IB_ASSERT(newAsset != IB::InvalidAsset, "Cloning API expects to be able to create an asset.");
            Serialization::ReadStream readStream = { writeStream.Buffer };
            Asset::Handle baseAsset = InvalidAsset;
            IB::wait(loadAssetInPlaceAsync(readStream, type, newAsset, baseAsset,
                [](void *, IB::Asset::Handle)
                {
                }, nullptr));

            return newAsset;
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
            if (resourceHandle == InvalidResource)
            {
                return "";
            }
            else
            {
                uint32_t hashTableIndex = Resource::toValue(resourceHandle) % MaxTableEntries;
                IB_ASSERT(ResourceHashTable[hashTableIndex].RefCount > 0, "Resource is not loaded!");

                IB::Resource::Data *resource = volatileLoad(&ResourceHashTable[hashTableIndex].Resource);
                // Assure we don't move our resource reads/writes past this point.
                // Our reads are dependent on the volatileLoad above however, perhaps we can avoid the acquire due to a data dependency?
                threadAcquire();

                return resource->Path;
            }
        }

    } // namespace Asset
} // namespace IB
