#include "IBEntity.h"
#include "IBAllocator.h"
#include "IBCollection.h"
#include "IBAsset.h"
#include "IBPlatform.h"
#include "IBLogging.h"

#include <imgui/imgui.h>

namespace IB
{
    namespace Property
    {
        namespace
        {
            struct Data
            {
                Asset::FourCC Type;
                Handle Handle = InvalidProperty;
            };
        }
    }

    namespace Entity
    {
        namespace
        {
            constexpr uint32_t MaxNameLength = 64;
            struct Data
            {
                DynamicArray<Property::Data> Properties;
                char Name[MaxNameLength] = {};
            };
        }
    }

    namespace
    {
        ThreadSafePool<Entity::Data> ActiveEntities;
        class EntityAssetStreamer : public IB::Asset::IStreamer
        {
        public:
            IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
            {
                enum State
                {
                    LoadProperties = 0,
                    Complete
                };

                if (context->State == LoadProperties)
                {
                    Entity::Data& entity = ActiveEntities.add();

                    if (context->SaveVersion > 1)
                    {
                        fromBinary(&context->Stream, entity.Name, Entity::MaxNameLength);
                    }

                    uint32_t propertyCount;
                    fromBinary(&context->Stream, &propertyCount);
                    entity.Properties.reserve(propertyCount);

                    IB_ASSERT(propertyCount <= IB::Asset::MaxDependencyCount, "Too many properties!");
                    IB::Job::Handle loadHandles[IB::Asset::MaxDependencyCount];
                    uint32_t handleCount = 0;
                    for (uint32_t i = 0; i < propertyCount; i++)
                    {
                        Property::Data& entityProperty = entity.Properties.add();

                        fromBinary(&context->Stream, &entityProperty.Type);
                        uint32_t offset;
                        fromBinary(&context->Stream, &offset);

                        // Passing pointer to dynamic array item is OK here. We've reserved the memory and will not resize until the load is complete.
                        loadHandles[handleCount++] = IB::Asset::loadSubAssetAsync(context, entityProperty.Type, Asset::InvalidAsset,
                            [](void *data, IB::Asset::Handle asset)
                            {
                                *reinterpret_cast<IB::Property::Handle *>(data) = toPropertyHandle(asset);
                            }, &entityProperty.Handle);
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
                Entity::Data* entity = reinterpret_cast<Entity::Data*>(Asset::toValue(context->Asset));
                toBinary(context->Stream, entity->Name, Entity::MaxNameLength);

                toBinary(context->Stream, entity->Properties.count());
                for (uint32_t i = 0; i < entity->Properties.count(); i++)
                {
                    toBinary(context->Stream, entity->Properties[i].Type);

                    uint32_t dummyWriteSize = 0;
                    toBinary(context->Stream, dummyWriteSize);
                    uint32_t writeStart = flush(context->Stream);
                    IB::Asset::saveSubAssetThreadSafe(context->Stream, entity->Properties[i].Type, toAssetHandle(entity->Properties[i].Handle));
                    uint32_t writeEnd = flush(context->Stream);

                    // Write our written size right before our sub asset.
                    uint32_t writeSize = writeEnd - writeStart;
                    IB::writeToFile(context->Stream->File, &writeSize, sizeof(uint32_t), writeStart - sizeof(uint32_t));
                }
            }

            void unloadThreadSafe(IB::Asset::Handle assetHandle) override
            {
                Entity::Data* entity = reinterpret_cast<Entity::Data*>(Asset::toValue(assetHandle));
                for (uint32_t i = 0; i < entity->Properties.count(); i++)
                {
                    IB::Asset::unloadSubAssetThreadSafe(toAssetHandle(entity->Properties[i].Handle), entity->Properties[i].Type);
                }

                ActiveEntities.remove(*entity);
            }
        };

        EntityAssetStreamer EntityStreamer;

        struct CellAsset
        {
            DynamicArray<Entity::Handle> Entities;
        };

        // Since we don't expect to have many cells, a simple threadsafe allocation scheme is fine

        constexpr uint32_t MaxCellCount = 32;
        CellAsset Cells[MaxCellCount];
        uint32_t CellAllocations[MaxCellCount] = {};

        class CellAssetStreamer : public Asset::IStreamer
        {
            IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
            {
                enum State
                {
                    LoadEntities = 0,
                    Complete
                };

                if (context->State == LoadEntities)
                {
                    uint32_t createdCell = MaxCellCount;
                    for (uint32_t i = 0; i < MaxCellCount; i++)
                    {
                        if (atomicCompareExchange(&CellAllocations[i], false, true) == false)
                        {
                            createdCell = i;
                        }
                    }
                    IB_ASSERT(createdCell != MaxCellCount, "Failed to create a cell! How many cells do we have active?");

                    CellAsset& cell = Cells[createdCell];

                    uint32_t entityCount;
                    fromBinary(&context->Stream, &entityCount);
                    cell.Entities.reserve(entityCount);

                    IB_ASSERT(entityCount <= IB::Asset::MaxDependencyCount, "Too many properties!");
                    IB::Job::Handle loadHandles[IB::Asset::MaxDependencyCount];
                    uint32_t handleCount = 0;
                    for (uint32_t i = 0; i < entityCount; i++)
                    {
                        IB::Entity::Handle& entity = cell.Entities.add();

                        uint32_t offset;
                        fromBinary(&context->Stream, &offset);

                        // Passing pointer to dynamic array item is OK here. We've reserved the memory and will not resize until the load is complete.
                        loadHandles[handleCount++] = IB::Asset::loadSubAssetAsync(context, IB::Asset::toFourCC("ENTT"), Asset::InvalidAsset,
                            [](void *data, IB::Asset::Handle asset)
                            {
                                *reinterpret_cast<IB::Entity::Handle *>(data) = toEntityHandle(asset);
                            }, &entity);

                        advance(&context->Stream, offset);
                    }

                    context->Data = createdCell;
                    return IB::Asset::wait(loadHandles, handleCount, Complete);
                }
                else
                {
                    return IB::Asset::complete(Asset::toHandle(context->Data));
                }
            }

            void saveThreadSafe(IB::Asset::SaveContext *context) override
            {
                CellAsset& cell = Cells[Asset::toValue(context->Asset)];
                toBinary(context->Stream, cell.Entities.count());
                for (uint32_t i = 0; i < cell.Entities.count(); i++)
                {
                    uint32_t dummyWriteSize = 0;
                    toBinary(context->Stream, dummyWriteSize);
                    uint32_t writeStart = flush(context->Stream);

                    IB::Asset::saveSubAssetThreadSafe(context->Stream, IB::Asset::toFourCC("ENTT"), toAssetHandle(cell.Entities[i]));

                    uint32_t writeEnd = flush(context->Stream);

                    // Write our written size right before our sub asset.
                    uint32_t writeSize = writeEnd - writeStart;
                    IB::writeToFile(context->Stream->File, &writeSize, sizeof(uint32_t), writeStart - sizeof(uint32_t));
                }
            }

            void unloadThreadSafe(IB::Asset::Handle assetHandle) override
            {
                CellAsset& cell = Cells[Asset::toValue(assetHandle)];
                for (uint32_t i = 0; i < cell.Entities.count(); i++)
                {
                    IB::Asset::unloadSubAssetThreadSafe(toAssetHandle(cell.Entities[i]), IB::Asset::toFourCC("ENTT"));
                }

                volatileStore<uint32_t>(&CellAllocations[Asset::toValue(assetHandle)], false);
            }
        };
        CellAssetStreamer CellStreamer;
    }

    namespace Editor
    {
        using FrameCallback = void(Asset::Handle asset);
        void addAssetEditor(Asset::FourCC type, FrameCallback* frame);
        void drawAssetEditor(Asset::FourCC type, Asset::Handle handle);

        void entityEditor(Asset::Handle asset)
        {
            Entity::Data* entity = reinterpret_cast<Entity::Data*>(toEntityHandle(asset));
            ImGui::Text("Entity");
            ImGui::Separator();
            if (ImGui::BeginTable("Inspector Table", 2))
            {
                ImGui::TableNextColumn();
                ImGui::Text("Name");

                ImGui::TableNextColumn();
                ImGui::InputText("", entity->Name, Entity::MaxNameLength);

                ImGui::EndTable();
            }

            ImGui::Text("Properties");
            ImGui::Separator();

            for (uint32_t i = 0; i < entity->Properties.count(); i++)
            {
                Editor::drawAssetEditor(entity->Properties[i].Type, toAssetHandle(entity->Properties[i].Handle));
            }
        }
    }

    void initEntitySystem()
    {
        Asset::addStreamer(Asset::toFourCC("ENTT"), &EntityStreamer);
        Asset::addStreamer(Asset::toFourCC("CELL"), &CellStreamer);

        Editor::addAssetEditor(Asset::toFourCC("ENTT"), &Editor::entityEditor);
    }

    void killEntitySystem()
    {

    }

    Entity::Handle createEntity()
    {
        Entity::Data& entity = ActiveEntities.add();
        return Entity::toHandle(reinterpret_cast<uint64_t>(&entity));
    }

    void addPropertyToEntity(Entity::Handle entityHandle, Asset::FourCC type, Property::Handle propertyHandle)
    {
        Entity::Data* entity = reinterpret_cast<Entity::Data*>(entityHandle);
        entity->Properties.add(type, propertyHandle);
    }

    Property::Handle getPropertyFromEntity(Entity::Handle entityHandle, Asset::FourCC type)
    {
        Entity::Data* entity = reinterpret_cast<Entity::Data*>(entityHandle);
        for (uint32_t i = 0; i < entity->Properties.count(); i++)
        {
            if (entity->Properties[i].Type == type)
            {
                return entity->Properties[i].Handle;
            }
        }

        return InvalidProperty;
    }

    char const* getEntityName(Entity::Handle entityHandle)
    {
        Entity::Data* entity = reinterpret_cast<Entity::Data*>(entityHandle);
        if (entity == nullptr)
        {
            return "";
        }
        else if (entity->Name[0] == '\0')
        {
            return "Entity";
        }
        else
        {
            return entity->Name;
        }
    }

    Cell::Handle createCell()
    {
        uint32_t createdCell = MaxCellCount;
        for (uint32_t i = 0; i < MaxCellCount; i++)
        {
            if (atomicCompareExchange(&CellAllocations[i], false, true) == false)
            {
                createdCell = i;
            }
        }

        IB_ASSERT(createdCell != MaxCellCount, "Failed to create a cell! How many cells do we have active?");
        return Cell::toHandle(createdCell);
    }

    void addEntityToCell(Cell::Handle cellHandle, Entity::Handle entity)
    {
        CellAsset& cell = Cells[Cell::toValue(cellHandle)];
        cell.Entities.add(entity);
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
}

