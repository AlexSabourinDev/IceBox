#define _CRT_SECURE_NO_WARNINGS

#include "IBEntity.h"
#include "IBAllocator.h"
#include "IBCollection.h"
#include "IBAsset.h"
#include "IBPlatform.h"
#include "IBLogging.h"
#include "IBEditor.h"
#include "IBMath.h"

#include <imgui/imgui.h>

#include <string.h>

// Property
namespace IB
{
    namespace Property
    {
        namespace
        {
            struct Data
            {
                Asset::FourCC Type = InvalidFourCC;
                Handle Handle = InvalidProperty;
            };
        }
    }
}

namespace IB
{
    Transform* toTransform(Asset::Handle asset) { return reinterpret_cast<Transform*>(IB::Asset::toValue(asset)); }

    ThreadSafePool<Transform> LocalTransforms;
    class TransformPropertyStreamer : public IB::Asset::IStreamer
    {
    public:
        IB::Asset::Handle create() override
        {
            Transform& transform = LocalTransforms.add();
            return IB::Asset::toHandle(reinterpret_cast<uint64_t>(&transform));
        }

        IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
        {
            Transform baseTransform = {};
            if (context->BaseAsset != IB::InvalidAsset)
            {
                baseTransform = *toTransform(context->BaseAsset);
            }

            Transform& transform = *toTransform(context->LoadingAsset);
            Serialization::Delta::fromBinary(&context->Stream, &transform, baseTransform);

            return Asset::complete(Asset::toHandle(reinterpret_cast<uint64_t>(&transform)));
        }

        void saveThreadSafe(IB::Asset::SaveContext *context) override
        {
            Transform baseTransform = {};
            if (context->BaseAsset != IB::InvalidAsset)
            {
                baseTransform = *toTransform(context->BaseAsset);
            }

            Transform& transform = *toTransform(context->Asset);
            Serialization::Delta::toBinary(context->Stream, transform, baseTransform);
        }

        void unloadThreadSafe(IB::Asset::Handle assetHandle) override
        {
            LocalTransforms.remove(*toTransform(assetHandle));
        }
    };

    TransformPropertyStreamer TransformStreamer;

    Property::Handle createTransformProperty()
    {
        return Property::toHandle(reinterpret_cast<uint64_t>(&LocalTransforms.add()));
    }

    Transform const& getLocalTransform(Property::Handle handle)
    {
        IB_ASSERT(handle != InvalidProperty, "Trying to retrieve an invalid transform!");
        return *reinterpret_cast<Transform*>(Property::toValue(handle));
    }

    void setLocalTransform(Property::Handle handle, Transform const& transform)
    {
        IB_ASSERT(handle != InvalidProperty, "Trying to retrieve an invalid transform!");
        *reinterpret_cast<Transform*>(Property::toValue(handle)) = transform;
    }

    namespace Editor
    {
        void transformEditor(Asset::Handle asset)
        {
            Transform* transform = reinterpret_cast<Transform*>(Asset::toValue(asset));

            ImGizmo::translation(&transform->Position, transform->Rotation);

            if (ImGui::TreeNode("Transform"))
            {
                if (ImGui::BeginTable("Inspector Table", 2))
                {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableSetupColumn("Data", ImGuiTableColumnFlags_None);

                    ImGui::TableNextColumn();
                    ImGui::Text("Position");

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputFloat3("#Position", toArray(&transform->Position));

                    static Float3 euler = { 0.0f, 0.0f, 0.0f };
                    ImGui::TableNextColumn();
                    ImGui::Text("Rotation");

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputFloat3("#Rotation", toArray(&euler));
                    transform->Rotation = fromEulerAngles(euler * DegToRad);

                    ImGui::TableNextColumn();
                    ImGui::Text("Scale");

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputFloat("#Scale", &transform->Scale);

                    ImGui::EndTable();
                }
                ImGui::TreePop();

            }
        }
    }
}

// Entity
namespace IB
{
    namespace Entity
    {
        namespace
        {
            constexpr uint32_t MaxNameLength = 64;
            struct EntityName
            {
                char String[MaxNameLength] = {};
            };

            bool operator!=(EntityName const& left, EntityName const& right)
            {
                return strcmp(left.String, right.String) != 0;
            }

            struct Data
            {
                DynamicArray<Property::Data> Properties;
                EntityName Name;
            };

            Entity::Data* toData(Asset::Handle assetHandle) { return reinterpret_cast<Entity::Data*>(Asset::toValue(assetHandle)); }
            Entity::Data* toData(Entity::Handle entityHandle) { return reinterpret_cast<Entity::Data*>(Entity::toValue(entityHandle)); }

            Property::Handle findProperty(Entity::Data const* entityData, Asset::FourCC type)
            {
                for (uint32_t i = 0; i < entityData->Properties.count(); i++)
                {
                    if (entityData->Properties[i].Type == type)
                    {
                        return entityData->Properties[i].Handle;
                    }
                }

                return InvalidProperty;
            }
        }
    }

    namespace
    {
        const Entity::Data EmptyEntity;

        ThreadSafePool<Entity::Data> ActiveEntities;
        class EntityAssetStreamer : public IB::Asset::IStreamer
        {
        public:
            IB::Asset::Handle create() override
            {
                Entity::Data& entity = ActiveEntities.add();
                return IB::Asset::toHandle(reinterpret_cast<uint64_t>(&entity));
            }

            IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
            {
                enum State
                {
                    LoadProperties = 0,
                    Complete
                };

                if (context->State == LoadProperties)
                {
                    Entity::Data const* baseEntity = &EmptyEntity;
                    if (context->BaseAsset != InvalidAsset)
                    {
                        baseEntity = Entity::toData(context->BaseAsset);
                    }

                    Entity::Data& entity = *Entity::toData(context->LoadingAsset);
                    Serialization::Delta::fromBinary(&context->Stream, &entity.Name, baseEntity->Name);

                    uint32_t propertyCount;
                    fromBinary(&context->Stream, &propertyCount);
                    entity.Properties.reserve(propertyCount);
                    for (uint32_t i = 0; i < propertyCount; i++)
                    {
                        if (i >= entity.Properties.count())
                        {
                            entity.Properties.add();
                        }
                    }

                    IB_ASSERT(propertyCount <= IB::Asset::MaxDependencyCount, "Too many properties!");
                    IB::Job::Handle loadHandles[IB::Asset::MaxDependencyCount];
                    uint32_t handleCount = 0;
                    for (uint32_t i = 0; i < propertyCount; i++)
                    {
                        Asset::FourCC propertyType;
                        fromBinary(&context->Stream, &propertyType);

                        Property::Data* entityProperty = nullptr;

                        // Find or populate a new property entry. If we already had an entry
                        // Then we want to use it for our in-place deserialization.
                        {
                            for (uint32_t propertyIndex = 0; propertyIndex < entity.Properties.count(); propertyIndex++)
                            {
                                if (entity.Properties[propertyIndex].Type == propertyType)
                                {
                                    entityProperty = &entity.Properties[propertyIndex];
                                    break;
                                }

                                if (entity.Properties[propertyIndex].Type == InvalidFourCC)
                                {
                                    entityProperty = &entity.Properties[propertyIndex];
                                }
                            }

                            IB_ASSERT(entityProperty != nullptr, "Failed to find empty slot!");

                            entityProperty->Type = propertyType;
                            if (entityProperty->Handle == InvalidProperty)
                            {
                                entityProperty->Handle = toPropertyHandle(Asset::createAsset(entityProperty->Type));
                            }
                        }

                        Property::Handle baseProperty = findProperty(baseEntity, entityProperty->Type);

                        uint32_t offset;
                        fromBinary(&context->Stream, &offset);

                        // Passing pointer to dynamic array item is OK here. We've reserved the memory and will not resize until the load is complete.
                        Asset::LoadSubAssetDesc loadSubAssetDesc =
                        {
                            context,
                            entityProperty->Type,
                            toAssetHandle(entityProperty->Handle), // Create a new property asset
                            toAssetHandle(baseProperty),
                        };

                        loadHandles[handleCount++] = IB::Asset::loadSubAssetAsync(loadSubAssetDesc,
                            [](void *data, IB::Asset::Handle asset)
                        {
                            *reinterpret_cast<IB::Property::Handle *>(data) = toPropertyHandle(asset);
                        }, &entityProperty->Handle);
                        advance(&context->Stream, offset);
                    }

                    context->Data = reinterpret_cast<uint64_t>(&entity);
                    return IB::Asset::wait(loadHandles, handleCount, Complete);
                }
                else
                {
                    return IB::Asset::complete(Asset::toHandle(context->Data));
                }
            }

            void saveThreadSafe(IB::Asset::SaveContext *context) override
            {
                Entity::Data const* baseEntity = &EmptyEntity;
                if (context->BaseAsset != InvalidAsset)
                {
                    baseEntity = Entity::toData(context->BaseAsset);
                }

                Entity::Data* entity = Entity::toData(context->Asset);
                Serialization::Delta::toBinary(context->Stream, entity->Name, baseEntity->Name);

                toBinary(context->Stream, entity->Properties.count());
                for (uint32_t i = 0; i < entity->Properties.count(); i++)
                {
                    toBinary(context->Stream, entity->Properties[i].Type);

                    Property::Handle baseProperty = findProperty(baseEntity, entity->Properties[i].Type);

                    uint32_t dummyWriteSize = 0;
                    toBinary(context->Stream, dummyWriteSize);
                    uint32_t writeStart = context->Stream->BufferCursor;
                    IB::Asset::saveAssetThreadSafe(context->Stream, entity->Properties[i].Type, toAssetHandle(entity->Properties[i].Handle), toAssetHandle(baseProperty));
                    uint32_t writeEnd = context->Stream->BufferCursor;

                    // Write our written size right before our sub asset.
                    uint32_t writeSize = writeEnd - writeStart;
                    memcpy(context->Stream->Buffer + writeStart - sizeof(uint32_t), &writeSize, sizeof(uint32_t));
                }
            }

            void unloadThreadSafe(IB::Asset::Handle assetHandle) override
            {
                Entity::Data* entity = Entity::toData(assetHandle);
                for (uint32_t i = 0; i < entity->Properties.count(); i++)
                {
                    IB::Asset::unloadAssetThreadSafe(entity->Properties[i].Type, toAssetHandle(entity->Properties[i].Handle));
                }

                ActiveEntities.remove(*entity);
            }
        };

        EntityAssetStreamer EntityStreamer;
    }

    // Entity API
    Entity::Handle createEntity()
    {
        Entity::Data& entity = ActiveEntities.add();
        return Entity::toHandle(reinterpret_cast<uint64_t>(&entity));
    }

    void addPropertyToEntity(Entity::Handle entityHandle, Asset::FourCC type, Property::Handle propertyHandle)
    {
        Entity::Data* entity = Entity::toData(entityHandle);
        IB_ASSERT(findProperty(entity, type) == InvalidProperty, "Assuming that we can't have multiple of a particular property.");
        entity->Properties.add(type, propertyHandle);
    }

    Property::Handle getPropertyFromEntity(Entity::Handle entityHandle, Asset::FourCC type)
    {
        return findProperty(Entity::toData(entityHandle), type);
    }

    char const* getEntityName(Entity::Handle entityHandle)
    {
        Entity::Data* entity = Entity::toData(entityHandle);
        if (entity == nullptr)
        {
            return "";
        }
        else if (entity->Name.String[0] == '\0')
        {
            return "Entity";
        }
        else
        {
            return entity->Name.String;
        }
    }

    void setEntityName(Entity::Handle entityHandle, char const* name)
    {
        Entity::Data* entity = Entity::toData(entityHandle);
        IB_ASSERT(strlen(name) < Entity::MaxNameLength, "Entity name is too long! Stomping on memory.");
        strcpy(entity->Name.String, name);
    }

    namespace Editor
    {
        void entityEditor(Asset::Handle asset)
        {
            Entity::Data* entity = Entity::toData(asset);

            ImGui::Text("Entity");
            ImGui::Separator();
            if (ImGui::BeginTable("Inspector Table", 2))
            {
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("Data", ImGuiTableColumnFlags_None);

                ImGui::TableNextColumn();
                ImGui::Text("Name");

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputText("", entity->Name.String, Entity::MaxNameLength);

                ImGui::EndTable();
            }

            ImGui::Text("Properties");
            ImGui::Separator();

            if (ImGui::Button("Add Transform"))
            {
                addPropertyToEntity(toEntityHandle(asset), Asset::toFourCC("TFRM"), createTransformProperty());
            }

            for (uint32_t i = 0; i < entity->Properties.count(); i++)
            {
                Editor::drawAssetEditor(entity->Properties[i].Type, toAssetHandle(entity->Properties[i].Handle));
            }
        }
    }
}

// Cell
namespace IB
{
    namespace
    {
        // Entity API

        struct EntityID
        {
            uint32_t HierarchyID = 0; // How high up the parent hierarchy is our entity
            uint32_t LocalID = 0; // Local ID within our cell
        };

        struct CellAsset
        {
            Resource::Handle BaseCell = InvalidResource;
            DynamicArray<Entity::Handle> Entities;
            DynamicArray<EntityID> EntityIDs;
            uint32_t NextEntityID = 0;
        };
        CellAsset EmptyCellAsset = {};

        // Since we don't expect to have many cells, a simple threadsafe allocation scheme is fine

        constexpr uint32_t MaxCellCount = 32;
        CellAsset Cells[MaxCellCount];
        uint32_t CellAllocations[MaxCellCount] = {};

        int32_t getBaseEntityIndex(EntityID const entityID, CellAsset const& baseCell)
        {
            int32_t entityIndex = -1;
            for (uint32_t i = 0; i < baseCell.EntityIDs.count(); i++)
            {
                if (baseCell.EntityIDs[i].HierarchyID == entityID.HierarchyID - 1
                    && baseCell.EntityIDs[i].LocalID == entityID.LocalID)
                {
                    entityIndex = i;
                    break;
                }
            }
            return entityIndex;
        };

        class CellAssetStreamer : public Asset::IStreamer
        {
            IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
            {
                enum State
                {
                    LoadBase = 0,
                    LoadEntities,
                    Complete
                };

                if (context->State == LoadBase)
                {
                    uint32_t createdCell = MaxCellCount;
                    for (uint32_t i = 0; i < MaxCellCount; i++)
                    {
                        if (atomicCompareExchange(&CellAllocations[i], false, true) == false)
                        {
                            createdCell = i;
                            break;
                        }
                    }
                    IB_ASSERT(createdCell != MaxCellCount, "Failed to create a cell! How many cells do we have active?");

                    CellAsset& cell = Cells[createdCell];
                    context->Data = createdCell;

                    char const *baseCellPath = "";
                    fromBinary(&context->Stream, &baseCellPath);

                    Job::Handle baseCellLoad = Asset::loadResourceAsync(baseCellPath, { Asset::toFourCC("CELL") }, &cell.BaseCell);
                    return IB::Asset::wait(&baseCellLoad, 1, LoadEntities);
                }
                else if (context->State == LoadEntities)
                {
                    CellAsset& cell = Cells[context->Data];

                    CellAsset* baseCell = &EmptyCellAsset;
                    if (cell.BaseCell != InvalidResource)
                    {
                        Asset::Handle baseCellAsset = Asset::getAssetFromResource(cell.BaseCell);
                        baseCell = &Cells[Asset::toValue(baseCellAsset)];
                    }

                    fromBinary(&context->Stream, &cell.NextEntityID);

                    uint32_t entityCount;
                    fromBinary(&context->Stream, &entityCount);
                    cell.Entities.reserve(entityCount);
                    cell.EntityIDs.reserve(entityCount);

                    IB_ASSERT(entityCount <= IB::Asset::MaxDependencyCount, "Too many properties!");
                    IB::Job::Handle loadHandles[IB::Asset::MaxDependencyCount];
                    uint32_t handleCount = 0;
                    for (uint32_t i = 0; i < entityCount; i++)
                    {
                        EntityID& entityID = cell.EntityIDs.add();
                        fromBinary(&context->Stream, &entityID);

                        IB::Entity::Handle& entity = cell.Entities.add();

                        bool baseHasEntity = false;
                        fromBinary(&context->Stream, &baseHasEntity);

                        Asset::Handle baseAsset = InvalidAsset;
                        if (baseHasEntity)
                        {
                            int32_t entityIndex = getBaseEntityIndex(entityID, *baseCell);
                            if (entityIndex >= 0)
                            {
                                baseAsset = toAssetHandle(baseCell->Entities[entityIndex]);
                            }
                        }

                        uint32_t offset;
                        fromBinary(&context->Stream, &offset);

                        // Passing pointer to dynamic array item is OK here. We've reserved the memory and will not resize until the load is complete.
                        Asset::LoadSubAssetDesc loadSubAssetDesc =
                        {
                            context,
                            Asset::toFourCC("ENTT"),
                            Asset::createAsset(IB::Asset::toFourCC("ENTT")),
                            baseAsset
                        };
                        
                        loadHandles[handleCount++] = IB::Asset::loadSubAssetAsync(loadSubAssetDesc,
                            [](void *data, IB::Asset::Handle asset)
                            {
                                *reinterpret_cast<IB::Entity::Handle *>(data) = toEntityHandle(asset);
                            }, &entity);

                        advance(&context->Stream, offset);
                    }

                    return IB::Asset::wait(loadHandles, handleCount, Complete);
                }
                else
                {
                    return IB::Asset::complete(Asset::toHandle(context->Data));
                }
            }

            void saveThreadSafe(IB::Asset::SaveContext *context) override
            {
                CellAsset const& cell = Cells[Asset::toValue(context->Asset)];
                char const *baseCellPath = IB::Asset::getResourcePath(cell.BaseCell);
                toBinary(context->Stream, baseCellPath);
                toBinary(context->Stream, cell.NextEntityID);

                CellAsset const* baseCell = &EmptyCellAsset;
                if (cell.BaseCell != InvalidResource)
                {
                    Asset::Handle baseCellAsset = Asset::getAssetFromResource(cell.BaseCell);
                    baseCell = &Cells[Asset::toValue(baseCellAsset)];
                }

                toBinary(context->Stream, cell.Entities.count());
                for (uint32_t i = 0; i < cell.Entities.count(); i++)
                {
                    EntityID const entityID = cell.EntityIDs[i];
                    toBinary(context->Stream, entityID);

                    int32_t const baseEntityIndex = getBaseEntityIndex(entityID, *baseCell);

                    bool const baseHasEntity = baseEntityIndex >= 0;
                    toBinary(context->Stream, baseHasEntity);

                    Asset::Handle baseEntityAsset = InvalidAsset;
                    if (baseHasEntity)
                    {
                        baseEntityAsset = toAssetHandle(baseCell->Entities[baseEntityIndex]);
                    }

                    uint32_t dummyWriteSize = 0;
                    toBinary(context->Stream, dummyWriteSize);
                    uint32_t writeStart = context->Stream->BufferCursor;

                    IB::Asset::saveAssetThreadSafe(context->Stream, IB::Asset::toFourCC("ENTT"), toAssetHandle(cell.Entities[i]), baseEntityAsset);

                    uint32_t writeEnd = context->Stream->BufferCursor;

                    // Write our written size right before our sub asset.
                    uint32_t writeSize = writeEnd - writeStart;
                    memcpy(context->Stream->Buffer + writeStart - sizeof(uint32_t), &writeSize, sizeof(uint32_t));
                }
            }

            void unloadThreadSafe(IB::Asset::Handle assetHandle) override
            {
                CellAsset& cell = Cells[Asset::toValue(assetHandle)];
                Asset::releaseResourceAsync(cell.BaseCell);
                for (uint32_t i = 0; i < cell.Entities.count(); i++)
                {
                    IB::Asset::unloadAssetThreadSafe(IB::Asset::toFourCC("ENTT"), toAssetHandle(cell.Entities[i]));
                }

                cell = {};
                threadRelease(); // Make sure our writes are released before we tell the other threads.
                volatileStore<uint32_t>(&CellAllocations[Asset::toValue(assetHandle)], false);
            }
        };
        CellAssetStreamer CellStreamer;
    }

    namespace Editor
    {
        void cellEditor(Asset::Handle asset)
        {
            Cell::Handle cellHandle = toCellHandle(asset);
            CellAsset& cell = Cells[Cell::toValue(cellHandle)];

            ImGui::Text("Cell");
            ImGui::Separator();

            if (ImGui::BeginTable("Inspector Table", 2))
            {
                if (cell.BaseCell != InvalidResource)
                {
                    ImGui::TableNextColumn();
                    ImGui::Text("Base Cell");

                    ImGui::TableNextColumn();
                    ImGui::Text(Asset::getResourcePath(cell.BaseCell));
                }

                ImGui::EndTable();
            }
        }
    }

    // Cell API
    Cell::Handle createCell(Resource::Handle baseCell)
    {
        uint32_t createdCellIndex = MaxCellCount;
        for (uint32_t i = 0; i < MaxCellCount; i++)
        {
            if (atomicCompareExchange(&CellAllocations[i], false, true) == false)
            {
                createdCellIndex = i;
                break;
            }
        }

        IB_ASSERT(createdCellIndex != MaxCellCount, "Failed to create a cell! How many cells do we have active?");

        CellAsset& createdCell = Cells[createdCellIndex];
        if (baseCell != InvalidResource)
        {
            Asset::acquireResource(baseCell);
            createdCell.BaseCell = baseCell;

            Asset::Handle cellAssetHandle = Asset::getAssetFromResource(baseCell);
            CellAsset& baseCellAsset = Cells[Asset::toValue(cellAssetHandle)];

            uint32_t const entityCount = baseCellAsset.EntityIDs.count();
            createdCell.EntityIDs.reserve(entityCount);
            createdCell.Entities.reserve(entityCount);

            for (uint32_t i = 0; i < entityCount; i++)
            {
                Asset::Handle baseEntityAsset = toAssetHandle(baseCellAsset.Entities[i]);
                Asset::Handle clonedAsset = Asset::cloneAssetThreadSafe(baseEntityAsset, Asset::toFourCC("ENTT"));
                if (clonedAsset != InvalidAsset)
                {
                    createdCell.EntityIDs.add(baseCellAsset.EntityIDs[i].HierarchyID + 1, baseCellAsset.EntityIDs[i].LocalID);
                    createdCell.Entities.add(toEntityHandle(clonedAsset));
                }
            }
        }

        return Cell::toHandle(createdCellIndex);
    }

    void addEntityToCell(Cell::Handle cellHandle, Entity::Handle entity)
    {
        CellAsset& cell = Cells[Cell::toValue(cellHandle)];
        cell.Entities.add(entity);
        cell.EntityIDs.add(EntityID{0, cell.NextEntityID++});
    }

    void getEntityList(Cell::Handle cellHandle, Entity::Handle** entities, uint32_t* entityCount)
    {
        if (cellHandle != InvalidCell)
        {
            CellAsset& cell = Cells[Cell::toValue(cellHandle)];
            *entities = cell.Entities.data();
            *entityCount = cell.Entities.count();
        }
        else
        {
            *entityCount = 0;
        }
    }

    void initEntitySystem()
    {
        Asset::addStreamer(Asset::toFourCC("ENTT"), &EntityStreamer);
        Asset::addStreamer(Asset::toFourCC("CELL"), &CellStreamer);
        Asset::addStreamer(Asset::toFourCC("TFRM"), &TransformStreamer);

        Editor::addAssetEditor(Asset::toFourCC("CELL"), &Editor::cellEditor);
        Editor::addAssetEditor(Asset::toFourCC("ENTT"), &Editor::entityEditor);
        Editor::addAssetEditor(Asset::toFourCC("TFRM"), &Editor::transformEditor);
    }

    void killEntitySystem()
    {

    }
}

