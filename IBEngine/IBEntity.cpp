#include "IBEntity.h"
#include "IBAllocator.h"

namespace IB
{
    namespace
    {
        template< typename T >
        class DynamicArray final
        {
        public:
            DynamicArray() = default;
            DynamicArray(const DynamicArray& rhs)
            {
                operator=(rhs);
            }

            DynamicArray& operator=(const DynamicArray& rhs)
            {
                clear();
                reserve(rhs.MaxItemCount);
                for (uint32_t i = 0; i < rhs.ItemCount; i++)
                {
                    new(&(data()[i])) T(rhs[i]);
                }
                ItemCount = rhs.ItemCount;
                return *this;
            }

            ~DynamicArray()
            {
                clear();
            }

            template< typename... TArgs >
            T& add(TArgs&&... args)
            {
                if (ItemCount + 1 > MaxItemCount)
                {
                    uint32_t nextCount = MaxItemCount == 0 ? 1 : MaxItemCount * 2;
                    reserve(nextCount);
                }

                T* item = new(&(data()[ItemCount])) T{ std::forward<TArgs>(args)... };
                ItemCount++;

                return *item;
            }

            void reserve(uint32_t count)
            {
                if (count > MaxItemCount)
                {
                    void* newBuffer = memoryAllocate(count * sizeof(T), alignof(T));
                    // memcpy is OK here because we're creating an identical
                    // bitwise copy of our object to another memory location.
                    // This is not creating a copy, we're essentially moving our object's location in memory.
                    // If we were creating a copy, we would want to use our copy constructor.
                    memcpy(newBuffer, Buffer, ItemCount * sizeof(T));

                    memoryFree(Buffer);
                    Buffer = newBuffer;

                    MaxItemCount = count;
                }
            }

            void clear()
            {
                for (uint32_t i = 0; i < MaxItemCount; i++)
                {
                    data()[i].~T();
                }
                memoryFree(Buffer);
                Buffer = nullptr;
                ItemCount = 0;
                MaxItemCount = 0;
            }

            T* data() const { return reinterpret_cast<T*>(Buffer); }
            T& operator[](uint32_t i) const { return data()[i]; }
            uint32_t count() const { return ItemCount; }
        private:
            void* Buffer = nullptr;
            uint32_t ItemCount = 0;
            uint32_t MaxItemCount = 0;
        };

        struct Entity
        {
            struct Property
            {
                Asset::FourCC Type;
                PropertyHandle Handle;
            };

            DynamicArray<Property> Properties;
        };

        ThreadSafePool<Entity> ActiveEntities;
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
                    Entity& entity = ActiveEntities.add();

                    uint32_t propertyCount;
                    fromBinary(&context->Stream, &propertyCount);
                    entity.Properties.reserve(propertyCount);

                    IB_ASSERT(propertyCount <= IB::Asset::MaxDependencyCount, "Too many properties!");
                    IB::JobHandle loadHandles[IB::Asset::MaxDependencyCount];
                    uint32_t handleCount = 0;
                    for (uint32_t i = 0; i < propertyCount; i++)
                    {
                        Entity::Property& entityProperty = entity.Properties.add();

                        fromBinary(&context->Stream, &entityProperty.Type);
                        uint32_t offset;
                        fromBinary(&context->Stream, &offset);

                        // Passing pointer to dynamic array item is OK here. We've reserved the memory and will not resize until the load is complete.
                        loadHandles[handleCount++] = IB::Asset::loadSubAssetAsync(context->Stream, entityProperty.Type, { 0 },
                            [](void *data, IB::Asset::AssetHandle asset)
                            {
                                *reinterpret_cast<IB::PropertyHandle *>(data) = toPropertyHandle(asset);
                            }, &entityProperty.Handle);
                        advance(&context->Stream, offset);
                    }

                    context->Data = reinterpret_cast<uint64_t>(&entity);
                    return IB::Asset::wait(loadHandles, handleCount, Complete);
                }
                else
                {
                    return IB::Asset::complete({ context->Data });
                }
            }

            void saveThreadSafe(IB::Asset::SaveContext *context) override
            {
                Entity* entity = reinterpret_cast<Entity*>(context->Asset.Value);
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

            void unloadThreadSafe(IB::Asset::AssetHandle assetHandle) override
            {
                Entity* entity = reinterpret_cast<Entity*>(assetHandle.Value);
                for (uint32_t i = 0; i < entity->Properties.count(); i++)
                {
                    IB::Asset::unloadSubAssetThreadSafe(toAssetHandle(entity->Properties[i].Handle), entity->Properties[i].Type);
                }

                ActiveEntities.remove(*entity);
            }
        };

        EntityAssetStreamer EntityStreamer;
    }

    void initEntitySystem()
    {
        Asset::addStreamer(Asset::toFourCC("ENTT"), &EntityStreamer);
    }

    void killEntitySystem()
    {

    }

    EntityHandle createEntity()
    {
        Entity& entity = ActiveEntities.add();
        return EntityHandle{ reinterpret_cast<uint64_t>(&entity) };
    }

    void addPropertyToEntity(EntityHandle entityHandle, Asset::FourCC type, PropertyHandle propertyHandle)
    {
        Entity* entity = reinterpret_cast<Entity*>(entityHandle.Value);
        entity->Properties.add(type, propertyHandle);
    }
}

