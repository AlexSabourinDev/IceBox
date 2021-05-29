#pragma once

#include "IBEngineAPI.h"
#include "IBSerialization.h"
#include <stdint.h>

namespace IB
{
    namespace Job
    {
        enum Handle : uint64_t;
    }

    namespace Resource
    {
        enum Handle : uint32_t {};
        constexpr Handle toHandle(uint32_t value) { return static_cast<Handle>(value); }
        constexpr uint32_t toValue(Handle handle) { return static_cast<uint32_t>(handle); }
    }

    namespace Asset
    {
        enum FourCC : uint32_t {};
        constexpr FourCC toFourCC(uint32_t value) { return static_cast<FourCC>(value); }

        constexpr FourCC InvalidFourCC = toFourCC(0);
        constexpr FourCC toFourCC(char const (&text)[5]) // Char array is of 5 due to null terminator
        {
            return toFourCC(text[0] | (text[1] << 8) | (text[2] << 16) | (text[3] << 24));
        }

        // Asset handle
        enum Handle : uint64_t {};
        constexpr Handle toHandle(uint64_t value) { return static_cast<Handle>(value); }
        constexpr uint64_t toValue(Handle handle) { return static_cast<uint64_t>(handle); }
        constexpr Handle InvalidAsset = toHandle(UINT64_MAX);
        constexpr Resource::Handle InvalidResource = Resource::toHandle(0);

        struct LoadContext
        {
            Serialization::MemoryStream Stream;
            Asset::Handle ParentAsset = {};
            Job::Handle Handle = {};
            uint64_t Data = 0;
            uint32_t State = 0;
            uint32_t SaveVersion = 0;
        };

        struct SaveContext
        {
            Serialization::FileStream *Stream;
            Asset::Handle Asset;
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
                    Job::Handle Dependencies[MaxDependencyCount];
                    uint32_t DependencyCount;
                    uint32_t NextState;
                } Advance;

                struct
                {
                    Asset::Handle Handle;
                } Complete;
            } Data;
            uint32_t Continuation = Complete;
        };
        IB_API LoadContinuation wait(Job::Handle *dependencies, uint32_t dependencyCount, uint32_t nextState);
        IB_API LoadContinuation complete(Asset::Handle handle);

        class IStreamer
        {
        public:
            virtual LoadContinuation loadAsync(LoadContext *context) = 0;
            virtual void unloadThreadSafe(Asset::Handle handle) = 0;
            virtual void saveThreadSafe(SaveContext *context);
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

        using OnResourceLoad = void(void *data, Resource::Handle resource, ResourceLoad::State loadState);
        using OnSubAssetLoad = void(void *data, Asset::Handle asset);

        // Streamer API
        IB_API void addStreamer(FourCC type, IStreamer *streamer);
        IB_API Job::Handle loadSubAssetAsync(LoadContext const* parentContext, FourCC type, Asset::Handle parentAsset, OnSubAssetLoad *onSubAssetLoad, void *data);
        IB_API void unloadSubAssetThreadSafe(Asset::Handle asset, FourCC type);
        IB_API void saveSubAssetThreadSafe(Serialization::FileStream *stream, FourCC type, Asset::Handle asset);

        // User API
        IB_API Resource::Handle createResourceThreadSafe(char const *assetPath, FourCC type, Asset::Handle asset);
        IB_API Job::Handle loadResourceAsync(char const *assetPath, FourCC type, OnResourceLoad *onResourceLoad, void *data = nullptr);
        IB_API Job::Handle releaseResourceAsync(Resource::Handle resource);
        IB_API Job::Handle saveResourceAsync(Resource::Handle resource);

        IB_API Asset::Handle getAssetFromResource(Resource::Handle resourceHandle);
        IB_API char const *getResourcePath(Resource::Handle resourceHandle);
        IB_API bool isResourceAssetAvailable(Resource::Handle resourceHandle);

        inline Job::Handle loadResourceAsync(char const *assetPath, FourCC type, Resource::Handle *outputResource)
        {
            return loadResourceAsync(assetPath, type, [](void *data, Resource::Handle resource, ResourceLoad::State loadState) {
                if (loadState == ResourceLoad::Available)
                {
                    *reinterpret_cast<Resource::Handle *>(data) = resource;
                }
            },
                                     outputResource);
        }

        template <typename StreamType>
        inline Job::Handle loadSubAssetAsync(StreamType stream, FourCC type, Asset::Handle parentAsset, Asset::Handle *outputAsset)
        {
            return loadSubAssetAsync(stream, type, parentAsset, [](void *data, Asset::Handle asset, ResourceLoad::State loadState) {
                if (loadState == ResourceLoad::Available)
                {
                    *reinterpret_cast<Asset::Handle *>(data) = asset;
                }
            },
                                     outputAsset);
        }

        template <typename StreamType>
        inline Job::Handle loadSubAssetAsync(StreamType stream, FourCC type, Asset::Handle parentAsset)
        {
            return loadSubAssetAsync(stream, type, parentAsset, [](void *, Asset::Handle, ResourceLoad::State) {}, nullptr);
        }
    } // namespace Asset
} // namespace IB
