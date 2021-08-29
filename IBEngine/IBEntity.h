#pragma once

#include "IBEngineAPI.h"
#include <stdint.h>

namespace IB
{
    struct Transform;

    namespace Asset
    {
        enum FourCC : uint32_t;
        constexpr FourCC toFourCC(char const (&text)[5]);

        enum Handle : uint64_t;
        constexpr Handle toHandle(uint64_t value);
        constexpr uint64_t toValue(Handle handle);
    }

    namespace Resource
    {
        enum Handle : uint32_t;
        constexpr Handle toHandle(uint32_t value);
    }

    namespace Property
    {
        enum Handle : uint64_t { };
        constexpr Handle toHandle(uint64_t value) { return static_cast<Handle>(value); }
        constexpr uint64_t toValue(Handle handle) { return static_cast<uint64_t>(handle); }
    };
    constexpr Property::Handle InvalidProperty = Property::toHandle(UINT64_MAX);

    inline Property::Handle toPropertyHandle(Asset::Handle asset) { return Property::toHandle(Asset::toValue(asset)); }
    inline Asset::Handle toAssetHandle(Property::Handle property) { return Asset::toHandle(Property::toValue(property)); }

    // Entity API
    namespace Entity
    {
        enum Handle : uint64_t {};
        constexpr Handle toHandle(uint64_t value) { return static_cast<Handle>(value); }
        constexpr uint64_t toValue(Handle handle) { return static_cast<uint64_t>(handle); }
    };
    constexpr Entity::Handle InvalidEntity = Entity::toHandle(0);

    inline Entity::Handle toEntityHandle(Asset::Handle asset) { return Entity::toHandle(Asset::toValue(asset)); }
    inline Asset::Handle toAssetHandle(Entity::Handle entity) { return Asset::toHandle(Entity::toValue(entity)); }

    IB_API void initEntitySystem();
    IB_API void killEntitySystem();

    Asset::FourCC const EntityType = Asset::toFourCC("ENTT");
    IB_API Entity::Handle createEntity(char const* name = "");
    IB_API void destroyEntity(Entity::Handle entity);
    IB_API void addPropertyToEntity(Entity::Handle entity, Asset::FourCC type, Property::Handle propertyHandle);
    IB_API Property::Handle getPropertyFromEntity(Entity::Handle entity, Asset::FourCC type);
    IB_API char const* getEntityName(Entity::Handle entity);
    IB_API void setEntityName(Entity::Handle entityHandle, char const* name);

    // Cell API
    namespace Cell
    {
        enum Handle : uint64_t {};
        constexpr Handle toHandle(uint64_t value) { return static_cast<Handle>(value); }
        constexpr uint64_t toValue(Handle handle) { return static_cast<uint64_t>(handle); }
    };
    constexpr Cell::Handle InvalidCell = Cell::toHandle(UINT64_MAX);

    inline Cell::Handle toCellHandle(Asset::Handle asset) { return Cell::toHandle(Asset::toValue(asset)); }
    inline Asset::Handle toAssetHandle(Cell::Handle cell) { return Asset::toHandle(Cell::toValue(cell)); }

    Asset::FourCC const CellType = Asset::toFourCC("CELL");
    IB_API Cell::Handle createCell(Resource::Handle baseCell = Resource::toHandle(0));
    IB_API void addEntityToCell(Cell::Handle cell, Entity::Handle entity);
    IB_API void getEntityList(Cell::Handle cell, Entity::Handle** entities, uint32_t* entityCount);

    // Transform API
    Asset::FourCC const TransformPropertyType = Asset::toFourCC("TFRM");
    IB_API Property::Handle createTransformProperty();
    IB_API Transform const& getLocalTransform(Property::Handle handle);
    inline Transform const& getWorldTransform(Property::Handle handle) { return getLocalTransform(handle); } // No transform hierarchy yet.
    IB_API void setLocalTransform(Property::Handle handle, Transform const& transform);
}

