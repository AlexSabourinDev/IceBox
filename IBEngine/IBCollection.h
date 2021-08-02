#pragma once

#include "IBAllocator.h"

namespace IB
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

        void remove()
        {
            data()[ItemCount - 1].~T();
            ItemCount--;
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

        T& back() { return data()[ItemCount - 1]; }
    private:
        void* Buffer = nullptr;
        uint32_t ItemCount = 0;
        uint32_t MaxItemCount = 0;
    };
}
