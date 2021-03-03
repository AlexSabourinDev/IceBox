#pragma once

#include "IBEngineAPI.h"
#include "IBAsset.h"

namespace IB
{
    struct PropertyHandle
    {
        uint64_t Value;
    };
    constexpr PropertyHandle InvalidProperty = { UINT64_MAX };

    struct EntityHandle
    {
        uint64_t Value;
    };

    struct CellHandle
    {
        uint64_t Value;
    };

    inline PropertyHandle toPropertyHandle(Asset::AssetHandle asset) { return { asset.Value }; }
    inline Asset::AssetHandle toAssetHandle(PropertyHandle property) { return { property.Value }; }
    inline EntityHandle toEntityHandle(Asset::AssetHandle asset) { return { asset.Value }; }
    inline Asset::AssetHandle toAssetHandle(EntityHandle entity) { return { entity.Value }; }
    inline Asset::AssetHandle toAssetHandle(CellHandle cell) { return { cell.Value }; }

    IB_API void initEntitySystem();
    IB_API void killEntitySystem();

    // Entity API
    IB_API EntityHandle createEntity();
    IB_API void addPropertyToEntity(EntityHandle entity, Asset::FourCC type, PropertyHandle propertyHandle);
    IB_API PropertyHandle getPropertyFromEntity(EntityHandle entity, Asset::FourCC type);

    // Cell API
    IB_API CellHandle createCell();
    IB_API void addEntityToCell(CellHandle cell, EntityHandle entity);
    IB_API void getEntityList(CellHandle cell, EntityHandle** entities, uint32_t* entityCount);
}

