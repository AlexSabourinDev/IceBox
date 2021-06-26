#pragma once
#include "IBEngineAPI.h"

#include <stdint.h>

namespace IB
{
    namespace Asset
    {
        enum Handle : uint64_t;
        enum FourCC : uint32_t;
    }

    namespace Resource
    {
        enum Handle : uint32_t;
    }

    namespace Editor
    {
        using AssetEditorCallback = void(Asset::Handle asset);
        IB_API void addAssetEditor(Asset::FourCC type, AssetEditorCallback* frame);
        IB_API void drawAssetEditor(Asset::FourCC type, Asset::Handle handle);

        struct FrameState
        {
            Resource::Handle* CellResource;
        };
        IB_API void frame(FrameState* state);
    }
}

