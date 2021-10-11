#pragma once

#include "IBEngineAPI.h"
#include "IBSerialization.h"
#include <stdint.h>

namespace IB
{
    namespace Job
    {
        enum class Handle : uint64_t;
    }

    namespace Resource
    {
        enum class Handle : uint32_t {};
        constexpr Handle toHandle(uint32_t value) { return static_cast<Handle>(value); }
        constexpr uint32_t toValue(Handle handle) { return static_cast<uint32_t>(handle); }
    }
    constexpr Resource::Handle InvalidResource = Resource::toHandle(0);

    namespace Asset
    {
        enum FourCC : uint32_t {};
        constexpr FourCC toFourCC(uint32_t value) { return static_cast<FourCC>(value); }

        constexpr FourCC toFourCC(char const (&text)[5]) // Char array is of 5 due to null terminator
        {
            return toFourCC(text[0] | (text[1] << 8) | (text[2] << 16) | (text[3] << 24));
        }

        // Asset handle
        enum class Handle : uint64_t {};
        constexpr Handle toHandle(uint64_t value) { return static_cast<Handle>(value); }
        constexpr uint64_t toValue(Handle handle) { return static_cast<uint64_t>(handle); }
    }

    constexpr Asset::FourCC InvalidFourCC = Asset::toFourCC(0);
    constexpr Asset::Handle InvalidAsset = Asset::toHandle(UINT64_MAX);

    namespace Asset
    {
        struct LoadContext
        {
            Serialization::ReadStream Stream;
            Asset::Handle LoadingAsset = InvalidAsset; // The asset to which we want to load
            Asset::Handle BaseAsset = InvalidAsset; // The asset from which we might derive
            Job::Handle Handle = {};
            uint64_t Data = 0;
            uint32_t State = 0;
            uint32_t SaveVersion = 0;
        };

        struct SaveContext
        {
            Serialization::WriteStream *Stream;
            Asset::Handle Asset;
            Asset::Handle BaseAsset = InvalidAsset;
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
            // Create an empty asset for use in future load calls.
            // Allows us to facilitate cloning an asset in-place.
            // If the asset does not support in-plane loading, simply create the asset
            // in loadAsync.
            virtual Asset::Handle create() { return InvalidAsset; }
            virtual LoadContinuation loadAsync(LoadContext *context) = 0;
            virtual void unloadThreadSafe(Asset::Handle handle) = 0;
            virtual void saveThreadSafe(SaveContext *context);
            virtual bool memoryMapped() const { return false; }
        };
        // NOTE: You might get a load state of available before you
        // receive a load state of Loading
        // This is up to you to decide how to handle that.
        struct ResourceLoad
        {
            enum State
            {
                Loading = 0,
                Available,
                NotExisting
            };
        };

        using OnResourceLoad = void(void *data, Resource::Handle resource, ResourceLoad::State loadState);
        using OnSubAssetLoad = void(void *data, Asset::Handle asset);

        struct DeltaAssetDesc
        {
            FourCC Type = InvalidFourCC;
        };

        // Streamer API
        IB_API void addStreamer(FourCC type, IStreamer *streamer);

        struct LoadSubAssetDesc
        {
            LoadContext const* ParentContext;
            FourCC Type = InvalidFourCC;
            Asset::Handle LoadingAsset = InvalidAsset;
            Asset::Handle BaseAsset = InvalidAsset;
        };
        IB_API Asset::Handle createAsset(FourCC type);
        IB_API Job::Handle loadSubAssetAsync(LoadSubAssetDesc desc, OnSubAssetLoad *onSubAssetLoad, void *data);
        IB_API void unloadAssetThreadSafe(FourCC type, Asset::Handle asset);
        IB_API void saveAssetThreadSafe(Serialization::WriteStream *stream, FourCC type, Asset::Handle asset, Asset::Handle baseAsset = InvalidAsset);

        inline Job::Handle loadSubAssetAsync(LoadSubAssetDesc desc, Asset::Handle *outputAsset)
        {
            return loadSubAssetAsync(desc, [](void *data, Asset::Handle asset)
            {
                *reinterpret_cast<Asset::Handle *>(data) = asset;
            },
                outputAsset);
        }

        inline Job::Handle loadSubAssetAsync(LoadSubAssetDesc desc)
        {
            return loadSubAssetAsync(desc, [](void *, Asset::Handle) {}, nullptr);
        }


        // User API
        IB_API Resource::Handle createResourceThreadSafe(char const *assetPath, FourCC type, Asset::Handle asset);
        IB_API Job::Handle loadResourceAsync(char const *assetPath, FourCC type, OnResourceLoad *onResourceLoad, void *data = nullptr);
        IB_API void acquireResource(Resource::Handle resource); // add a new reference to our resource
        IB_API Job::Handle releaseResourceAsync(Resource::Handle resource);
        IB_API Job::Handle saveResourceAsync(Resource::Handle resource);
        IB_API Asset::Handle cloneAssetThreadSafe(Asset::Handle asset, FourCC type);

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
    } // namespace Asset
} // namespace IB
