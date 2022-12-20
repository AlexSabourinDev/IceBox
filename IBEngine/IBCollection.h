#pragma once

#include "IBAllocator.h"
#include "IBLogging.h"
#include <string.h>
#include <stdint.h>

namespace IB
{
    template< typename T >
    struct Span
    {
        T& operator[](uint32_t i) const { return Begin[i]; }
        operator Span<T const>() const { return Span<T const>{Begin, Count}; }

        T* Begin = nullptr;
        uint32_t Count = 0;
    };

    template< typename T, uint32_t Count >
    Span<T> toSpan(T(&arr)[Count])
    {
        return Span<T>{arr, Count};
    }

    template< typename T >
    class DynamicArray final
    {
    public:
        DynamicArray() = default;
        DynamicArray(const DynamicArray &rhs);
        DynamicArray &operator=(const DynamicArray &rhs);
        ~DynamicArray();
        template< typename... TArgs >
        T &add(TArgs &&...args);
        void remove();
        void resize(uint32_t count);
        void reserve(uint32_t count);
        void setBuffer(T const *sourceBuffer, uint32_t count);
        void clear(bool freeMemory = true);

        T* data() const { return reinterpret_cast<T*>(Buffer); }
        T& operator[](uint32_t i) const { return data()[i]; }
        uint32_t count() const { return ItemCount; }

        T& back() { return data()[ItemCount - 1]; }
    private:
        void* Buffer = nullptr;
        uint32_t ItemCount = 0;
        uint32_t MaxItemCount = 0;
    };

    // Linear probe hash maps will use the hash for an initial guess at the location of an object and will then
    // try to find the appropriate entry through manual comparison afterwards
    template <typename Key, typename Value, typename uint64_t(keyHash)(Key const&)>
    class LinearProbeHashMap
    {
    public:
        static uint32_t constexpr InvalidKeyIndex = UINT32_MAX;

        uint32_t getIndex(uint64_t hash, Key const &key) const;
        uint32_t getIndex(Key const &key) const;
        bool isSet(Key const &key) const;
        bool tryGet(Key const &key, Value *value) const;
        // Collection may get resized. Don't store pointer for later use.
        Value *getUnsafe(Key const &key);
        void clear(Key const &key);
        // Clear all keys
        void clear(bool freeMemory = true);
        void set(Key const &key, Value const &value);

    private:
        struct KeyValue
        {
            Key Key;
            Value Value;
        };

        // Currently, we just store hashes as uint64, if we need something else, we can revisit
        DynamicArray<uint64_t> Hashes;
        // Store our hashes and keyvalues in separate arrays so that we can look through our keyvalues
        // without having to load our KeyValues into memory until we need it
        DynamicArray<KeyValue> KeyValues;
        uint32_t SetCount = 0;
    };

    // DynamicArray implementation
    template <typename T>
    DynamicArray<T>::DynamicArray(const DynamicArray<T> &rhs)
    {
        operator=(rhs);
    }

    template <typename T>
    DynamicArray<T> &DynamicArray<T>::operator=(const DynamicArray<T> &rhs)
    {
        clear();
        reserve(rhs.MaxItemCount);
        for (uint32_t i = 0; i < rhs.ItemCount; i++)
        {
            new (&(data()[i])) T(rhs[i]);
        }
        ItemCount = rhs.ItemCount;
        return *this;
    }

    template <typename T>
    DynamicArray<T>::~DynamicArray()
    {
        clear();
    }

    template<typename T>
    template <typename... TArgs>
    T & DynamicArray<T>::add(TArgs &&...args)
    {
        if (ItemCount + 1 > MaxItemCount)
        {
            uint32_t nextCount = MaxItemCount == 0 ? 1 : MaxItemCount * 2;
            reserve(nextCount);
        }

        T *item = new (&(data()[ItemCount])) T{std::forward<TArgs>(args)...};
        ItemCount++;

        return *item;
    }

    template <typename T>
    void DynamicArray<T>::remove()
    {
        data()[ItemCount - 1].~T();
        ItemCount--;
    }

    template <typename T>
    void DynamicArray<T>::resize(uint32_t count)
    {
        if (count > MaxItemCount)
        {
            reserve(count);
        }

        if (count > ItemCount)
        {
            for (uint32_t i = ItemCount; i < count; i++)
            {
                new (&(data()[i])) T{};
            }
        }
        else if (count < ItemCount)
        {
            for (uint32_t i = ItemCount; i > count; i--)
            {
                data()[i - 1].~T();
            }
        }
        ItemCount = count;
    }

    template <typename T>
    void DynamicArray<T>::reserve(uint32_t count)
    {
        if (count > MaxItemCount)
        {
            void *newBuffer = memoryAllocate(count * sizeof(T), alignof(T));

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

    template <typename T>
    void DynamicArray<T>::setBuffer(T const *sourceBuffer, uint32_t count)
    {
        clear();
        reserve(count);
        memcpy(Buffer, sourceBuffer, count * sizeof(T));
        ItemCount = count;
    }

    template <typename T>
    void DynamicArray<T>::clear(bool freeMemory)
    {
        for (uint32_t i = 0; i < ItemCount; i++)
        {
            data()[i].~T();
        }

        if (freeMemory)
        {
            memoryFree(Buffer);
            Buffer = nullptr;
            MaxItemCount = 0;
        }

        ItemCount = 0;
    }

    // LinearProbeHashMap Implementation
    template <typename Key, typename Value, typename uint64_t(keyHash)(Key const &)>
    uint32_t LinearProbeHashMap<Key, Value, keyHash>::getIndex(uint64_t hash, Key const &key) const
    {
        uint32_t itemCount = Hashes.count();
        if (itemCount == 0)
        {
            return InvalidKeyIndex;
        }

        // Look at the second half first
        for (uint32_t i = hash % itemCount; i < itemCount; i++)
        {
            if (Hashes[i] == hash)
            {
                if (KeyValues[i].Key == key)
                {
                    return i;
                }
            }
        }

        // Look at the first half next
        for (uint32_t i = 0; i < hash % itemCount; i++)
        {
            if (Hashes[i] == hash)
            {
                if (KeyValues[i].Key == key)
                {
                    return i;
                }
            }
        }

        return InvalidKeyIndex;
    }

    template <typename Key, typename Value, typename uint64_t(keyHash)(Key const &)>
    uint32_t LinearProbeHashMap<Key, Value, keyHash>::getIndex(Key const &key) const
    {
        uint64_t hash = keyHash(key);
        IB_ASSERT(hash != 0, "Expecting our hash function to not return 0!");

        return getIndex(hash, key);
    }

    template <typename Key, typename Value, typename uint64_t(keyHash)(Key const &)>
    bool LinearProbeHashMap<Key, Value, keyHash>::isSet(Key const &key) const
    {
        return getIndex(key) != InvalidKeyIndex;
    }

    template <typename Key, typename Value, typename uint64_t(keyHash)(Key const &)>
    Value* LinearProbeHashMap<Key, Value, keyHash>::getUnsafe(Key const &key)
    {
        uint32_t index = getIndex(key);
        if (index == InvalidKeyIndex)
        {
            return nullptr;
        }

        return &KeyValues[index].Value;
    }

    template <typename Key, typename Value, typename uint64_t(keyHash)(Key const &)>
    bool LinearProbeHashMap<Key, Value, keyHash>::tryGet(Key const &key, Value *value) const
    {
        uint32_t index = getIndex(key);
        if (index == InvalidKeyIndex)
        {
            return false;
        }

        *value = KeyValues[index].Value;
        return true;
    }

    template <typename Key, typename Value, typename uint64_t(keyHash)(Key const &)>
    void LinearProbeHashMap<Key, Value, keyHash>::clear(Key const &key)
    {
        uint32_t index = getIndex(key);
        if (index != InvalidKeyIndex)
        {
            Hashes[index] = {};
            KeyValues[index] = {};
            SetCount--;
        }
    }

    template <typename Key, typename Value, typename uint64_t(keyHash)(Key const &)>
    void LinearProbeHashMap<Key, Value, keyHash>::clear(bool freeMemory)
    {
        Hashes.clear(freeMemory);
        KeyValues.clear(freeMemory);
        SetCount = 0;
    }

    template <typename Key, typename Value, typename uint64_t(keyHash)(Key const &)>
    void LinearProbeHashMap<Key, Value, keyHash>::set(Key const &key, Value const &value)
    {
        if (SetCount == Hashes.count())
        {
            // No rebalancing of our keys right now. Revisit later if this becomes a problem.
            uint32_t newCount = SetCount > 0 ? SetCount * 2 : 1;
            Hashes.resize(newCount);
            KeyValues.resize(newCount);
        }

        uint64_t hashValue = keyHash(key);
        IB_ASSERT(hashValue != 0, "Expecting our hash function to not return 0!");

        uint32_t index = getIndex(hashValue, key);
        if (index != InvalidKeyIndex)
        {
            KeyValues[index].Value = value;
        }
        else
        {
            // Look for an empty slot starting at our hash
            uint32_t itemCount = Hashes.count();

            // Start with the second half
            for (uint32_t i = hashValue % itemCount; i < itemCount; i++)
            {
                if (Hashes[i] == 0)
                {
                    index = i;
                    break;
                }
            }

            // If we didn't find an empty slot in the second half, try the first half
            if (index == InvalidKeyIndex)
            {
                for (uint32_t i = 0; i < hashValue % itemCount; i++)
                {
                    if (Hashes[i] == 0)
                    {
                        index = i;
                        break;
                    }
                }
            }

            IB_ASSERT(index != InvalidKeyIndex, "By this point, we should have an empty slot somewhere!");
            if (index != InvalidKeyIndex)
            {
                Hashes[index] = hashValue;
                KeyValues[index] = {key, value};
                SetCount++;
            }
        }
    }
}
