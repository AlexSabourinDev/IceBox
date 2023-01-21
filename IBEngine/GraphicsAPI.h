#pragma once

#define VK_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

namespace IB
{
    namespace VkMemory
    {
        uint32_t constexpr MaxAllocatorPools = 10;
        uint32_t constexpr MaxMemoryBlocks = 1000;

        struct Category
        {
            enum Enum
            {
                Misc = 0,
                TransferSource,
                Mesh,
                Image,
                Count
            };
        };

        struct MemoryBlock
        {
            VkDeviceSize Size = 0;
            VkDeviceSize Offset = 0;
            uint32_t Id = UINT32_MAX;
            uint32_t NextIndex = UINT32_MAX;
            uint32_t ParentIndex = UINT32_MAX;
            uint32_t SiblingIndex = UINT32_MAX;
            bool Allocated = false;
        };

        struct MemoryPool
        {
            VkDeviceMemory Memory = {};
            VkDeviceSize Size = {};
            uint32_t HeadIndex = UINT32_MAX;
            uint32_t NextId = UINT32_MAX;
            uint32_t MemoryType = UINT32_MAX;
            Category::Enum Category;
            void *Map = nullptr;
        };

        struct Allocator
        {
            MemoryPool MemoryPools[MaxAllocatorPools] = {};
            MemoryBlock BlockPool[MaxMemoryBlocks] = {};
            uint32_t FreeBlocks[MaxMemoryBlocks] = {};
            uint32_t FreeBlockCount = 0;
        };

        struct Allocation
        {
            VkDeviceMemory Memory = {};
            VkDeviceSize Offset = {};
            uint32_t Id = 0;
            uint32_t PoolIndex = 0;
        };
    }
}
  
// Core API
namespace IB
{
    VkAllocationCallbacks *const NoVkAllocator = nullptr;
    void vkCheck(VkResult result)
    {
        if (VK_SUCCESS != result)
        {
            IB_ASSERT(false, "Failed VK Check.");
        }
    }
    #define IB_VKCHECK(call) IB::vkCheck(call);

    struct GraphicsQueue
    {
        enum Enum
        {
            Present = 0,
            Graphics,
            Compute,
            Transfer,
            Count
        };
    };

	struct GraphicsAPI
	{
        VkInstance Instance;
        VkPhysicalDevice PhysicalDevice;
        VkDevice LogicalDevice;

        VkDescriptorPool DescriptorPool;
        VkPipelineCache PipelineCache;

        struct
        {
            uint32_t Index;
            VkQueue Queue;
            VkCommandPool CommandPool;
        } Queues[GraphicsQueue::Count];
	};
	
    struct GraphicsAPIDesc
    {
        // Win32
        void* Win32MainWindowHandle;
        void* Win32MainInstanceHandle;
    };

	void initGraphicsAPI(GraphicsAPIDesc desc, GraphicsAPI* outGraphicsAPI);
    void killGraphicsAPI(GraphicsAPI* graphicsAPI);
}

// Texture
namespace IB
{
    struct Texture
    {
        VkMemory::Allocation Allocation = {};
        VkImage Image = {};
        VkImageView View = {};
        VkImageAspectFlags Aspect = {};
    };

    VkImageMemoryBarrier createMemoryBarrier(
        Texture const* texture,
        VkAccessFlagBits sourceAccessMask,
        VkAccessFlagBits dstAccessMask,
        VkImageLayout oldLayout,
        VkImageLayout newLayout);
}

// Command buffer
namespace IB
{
    struct CommandBuffer
    {
        VkCommandBuffer Commands;
        VkSemaphore FinishedSemaphore;
        GraphicsQueue::Enum QueueType;
    };

    struct CommandBufferDesc
    {
        GraphicsQueue::Enum QueueType;
    };

    // TODO: Remove - not a useful abstraction.
    void initCommandBuffer(GraphicsAPI* api, CommandBufferDesc desc, CommandBuffer* outCommandBuffer);
    void killCommandBuffer(GraphicsAPI* api, CommandBuffer* commandBuffer);

    struct SubmitCommandBufferDesc
    {
        CommandBuffer* CommandBuffer;
        VkSemaphore WaitSemaphore;
        VkFence SubmissionFence;
    };
    void submitCommandBuffer(GraphicsAPI* api, SubmitCommandBufferDesc desc);
}

// Surface
namespace IB
{
    uint32_t constexpr FramebufferCount = 2;

    struct GraphicsSurface
    {
        VkSurfaceKHR VulkanSurface;
        VkSurfaceFormatKHR Format;
        VkExtent2D Extents;
        VkPresentModeKHR PresentMode;
        VkSwapchainKHR Swapchain;

        struct
        {
            Texture Texture;
            VkSemaphore AcquireSemaphore;
        } Framebuffers[FramebufferCount];
    };

    struct SurfaceDesc
    {
        void* Win32WindowHandle = nullptr;
        void* Win32InstanceHandle = nullptr;
        bool UseVSync = true;
    };

    void initWin32GraphicsSurface(GraphicsAPI* api, SurfaceDesc desc, GraphicsSurface* outSurface);
    void killGraphicsSurface(GraphicsAPI* api, GraphicsSurface* surface);

    struct SurfaceState
    {
        enum Enum
        {
            Ok = 0,
            ShouldRebuild
        };
    };

    struct SurfacePrepareDesc
    {
        GraphicsSurface* Surface;
        uint32_t Framebuffer;
    };
    struct SurfacePrepareResult
    {
        uint32_t ImageIndex;
        SurfaceState::Enum SurfaceState;
    };
    bool prepareSurface(GraphicsAPI* api, SurfacePrepareDesc prepareDesc, SurfacePrepareResult* outPrepareResult);

    struct SurfacePresentDesc
    {
        GraphicsSurface* Surface;
        SurfacePrepareResult PrepareResult;
        CommandBuffer PresentCommands;
    };
    struct SurfacePresentResult
    {
        SurfaceState::Enum SurfaceState;
    };
    void presentSurface(GraphicsAPI* api, SurfacePresentDesc presentDesc, SurfacePresentResult* outPresentResult);
    void rebuildSurface(GraphicsAPI* api, GraphicsSurface* surface);
}

// Staging
namespace IB
{
    uint32_t constexpr StagingSize = 32 * 1024 * 1024; // 32MB of staging space
    uint32_t constexpr MaxStagingFenceCount = 128;
    uint32_t constexpr MaxTransferCount = 128;
    uint32_t constexpr MaxDescriptorUpdateCount = 128;
    struct StagingCore
    {
        VkBuffer Buffer;
        IB::VkMemory::Allocation Allocation;
        void* MappedMemory = nullptr;
        uint32_t WriteOffset = 0;

        VkFence Fences[MaxStagingFenceCount];
        uint32_t ActiveFenceCount = 0;

        struct
        {
            CommandBuffer Commands[MaxTransferCount];
            uint32_t ActiveCount = 0;
        } Transfers;

        struct
        {
            VkDescriptorImageInfo DescriptorInfos[MaxDescriptorUpdateCount];
            VkWriteDescriptorSet DescriptorWrites[MaxDescriptorUpdateCount];
            uint32_t ActiveCount = 0;
        } DescriptorUpdates;
    };

    struct StagingBuffer
    {
        VkBuffer Buffer;
        void* Memory;
        uint32_t Offset;
        VkFence Fence;
    };

    struct StagingDesc
    {
        VkMemory::Allocator* Allocator;
    };

    void initStaging(GraphicsAPI* api, StagingDesc desc, StagingCore* outStaging);
    void killStaging(GraphicsAPI* api, VkMemory::Allocator* allocator, StagingCore* staging);
    StagingBuffer requestStagingBuffer(StagingCore* staging, uint32_t size, uint32_t alignment = 1);
}

// TODO: Move to CPP when in a good place
#ifdef IB_GRAPHICS_API_IMPL

#include "IBLogging.h"

// Utilities
namespace
{
    template <typename T, uint32_t Count>
    uint32_t constexpr arrayCount(T (&arr)[Count])
    {
        (void)arr;
        return Count;
    }
}

// Allocator
namespace IB
{
    namespace VkMemory
    {
        uint32_t constexpr MaxAllocatorPoolSizes[Category::Count] = {
            1024 * 1024 * 16,
            1024 * 1024 * 64,
            1024 * 1024 * 64,
            1024 * 1024 * 512,
        };

        struct AllocationRequest
        {
            VkPhysicalDevice PhysicalDevice;
            VkDevice LogicalDevice;
            VkDeviceSize Size;
            VkDeviceSize Alignment;
            uint32_t TypeBits;
            VkMemoryPropertyFlags RequiredFlags;
            VkMemoryPropertyFlags PreferedFlags;
            Category::Enum Category;
        };

        uint32_t findMemoryIndex(VkPhysicalDevice physicalDevice, uint32_t typeBits, VkMemoryPropertyFlags requiredFlags, VkMemoryPropertyFlags preferedFlags)
        {
            uint32_t preferedMemoryIndex = UINT32_MAX;
            VkPhysicalDeviceMemoryProperties physicalDeviceProperties;
            vkGetPhysicalDeviceMemoryProperties(physicalDevice, &physicalDeviceProperties);

            VkMemoryType *types = physicalDeviceProperties.memoryTypes;

            for (uint32_t i = 0; i < physicalDeviceProperties.memoryTypeCount; ++i)
            {
                if ((typeBits & (1 << i)) && (types[i].propertyFlags & (requiredFlags | preferedFlags)) == (requiredFlags | preferedFlags))
                {
                    return i;
                }
            }

            if (preferedMemoryIndex == UINT32_MAX)
            {
                for (uint32_t i = 0; i < physicalDeviceProperties.memoryTypeCount; ++i)
                {
                    if ((typeBits & (1 << i)) && (types[i].propertyFlags & requiredFlags) == requiredFlags)
                    {
                        return i;
                    }
                }
            }

            return UINT32_MAX;
        }

        void initAllocator(Allocator *allocator)
        {
            allocator->FreeBlockCount = MaxMemoryBlocks;
            for (uint32_t freeBlockIndex = 0; freeBlockIndex < allocator->FreeBlockCount; freeBlockIndex++)
            {
                allocator->FreeBlocks[freeBlockIndex] = freeBlockIndex;
            }
        }

        void killAllocator(VkDevice device, Allocator *allocator)
        {
            for (unsigned int i = 0; i < MaxAllocatorPools; i++)
            {
                uint32_t iter = allocator->MemoryPools[i].HeadIndex;
                while (iter != UINT32_MAX)
                {
                    allocator->FreeBlocks[allocator->FreeBlockCount] = iter;
                    allocator->FreeBlockCount++;

                    MemoryBlock *currentBlock = &allocator->BlockPool[iter];
                    currentBlock->NextIndex = UINT32_MAX;
                    iter = currentBlock->NextIndex;
                }

                if (allocator->MemoryPools[i].HeadIndex != UINT32_MAX)
                {
                    allocator->MemoryPools[i].HeadIndex = UINT32_MAX;
                    vkFreeMemory(device, allocator->MemoryPools[i].Memory, NULL);
                }
            }
        }

        void *mapAllocation(VkDevice logicalDevice, Allocator *allocator, Allocation allocation)
        {
            if (allocator->MemoryPools[allocation.PoolIndex].Map == nullptr)
            {
                unsigned int flags = 0;
                IB_VKCHECK(vkMapMemory(logicalDevice, allocator->MemoryPools[allocation.PoolIndex].Memory, 0, VK_WHOLE_SIZE, flags, &allocator->MemoryPools[allocation.PoolIndex].Map));
            }

            return reinterpret_cast<uint8_t *>(allocator->MemoryPools[allocation.PoolIndex].Map) + allocation.Offset;
        }

        void unmapAllocation(Allocator * /*allocator*/, Allocation /*allocation*/)
        {
            // Does nothing
        }

        Allocation allocateDeviceMemory(Allocator *allocator, AllocationRequest request)
        {
            uint32_t const memoryTypeIndex = findMemoryIndex(request.PhysicalDevice, request.TypeBits, request.RequiredFlags, request.PreferedFlags);
            IB_ASSERT(memoryTypeIndex != UINT32_MAX, "Invalid memory type index.");

            uint32_t foundPoolIndex = UINT32_MAX;
            for (uint32_t i = 0; i < MaxAllocatorPools; i++)
            {
                if (allocator->MemoryPools[i].MemoryType == memoryTypeIndex && allocator->MemoryPools[i].Category == request.Category)
                {
                    foundPoolIndex = i;
                    break;
                }
            }

            if (foundPoolIndex == UINT32_MAX)
            {
                for (unsigned int i = 0; i < MaxAllocatorPools; i++)
                {
                    MemoryPool *memoryPool = &allocator->MemoryPools[i];
                    if (memoryPool->HeadIndex == UINT32_MAX)
                    {
                        memoryPool->Size = MaxAllocatorPoolSizes[request.Category];

                        VkMemoryAllocateInfo memoryAllocInfo = {};
                        memoryAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                        memoryAllocInfo.allocationSize = memoryPool->Size;
                        memoryAllocInfo.memoryTypeIndex = memoryTypeIndex;

                        IB_VKCHECK(vkAllocateMemory(request.LogicalDevice, &memoryAllocInfo, NULL, &memoryPool->Memory));

                        IB_ASSERT(allocator->FreeBlockCount > 0, "No free blocks left!");
                        uint32_t newBlockIndex = allocator->FreeBlocks[allocator->FreeBlockCount - 1];
                        allocator->FreeBlockCount--;

                        MemoryBlock *block = &allocator->BlockPool[newBlockIndex];
                        *block = MemoryBlock{};//Clear our block before use
                        block->Size = memoryPool->Size;
                        block->Offset = 0;
                        block->Allocated = false;

                        memoryPool->HeadIndex = newBlockIndex;
                        memoryPool->NextId = 1;
                        memoryPool->MemoryType = memoryTypeIndex;
                        memoryPool->Category = request.Category;

                        foundPoolIndex = i;
                        break;
                    }
                }
            }

            IB_ASSERT(foundPoolIndex != UINT32_MAX, "Failed to find a memory pool.");

            VkDeviceSize allocationSize = request.Size + (request.Alignment - request.Size % request.Alignment);
            // Fun little trick to round to next nearest power of 2 from https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
            // Reduce number by 1, will handle powers of 2
            allocationSize--;
            // Set all the lower bits to get a full set bit pattern giving us Pow2 - 1
            allocationSize |= allocationSize >> 1;
            allocationSize |= allocationSize >> 2;
            allocationSize |= allocationSize >> 4;
            allocationSize |= allocationSize >> 8;
            allocationSize |= allocationSize >> 16;
            // Add 1 to push to pow of 2
            allocationSize++;

            MemoryPool *memoryPool = &allocator->MemoryPools[foundPoolIndex];
            // Look for a free block our size
            for (uint32_t iter = memoryPool->HeadIndex; iter != UINT32_MAX; iter = allocator->BlockPool[iter].NextIndex)
            {
                MemoryBlock *memoryBlock = &allocator->BlockPool[iter];
                if (!memoryBlock->Allocated && memoryBlock->Size == allocationSize)
                {
                    memoryBlock->Allocated = true;

                    Allocation allocation = {};
                    allocation.Memory = memoryPool->Memory;
                    allocation.Offset = memoryBlock->Offset;
                    allocation.Id = memoryBlock->Id;
                    allocation.PoolIndex = foundPoolIndex;
                    return allocation;
                }
            }

            // Couldn't find a block the right size, create one from our closest block
            uint32_t smalledBlockIndex = UINT32_MAX;
            for (uint32_t iter = memoryPool->HeadIndex; iter != UINT32_MAX; iter = allocator->BlockPool[iter].NextIndex)
            {
                MemoryBlock *block = &allocator->BlockPool[iter];

                if (!block->Allocated && block->Size > allocationSize)
                {
                    if (smalledBlockIndex == UINT32_MAX || block->Size < allocator->BlockPool[smalledBlockIndex].Size)
                    {
                        smalledBlockIndex = iter;
                    }
                }
            }

            uint32_t iterIndex = smalledBlockIndex;
            if (iterIndex == UINT32_MAX)
            {
                IB_ASSERT(false, "Failed to allocate a block.");
                return Allocation{};
            }

            while (allocator->BlockPool[iterIndex].Size > allocationSize)
            {
                MemoryBlock* iter = &allocator->BlockPool[iterIndex];
                VkDeviceSize newBlockSize = iter->Size / 2;

                iter->Allocated = true;
                IB_ASSERT(allocator->FreeBlockCount >= 2, "We should have at least 2 blocks free before we split.");

                uint32_t leftIndex = allocator->FreeBlocks[allocator->FreeBlockCount - 1];
                allocator->FreeBlockCount--;

                MemoryBlock *left = &allocator->BlockPool[leftIndex];
                left->Offset = iter->Offset;
                left->Size = newBlockSize;
                left->Id = memoryPool->NextId;
                left->Allocated = false;
                ++memoryPool->NextId;

                uint32_t rightIndex = allocator->FreeBlocks[allocator->FreeBlockCount - 1];
                allocator->FreeBlockCount--;

                MemoryBlock *right = &allocator->BlockPool[rightIndex];
                right->Offset = iter->Offset + newBlockSize;
                right->Size = newBlockSize;
                right->Id = memoryPool->NextId;
                right->Allocated = false;
                ++memoryPool->NextId;

                left->NextIndex = rightIndex;
                left->ParentIndex = iterIndex;
                left->SiblingIndex = rightIndex;
                IB_ASSERT(allocator->BlockPool[left->NextIndex].Size <= left->Size, "Our left sibling should always point to an equal or smaller block.");
                IB_ASSERT(left->NextIndex != UINT32_MAX, "Our left sibling should never point to the terminator.");

                right->NextIndex = iter->NextIndex;
                right->ParentIndex = iterIndex;
                right->SiblingIndex = leftIndex;
                IB_ASSERT(right->NextIndex == UINT32_MAX || allocator->BlockPool[right->NextIndex].Size != right->Size, "Our right sibling should never point to it's own sibling.");

                iter->NextIndex = leftIndex;

                iterIndex = leftIndex;
            }

            MemoryBlock* allocationBlock = &allocator->BlockPool[iterIndex];
            allocationBlock->Allocated = true;

            Allocation allocation = {};
            allocation.Memory = memoryPool->Memory;
            allocation.Offset = allocationBlock->Offset;
            allocation.Id = allocationBlock->Id;
            allocation.PoolIndex = foundPoolIndex;
            return allocation;
        }

        void freeMemoryBlock(Allocator *allocator, MemoryPool *memoryPool, uint32_t currentBlockIndex)
        {
            MemoryBlock* currentBlock = &allocator->BlockPool[currentBlockIndex];

            currentBlock->Allocated = false;
            if (currentBlock->SiblingIndex != UINT32_MAX)
            {
                MemoryBlock *siblingBlock = &allocator->BlockPool[currentBlock->SiblingIndex];
                IB_ASSERT(siblingBlock->Size == currentBlock->Size, "Sibling is not the right size!");

                if (!siblingBlock->Allocated)
                {
                    allocator->FreeBlocks[allocator->FreeBlockCount] = currentBlockIndex;
                    allocator->FreeBlocks[allocator->FreeBlockCount + 1] = currentBlock->SiblingIndex;
                    allocator->FreeBlockCount += 2;

                    IB_ASSERT(currentBlock->ParentIndex != UINT32_MAX, "Blocks with siblings should have parents");

                    MemoryBlock *parentBlock = &allocator->BlockPool[currentBlock->ParentIndex];
                    IB_ASSERT(parentBlock->Size == currentBlock->Size * 2, "Parent block is incorrect!");

                    // The rightmost block has to move it's next index to our parent
                    if (currentBlock->Offset > siblingBlock->Offset)
                    {
                        parentBlock->NextIndex = currentBlock->NextIndex;
                    }
                    else
                    {
                        parentBlock->NextIndex = siblingBlock->NextIndex;
                    }

                    // Make sure to merge upwards
                    freeMemoryBlock(allocator, memoryPool, currentBlock->ParentIndex);
                }
            }
        }

        void freeDeviceMemory(Allocator *allocator, Allocation allocation)
        {
            MemoryPool *memoryPool = &allocator->MemoryPools[allocation.PoolIndex];

            for (uint32_t iter = memoryPool->HeadIndex; iter != UINT32_MAX; iter = allocator->BlockPool[iter].NextIndex)
            {
                MemoryBlock *currentBlock = &allocator->BlockPool[iter];
                if (currentBlock->Id == allocation.Id)
                {
                    freeMemoryBlock(allocator, memoryPool, iter);
                    return;
                }
            }

            IB_ASSERT(false, "Failed to find our block!");
        }
    }
}

// Core API
namespace IB
{
    // #define IB_VKDEBUG

    const char *InstanceExtensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    const char *DeviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME};
    const char *ValidationLayers[] = {"VK_LAYER_KHRONOS_validation"};

    uint32_t constexpr MaxUniformBufferCount = 1000;
    uint32_t constexpr MaxStorageBufferCount = 1000;
    uint32_t constexpr MaxStorageImageCount = 100;
    uint32_t constexpr MaxImageSamplerCount = 1000;
    uint32_t constexpr MaxDescriptorSetCount = 1000;
    uint32_t constexpr MaxSampledImageCount = 1000;
    
    // Forward from surface API
    VkSurfaceKHR createWin32VkSurface(VkInstance vkInstance, void* windowHandle, void* instanceHandle);
    void initGraphicsAPI(GraphicsAPIDesc desc, GraphicsAPI* outGraphicsAPI)
	{
        // Instance
        {
            VkApplicationInfo appInfo = {};
            appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            appInfo.pNext = NULL;
            appInfo.pApplicationName = "IceBox";
            appInfo.applicationVersion = 1;
            appInfo.pEngineName = "IceBox";
            appInfo.engineVersion = 1;
            appInfo.apiVersion = VK_MAKE_VERSION(1, 2, VK_HEADER_VERSION);

            VkInstanceCreateInfo createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            createInfo.pNext = NULL;
            createInfo.pApplicationInfo = &appInfo;
            createInfo.enabledExtensionCount = arrayCount(InstanceExtensions);
            createInfo.ppEnabledExtensionNames = InstanceExtensions;

    #ifdef IB_VKDEBUG
            createInfo.enabledLayerCount = arrayCount(ValidationLayers);
            createInfo.ppEnabledLayerNames = ValidationLayers;
    #endif // IB_VKDEBUG

            IB_VKCHECK(vkCreateInstance(&createInfo, NoVkAllocator, &outGraphicsAPI->Instance));
        }

        // Device
        {
            uint32_t constexpr maxPhysicalDeviceCount = 10;
            uint32_t constexpr maxPhysicalDeviceProperties = 100;

            uint32_t physicalDeviceCount;
            VkPhysicalDevice physicalDevices[maxPhysicalDeviceCount];

            uint32_t queuePropertyCounts[maxPhysicalDeviceCount];
            VkQueueFamilyProperties queueProperties[maxPhysicalDeviceCount][maxPhysicalDeviceProperties];

            // Enumerate the physical device properties
            {
                IB_VKCHECK(vkEnumeratePhysicalDevices(outGraphicsAPI->Instance, &physicalDeviceCount, NULL));
                physicalDeviceCount = physicalDeviceCount <= maxPhysicalDeviceCount ? physicalDeviceCount : maxPhysicalDeviceCount;

                IB_VKCHECK(vkEnumeratePhysicalDevices(outGraphicsAPI->Instance, &physicalDeviceCount, physicalDevices));
                for (uint32_t deviceIndex = 0; deviceIndex < physicalDeviceCount; deviceIndex++)
                {
                    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[deviceIndex], &queuePropertyCounts[deviceIndex], NULL);
                    IB_ASSERT(queuePropertyCounts[deviceIndex] > 0 && queuePropertyCounts[deviceIndex] <= maxPhysicalDeviceProperties, "Unexpected queue properties!");

                    queuePropertyCounts[deviceIndex] = queuePropertyCounts[deviceIndex] <= maxPhysicalDeviceProperties ? queuePropertyCounts[deviceIndex] : maxPhysicalDeviceProperties;
                    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[deviceIndex], &queuePropertyCounts[deviceIndex], queueProperties[deviceIndex]);
                }
            }

            // Select the device
            {
                uint32_t physicalDeviceIndex = UINT32_MAX;
                for (uint32_t deviceIndex = 0; deviceIndex < physicalDeviceCount; deviceIndex++)
                {
                    uint32_t graphicsQueue = UINT32_MAX;
                    uint32_t computeQueue = UINT32_MAX;
                    uint32_t presentQueue = UINT32_MAX;
                    uint32_t transferQueue = UINT32_MAX;

                    // Find our graphics queue
                    for (uint32_t propIndex = 0; propIndex < queuePropertyCounts[deviceIndex]; propIndex++)
                    {
                        if (queueProperties[deviceIndex][propIndex].queueCount == 0)
                        {
                            continue;
                        }

                        if (queueProperties[deviceIndex][propIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                        {
                            graphicsQueue = propIndex;
                        }

                        if (queueProperties[deviceIndex][propIndex].queueFlags & VK_QUEUE_COMPUTE_BIT)
                        {
                            computeQueue = propIndex;
                        }

                        if (queueProperties[deviceIndex][propIndex].queueFlags & VK_QUEUE_TRANSFER_BIT)
                        {
                            transferQueue = propIndex;
                        }
                    }

                    // Create a temporary surface based on our main window to select our device.
                    VkSurfaceKHR const temporarySurface = createWin32VkSurface(outGraphicsAPI->Instance, desc.Win32MainWindowHandle, desc.Win32MainInstanceHandle);

                    // Find our present queue
                    for (uint32_t propIndex = 0; propIndex < queuePropertyCounts[deviceIndex]; propIndex++)
                    {
                        if (queueProperties[deviceIndex][propIndex].queueCount == 0)
                        {
                            continue;
                        }

                        VkBool32 supportsPresent = VK_FALSE;
                        IB_VKCHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevices[deviceIndex], propIndex, temporarySurface, &supportsPresent));

                        if (supportsPresent)
                        {
                            presentQueue = propIndex;
                            break;
                        }
                    }

                    vkDestroySurfaceKHR(outGraphicsAPI->Instance, temporarySurface, NoVkAllocator);

                    // Did we find a device supporting both graphics, present and compute.
                    if (graphicsQueue != UINT32_MAX && presentQueue != UINT32_MAX && computeQueue != UINT32_MAX)
                    {
                        outGraphicsAPI->Queues[GraphicsQueue::Graphics].Index = graphicsQueue;
                        outGraphicsAPI->Queues[GraphicsQueue::Compute].Index = computeQueue;
                        outGraphicsAPI->Queues[GraphicsQueue::Present].Index = presentQueue;
                        outGraphicsAPI->Queues[GraphicsQueue::Transfer].Index = transferQueue;
                        physicalDeviceIndex = deviceIndex;
                        break;
                    }
                }

                IB_ASSERT(physicalDeviceIndex != UINT32_MAX, "Failed to select a physical device!");
                outGraphicsAPI->PhysicalDevice = physicalDevices[physicalDeviceIndex];
            }
        }

        // Create the logical device
        {
            VkDeviceQueueCreateInfo queueCreateInfo[GraphicsQueue::Count] = {};
            uint32_t queueCreateInfoCount = 0;

            for (uint32_t i = 0; i < GraphicsQueue::Count; i++)
            {
                // Have we already checked this index
                {
                    bool queueFound = false;
                    for (uint32_t j = 0; j < i; j++)
                    {
                        if (outGraphicsAPI->Queues[j].Index == outGraphicsAPI->Queues[i].Index)
                        {
                            queueFound = true;
                        }
                    }

                    if (queueFound)
                    {
                        continue;
                    }
                }

                static const float queuePriority = 1.0f;

                VkDeviceQueueCreateInfo createQueueInfo = {};
                createQueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
                createQueueInfo.queueCount = 1;
                createQueueInfo.queueFamilyIndex = outGraphicsAPI->Queues[i].Index;
                createQueueInfo.pQueuePriorities = &queuePriority;

                queueCreateInfo[queueCreateInfoCount++] = createQueueInfo;
            }

            VkPhysicalDeviceFeatures physicalDeviceFeatures = {};
            physicalDeviceFeatures.shaderStorageImageWriteWithoutFormat = VK_TRUE;
            physicalDeviceFeatures.multiDrawIndirect = VK_TRUE;
            physicalDeviceFeatures.drawIndirectFirstInstance = VK_TRUE;

            VkPhysicalDeviceShaderDrawParametersFeatures shaderDrawParamFeatures = {};
            shaderDrawParamFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
            shaderDrawParamFeatures.shaderDrawParameters = VK_TRUE;

            VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderingFeature = {};
            dynamicRenderingFeature.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
            dynamicRenderingFeature.pNext = &shaderDrawParamFeatures;
            dynamicRenderingFeature.dynamicRendering = VK_TRUE;

            VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures = {};
            indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT;
            indexingFeatures.pNext = &dynamicRenderingFeature;
            indexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;
            indexingFeatures.runtimeDescriptorArray = VK_TRUE;

            VkDeviceCreateInfo deviceCreateInfo = {};
            deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            deviceCreateInfo.pNext = &indexingFeatures;
            deviceCreateInfo.enabledExtensionCount = arrayCount(DeviceExtensions);
            deviceCreateInfo.queueCreateInfoCount = queueCreateInfoCount;
            deviceCreateInfo.pQueueCreateInfos = queueCreateInfo;
            deviceCreateInfo.pEnabledFeatures = &physicalDeviceFeatures;
            deviceCreateInfo.ppEnabledExtensionNames = DeviceExtensions;

#ifdef IB_VKDEBUG
            deviceCreateInfo.enabledLayerCount = arrayCount(ValidationLayers);
            deviceCreateInfo.ppEnabledLayerNames = ValidationLayers;
#endif // IB_VKDEBUG

            IB_VKCHECK(vkCreateDevice(outGraphicsAPI->PhysicalDevice, &deviceCreateInfo, NoVkAllocator, &outGraphicsAPI->LogicalDevice));
            for (uint32_t i = 0; i < GraphicsQueue::Count; i++)
            {
                vkGetDeviceQueue(outGraphicsAPI->LogicalDevice, outGraphicsAPI->Queues[i].Index, 0, &outGraphicsAPI->Queues[i].Queue);
            }
        }

        // Create the descriptor pools
        {
            VkDescriptorPoolSize descriptorPoolSizes[5];
            descriptorPoolSizes[0] = VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MaxUniformBufferCount};
            descriptorPoolSizes[1] = VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MaxStorageBufferCount};
            descriptorPoolSizes[2] = VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MaxStorageImageCount};
            descriptorPoolSizes[3] = VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, MaxImageSamplerCount};
            descriptorPoolSizes[4] = VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MaxSampledImageCount};

            VkDescriptorPoolCreateInfo descriptorPoolCreate = {};
            descriptorPoolCreate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            descriptorPoolCreate.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            descriptorPoolCreate.maxSets = MaxDescriptorSetCount;
            descriptorPoolCreate.poolSizeCount = arrayCount(descriptorPoolSizes);
            descriptorPoolCreate.pPoolSizes = descriptorPoolSizes;

            IB_VKCHECK(vkCreateDescriptorPool(outGraphicsAPI->LogicalDevice, &descriptorPoolCreate, NoVkAllocator, &outGraphicsAPI->DescriptorPool));
        }

        // Create the pipeline cache
        {
            VkPipelineCacheCreateInfo pipelineCacheCreate = {};
            pipelineCacheCreate.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

            IB_VKCHECK(vkCreatePipelineCache(outGraphicsAPI->LogicalDevice, &pipelineCacheCreate, NoVkAllocator, &outGraphicsAPI->PipelineCache));
        }

        // Create the command pools
        for (uint32_t i = 0; i < GraphicsQueue::Count; i++)
        {
            VkCommandPoolCreateInfo commandPoolCreateInfo = {};
            commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            commandPoolCreateInfo.queueFamilyIndex = outGraphicsAPI->Queues[i].Index;

            IB_VKCHECK(vkCreateCommandPool(outGraphicsAPI->LogicalDevice, &commandPoolCreateInfo, NoVkAllocator, &outGraphicsAPI->Queues[i].CommandPool));
        }
    }

    void killGraphicsAPI(GraphicsAPI* graphicsAPI)
    {
        for (uint32_t i = 0; i < GraphicsQueue::Count; i++)
        {
            vkDestroyCommandPool(graphicsAPI->LogicalDevice, graphicsAPI->Queues[i].CommandPool, NoVkAllocator);
        }

        vkDestroyPipelineCache(graphicsAPI->LogicalDevice, graphicsAPI->PipelineCache, NoVkAllocator);
        vkDestroyDescriptorPool(graphicsAPI->LogicalDevice, graphicsAPI->DescriptorPool, NoVkAllocator);
       
        vkDestroyDevice(graphicsAPI->LogicalDevice, NoVkAllocator);
        vkDestroyInstance(graphicsAPI->Instance, NoVkAllocator);
    }
}

// Texture
namespace IB
{
    VkImageMemoryBarrier createMemoryBarrier(
        Texture* texture,
        VkAccessFlagBits sourceAccessMask,
        VkAccessFlagBits dstAccessMask,
        VkImageLayout oldLayout,
        VkImageLayout newLayout)
    {
        VkImageSubresourceRange imageSubresource = {};
        imageSubresource.aspectMask = texture->Aspect;
        imageSubresource.levelCount = 1;
        imageSubresource.baseMipLevel = 0;
        imageSubresource.baseArrayLayer = 0;
        imageSubresource.layerCount = 1;

        VkImageMemoryBarrier imageBarrier = {};
        imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageBarrier.oldLayout = oldLayout;
        imageBarrier.newLayout = newLayout;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.image = texture->Image;
        imageBarrier.subresourceRange = imageSubresource;
        imageBarrier.srcAccessMask = sourceAccessMask;
        imageBarrier.dstAccessMask = dstAccessMask;

        return imageBarrier;
    }
}

// CommandBuffer
namespace IB
{
    void initCommandBuffer(GraphicsAPI* api, CommandBufferDesc desc, CommandBuffer* outCommandBuffer)
    {
        outCommandBuffer->QueueType = desc.QueueType;

        // CommandBuffer
        VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
        commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandBufferAllocateInfo.commandBufferCount = 1;
        commandBufferAllocateInfo.commandPool = api->Queues[desc.QueueType].CommandPool;

        IB_VKCHECK(vkAllocateCommandBuffers(api->LogicalDevice, &commandBufferAllocateInfo, &outCommandBuffer->Commands));

        // Semaphore
        VkSemaphoreCreateInfo semaphoreCreateInfo = {};
        semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        IB_VKCHECK(vkCreateSemaphore(api->LogicalDevice, &semaphoreCreateInfo, NoVkAllocator, &outCommandBuffer->FinishedSemaphore));
    }

    void killCommandBuffer(GraphicsAPI* api, CommandBuffer* commandBuffer)
    {
        vkFreeCommandBuffers(api->LogicalDevice, api->Queues[commandBuffer->QueueType].CommandPool, 1, &commandBuffer->Commands);
        vkDestroySemaphore(api->LogicalDevice, commandBuffer->FinishedSemaphore, NoVkAllocator);
    }

    void submitCommandBuffer(GraphicsAPI* api, SubmitCommandBufferDesc desc)
    {
        VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &desc.CommandBuffer->Commands;
        
        if(desc.WaitSemaphore != VK_NULL_HANDLE)
        {
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &desc.WaitSemaphore;
        }

        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &desc.CommandBuffer->FinishedSemaphore;
        submitInfo.pWaitDstStageMask = &dstStageMask;

        IB_VKCHECK(vkQueueSubmit(api->Queues[desc.CommandBuffer->QueueType].Queue, 1, &submitInfo, desc.SubmissionFence));
    }
}

// Surface
namespace IB
{
    VkSurfaceKHR createWin32VkSurface(VkInstance vkInstance, void* windowHandle, void* instanceHandle)
    {
        VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
        surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surfaceCreateInfo.hinstance = reinterpret_cast<HINSTANCE>(instanceHandle);
        surfaceCreateInfo.hwnd = reinterpret_cast<HWND>(windowHandle);

        VkSurfaceKHR surface;
        IB_VKCHECK(vkCreateWin32SurfaceKHR(vkInstance, &surfaceCreateInfo, IB::NoVkAllocator, &surface));

        return surface;
    }

    void initWin32GraphicsSurface(GraphicsAPI* api, SurfaceDesc desc, GraphicsSurface* outSurface)
    {
        outSurface->VulkanSurface = createWin32VkSurface(api->Instance, desc.Win32WindowHandle, desc.Win32InstanceHandle);

        // Extents
        {
            VkSurfaceCapabilitiesKHR surfaceCapabilities;
            IB_VKCHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(api->PhysicalDevice, outSurface->VulkanSurface, &surfaceCapabilities));

            IB_ASSERT(surfaceCapabilities.currentExtent.width != UINT32_MAX, "Surface has invalid width.");
            outSurface->Extents = surfaceCapabilities.currentExtent;
        }

        // Present mode
        outSurface->PresentMode = VK_PRESENT_MODE_FIFO_KHR;
        if (!desc.UseVSync)
        {
            uint32_t constexpr maxPresentModes = 100;
            uint32_t presentModeCount;
            VkPresentModeKHR presentModes[maxPresentModes];

            IB_VKCHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(api->PhysicalDevice, outSurface->VulkanSurface, &presentModeCount, NULL));
            IB_ASSERT(presentModeCount > 0, "Failed to find a present mode.");
            presentModeCount = presentModeCount < maxPresentModes ? presentModeCount : maxPresentModes;
            IB_VKCHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(api->PhysicalDevice, outSurface->VulkanSurface, &presentModeCount, presentModes));

            for (uint32_t i = 0; i < presentModeCount; i++)
            {
                if (VK_PRESENT_MODE_MAILBOX_KHR == presentModes[i])
                {
                    outSurface->PresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
                    break;
                }
            }
        }

        // Surface format
        {
            uint32_t constexpr maxSurfaceFormatCount = 100;
            uint32_t surfaceFormatCount;
            VkSurfaceFormatKHR surfaceFormats[maxSurfaceFormatCount];

            IB_VKCHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(api->PhysicalDevice, outSurface->VulkanSurface, &surfaceFormatCount, NULL));
            IB_ASSERT(surfaceFormatCount > 0, "Failed to find any surface formats.");
            surfaceFormatCount = surfaceFormatCount < maxSurfaceFormatCount ? surfaceFormatCount : maxSurfaceFormatCount;
            IB_VKCHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(api->PhysicalDevice, outSurface->VulkanSurface, &surfaceFormatCount, surfaceFormats));

            if (1 == surfaceFormatCount && VK_FORMAT_UNDEFINED == surfaceFormats[0].format)
            {
                outSurface->Format.format = VK_FORMAT_R8G8B8A8_UNORM;
                outSurface->Format.colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
            }
            else
            {
                outSurface->Format = surfaceFormats[0];
                for (uint32_t i = 0; i < surfaceFormatCount; i++)
                {
                    if (VK_FORMAT_R8G8B8A8_UNORM == surfaceFormats[i].format && VK_COLORSPACE_SRGB_NONLINEAR_KHR == surfaceFormats[i].colorSpace)
                    {
                        outSurface->Format = surfaceFormats[i];
                        break;
                    }
                }
            }
        }

        // Swapchain
        {
            VkSwapchainCreateInfoKHR swapchainCreate = {};
            swapchainCreate.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
            swapchainCreate.pNext = NULL;
            swapchainCreate.minImageCount = FramebufferCount;
            swapchainCreate.imageArrayLayers = 1;
            swapchainCreate.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
            swapchainCreate.surface = outSurface->VulkanSurface;
            swapchainCreate.imageFormat = outSurface->Format.format;
            swapchainCreate.imageColorSpace = outSurface->Format.colorSpace;
            swapchainCreate.imageExtent = outSurface->Extents;
            swapchainCreate.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
            swapchainCreate.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            swapchainCreate.presentMode = outSurface->PresentMode;
            swapchainCreate.clipped = VK_TRUE;
            swapchainCreate.pQueueFamilyIndices = NULL;
            swapchainCreate.queueFamilyIndexCount = 0;

            uint32_t swapchainShareIndices[2];
            if (api->Queues[GraphicsQueue::Graphics].Index != api->Queues[GraphicsQueue::Present].Index)
            {
                swapchainShareIndices[0] = api->Queues[GraphicsQueue::Graphics].Index;
                swapchainShareIndices[1] = api->Queues[GraphicsQueue::Present].Index;

                swapchainCreate.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
                swapchainCreate.queueFamilyIndexCount = 2;
                swapchainCreate.pQueueFamilyIndices = swapchainShareIndices;
            }
            else
            {
                swapchainCreate.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            }

            IB_VKCHECK(vkCreateSwapchainKHR(api->LogicalDevice, &swapchainCreate, NoVkAllocator, &outSurface->Swapchain));

            VkImage swapchainImages[FramebufferCount];

            uint32_t swapchainImageCount = FramebufferCount;
            IB_VKCHECK(vkGetSwapchainImagesKHR(api->LogicalDevice, outSurface->Swapchain, &swapchainImageCount, swapchainImages));
            IB_ASSERT(swapchainImageCount >= FramebufferCount, "Too few swapchain images!");

            for (uint32_t fb = 0; fb < FramebufferCount; fb++)
            {
                VkImageViewCreateInfo imageViewCreate = {};
                imageViewCreate.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                imageViewCreate.viewType = VK_IMAGE_VIEW_TYPE_2D;
                imageViewCreate.components.r = VK_COMPONENT_SWIZZLE_R;
                imageViewCreate.components.g = VK_COMPONENT_SWIZZLE_G;
                imageViewCreate.components.b = VK_COMPONENT_SWIZZLE_B;
                imageViewCreate.components.a = VK_COMPONENT_SWIZZLE_A;
                imageViewCreate.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                imageViewCreate.subresourceRange.baseMipLevel = 0;
                imageViewCreate.subresourceRange.levelCount = 1;
                imageViewCreate.subresourceRange.baseArrayLayer = 0;
                imageViewCreate.subresourceRange.layerCount = 1;
                imageViewCreate.image = swapchainImages[fb];
                imageViewCreate.format = outSurface->Format.format;

                outSurface->Framebuffers[fb].Texture.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
                outSurface->Framebuffers[fb].Texture.Image = swapchainImages[fb];
                IB_VKCHECK(vkCreateImageView(api->LogicalDevice, &imageViewCreate, NoVkAllocator, &outSurface->Framebuffers[fb].Texture.View));
            }
        }

        // Acquire semaphore
        {
            VkSemaphoreCreateInfo semaphoreCreateInfo = {};
            semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            for (uint32_t i = 0; i < FramebufferCount; i++)
            {
                IB_VKCHECK(vkCreateSemaphore(api->LogicalDevice, &semaphoreCreateInfo, NoVkAllocator, &outSurface->Framebuffers[i].AcquireSemaphore));
            }
        }
    }

    void killGraphicsSurface(GraphicsAPI* api, GraphicsSurface* surface)
    {
        for (uint32_t fb = 0; fb < FramebufferCount; fb++)
        {
            vkDestroySemaphore(api->LogicalDevice, surface->Framebuffers[fb].AcquireSemaphore, NoVkAllocator);
            vkDestroyImageView(api->LogicalDevice, surface->Framebuffers[fb].Texture.View, NoVkAllocator);
        }
        vkDestroySwapchainKHR(api->LogicalDevice, surface->Swapchain, NoVkAllocator);
        vkDestroySurfaceKHR(api->Instance, surface->VulkanSurface, NoVkAllocator);
    }

    bool prepareSurface(GraphicsAPI* api, SurfacePrepareDesc prepareDesc, SurfacePrepareResult* outPrepareResult)
    {
        VkSemaphore acquireSemaphore = prepareDesc.Surface->Framebuffers[prepareDesc.Framebuffer].AcquireSemaphore;
        VkResult acquireResult = vkAcquireNextImageKHR(api->LogicalDevice, prepareDesc.Surface->Swapchain, UINT64_MAX, acquireSemaphore, VK_NULL_HANDLE, &outPrepareResult->ImageIndex);
        
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR)
        {
            outPrepareResult->SurfaceState = SurfaceState::ShouldRebuild;
        }
        else
        {
            outPrepareResult->SurfaceState = SurfaceState::Ok;
        }
        
        return (acquireResult == VK_SUCCESS);
    }

    void presentSurface(GraphicsAPI* api, SurfacePresentDesc presentDesc, SurfacePresentResult* outPresentResult)
    {
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &presentDesc.PresentCommands.FinishedSemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &presentDesc.Surface->Swapchain;
        presentInfo.pImageIndices = &presentDesc.PrepareResult.ImageIndex;

        VkResult presentResult = vkQueuePresentKHR(api->Queues[GraphicsQueue::Present].Queue, &presentInfo);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
        {
            outPresentResult->SurfaceState = SurfaceState::ShouldRebuild;
        }
        else
        {
            outPresentResult->SurfaceState = SurfaceState::Ok;
        }
    }

    void rebuildSurface(GraphicsAPI* /*api*/, GraphicsSurface* /*surface*/)
    {
        //vkDeviceWaitIdle(API.LogicalDevice);
        //VkSurfaceCapabilitiesKHR surfaceCapabilities;
        //IB_VKCHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(API.PhysicalDevice, RendererContext.Present.Surface, &surfaceCapabilities));
        //IB_ASSERT(surfaceCapabilities.currentExtent.width != UINT32_MAX, "Surface extents are undefined.");
        //buildSurfaceSwapchain(surfaceCapabilities.currentExtent);
    }
}

// Staging
namespace IB
{
    void initStaging(GraphicsAPI* api, StagingDesc desc, StagingCore* outStaging)
    {
        // Staging Buffer
        {
            VkBufferCreateInfo bufferCreate = {};
            bufferCreate.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreate.size = StagingSize;
            bufferCreate.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

            IB_VKCHECK(vkCreateBuffer(api->LogicalDevice, &bufferCreate, NoVkAllocator, &outStaging->Buffer));

            VkMemoryRequirements memoryRequirements;
            vkGetBufferMemoryRequirements(api->LogicalDevice, outStaging->Buffer, &memoryRequirements);

            VkMemory::AllocationRequest request;
            request.Alignment = memoryRequirements.alignment;
            request.Size = StagingSize;
            request.LogicalDevice = api->LogicalDevice;
            request.PhysicalDevice = api->PhysicalDevice;
            request.RequiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            request.PreferedFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            request.TypeBits = memoryRequirements.memoryTypeBits;
            request.Category = VkMemory::Category::TransferSource;

            outStaging->Allocation = allocateDeviceMemory(desc.Allocator, request);
            IB_VKCHECK(vkBindBufferMemory(api->LogicalDevice, outStaging->Buffer, outStaging->Allocation.Memory, outStaging->Allocation.Offset));

            outStaging->MappedMemory = mapAllocation(api->LogicalDevice, desc.Allocator, outStaging->Allocation);
        }

        // Fences
        {
            VkFenceCreateInfo fenceCreateInfo = {};
            fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;        
            for (uint32_t i = 0; i < MaxStagingFenceCount; i++)
            {
                IB_VKCHECK(vkCreateFence(api->LogicalDevice, &fenceCreateInfo, NoVkAllocator, &outStaging->Fences[i]));
            }
        }
    }

    void killStaging(GraphicsAPI* api, VkMemory::Allocator* allocator, StagingCore* staging)
    {
        vkDestroyBuffer(api->LogicalDevice, staging->Buffer, NoVkAllocator);
        freeDeviceMemory(allocator, staging->Allocation);

        for (uint32_t i = 0; i < MaxStagingFenceCount; i++)
        {
            vkDestroyFence(api->LogicalDevice, staging->Fences[i], NoVkAllocator);
        }
    }

    StagingBuffer requestStagingBuffer(StagingCore* staging, uint32_t size, uint32_t alignment)
    {
        uint32_t alignmentBump = staging->WriteOffset % alignment;
        alignmentBump = alignmentBump != 0 ? alignment - alignmentBump : alignmentBump;

        IB_ASSERT(staging->WriteOffset + size + alignmentBump < StagingSize, "TODO: Handle flushing our staging buffer if we reach the maximum.");

        staging->WriteOffset += alignmentBump;

        StagingBuffer stagingBuffer = {};
        stagingBuffer.Buffer = staging->Buffer;
        stagingBuffer.Memory = reinterpret_cast<uint8_t*>(staging->MappedMemory) + staging->WriteOffset;
        stagingBuffer.Offset = staging->WriteOffset;
        IB_ASSERT(staging->ActiveFenceCount < MaxStagingFenceCount, "TODO: Handle flushing if we run out of fences.");
        stagingBuffer.Fence = staging->Fences[staging->ActiveFenceCount++];
        staging->WriteOffset += size;

        return stagingBuffer;
    }
}

#endif // IB_GRAPHICS_API_IMPL

