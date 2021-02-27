#pragma once

#include "IBEngineAPI.h"
#include "IBSerialization.h"
#include "IBJobs.h"
#include "IBLogging.h"
#include <stdint.h>

namespace IB
{
    namespace Asset
    {
        struct FourCC
        {
            uint32_t Value;
        };

        constexpr FourCC toFourCC(char const (&text)[5]) // Char array is of 5 due to null terminator
        {
            return {static_cast<uint32_t>(text[0] | (text[1] << 8) | (text[2] << 16) | (text[3] << 24))};
        }

        struct AssetHandle
        {
            uint64_t Value = UINT64_MAX;
        };
        constexpr AssetHandle InvalidAsset = {UINT64_MAX};

        struct ResourceHandle
        {
            uint32_t Hash;
        };

        struct LoadContext
        {
            Serialization::MemoryStream Stream;
            AssetHandle ParentAsset = {};
            JobHandle Handle = {};
            uint64_t Data = 0;
            uint32_t State = 0;
        };

        struct SaveContext
        {
            Serialization::FileStream *Stream;
            AssetHandle Asset;
        };

        constexpr uint32_t MaxDependencyCount = 32;
        struct LoadContinuation
        {
            enum
            {
                Advance,
                Complete
            };

            union {
                struct
                {
                    JobHandle Dependencies[MaxDependencyCount];
                    uint32_t DependencyCount;
                    uint32_t NextState;
                } Advance;

                struct
                {
                    AssetHandle Handle;
                } Complete;
            } Data;
            uint32_t Continuation = Complete;
        };
        IB_API LoadContinuation wait(JobHandle *dependencies, uint32_t dependencyCount, uint32_t nextState);
        IB_API LoadContinuation complete(AssetHandle handle);

        class IStreamer
        {
        public:
            virtual LoadContinuation loadAsync(LoadContext *context) = 0;
            virtual void unloadThreadSafe(AssetHandle handle) = 0;
            virtual void saveThreadSafe(SaveContext *context)
            {
                (void)context;
                IB_ASSERT(false, "Loader does not support saving this asset");
            }
        };
        // NOTE: You might get a load state of available before you
        // receive a load state of Loading
        // This is up to you to decide how to handle that.
        struct ResourceLoad
        {
            enum State
            {
                Loading = 0,
                Available = 1
            };
        };

        using OnResourceLoad = void(void *data, ResourceHandle resource, ResourceLoad::State loadState);
        using OnSubAssetLoad = void(void *data, AssetHandle asset);

        // Streamer API
        IB_API void addStreamer(FourCC type, IStreamer *streamer);
        IB_API JobHandle loadSubAssetAsync(Serialization::MemoryStream stream, FourCC type, AssetHandle parentAsset, OnSubAssetLoad *onSubAssetLoad, void *data);
        IB_API void unloadSubAssetThreadSafe(AssetHandle asset, FourCC type);
        IB_API void saveSubAssetThreadSafe(Serialization::FileStream *stream, FourCC type, AssetHandle asset);

        // User API
        IB_API ResourceHandle createResourceThreadSafe(char const *assetPath, FourCC type, AssetHandle asset);
        IB_API JobHandle loadResourceAsync(char const *assetPath, FourCC type, OnResourceLoad *onResourceLoad, void *data);
        IB_API JobHandle releaseResourceAsync(ResourceHandle resource);
        IB_API JobHandle saveResourceAsync(ResourceHandle resource);

        IB_API AssetHandle GetAssetFromResource(ResourceHandle resourceHandle);
        IB_API char const *GetResourcePath(ResourceHandle resourceHandle);
        IB_API bool IsResourceAssetAvailable(ResourceHandle resourceHandle);

        inline JobHandle loadResourceAsync(char const *assetPath, FourCC type, ResourceHandle *outputResource)
        {
            return loadResourceAsync(assetPath, type, [](void *data, ResourceHandle resource, ResourceLoad::State loadState) {
                if (loadState == ResourceLoad::Available)
                {
                    *reinterpret_cast<ResourceHandle *>(data) = resource;
                }
            },
                                     outputResource);
        }

        template <typename StreamType>
        inline JobHandle loadSubAssetAsync(StreamType stream, FourCC type, AssetHandle parentAsset, AssetHandle *outputAsset)
        {
            return loadSubAssetAsync(stream, type, parentAsset, [](void *data, AssetHandle asset, ResourceLoad::State loadState) {
                if (loadState == ResourceLoad::Available)
                {
                    *reinterpret_cast<AssetHandle *>(data) = asset;
                }
            },
                                     outputAsset);
        }

        template <typename StreamType>
        inline JobHandle loadSubAssetAsync(StreamType stream, FourCC type, AssetHandle parentAsset)
        {
            return loadSubAssetAsync(stream, type, parentAsset, [](void *, AssetHandle, ResourceLoad::State) {}, nullptr);
        }
    } // namespace Asset
} // namespace IB
