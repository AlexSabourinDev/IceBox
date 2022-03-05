#define _CRT_SECURE_NO_WARNINGS

#include "IBEntity.h"
#include "IBAllocator.h"
#include "IBCollection.h"
#include "IBAsset.h"
#include "IBPlatform.h"
#include "IBLogging.h"
#include "IBEditor.h"
#include "IBMath.h"
#include "IBJobs.h"

#include <imgui/imgui.h>

#include <string.h>

static_assert(static_cast<uint64_t>(IB::InvalidProperty) == static_cast<uint64_t>(IB::InvalidAsset), "Invalid property is expected to match invalid asset.");
static_assert(static_cast<uint64_t>(IB::InvalidCell) == static_cast<uint64_t>(IB::InvalidAsset), "Invalid cell is expected to match invalid asset.");
static_assert(static_cast<uint64_t>(IB::InvalidEntity) == static_cast<uint64_t>(IB::InvalidAsset), "Invalid entity is expected to match invalid asset.");

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
        void imguiTransform(Transform *transform)
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

                ImGui::TableNextColumn();
                ImGui::Text("Rotation");

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);

                Float3 euler = toEulerAngles(transform->Rotation) * RadToDeg;
                // round our euler angles to the third decimal
                euler = round(euler * 1000.0f) / 1000.0f;
                euler.x = euler.x == -0.0f ? 0.0f : euler.x; // Sanitize our entries of -0.0f
                euler.y = euler.y == -0.0f ? 0.0f : euler.y; // Sanitize our entries of -0.0f
                euler.z = euler.z == -0.0f ? 0.0f : euler.z; // Sanitize our entries of -0.0f

                ImGui::InputFloat3("#Rotation", toArray(&euler));
                transform->Rotation = fromEulerAngles(euler * DegToRad);

                ImGui::TableNextColumn();
                ImGui::Text("Scale");

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputFloat("#Scale", &transform->Scale);

                ImGui::EndTable();
            }
        }

        void transformEditor(Asset::Handle asset, void* parentData)
        {
            Transform* transform = reinterpret_cast<Transform*>(Asset::toValue(asset));

            PropertyEditorState* editorState = reinterpret_cast<PropertyEditorState*>(parentData);
            if (propertyHeader("Transform", &editorState->DestroyProperty))
            {
                imguiTransform(transform);
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
            uint32_t constexpr MaxNameLength = 64;
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
        Entity::Data const EmptyEntity;

        struct PropertyActivator
        {
            Asset::FourCC Type;
            IPropertyActivator* Activator = nullptr;
        };

        DynamicArray<PropertyActivator> PropertyActivators;

        IPropertyActivator* getPropertyActivator(Asset::FourCC type)
        {
            for (uint32_t i = 0; i < PropertyActivators.count(); i++)
            {
                if (PropertyActivators[i].Type == type)
                {
                    return PropertyActivators[i].Activator;
                }
            }
            return nullptr;
        }

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
                    FinalizeLoading,
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
                            [handle = &entityProperty->Handle](IB::Asset::Handle asset)
                        {
                            *handle = toPropertyHandle(asset);
                        });
                        advance(&context->Stream, offset);
                    }

                    context->Data = reinterpret_cast<uint64_t>(&entity);
                    return IB::Asset::wait(loadHandles, handleCount, FinalizeLoading);
                }
                else if (context->State == FinalizeLoading)
                {
                    IB::Job::Handle loadHandles[IB::Asset::MaxDependencyCount];
                    uint32_t handleCount = 0;

                    Entity::Data* entity = reinterpret_cast<Entity::Data*>(context->Data);
                    for (uint32_t i = 0; i < entity->Properties.count(); i++)
                    {
                        IPropertyActivator* activator = getPropertyActivator(entity->Properties[i].Type);
                        if (activator != nullptr)
                        {
                            loadHandles[handleCount++] = activator->onLoadAsync(Entity::toHandle(context->Data));
                        }
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
                destroyEntity(toEntityHandle(assetHandle));
            }
        };

        EntityAssetStreamer EntityStreamer;
    }

    // Entity API
    void addPropertyActivator(Asset::FourCC propertyType, IPropertyActivator* activator)
    {
        PropertyActivators.add(PropertyActivator{ propertyType, activator });
    }

    Entity::Handle createEntity(char const* name)
    {
        Entity::Data& entity = ActiveEntities.add();
        strcpy(entity.Name.String, name);

        return Entity::toHandle(reinterpret_cast<uint64_t>(&entity));
    }

    void destroyEntity(Entity::Handle entityHandle)
    {
        Entity::Data* entity = Entity::toData(entityHandle);
        for (uint32_t i = 0; i < entity->Properties.count(); i++)
        {
            IB::Asset::unloadAssetThreadSafe(entity->Properties[i].Type, toAssetHandle(entity->Properties[i].Handle));
        }

        ActiveEntities.remove(*entity);
    }

    void addPropertyToEntity(Entity::Handle entityHandle, Asset::FourCC type, Property::Handle propertyHandle)
    {
        Entity::Data* entity = Entity::toData(entityHandle);
        IB_ASSERT(findProperty(entity, type) == InvalidProperty, "Assuming that we can't have multiple of a particular property.");
        entity->Properties.add(type, propertyHandle);
    }

    void removePropertyFromEntity(Entity::Handle entity, Asset::FourCC type)
    {
        Entity::Data* entityData = Entity::toData(entity);
        for (uint32_t i = 0; i < entityData->Properties.count(); i++)
        {
            if (entityData->Properties[i].Type == type)
            {
                entityData->Properties[i] = entityData->Properties[entityData->Properties.count() - 1];
                entityData->Properties.remove();
                break;
            }
        }
    }

    Property::Handle getPropertyFromEntity(Entity::Handle entityHandle, Asset::FourCC type)
    {
        if (entityHandle == InvalidEntity)
        {
            return InvalidProperty;
        }
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
        bool propertyHeader(char const* name, bool* shouldDestroyProperty)
        {
            ImGui::AlignTextToFramePadding();
            bool propertyTreeOpen = ImGui::TreeNodeEx(name, ImGuiTreeNodeFlags_AllowItemOverlap);
            ImGui::SameLine();
            ImGui::PushID(name);
            if (ImGui::Button("-"))
            {
                *shouldDestroyProperty = true;
            }
            ImGui::PopID();

            return propertyTreeOpen;
        }

        void entityEditor(Asset::Handle asset, void*)
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
            ImGui::SameLine();
            if (ImGui::Button("+"))
            {
                ImGui::OpenPopup("AddPropertyPopup");
            }
            ImGui::Separator();

            struct ExposedProperty
            {
                Asset::FourCC Type;
                char const* Name;
            };

            ExposedProperty constexpr exposedProperties[] =
            {
                { Asset::toFourCC("TFRM"), "Transform" },
                { Asset::toFourCC("RNDR"), "Renderer" },
                { Asset::toFourCC("CMRA"), "Camera" },
                { Asset::toFourCC("LNSW"), "Line Sweep" },
                { Asset::toFourCC("DLGH"), "Directional Light"}
            };
            uint32_t constexpr exposedPropertyCount = 5;

            if (ImGui::BeginPopup("AddPropertyPopup"))
            {
                for (uint32_t i = 0; i < exposedPropertyCount; i++)
                {
                    if (ImGui::MenuItem(exposedProperties[i].Name))
                    {
                        Asset::FourCC type = exposedProperties[i].Type;
                        addPropertyToEntity(toEntityHandle(asset), type, toPropertyHandle(Asset::createAsset(type)));
                        ImGui::CloseCurrentPopup();
                    }
                }

                ImGui::EndPopup();
            }

            Entity::Handle entityHandle = toEntityHandle(asset);
            for (uint32_t i = 0; i < entity->Properties.count(); i++)
            {
                PropertyEditorState editorState = { entityHandle };
                Editor::drawAssetEditor(entity->Properties[i].Type, toAssetHandle(entity->Properties[i].Handle), &editorState);
            
                if (editorState.DestroyProperty)
                {
                    removePropertyFromEntity(entityHandle, entity->Properties[i].Type);
                    Asset::unloadAssetThreadSafe(entity->Properties[i].Type, toAssetHandle(entity->Properties[i].Handle));
                    i--; // Move back one, we've removed our property from our entity
                }
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

        uint32_t constexpr MaxCellCount = 32;
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

                    Job::Handle baseCellLoad = Asset::loadResourceAsync(baseCellPath, { CellType }, &cell.BaseCell);
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
                            EntityType,
                            Asset::createAsset(EntityType),
                            baseAsset
                        };
                        
                        loadHandles[handleCount++] = IB::Asset::loadSubAssetAsync(loadSubAssetDesc,
                            [&entity](IB::Asset::Handle asset)
                            {
                                entity = toEntityHandle(asset);
                            });

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

                    IB::Asset::saveAssetThreadSafe(context->Stream, IB::EntityType, toAssetHandle(cell.Entities[i]), baseEntityAsset);

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
                    IB::Asset::unloadAssetThreadSafe(IB::EntityType, toAssetHandle(cell.Entities[i]));
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
        void cellEditor(Asset::Handle asset, void*)
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
                Asset::Handle clonedAsset = Asset::cloneAssetThreadSafe(baseEntityAsset, EntityType);
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
        Asset::addStreamer(EntityType, &EntityStreamer);
        Asset::addStreamer(CellType, &CellStreamer);
        Asset::addStreamer(TransformPropertyType, &TransformStreamer);

        Editor::addAssetEditor(CellType, &Editor::cellEditor);
        Editor::addAssetEditor(EntityType, &Editor::entityEditor);
        Editor::addAssetEditor(TransformPropertyType, &Editor::transformEditor);
    }

    void killEntitySystem()
    {

    }
}

