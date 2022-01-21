#pragma once

#include "IBEngineAPI.h"
#include <stdint.h>

namespace IB
{
    struct Transform;

    namespace Job
    {
        enum class Handle : uint64_t;
    }

    namespace Asset
    {
        enum class FourCC : uint32_t;
        FourCC constexpr toFourCC(char const (&text)[5]);

        enum class Handle : uint64_t;
        Handle constexpr toHandle(uint64_t value);
        uint64_t constexpr toValue(Handle handle);
    } // namespace Asset

    namespace Resource
    {
        enum class Handle : uint32_t;
        Handle constexpr toHandle(uint32_t value);
    }

    namespace Property
    {
        enum class Handle : uint64_t { };
        Handle constexpr toHandle(uint64_t value) { return static_cast<Handle>(value); }
        uint64_t constexpr toValue(Handle handle) { return static_cast<uint64_t>(handle); }
    }; // namespace Property
    Property::Handle constexpr InvalidProperty = Property::toHandle(UINT64_MAX);

    inline Property::Handle toPropertyHandle(Asset::Handle asset) { return Property::toHandle(Asset::toValue(asset)); }
    inline Asset::Handle toAssetHandle(Property::Handle property) { return Asset::toHandle(Property::toValue(property)); }

    // Entity API
    namespace Entity
    {
        enum class Handle : uint64_t {};
        Handle constexpr toHandle(uint64_t value) { return static_cast<Handle>(value); }
        uint64_t constexpr toValue(Handle handle) { return static_cast<uint64_t>(handle); }
    }; // namespace Entity
    Entity::Handle constexpr InvalidEntity = Entity::toHandle(UINT64_MAX);

    inline Entity::Handle toEntityHandle(Asset::Handle asset) { return Entity::toHandle(Asset::toValue(asset)); }
    inline Asset::Handle toAssetHandle(Entity::Handle entity) { return Asset::toHandle(Entity::toValue(entity)); }

    class IPropertyActivator
    {
    public:
        virtual Job::Handle onLoadAsync(Entity::Handle entity) const = 0;
    };

    IB_API void initEntitySystem();
    IB_API void killEntitySystem();

    Asset::FourCC const EntityType = Asset::toFourCC("ENTT");
    IB_API Entity::Handle createEntity(char const* name = "");
    IB_API void destroyEntity(Entity::Handle entity);
    IB_API void addPropertyToEntity(Entity::Handle entity, Asset::FourCC type, Property::Handle propertyHandle);

    IB_API void addPropertyActivator(Asset::FourCC propertyType, IPropertyActivator* activator);

    namespace Editor
    {
        struct PropertyEditorState
        {
            Entity::Handle EntityHandle;
            bool DestroyProperty = false;
        };

        bool propertyHeader(char const* name, bool* shouldDestroyProperty);
    }

    // Doesn't destroy the property.
    // Call Asset::unloadAssetThreadsafe to properly clean it up.
    IB_API void removePropertyFromEntity(Entity::Handle entity, Asset::FourCC type);
    IB_API Property::Handle getPropertyFromEntity(Entity::Handle entity, Asset::FourCC type);
    IB_API char const* getEntityName(Entity::Handle entity);
    IB_API void setEntityName(Entity::Handle entityHandle, char const* name);

    // Cell API
    namespace Cell
    {
        enum class Handle : uint64_t {};
        Handle constexpr toHandle(uint64_t value) { return static_cast<Handle>(value); }
        uint64_t constexpr toValue(Handle handle) { return static_cast<uint64_t>(handle); }
    }; // namespace Cell
    Cell::Handle constexpr InvalidCell = Cell::toHandle(UINT64_MAX);

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

    namespace Editor
    {
        void imguiTransform(Transform* transform);
    }
}

