#pragma once

#include "IBEngineAPI.h"
#include "IBAsset.h"

namespace IB
{
    struct PropertyHandle
    {
        uint64_t Value;
    };

    struct EntityHandle
    {
        uint64_t Value;
    };

    inline PropertyHandle toPropertyHandle(Asset::AssetHandle asset) { return { asset.Value }; }
    inline Asset::AssetHandle toAssetHandle(PropertyHandle property) { return { property.Value }; }
    inline EntityHandle toEntityHandle(Asset::AssetHandle asset) { return { asset.Value }; }
    inline Asset::AssetHandle toAssetHandle(EntityHandle entity) { return { entity.Value }; }

    IB_API void initEntitySystem();
    IB_API void killEntitySystem();
    IB_API EntityHandle createEntity();
    IB_API void addPropertyToEntity(EntityHandle entity, Asset::FourCC type, PropertyHandle propertyHandle);
}

