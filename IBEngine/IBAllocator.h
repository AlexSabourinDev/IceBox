#pragma once

#include "IBEngineAPI.h"

#include <stdint.h>
#include <utility>
#include <new>

/*
## Why do we want a custom allocator?
We want a custom allocator for a few reasons:
- It will allow us to capture memory usage statistics and output them to a file for exploration
- It can be more optimal for our use case than the general memory allocator
- It's an interesting challenge to write

Some downsides however are:
- It will likely be somewhat buggy for some time
- It might actually be less optimal than the general case at the beginning

Ideally, these downsides will slowly fade in time as we iterate and improve on the allocator.

## Usage Guidelines
- Use IBAllocator over new/delete in engine code
    - We want to make sure we can gather statistics on memory usage.
    - We want to make sure we have control of the performance of our allocations.
- Prefer IBAllocator over new/delete in C++ editor code
    It could be elegant to be able to also gather statistics on our editor memory usage.
- If you find you are making many short lived allocations (function scope/frame scope), consider if we want to add a scrap allocator.
- If you find yourself using the standard library containers, consider using our custom allocator instead of the standard allocator.
*/

namespace IB
{
    IB_API void *memoryAllocate(size_t size, size_t alignment); // threadsafe
    IB_API void memoryFree(void *memory); // threadsafe

    template <typename T, typename... TArgs>
    T *allocate(TArgs &&... args)
    {
        void *memory = memoryAllocate(sizeof(T), alignof(T));
        return new (memory) T{std::forward<TArgs>(args)...};
    }

    template <typename T>
    void deallocate(T *object)
    {
        object->~T();
        memoryFree(object);
    }

    template <typename T, typename... TArgs>
    T *allocateArray(uint32_t count, TArgs &&... args)
    {
        void *memory = memoryAllocate(sizeof(T) * count, alignof(T));
        T *arrayMemory = reinterpret_cast<T *>(memory);
        for (uint32_t i = 0; i < count; i++)
        {
            new (arrayMemory + i) T(std::forward<TArgs>(args)...);
        }

        return arrayMemory;
    }

    template <typename T>
    void deallocateArray(T const *arrayMemory, uint32_t count)
    {
        if (count > 0)
        {
            for (uint32_t i = 0; i < count; i++)
            {
                arrayMemory[i].~T();
            }
            // Remove const here, we want to be able to deallocate a const pointer,
            // but this will definitely modify the memory.
            memoryFree(const_cast<T *>(arrayMemory));
        }
    }

    struct BlockPool
    {
        void *Memory = nullptr;
        size_t BlockSize = 0;
    };

    IB_API BlockPool createBlockPool(size_t blockSize, size_t blockAlignment);
    IB_API void destroyBlockPool(BlockPool *pool);
    IB_API void *memoryAllocate(BlockPool *pool); // threadsafe
    IB_API void memoryFree(BlockPool *pool, void *blockMemory); // threadsafe

    template< typename T>
    struct ThreadSafePool final
    {
        ThreadSafePool() = default;
        ~ThreadSafePool()
        {
            destroyBlockPool(&Pool);
        }

        ThreadSafePool(const ThreadSafePool&) = delete;
        ThreadSafePool& operator=(const ThreadSafePool&) = delete;

        template< typename... TArgs>
        T& add(TArgs&&... args)
        {
            void *memory = memoryAllocate(&Pool);
            T* newObject = new (memory) T{ std::forward<TArgs>(args)... };
            return *newObject;
        }

        void remove(T& object)
        {
            object.~T();
            memoryFree(&Pool, &object);
        }

        BlockPool Pool = createBlockPool(sizeof(T), alignof(T));
    };
} // namespace IB
