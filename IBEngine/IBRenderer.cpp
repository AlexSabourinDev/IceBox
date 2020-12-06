#include "IBRenderer.h"
#include "IBLogging.h"
#include "IBPlatform.h"

#include <stdlib.h>

#define VK_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

/*
Phew, I assume if you're reading this comment you'd like an overview of how the IceBox renderer works.
I'll do my best to describe it here, but it's a lot to talk about and it might be easier to simply email
me if you would like a more thorough breakdown of my thought process (alexsabourindev@gmail.com)

The primary approach to the IceBox renderer is the giant "Context" object.
Vulkan is a large beast and creating new classes for every single case seems like too much work for me.

As a result, we make heavy use of anonymous structures to categorize the data in the context object.

Ignoring the overall code structure, the high level concept of the Ice Box renderer
is to create as little as possible code to do what we're looking to do.
This means that the renderer is actually a very straightforward representation of how we want to render data.
The renderer has very little in terms of generic functionality, it's very specific to our use case.

I would like to keep it that way.

I prefer to make something generic once we have concrete use cases for making it generic.
To facilitate this "deferred" decision making, keeping as much of the renderer in as few files
as possible will facilitate moving things around without changing the public API.

If we run into a scenario where multiple people need to work on the renderer at once and splitting some code into
multiple files - I would suggest being very considerate about how those files are split and potentially
even marking them as "private" by putting them in a private folder.

Making a public API introduces rigidity in the functionality of the renderer, and if something can avoid being exposed
I would highly prefer it that way.

Now, in terms of the renderer. I will unfortunately, not be describing the vulkan specific details.
Vulkan is complex and vast, I would recommend reading up on it before diving in (or using this to dive in! I've tried to keep the implementations straightforward)

Our renderer is setup in such a way that a "material" which is a rendering technique used to render a mesh
has all of the data it needs created on renderer initialization.

I've tried to keep things simple for implementation simplicity and to satisfy my innate laziness.

As you can see in the context, at the time of writting, a single "Forward" graphics pipeline is created
And all the constant descriptors are created ahead of time.

This means that the renderer does not support permutations for our materials. Since our renderer is not intended
to be a generic renderer, I think this is an acceptable decision. It simplifies the code and mental model used for the renderer.

In general for this renderer, graphics pipelines are intended as the "templates" for a material. They will represent
all the functionality that is shared for a simple material.

As can be seen for the Forward material, this means the descriptors for meshes and textures are constant (since we're using a bindless scheme for these 2 aspects)

Once you want to create a forward material instance, a new descriptor is created for that material instance and it's data is assigned.

At the time of writting, data sent to the renderer cannot be modified. We will likely add a scheme for modifying
specific data on an as-needed basis as the engine develops.

You'll notice we're doing bindless meshes and textures, this is mostly to minimize the number of descriptors that have to be created.
It makes things a lot simpler.

The allocator is a simple buddy allocator, some scheme for visualizing GPU memory usage would likely be nice.

You'll also notice you can't destroy resources yet. This is a WIP until we need it.
The current issue, is that meshes could be destroyed but they're allocation scheme might introduce some significant
fragmentation in the mesh buffer. However, since the offsets can be changed, I wonder if we can run a packing job
to make sure that empty spaces are cleared. The complexity of this will depend on use case. Maybe we can just
ignore the problem entirely for now.

I would also suggest strongly resisting the urge to break the large functions into smaller functions unless an excellent case is made for it.

I find that breaking up single use code into functions like the code in the initialization function
only creates an illusion of simplicity. Trying to truly understand the function becomes an exercise in
jumping around the file to read every function. (This is all personal preference of course)

I would suggest instead wrapping specific segments into their own scopes to avoid leaking variables
as well as making the creation of a function quite trivial by making the scope into it's own function.

That's pretty much it!
*/

#define IB_VKCHECK(call)                      \
    if (VK_SUCCESS != call)                   \
    {                                         \
        IB_ASSERT(false, "Failed VK Check."); \
    }

#define IB_VKDEBUG

namespace
{
    // Utilities
    template <typename T, uint32_t Count>
    constexpr uint32_t arrayCount(T (&arr)[Count])
    {
        (void)arr;
        return Count;
    }
    constexpr VkAllocationCallbacks *NoAllocator = nullptr;

    // Allocator

    constexpr uint32_t MaxAllocatorPools = 10;
    constexpr uint32_t MaxMemoryBlocks = 1000;
    constexpr uint32_t MaxAllocatorPoolSize = 1024 * 1024 * 64;

    struct MemoryBlock
    {
        VkDeviceSize Size = 0;
        VkDeviceSize Offset = 0;
        uint32_t Id = UINT32_MAX;
        uint32_t NextIndex = UINT32_MAX;
        bool Allocated = false;
    };

    struct MemoryPool
    {
        VkDeviceMemory Memory = {};
        VkDeviceSize Size = {};
        uint32_t HeadIndex = UINT32_MAX;
        uint32_t NextId = UINT32_MAX;
        uint32_t MemoryType = UINT32_MAX;
        void *Map = nullptr;
    };

    struct Allocation
    {
        VkDeviceMemory Memory = {};
        VkDeviceSize Offset = {};
        uint32_t Id = 0;
        uint32_t PoolIndex = 0;
    };

    struct Allocator
    {
        MemoryPool MemoryPools[MaxAllocatorPools] = {};
        MemoryBlock BlockPool[MaxMemoryBlocks] = {};
        uint32_t FreeBlocks[MaxMemoryBlocks] = {};
        uint32_t FreeBlockCount = 0;
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

    void createAllocator(Allocator *allocator)
    {
        allocator->FreeBlockCount = MaxMemoryBlocks;
        for (uint32_t freeBlockIndex = 0; freeBlockIndex < allocator->FreeBlockCount; freeBlockIndex++)
        {
            allocator->FreeBlocks[freeBlockIndex] = freeBlockIndex;
        }
    }

    void destroyAllocator(VkDevice device, Allocator *allocator)
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

    Allocation allocateDeviceMemory(VkDevice device, Allocator *allocator, uint32_t memoryTypeIndex, VkDeviceSize size, VkDeviceSize alignment)
    {
        IB_ASSERT(memoryTypeIndex != UINT32_MAX, "Invalid memory type index.");

        uint32_t foundPoolIndex = UINT32_MAX;
        for (uint32_t i = 0; i < MaxAllocatorPools; i++)
        {
            if (allocator->MemoryPools[i].MemoryType == memoryTypeIndex)
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
                    memoryPool->Size = MaxAllocatorPoolSize;

                    VkMemoryAllocateInfo memoryAllocInfo = {};
                    memoryAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
                    memoryAllocInfo.allocationSize = memoryPool->Size;
                    memoryAllocInfo.memoryTypeIndex = memoryTypeIndex;

                    IB_VKCHECK(vkAllocateMemory(device, &memoryAllocInfo, NULL, &memoryPool->Memory));

                    IB_ASSERT(allocator->FreeBlockCount > 0, "No free blocks left!");
                    uint32_t newBlockIndex = allocator->FreeBlocks[allocator->FreeBlockCount - 1];
                    allocator->FreeBlockCount--;

                    MemoryBlock *block = &allocator->BlockPool[newBlockIndex];
                    block->Size = memoryPool->Size;
                    block->Offset = 0;
                    block->Allocated = false;

                    memoryPool->HeadIndex = newBlockIndex;
                    memoryPool->NextId = 1;
                    memoryPool->MemoryType = memoryTypeIndex;

                    foundPoolIndex = i;
                    break;
                }
            }
        }

        IB_ASSERT(foundPoolIndex != UINT32_MAX, "Failed to find a memory pool.");

        VkDeviceSize allocationSize = size + (alignment - size % alignment);
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
        MemoryBlock *smallestBlock = NULL;
        for (uint32_t iter = memoryPool->HeadIndex; iter != UINT32_MAX; iter = allocator->BlockPool[iter].NextIndex)
        {
            MemoryBlock *block = &allocator->BlockPool[iter];

            if (smallestBlock == NULL || (block->Size > allocationSize && block->Size < smallestBlock->Size && !block->Allocated))
            {
                smallestBlock = block;
            }
        }

        MemoryBlock *iter = smallestBlock;
        if (iter == NULL)
        {
            IB_ASSERT(false, "Failed to allocate a block.");
            return Allocation{};
        }

        while (iter->Size > allocationSize && iter->Size / 2 > allocationSize)
        {
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
            right->NextIndex = iter->NextIndex;
            iter->NextIndex = leftIndex;

            iter = left;
        }

        iter->Allocated = true;

        Allocation allocation = {};
        allocation.Memory = memoryPool->Memory;
        allocation.Offset = iter->Offset;
        allocation.Id = iter->Id;
        allocation.PoolIndex = foundPoolIndex;
        return allocation;
    }

    void freeDeviceMemory(Allocator *allocator, Allocation allocation)
    {
        MemoryPool *memoryPool = &allocator->MemoryPools[allocation.PoolIndex];

        uint32_t prevIters[2] = {UINT32_MAX, UINT32_MAX};

        for (uint32_t iter = memoryPool->HeadIndex; iter != UINT32_MAX; iter = allocator->BlockPool[iter].NextIndex)
        {
            MemoryBlock *currentBlock = &allocator->BlockPool[iter];
            if (currentBlock->Id == allocation.Id)
            {
                currentBlock->Allocated = false;

                // We can't have a sibling to merge if there was never a previous iterator. This is because
                // the first previous iterator would be the root block that has no siblings
                if (prevIters[0] != UINT32_MAX)
                {
                    MemoryBlock *previousBlock = &allocator->BlockPool[prevIters[0]];
                    // Previous iterator is my size, it's my sibling. If it's not allocated, merge it
                    if (previousBlock->Size == currentBlock->Size && !previousBlock->Allocated)
                    {
                        MemoryBlock *parentBlock = &allocator->BlockPool[prevIters[1]];
                        parentBlock->Allocated = false;
                        parentBlock->NextIndex = currentBlock->NextIndex;

                        allocator->FreeBlocks[allocator->FreeBlockCount] = iter;
                        allocator->FreeBlocks[allocator->FreeBlockCount + 1] = prevIters[0];
                        allocator->FreeBlockCount += 2;
                    }
                    // Since we just checked to see if the previous iterator was our sibling and it wasnt
                    // we know that if we have a next iterator, it's our sibling
                    else if (currentBlock->NextIndex != UINT32_MAX)
                    {
                        MemoryBlock *nextBlock = &allocator->BlockPool[currentBlock->NextIndex];
                        if (!nextBlock->Allocated)
                        {
                            MemoryBlock *parentBlock = &allocator->BlockPool[prevIters[0]];

                            parentBlock->Allocated = false;
                            parentBlock->NextIndex = nextBlock->NextIndex;

                            allocator->FreeBlocks[allocator->FreeBlockCount] = currentBlock->NextIndex;
                            allocator->FreeBlocks[allocator->FreeBlockCount + 1] = iter;
                            allocator->FreeBlockCount += 2;
                        }
                    }
                }
                break;
            }

            prevIters[1] = prevIters[0];
            prevIters[0] = iter;
        }
    }

    // Main Renderer

    struct Queue
    {
        enum
        {
            Present = 0,
            Graphics,
            Compute,
            Transfer,
            Count
        };
    };

    struct PipelineType
    {
        enum
        {
            Default = 0,
            Count
        };
    };

    constexpr uint32_t MaxMeshCount = 1000;
    constexpr uint32_t MaxImageCount = 100;
    constexpr uint32_t MaxSamplerCount = 1000;
    constexpr uint32_t FrameBufferCount = 2;
    constexpr uint32_t MaxMaterialInstanceCount = 100;
    constexpr uint32_t MaxPhysicalImageCount = 10;
    constexpr uint32_t SubpassCount = 2;
    struct Context
    {
        VkInstance VulkanInstance;
        Allocator Allocator;

        struct
        {
            VkPhysicalDevice PhysicalDevice;
            VkDevice LogicalDevice;
            VkSurfaceKHR Surface;
            VkSurfaceFormatKHR SurfaceFormat;
            VkExtent2D SurfaceExtents;
            VkPresentModeKHR PresentMode;
            VkDescriptorPool DescriptorPool;
            VkPipelineCache PipelineCache;
            VkSwapchainKHR Swapchain;
            VkImage SwapchainImages[MaxPhysicalImageCount];
            uint32_t SwapchainImageCount;
            VkRenderPass RenderPass;

            struct
            {
                uint32_t Index;
                VkQueue Queue;
                VkCommandPool CommandPool;
            } Queues[Queue::Count];

            VkFence ImmediateFence;

            struct
            {
                VkSemaphore AcquireSemaphore;
                VkSemaphore FinishedSemaphore;
                VkImageView SwapchainImageView;
                VkFramebuffer Framebuffer;
                VkCommandBuffer PrimaryCommandBuffer;
                VkFence FinishedFence;

                Allocation DepthImageAllocation;
                VkImage DepthImage;
                VkImageView DepthImageView;

                Allocation DebugDepthImageAllocation;
                VkImage DebugDepthImage;
                VkImageView DebugDepthImageView;
            } FrameBuffer[FrameBufferCount];
            uint32_t ActiveFrame = 0;
        } Present;

        struct
        {
            struct
            {
                uint32_t VertexOffset = 0;
                uint32_t VertexSize = 0;
                uint32_t IndexOffset = 0;
                uint32_t IndexCount = 0;
            } Meshes[MaxMeshCount];
            uint32_t MeshCount = 0;
            uint32_t NextOffset = 0;

            VkBuffer MeshDataBuffers;
            uint32_t AllocationSize = 0;
            Allocation Allocation;
        } Geometry;

        struct
        {
            struct
            {
                VkImage Image;
                VkImageView ImageView;
                Allocation Allocation;
            } Images[MaxImageCount];
            uint32_t ImageCount = 0;
        } Textures;

        struct
        {
            struct
            {
                VkShaderModule VShader;
                VkShaderModule FShader;

                VkDescriptorSetLayout ShaderInstanceLayout;
                VkDescriptorSetLayout ShaderLayout;

                VkPipelineLayout PipelineLayout;
                VkPipeline Pipelines[PipelineType::Count * SubpassCount];

                VkDescriptorSet ShaderDescriptor;
                VkSampler Sampler;

                struct
                {
                    VkDescriptorSet ShaderDescriptor;
                    VkBuffer FShaderData;
                    Allocation Allocation;
                    uint32_t PipelineIndex;
                } Instances[MaxMaterialInstanceCount];
                uint32_t InstanceCount = 0;
            } Forward;
        } Materials;
    };
    Context RendererContext;

    // TODO: Reference to Windows, if we want multiplatform we'll have to change this.
    const char *InstanceExtensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    const char *DeviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

#ifdef IB_VKDEBUG
    const char *ValidationLayers[] = {"VK_LAYER_KHRONOS_validation"};
#else
    const char *ValidationLayers[] = {};
#endif

    struct ImageAllocation
    {
        VkDevice LogicalDevice;
        VkPhysicalDevice PhysicalDevice;
        uint32_t ImageUsage;
        VkFormat Format;
        VkImageAspectFlagBits ImageAspect;
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t Stride = 0;
        Allocator *Allocator = nullptr;
    };

    struct ImageAndView
    {
        Allocation Allocation;
        VkImage Image;
        VkImageView ImageView;
    };

    ImageAndView allocImageAndView(ImageAllocation alloc)
    {
        ImageAndView imageAndView;

        {
            VkImageCreateInfo imageCreate = {};
            imageCreate.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageCreate.imageType = VK_IMAGE_TYPE_2D;
            imageCreate.format = alloc.Format;
            imageCreate.extent = VkExtent3D{alloc.Width, alloc.Height, 1};
            imageCreate.mipLevels = 1;
            imageCreate.arrayLayers = 1;
            imageCreate.samples = VK_SAMPLE_COUNT_1_BIT;
            imageCreate.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageCreate.usage = alloc.ImageUsage;
            imageCreate.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageCreate.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            IB_VKCHECK(vkCreateImage(alloc.LogicalDevice, &imageCreate, NoAllocator, &imageAndView.Image));
        }

        // Allocation
        {
            VkMemoryRequirements memoryRequirements;
            vkGetImageMemoryRequirements(alloc.LogicalDevice, imageAndView.Image, &memoryRequirements);

            unsigned int preferredBits = 0;
            uint32_t memoryIndex = findMemoryIndex(alloc.PhysicalDevice, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, preferredBits);

            Allocation *allocation = &imageAndView.Allocation;
            *allocation = allocateDeviceMemory(alloc.LogicalDevice, alloc.Allocator, memoryIndex,
                                               alloc.Width * alloc.Height * alloc.Stride, memoryRequirements.alignment);

            IB_VKCHECK(vkBindImageMemory(alloc.LogicalDevice, imageAndView.Image, allocation->Memory, allocation->Offset));
        }

        // Image view
        {
            VkImageSubresourceRange subresourceRange = {};
            subresourceRange.aspectMask = alloc.ImageAspect;
            subresourceRange.levelCount = 1;
            subresourceRange.layerCount = 1;
            subresourceRange.baseMipLevel = 0;

            VkImageViewCreateInfo imageCreateViewInfo = {};
            imageCreateViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            imageCreateViewInfo.image = imageAndView.Image;
            imageCreateViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            imageCreateViewInfo.format = alloc.Format;

            imageCreateViewInfo.components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_G, VK_COMPONENT_SWIZZLE_B, VK_COMPONENT_SWIZZLE_A};
            imageCreateViewInfo.subresourceRange = subresourceRange;

            IB_VKCHECK(vkCreateImageView(alloc.LogicalDevice, &imageCreateViewInfo, NoAllocator, &imageAndView.ImageView));
        }

        return imageAndView;
    }

    void buildSurfaceSwapchain(VkExtent2D surfaceExtents)
    {
        if (RendererContext.Present.Swapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(RendererContext.Present.LogicalDevice, RendererContext.Present.Swapchain, NoAllocator);
            for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
            {
                vkDestroyImageView(RendererContext.Present.LogicalDevice, RendererContext.Present.FrameBuffer[fb].SwapchainImageView, NoAllocator);
                vkDestroyImage(RendererContext.Present.LogicalDevice, RendererContext.Present.FrameBuffer[fb].DepthImage, NoAllocator);
                vkDestroyImageView(RendererContext.Present.LogicalDevice, RendererContext.Present.FrameBuffer[fb].DepthImageView, NoAllocator);
                freeDeviceMemory(&RendererContext.Allocator, RendererContext.Present.FrameBuffer[fb].DepthImageAllocation);
                vkDestroyImage(RendererContext.Present.LogicalDevice, RendererContext.Present.FrameBuffer[fb].DebugDepthImage, NoAllocator);
                vkDestroyImageView(RendererContext.Present.LogicalDevice, RendererContext.Present.FrameBuffer[fb].DebugDepthImageView, NoAllocator);
                freeDeviceMemory(&RendererContext.Allocator, RendererContext.Present.FrameBuffer[fb].DebugDepthImageAllocation);
                vkDestroyFramebuffer(RendererContext.Present.LogicalDevice, RendererContext.Present.FrameBuffer[fb].Framebuffer, NoAllocator);
            }
        }

        RendererContext.Present.SurfaceExtents = surfaceExtents;

        // Swapchain
        {
            VkSwapchainCreateInfoKHR swapchainCreate = {};
            swapchainCreate.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
            swapchainCreate.pNext = NULL;
            swapchainCreate.minImageCount = FrameBufferCount;
            swapchainCreate.imageArrayLayers = 1;
            swapchainCreate.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
            swapchainCreate.surface = RendererContext.Present.Surface;
            swapchainCreate.imageFormat = RendererContext.Present.SurfaceFormat.format;
            swapchainCreate.imageColorSpace = RendererContext.Present.SurfaceFormat.colorSpace;
            swapchainCreate.imageExtent = surfaceExtents;
            swapchainCreate.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
            swapchainCreate.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
            swapchainCreate.presentMode = RendererContext.Present.PresentMode;
            swapchainCreate.clipped = VK_TRUE;
            swapchainCreate.pQueueFamilyIndices = NULL;
            swapchainCreate.queueFamilyIndexCount = 0;

            uint32_t swapchainShareIndices[2];
            if (RendererContext.Present.Queues[Queue::Graphics].Index != RendererContext.Present.Queues[Queue::Present].Index)
            {
                swapchainShareIndices[0] = RendererContext.Present.Queues[Queue::Graphics].Index;
                swapchainShareIndices[1] = RendererContext.Present.Queues[Queue::Present].Index;

                swapchainCreate.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
                swapchainCreate.queueFamilyIndexCount = 2;
                swapchainCreate.pQueueFamilyIndices = swapchainShareIndices;
            }
            else
            {
                swapchainCreate.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            }

            IB_VKCHECK(vkCreateSwapchainKHR(RendererContext.Present.LogicalDevice, &swapchainCreate, NoAllocator, &RendererContext.Present.Swapchain));

            IB_VKCHECK(vkGetSwapchainImagesKHR(RendererContext.Present.LogicalDevice, RendererContext.Present.Swapchain, &RendererContext.Present.SwapchainImageCount, NULL));
            RendererContext.Present.SwapchainImageCount = RendererContext.Present.SwapchainImageCount < MaxPhysicalImageCount ? RendererContext.Present.SwapchainImageCount : MaxPhysicalImageCount;
            IB_VKCHECK(vkGetSwapchainImagesKHR(RendererContext.Present.LogicalDevice, RendererContext.Present.Swapchain, &RendererContext.Present.SwapchainImageCount, RendererContext.Present.SwapchainImages));

            for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
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
                imageViewCreate.image = RendererContext.Present.SwapchainImages[fb];
                imageViewCreate.format = RendererContext.Present.SurfaceFormat.format;

                IB_VKCHECK(vkCreateImageView(RendererContext.Present.LogicalDevice, &imageViewCreate, NoAllocator, &RendererContext.Present.FrameBuffer[fb].SwapchainImageView));
            }
        }

        for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
        {
            // Depth image
            {
                ImageAllocation imageAlloc = {};
                imageAlloc.LogicalDevice = RendererContext.Present.LogicalDevice;
                imageAlloc.PhysicalDevice = RendererContext.Present.PhysicalDevice;
                imageAlloc.ImageUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                imageAlloc.Format = VK_FORMAT_D32_SFLOAT;
                imageAlloc.ImageAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
                imageAlloc.Width = surfaceExtents.width;
                imageAlloc.Height = surfaceExtents.height;
                imageAlloc.Stride = 4;
                imageAlloc.Allocator = &RendererContext.Allocator;

                ImageAndView imageAndView = allocImageAndView(imageAlloc);
                RendererContext.Present.FrameBuffer[fb].DepthImage = imageAndView.Image;
                RendererContext.Present.FrameBuffer[fb].DepthImageView = imageAndView.ImageView;
                RendererContext.Present.FrameBuffer[fb].DepthImageAllocation = imageAndView.Allocation;

                imageAndView = allocImageAndView(imageAlloc);
                RendererContext.Present.FrameBuffer[fb].DebugDepthImage = imageAndView.Image;
                RendererContext.Present.FrameBuffer[fb].DebugDepthImageView = imageAndView.ImageView;
                RendererContext.Present.FrameBuffer[fb].DebugDepthImageAllocation = imageAndView.Allocation;
            }

            // Create the framebuffer
            {
                VkImageView images[] =
                    {
                        RendererContext.Present.FrameBuffer[fb].SwapchainImageView,
                        RendererContext.Present.FrameBuffer[fb].DepthImageView,
                        RendererContext.Present.FrameBuffer[fb].DebugDepthImageView,
                    };

                VkFramebufferCreateInfo framebufferCreate = {};
                framebufferCreate.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                framebufferCreate.attachmentCount = arrayCount(images);
                framebufferCreate.width = surfaceExtents.width;
                framebufferCreate.height = surfaceExtents.height;
                framebufferCreate.layers = 1;
                framebufferCreate.renderPass = RendererContext.Present.RenderPass;
                framebufferCreate.pAttachments = images;

                IB_VKCHECK(vkCreateFramebuffer(RendererContext.Present.LogicalDevice, &framebufferCreate, NoAllocator, &RendererContext.Present.FrameBuffer[fb].Framebuffer));
            }
        }
    }
} // namespace

namespace IB
{
    // Forward declared from IBPlatformWin32.cpp
    namespace Win32
    {
        void getWindowHandleAndInstance(WindowHandle handle, HWND *window, HINSTANCE *instance);
    }
} // namespace IB

namespace
{
    VkSurfaceKHR createSurface(IB::WindowHandle window)
    {
        HWND windowHandle;
        HINSTANCE instanceHandle;
        IB::Win32::getWindowHandleAndInstance(window, &windowHandle, &instanceHandle);

        VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
        surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        surfaceCreateInfo.hinstance = instanceHandle;
        surfaceCreateInfo.hwnd = windowHandle;

        VkSurfaceKHR surface;
        IB_VKCHECK(vkCreateWin32SurfaceKHR(RendererContext.VulkanInstance, &surfaceCreateInfo, NoAllocator, &surface));
        return surface;
    }
} // namespace

namespace IB
{
    IB_API void initRenderer(RendererDesc const &desc)
    {
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
            createInfo.enabledLayerCount = arrayCount(ValidationLayers);
            createInfo.ppEnabledLayerNames = ValidationLayers;

            IB_VKCHECK(vkCreateInstance(&createInfo, NoAllocator, &RendererContext.VulkanInstance));
        }

        RendererContext.Present.Surface = createSurface(*desc.Window);

        // Device
        {
            constexpr uint32_t maxPhysicalDeviceCount = 10;
            constexpr uint32_t maxPhysicalDeviceProperties = 100;

            uint32_t physicalDeviceCount;
            VkPhysicalDevice physicalDevices[maxPhysicalDeviceCount];

            uint32_t queuePropertyCounts[maxPhysicalDeviceCount];
            VkQueueFamilyProperties queueProperties[maxPhysicalDeviceCount][maxPhysicalDeviceProperties];

            // Enumerate the physical device properties
            {
                IB_VKCHECK(vkEnumeratePhysicalDevices(RendererContext.VulkanInstance, &physicalDeviceCount, NULL));
                physicalDeviceCount = physicalDeviceCount <= maxPhysicalDeviceCount ? physicalDeviceCount : maxPhysicalDeviceCount;

                IB_VKCHECK(vkEnumeratePhysicalDevices(RendererContext.VulkanInstance, &physicalDeviceCount, physicalDevices));
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

                    // Find our present queue
                    for (uint32_t propIndex = 0; propIndex < queuePropertyCounts[deviceIndex]; propIndex++)
                    {
                        if (queueProperties[deviceIndex][propIndex].queueCount == 0)
                        {
                            continue;
                        }

                        VkBool32 supportsPresent = VK_FALSE;
                        IB_VKCHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevices[deviceIndex], propIndex, RendererContext.Present.Surface, &supportsPresent));

                        if (supportsPresent)
                        {
                            presentQueue = propIndex;
                            break;
                        }
                    }

                    // Did we find a device supporting both graphics, present and compute.
                    if (graphicsQueue != UINT32_MAX && presentQueue != UINT32_MAX && computeQueue != UINT32_MAX)
                    {
                        RendererContext.Present.Queues[Queue::Graphics].Index = graphicsQueue;
                        RendererContext.Present.Queues[Queue::Compute].Index = computeQueue;
                        RendererContext.Present.Queues[Queue::Present].Index = presentQueue;
                        RendererContext.Present.Queues[Queue::Transfer].Index = transferQueue;
                        physicalDeviceIndex = deviceIndex;
                        break;
                    }
                }

                IB_ASSERT(physicalDeviceIndex != UINT32_MAX, "Failed to select a physical device!");
                RendererContext.Present.PhysicalDevice = physicalDevices[physicalDeviceIndex];
            }

            // Create the logical device
            {
                VkDeviceQueueCreateInfo queueCreateInfo[Queue::Count] = {};
                uint32_t queueCreateInfoCount = 0;

                for (uint32_t i = 0; i < Queue::Count; i++)
                {
                    // Have we already checked this index
                    {
                        bool queueFound = false;
                        for (uint32_t j = 0; j < i; j++)
                        {
                            if (RendererContext.Present.Queues[j].Index == RendererContext.Present.Queues[i].Index)
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
                    createQueueInfo.queueFamilyIndex = RendererContext.Present.Queues[i].Index;
                    createQueueInfo.pQueuePriorities = &queuePriority;

                    queueCreateInfo[queueCreateInfoCount++] = createQueueInfo;
                }

                VkPhysicalDeviceFeatures physicalDeviceFeatures = {};
                physicalDeviceFeatures.shaderStorageImageWriteWithoutFormat = VK_TRUE;

                VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures = {};
                indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT;
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
                deviceCreateInfo.enabledLayerCount = arrayCount(ValidationLayers);
                deviceCreateInfo.ppEnabledLayerNames = ValidationLayers;

                IB_VKCHECK(vkCreateDevice(RendererContext.Present.PhysicalDevice, &deviceCreateInfo, NoAllocator, &RendererContext.Present.LogicalDevice));
                for (uint32_t i = 0; i < Queue::Count; i++)
                {
                    vkGetDeviceQueue(RendererContext.Present.LogicalDevice, RendererContext.Present.Queues[i].Index, 0, &RendererContext.Present.Queues[i].Queue);
                }
            }
        }

        // Create the descriptor pools
        {
            constexpr uint32_t maxUniformBufferCount = 1000;
            constexpr uint32_t maxStorageBufferCount = 1000;
            constexpr uint32_t maxStorageImageCount = 100;
            constexpr uint32_t maxImageSamplerCount = 1000;
            constexpr uint32_t maxDescriptorSetCount = 1000;
            constexpr uint32_t maxSampledImageCount = 1000;

            VkDescriptorPoolSize descriptorPoolSizes[5];
            descriptorPoolSizes[0] = VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxUniformBufferCount};
            descriptorPoolSizes[1] = VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxStorageBufferCount};
            descriptorPoolSizes[2] = VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, maxStorageImageCount};
            descriptorPoolSizes[3] = VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, maxImageSamplerCount};
            descriptorPoolSizes[4] = VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, maxSampledImageCount};

            VkDescriptorPoolCreateInfo descriptorPoolCreate = {};
            descriptorPoolCreate.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            descriptorPoolCreate.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            descriptorPoolCreate.maxSets = maxDescriptorSetCount;
            descriptorPoolCreate.poolSizeCount = arrayCount(descriptorPoolSizes);
            descriptorPoolCreate.pPoolSizes = descriptorPoolSizes;

            IB_VKCHECK(vkCreateDescriptorPool(RendererContext.Present.LogicalDevice, &descriptorPoolCreate, NoAllocator, &RendererContext.Present.DescriptorPool));
        }

        {
            VkPipelineCacheCreateInfo pipelineCacheCreate = {};
            pipelineCacheCreate.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

            IB_VKCHECK(vkCreatePipelineCache(RendererContext.Present.LogicalDevice, &pipelineCacheCreate, NoAllocator, &RendererContext.Present.PipelineCache));
        }

        for (uint32_t i = 0; i < Queue::Count; i++)
        {
            VkCommandPoolCreateInfo commandPoolCreateInfo = {};
            commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            commandPoolCreateInfo.queueFamilyIndex = RendererContext.Present.Queues[i].Index;

            IB_VKCHECK(vkCreateCommandPool(RendererContext.Present.LogicalDevice, &commandPoolCreateInfo, NoAllocator, &RendererContext.Present.Queues[i].CommandPool));
        }

        {
            VkFenceCreateInfo fenceCreateInfo = {};
            fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            IB_VKCHECK(vkCreateFence(RendererContext.Present.LogicalDevice, &fenceCreateInfo, NoAllocator, &RendererContext.Present.ImmediateFence));
        }

        createAllocator(&RendererContext.Allocator);

        {
            VkSemaphoreCreateInfo semaphoreCreateInfo = {};
            semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            for (uint32_t i = 0; i < FrameBufferCount; i++)
            {
                IB_VKCHECK(vkCreateSemaphore(RendererContext.Present.LogicalDevice, &semaphoreCreateInfo, NoAllocator, &RendererContext.Present.FrameBuffer[i].AcquireSemaphore));
            }
        }

        {
            constexpr uint32_t maxSurfaceFormatCount = 100;
            uint32_t surfaceFormatCount;
            VkSurfaceFormatKHR surfaceFormats[maxSurfaceFormatCount];

            IB_VKCHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(RendererContext.Present.PhysicalDevice, RendererContext.Present.Surface, &surfaceFormatCount, NULL));
            IB_ASSERT(surfaceFormatCount > 0, "Failed to find any surface formats.");
            surfaceFormatCount = surfaceFormatCount < maxSurfaceFormatCount ? surfaceFormatCount : maxSurfaceFormatCount;
            IB_VKCHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(RendererContext.Present.PhysicalDevice, RendererContext.Present.Surface, &surfaceFormatCount, surfaceFormats));

            if (1 == surfaceFormatCount && VK_FORMAT_UNDEFINED == surfaceFormats[0].format)
            {
                RendererContext.Present.SurfaceFormat.format = VK_FORMAT_R8G8B8A8_UNORM;
                RendererContext.Present.SurfaceFormat.colorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
            }
            else
            {
                RendererContext.Present.SurfaceFormat = surfaceFormats[0];
                for (uint32_t i = 0; i < surfaceFormatCount; i++)
                {
                    if (VK_FORMAT_R8G8B8A8_UNORM == surfaceFormats[i].format && VK_COLORSPACE_SRGB_NONLINEAR_KHR == surfaceFormats[i].colorSpace)
                    {
                        RendererContext.Present.SurfaceFormat = surfaceFormats[i];
                        break;
                    }
                }
            }
        }

        {
            VkSurfaceCapabilitiesKHR surfaceCapabilities;
            IB_VKCHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(RendererContext.Present.PhysicalDevice, RendererContext.Present.Surface, &surfaceCapabilities));

            IB_ASSERT(surfaceCapabilities.currentExtent.width != UINT32_MAX, "Surface has invalid width.");
            RendererContext.Present.SurfaceExtents = surfaceCapabilities.currentExtent;
        }

        RendererContext.Present.PresentMode = VK_PRESENT_MODE_FIFO_KHR;

        constexpr bool useVSync = true;
        if(!useVSync)
        {
            constexpr uint32_t maxPresentModes = 100;
            uint32_t presentModeCount;
            VkPresentModeKHR presentModes[maxPresentModes];

            IB_VKCHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(RendererContext.Present.PhysicalDevice, RendererContext.Present.Surface, &presentModeCount, NULL));
            IB_ASSERT(presentModeCount > 0, "Failed to find a present mode.");
            presentModeCount = presentModeCount < maxPresentModes ? presentModeCount : maxPresentModes;
            IB_VKCHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(RendererContext.Present.PhysicalDevice, RendererContext.Present.Surface, &presentModeCount, presentModes));

            for (uint32_t i = 0; i < presentModeCount; i++)
            {
                if (VK_PRESENT_MODE_MAILBOX_KHR == presentModes[i])
                {
                    RendererContext.Present.PresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
                    break;
                }
            }
        }

        {
            {
                VkAttachmentDescription output = {};
                output.samples = VK_SAMPLE_COUNT_1_BIT;
                output.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                output.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                output.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                output.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                output.format = RendererContext.Present.SurfaceFormat.format;

                VkAttachmentDescription depth = {};
                depth.samples = VK_SAMPLE_COUNT_1_BIT;
                depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
                depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
                depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
                depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                depth.format = VK_FORMAT_D32_SFLOAT;

                VkAttachmentDescription attachmentDescriptions[] = {output, depth, depth};

                // Attachments
                VkAttachmentReference outputAttachment = {};
                outputAttachment.attachment = 0;
                outputAttachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                VkAttachmentReference depthAttachment = {};
                depthAttachment.attachment = 1;
                depthAttachment.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

                VkSubpassDescription renderSubpass = {};
                renderSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
                renderSubpass.colorAttachmentCount = 1;
                renderSubpass.pColorAttachments = &outputAttachment;
                renderSubpass.pDepthStencilAttachment = &depthAttachment;

                VkAttachmentReference debugDepthAttachment = {};
                debugDepthAttachment.attachment = 2;
                debugDepthAttachment.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

                VkSubpassDescription debugSubpass = {};
                debugSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
                debugSubpass.colorAttachmentCount = 1;
                debugSubpass.pColorAttachments = &outputAttachment;
                debugSubpass.pDepthStencilAttachment = &debugDepthAttachment;

                VkSubpassDescription subpasses[] = { renderSubpass, debugSubpass };

                VkSubpassDependency dependency = {};
                dependency.srcSubpass = 0;
                dependency.dstSubpass = 1;
                dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

                VkRenderPassCreateInfo createRenderPass = {};
                createRenderPass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
                createRenderPass.attachmentCount = arrayCount(attachmentDescriptions);
                createRenderPass.subpassCount = arrayCount(subpasses);
                createRenderPass.dependencyCount = 1;
                createRenderPass.pDependencies = &dependency;
                createRenderPass.pAttachments = attachmentDescriptions;
                createRenderPass.pSubpasses = subpasses;

                IB_VKCHECK(vkCreateRenderPass(RendererContext.Present.LogicalDevice, &createRenderPass, NoAllocator, &RendererContext.Present.RenderPass));
            }

            buildSurfaceSwapchain(RendererContext.Present.SurfaceExtents);

            {
                VkSemaphoreCreateInfo semaphoreCreateInfo = {};
                semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

                for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
                {
                    IB_VKCHECK(vkCreateSemaphore(RendererContext.Present.LogicalDevice, &semaphoreCreateInfo, NoAllocator, &RendererContext.Present.FrameBuffer[fb].FinishedSemaphore));
                }
            }

            for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
            {
                VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
                commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                commandBufferAllocateInfo.commandBufferCount = 1;
                commandBufferAllocateInfo.commandPool = RendererContext.Present.Queues[Queue::Graphics].CommandPool;

                IB_VKCHECK(vkAllocateCommandBuffers(RendererContext.Present.LogicalDevice, &commandBufferAllocateInfo, &RendererContext.Present.FrameBuffer[fb].PrimaryCommandBuffer));
            }

            {
                VkFenceCreateInfo fenceCreateInfo = {};
                fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

                for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
                {
                    IB_VKCHECK(vkCreateFence(RendererContext.Present.LogicalDevice, &fenceCreateInfo, NoAllocator, &RendererContext.Present.FrameBuffer[fb].FinishedFence));
                }
            }
        }

        // Forward mat
        {
            VkShaderModuleCreateInfo createVShader = {};
            createVShader.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createVShader.pCode = (const uint32_t *)desc.Materials.Forward.VShader;
            createVShader.codeSize = desc.Materials.Forward.VShaderSize;
            IB_VKCHECK(vkCreateShaderModule(RendererContext.Present.LogicalDevice, &createVShader, NoAllocator, &RendererContext.Materials.Forward.VShader));

            VkShaderModuleCreateInfo createFShader = {};
            createFShader.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createFShader.pCode = (const uint32_t *)desc.Materials.Forward.FShader;
            createFShader.codeSize = desc.Materials.Forward.FShaderSize;
            IB_VKCHECK(vkCreateShaderModule(RendererContext.Present.LogicalDevice, &createFShader, NoAllocator, &RendererContext.Materials.Forward.FShader));

            // Graphics pipeline

            // Per material type binding
            {
                VkDescriptorSetLayoutBinding vertexBinding0 = {};
                vertexBinding0.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
                vertexBinding0.binding = 0;
                vertexBinding0.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                vertexBinding0.descriptorCount = 1;

                VkDescriptorSetLayoutBinding fragBinding0 = {};
                fragBinding0.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                fragBinding0.binding = 1;
                fragBinding0.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
                fragBinding0.descriptorCount = 1;

                VkDescriptorSetLayoutBinding fragBinding1 = {};
                fragBinding1.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                fragBinding1.binding = 2;
                fragBinding1.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                fragBinding1.descriptorCount = MaxImageCount;

                VkDescriptorSetLayoutBinding layoutBindings[] = {vertexBinding0, fragBinding0, fragBinding1};

                VkDescriptorBindingFlagsEXT bindFlags[] = {0, 0, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT};
                VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extendedInfo = {};
                extendedInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
                extendedInfo.bindingCount = arrayCount(bindFlags);
                extendedInfo.pBindingFlags = bindFlags;

                VkDescriptorSetLayoutCreateInfo createLayout = {};
                createLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                createLayout.pNext = &extendedInfo;
                createLayout.bindingCount = arrayCount(layoutBindings);
                createLayout.pBindings = layoutBindings;
                IB_VKCHECK(vkCreateDescriptorSetLayout(RendererContext.Present.LogicalDevice, &createLayout, NoAllocator, &RendererContext.Materials.Forward.ShaderLayout));
            }

            // Per material instance descriptor layouts
            {
                VkDescriptorSetLayoutBinding layoutBinding = {};
                layoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                layoutBinding.binding = 0;
                layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                layoutBinding.descriptorCount = 1;

                VkDescriptorSetLayoutBinding layoutBindings[] = {layoutBinding};

                VkDescriptorSetLayoutCreateInfo createLayout = {};
                createLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                createLayout.bindingCount = arrayCount(layoutBindings);
                createLayout.pBindings = layoutBindings;

                IB_VKCHECK(vkCreateDescriptorSetLayout(RendererContext.Present.LogicalDevice, &createLayout, NoAllocator, &RendererContext.Materials.Forward.ShaderInstanceLayout));
            }

            {
                VkPushConstantRange vertPushConstant = {};
                vertPushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
                vertPushConstant.offset = 0;
                vertPushConstant.size = 256;

                VkPushConstantRange pushConstantRanges[] = {vertPushConstant};

                VkDescriptorSetLayout layouts[] =
                    {
                        RendererContext.Materials.Forward.ShaderLayout,
                        RendererContext.Materials.Forward.ShaderInstanceLayout,
                    };

                VkPipelineLayoutCreateInfo pipelineLayoutCreate = {};
                pipelineLayoutCreate.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                pipelineLayoutCreate.setLayoutCount = arrayCount(layouts);
                pipelineLayoutCreate.pSetLayouts = layouts;
                pipelineLayoutCreate.pushConstantRangeCount = arrayCount(pushConstantRanges);
                pipelineLayoutCreate.pPushConstantRanges = pushConstantRanges;

                IB_VKCHECK(vkCreatePipelineLayout(RendererContext.Present.LogicalDevice, &pipelineLayoutCreate, NoAllocator, &RendererContext.Materials.Forward.PipelineLayout));
            }

            {
                VkPipelineVertexInputStateCreateInfo vertexInputCreate = {};
                vertexInputCreate.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

                // Input Assembly
                VkPipelineInputAssemblyStateCreateInfo inputAssemblyCreate = {};
                inputAssemblyCreate.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
                inputAssemblyCreate.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

                // Rasterization
                VkPipelineRasterizationStateCreateInfo rasterizationCreate = {};
                rasterizationCreate.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
                rasterizationCreate.rasterizerDiscardEnable = VK_FALSE;
                rasterizationCreate.depthBiasEnable = VK_FALSE;
                rasterizationCreate.depthClampEnable = VK_FALSE;
                rasterizationCreate.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
                rasterizationCreate.lineWidth = 1.0f;
                rasterizationCreate.polygonMode = VK_POLYGON_MODE_FILL;
                rasterizationCreate.cullMode = VK_CULL_MODE_BACK_BIT;

                VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
                colorBlendAttachment.blendEnable = VK_TRUE;
                colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
                colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
                colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

                // All color buffers have the same attachments
                VkPipelineColorBlendAttachmentState colorAttachments[] = {colorBlendAttachment};

                VkPipelineColorBlendStateCreateInfo colorBlendCreate = {};
                colorBlendCreate.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
                colorBlendCreate.attachmentCount = arrayCount(colorAttachments);
                colorBlendCreate.pAttachments = colorAttachments;

                VkPipelineDepthStencilStateCreateInfo depthStencilCreate = {};
                depthStencilCreate.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
                depthStencilCreate.depthTestEnable = VK_TRUE;
                depthStencilCreate.depthWriteEnable = VK_TRUE;
                depthStencilCreate.depthCompareOp = VK_COMPARE_OP_LESS;
                depthStencilCreate.depthBoundsTestEnable = VK_FALSE;
                depthStencilCreate.minDepthBounds = 0.0f;
                depthStencilCreate.maxDepthBounds = 1.0f;
                depthStencilCreate.stencilTestEnable = VK_FALSE;

                VkPipelineMultisampleStateCreateInfo multisampleCreate = {};
                multisampleCreate.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
                multisampleCreate.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

                VkPipelineShaderStageCreateInfo vertexStage = {};
                vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                vertexStage.pName = "vertexMain";
                vertexStage.module = RendererContext.Materials.Forward.VShader;
                vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;

                VkPipelineShaderStageCreateInfo fragStage = {};
                fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
                fragStage.pName = "fragMain";
                fragStage.module = RendererContext.Materials.Forward.FShader;
                fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;

                VkPipelineShaderStageCreateInfo shaderStages[] = {vertexStage, fragStage};
                VkRect2D scissor = {};
                scissor.extent = RendererContext.Present.SurfaceExtents;

                VkViewport viewport = {};
                viewport.x = 0;
                viewport.y = 0;
                viewport.width = (float)RendererContext.Present.SurfaceExtents.width;
                viewport.height = (float)RendererContext.Present.SurfaceExtents.height;
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;

                VkPipelineViewportStateCreateInfo viewportStateCreate = {};
                viewportStateCreate.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
                viewportStateCreate.viewportCount = 1;
                viewportStateCreate.pViewports = &viewport;
                viewportStateCreate.scissorCount = 1;
                viewportStateCreate.pScissors = &scissor;

                VkGraphicsPipelineCreateInfo baseGraphicsPipelineCreate = {};
                baseGraphicsPipelineCreate.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                baseGraphicsPipelineCreate.layout = RendererContext.Materials.Forward.PipelineLayout;
                baseGraphicsPipelineCreate.renderPass = RendererContext.Present.RenderPass;
                baseGraphicsPipelineCreate.pVertexInputState = &vertexInputCreate;
                baseGraphicsPipelineCreate.pInputAssemblyState = &inputAssemblyCreate;
                baseGraphicsPipelineCreate.pRasterizationState = &rasterizationCreate;
                baseGraphicsPipelineCreate.pColorBlendState = &colorBlendCreate;
                baseGraphicsPipelineCreate.pDepthStencilState = &depthStencilCreate;
                baseGraphicsPipelineCreate.pMultisampleState = &multisampleCreate;

                VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

                VkPipelineDynamicStateCreateInfo dynamicStateCreate = {};
                dynamicStateCreate.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
                dynamicStateCreate.dynamicStateCount = arrayCount(dynamicStates);
                dynamicStateCreate.pDynamicStates = dynamicStates;

                baseGraphicsPipelineCreate.pDynamicState = &dynamicStateCreate;
                baseGraphicsPipelineCreate.pViewportState = &viewportStateCreate;
                baseGraphicsPipelineCreate.stageCount = arrayCount(shaderStages);
                baseGraphicsPipelineCreate.pStages = shaderStages;

                baseGraphicsPipelineCreate.flags = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
                VkGraphicsPipelineCreateInfo subpass0Pipeline = baseGraphicsPipelineCreate;
                subpass0Pipeline.subpass = 0;

                VkGraphicsPipelineCreateInfo subpass1Pipeline = baseGraphicsPipelineCreate;
                subpass1Pipeline.subpass = 1;
                VkGraphicsPipelineCreateInfo pipelines[] =
                    {
                        subpass0Pipeline,
                        subpass1Pipeline
                    };

                IB_VKCHECK(vkCreateGraphicsPipelines(RendererContext.Present.LogicalDevice, RendererContext.Present.PipelineCache, arrayCount(pipelines), pipelines, NoAllocator, RendererContext.Materials.Forward.Pipelines));
            }
        }

        constexpr uint32_t meshBufferSize = 1024 * 1024 * 10;
        {
            VkBufferCreateInfo bufferCreate = {};
            bufferCreate.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreate.size = meshBufferSize;
            bufferCreate.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT; // buffers created through create buffer can always be transfered to

            VkBuffer *buffer = &RendererContext.Geometry.MeshDataBuffers;
            RendererContext.Geometry.AllocationSize = meshBufferSize;
            IB_VKCHECK(vkCreateBuffer(RendererContext.Present.LogicalDevice, &bufferCreate, NoAllocator, buffer));

            VkMemoryRequirements memoryRequirements;
            vkGetBufferMemoryRequirements(RendererContext.Present.LogicalDevice, *buffer, &memoryRequirements);

            unsigned int preferredBits = 0;
            uint32_t memoryIndex = findMemoryIndex(RendererContext.Present.PhysicalDevice, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, preferredBits);

            Allocation *allocation = &RendererContext.Geometry.Allocation;
            *allocation = allocateDeviceMemory(RendererContext.Present.LogicalDevice, &RendererContext.Allocator, memoryIndex, meshBufferSize, memoryRequirements.alignment);

            IB_VKCHECK(vkBindBufferMemory(RendererContext.Present.LogicalDevice, *buffer, allocation->Memory, allocation->Offset));
        }

        // Create our sampler
        {
            VkSamplerCreateInfo samplerCreate = {};
            samplerCreate.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerCreate.magFilter = VK_FILTER_NEAREST;
            samplerCreate.minFilter = VK_FILTER_NEAREST;
            samplerCreate.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerCreate.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerCreate.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            samplerCreate.anisotropyEnable = VK_FALSE;
            samplerCreate.maxAnisotropy = 0;
            samplerCreate.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
            samplerCreate.compareEnable = VK_FALSE;
            samplerCreate.compareOp = VK_COMPARE_OP_ALWAYS;
            samplerCreate.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            samplerCreate.mipLodBias = 0.0f;
            samplerCreate.minLod = 0.0f;
            samplerCreate.maxLod = 0.0f;

            IB_VKCHECK(vkCreateSampler(RendererContext.Present.LogicalDevice, &samplerCreate, NoAllocator, &RendererContext.Materials.Forward.Sampler));
        }

        // Write to our template descriptor set
        {
            VkDescriptorSetAllocateInfo descriptorSetAlloc = {};
            descriptorSetAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptorSetAlloc.descriptorPool = RendererContext.Present.DescriptorPool;
            descriptorSetAlloc.descriptorSetCount = 1;
            descriptorSetAlloc.pSetLayouts = &RendererContext.Materials.Forward.ShaderLayout;
            IB_VKCHECK(vkAllocateDescriptorSets(
                RendererContext.Present.LogicalDevice, &descriptorSetAlloc,
                &RendererContext.Materials.Forward.ShaderDescriptor));

            VkDescriptorBufferInfo meshBufferInfo = {};
            meshBufferInfo.buffer = RendererContext.Geometry.MeshDataBuffers;
            meshBufferInfo.offset = 0;
            meshBufferInfo.range = RendererContext.Geometry.AllocationSize;

            VkWriteDescriptorSet writeGeometry = {};
            writeGeometry.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeGeometry.dstSet = RendererContext.Materials.Forward.ShaderDescriptor;
            writeGeometry.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writeGeometry.dstBinding = 0;
            writeGeometry.descriptorCount = 1;
            writeGeometry.pBufferInfo = &meshBufferInfo;

            VkDescriptorImageInfo samplerInfo = {};
            samplerInfo.sampler = RendererContext.Materials.Forward.Sampler;

            VkWriteDescriptorSet writeSamplerSet = {};
            writeSamplerSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeSamplerSet.dstSet = RendererContext.Materials.Forward.ShaderDescriptor;
            writeSamplerSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            writeSamplerSet.dstBinding = 1;
            writeSamplerSet.descriptorCount = 1;
            writeSamplerSet.pImageInfo = &samplerInfo;

            VkWriteDescriptorSet writeDescriptorSets[] = {writeSamplerSet, writeGeometry};
            vkUpdateDescriptorSets(RendererContext.Present.LogicalDevice, arrayCount(writeDescriptorSets), writeDescriptorSets, 0, NULL);
        }
    }

    IB_API void killRenderer()
    {
        vkDeviceWaitIdle(RendererContext.Present.LogicalDevice);

        for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
        {
            vkDestroySemaphore(RendererContext.Present.LogicalDevice, RendererContext.Present.FrameBuffer[fb].AcquireSemaphore, NoAllocator);
            vkDestroySemaphore(RendererContext.Present.LogicalDevice, RendererContext.Present.FrameBuffer[fb].FinishedSemaphore, NoAllocator);
            vkDestroyImageView(RendererContext.Present.LogicalDevice, RendererContext.Present.FrameBuffer[fb].SwapchainImageView, NoAllocator);
            vkDestroyFramebuffer(RendererContext.Present.LogicalDevice, RendererContext.Present.FrameBuffer[fb].Framebuffer, NoAllocator);
            vkDestroyFence(RendererContext.Present.LogicalDevice, RendererContext.Present.FrameBuffer[fb].FinishedFence, NoAllocator);
            vkDestroyImage(RendererContext.Present.LogicalDevice, RendererContext.Present.FrameBuffer[fb].DepthImage, NoAllocator);
            vkDestroyImageView(RendererContext.Present.LogicalDevice, RendererContext.Present.FrameBuffer[fb].DepthImageView, NoAllocator);
            freeDeviceMemory(&RendererContext.Allocator, RendererContext.Present.FrameBuffer[fb].DepthImageAllocation);
            vkDestroyImage(RendererContext.Present.LogicalDevice, RendererContext.Present.FrameBuffer[fb].DebugDepthImage, NoAllocator);
            vkDestroyImageView(RendererContext.Present.LogicalDevice, RendererContext.Present.FrameBuffer[fb].DebugDepthImageView, NoAllocator);
            freeDeviceMemory(&RendererContext.Allocator, RendererContext.Present.FrameBuffer[fb].DebugDepthImageAllocation);

        }

        vkDestroyBuffer(RendererContext.Present.LogicalDevice, RendererContext.Geometry.MeshDataBuffers, NoAllocator);
        freeDeviceMemory(&RendererContext.Allocator, RendererContext.Geometry.Allocation);

        for (uint32_t i = 0; i < RendererContext.Textures.ImageCount; i++)
        {
            vkDestroyImage(RendererContext.Present.LogicalDevice, RendererContext.Textures.Images[i].Image, NoAllocator);
            vkDestroyImageView(RendererContext.Present.LogicalDevice, RendererContext.Textures.Images[i].ImageView, NoAllocator);
            freeDeviceMemory(&RendererContext.Allocator, RendererContext.Textures.Images[i].Allocation);
        }

        vkDestroyShaderModule(RendererContext.Present.LogicalDevice, RendererContext.Materials.Forward.VShader, NoAllocator);
        vkDestroyShaderModule(RendererContext.Present.LogicalDevice, RendererContext.Materials.Forward.FShader, NoAllocator);

        vkDestroyDescriptorSetLayout(RendererContext.Present.LogicalDevice, RendererContext.Materials.Forward.ShaderInstanceLayout, NoAllocator);
        vkDestroyDescriptorSetLayout(RendererContext.Present.LogicalDevice, RendererContext.Materials.Forward.ShaderLayout, NoAllocator);

        vkDestroyPipelineLayout(RendererContext.Present.LogicalDevice, RendererContext.Materials.Forward.PipelineLayout, NoAllocator);

        for (uint32_t i = 0; i < arrayCount(RendererContext.Materials.Forward.Pipelines); i++)
        {
            vkDestroyPipeline(RendererContext.Present.LogicalDevice, RendererContext.Materials.Forward.Pipelines[i], NoAllocator);
        }

        vkDestroySampler(RendererContext.Present.LogicalDevice, RendererContext.Materials.Forward.Sampler, NoAllocator);

        for (uint32_t i = 0; i < RendererContext.Materials.Forward.InstanceCount; i++)
        {
            vkDestroyBuffer(RendererContext.Present.LogicalDevice, RendererContext.Materials.Forward.Instances[i].FShaderData, NoAllocator);
            freeDeviceMemory(&RendererContext.Allocator, RendererContext.Materials.Forward.Instances[i].Allocation);
        }

        for (uint32_t i = 0; i < Queue::Count; i++)
        {
            vkDestroyCommandPool(RendererContext.Present.LogicalDevice, RendererContext.Present.Queues[i].CommandPool, NoAllocator);
        }

        vkDestroyFence(RendererContext.Present.LogicalDevice, RendererContext.Present.ImmediateFence, NoAllocator);
        vkDestroyRenderPass(RendererContext.Present.LogicalDevice, RendererContext.Present.RenderPass, NoAllocator);
        vkDestroySwapchainKHR(RendererContext.Present.LogicalDevice, RendererContext.Present.Swapchain, NoAllocator);
        vkDestroyPipelineCache(RendererContext.Present.LogicalDevice, RendererContext.Present.PipelineCache, NoAllocator);
        vkDestroyDescriptorPool(RendererContext.Present.LogicalDevice, RendererContext.Present.DescriptorPool, NoAllocator);
        vkDestroySurfaceKHR(RendererContext.VulkanInstance, RendererContext.Present.Surface, NoAllocator);

        destroyAllocator(RendererContext.Present.LogicalDevice, &RendererContext.Allocator);
        vkDestroyDevice(RendererContext.Present.LogicalDevice, NoAllocator);
        vkDestroyInstance(RendererContext.VulkanInstance, NoAllocator);
    }

    IB_API MeshHandle createMesh(MeshDesc const &desc)
    {
        uint32_t meshIndex = RendererContext.Geometry.MeshCount++;

        uint32_t vertexSize = desc.Vertices.Count * sizeof(Vertex);
        uint32_t indexSize = desc.Indices.Count * sizeof(uint16_t);
        {
            RendererContext.Geometry.Meshes[meshIndex].VertexSize = vertexSize;
            RendererContext.Geometry.Meshes[meshIndex].VertexOffset = RendererContext.Geometry.NextOffset;
            RendererContext.Geometry.Meshes[meshIndex].IndexOffset = vertexSize + RendererContext.Geometry.NextOffset;
            RendererContext.Geometry.Meshes[meshIndex].IndexCount = desc.Indices.Count;

            RendererContext.Geometry.NextOffset += vertexSize + indexSize;
            uint32_t alignmentBump = RendererContext.Geometry.NextOffset % sizeof(Vertex);
            RendererContext.Geometry.NextOffset += alignmentBump == 0 ? 0 : sizeof(Vertex) - alignmentBump;
        }

        VkBuffer srcBuffer;
        Allocation allocation;
        {
            VkBufferCreateInfo bufferCreate = {};
            bufferCreate.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreate.size = indexSize + vertexSize;
            bufferCreate.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

            IB_VKCHECK(vkCreateBuffer(RendererContext.Present.LogicalDevice, &bufferCreate, NoAllocator, &srcBuffer));

            VkMemoryRequirements memoryRequirements;
            vkGetBufferMemoryRequirements(RendererContext.Present.LogicalDevice, srcBuffer, &memoryRequirements);

            uint32_t memoryIndex = findMemoryIndex(RendererContext.Present.PhysicalDevice, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            allocation = allocateDeviceMemory(RendererContext.Present.LogicalDevice, &RendererContext.Allocator, memoryIndex, indexSize + vertexSize, memoryRequirements.alignment);
            IB_VKCHECK(vkBindBufferMemory(RendererContext.Present.LogicalDevice, srcBuffer, allocation.Memory, allocation.Offset));

            {
                void *memory = mapAllocation(RendererContext.Present.LogicalDevice, &RendererContext.Allocator, allocation);
                memcpy((uint8_t *)memory, (uint8_t *)desc.Vertices.Data, vertexSize);
                memcpy((uint8_t *)memory + vertexSize, (uint8_t *)desc.Indices.Data, indexSize);
                unmapAllocation(&RendererContext.Allocator, allocation);
            }
        }

        // TODO: We can likely store this in a cleanup structure when we request the dependency
        {
            VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
            commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            commandBufferAllocateInfo.commandBufferCount = 1;
            commandBufferAllocateInfo.commandPool = RendererContext.Present.Queues[Queue::Transfer].CommandPool;

            VkCommandBuffer commandBuffer;
            IB_VKCHECK(vkAllocateCommandBuffers(RendererContext.Present.LogicalDevice, &commandBufferAllocateInfo, &commandBuffer));

            VkCommandBufferBeginInfo beginBufferInfo = {};
            beginBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

            IB_VKCHECK(vkBeginCommandBuffer(commandBuffer, &beginBufferInfo));

            VkBufferCopy meshCopy = {};
            meshCopy.srcOffset = 0;
            meshCopy.dstOffset = RendererContext.Geometry.Meshes[meshIndex].VertexOffset;
            meshCopy.size = vertexSize + indexSize;

            vkCmdCopyBuffer(commandBuffer, srcBuffer, RendererContext.Geometry.MeshDataBuffers, 1, &meshCopy);

            IB_VKCHECK(vkEndCommandBuffer(commandBuffer));
            VkSubmitInfo submitInfo = {};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            IB_VKCHECK(vkQueueSubmit(RendererContext.Present.Queues[Queue::Transfer].Queue, 1, &submitInfo, RendererContext.Present.ImmediateFence));
            IB_VKCHECK(vkWaitForFences(RendererContext.Present.LogicalDevice, 1, &RendererContext.Present.ImmediateFence, VK_TRUE, UINT64_MAX));
            IB_VKCHECK(vkResetFences(RendererContext.Present.LogicalDevice, 1, &RendererContext.Present.ImmediateFence));

            vkFreeCommandBuffers(RendererContext.Present.LogicalDevice, RendererContext.Present.Queues[Queue::Transfer].CommandPool, 1, &commandBuffer);
            vkDestroyBuffer(RendererContext.Present.LogicalDevice, srcBuffer, NoAllocator);
            freeDeviceMemory(&RendererContext.Allocator, allocation);
        }

        return MeshHandle{meshIndex + 1};
    }

    IB_API ImageHandle createImage(ImageDesc const &desc)
    {
        VkFormat formats[ImageFormat::Count] = {};
        formats[ImageFormat::RGBA8] = VK_FORMAT_R8G8B8A8_UNORM;

        uint32_t formatSize[ImageFormat::Count] = {};
        formatSize[ImageFormat::RGBA8] = 4;

        // Image
        uint32_t imageIndex = RendererContext.Textures.ImageCount++;
        ImageAllocation imageAlloc = {};
        imageAlloc.LogicalDevice = RendererContext.Present.LogicalDevice;
        imageAlloc.PhysicalDevice = RendererContext.Present.PhysicalDevice;
        imageAlloc.ImageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageAlloc.Format = formats[desc.Format];
        imageAlloc.ImageAspect = VK_IMAGE_ASPECT_COLOR_BIT;
        imageAlloc.Width = desc.Width;
        imageAlloc.Height = desc.Height;
        imageAlloc.Stride = formatSize[desc.Format];
        imageAlloc.Allocator = &RendererContext.Allocator;

        ImageAndView imageAndView = allocImageAndView(imageAlloc);
        RendererContext.Textures.Images[imageIndex].Image = imageAndView.Image;
        RendererContext.Textures.Images[imageIndex].ImageView = imageAndView.ImageView;
        RendererContext.Textures.Images[imageIndex].Allocation = imageAndView.Allocation;

        VkBuffer srcBuffer;
        Allocation allocation;
        {
            uint32_t bufferSize = desc.Width * desc.Height * formatSize[desc.Format];
            VkBufferCreateInfo bufferCreate = {};
            bufferCreate.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreate.size = bufferSize;
            bufferCreate.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

            IB_VKCHECK(vkCreateBuffer(RendererContext.Present.LogicalDevice, &bufferCreate, NoAllocator, &srcBuffer));

            VkMemoryRequirements memoryRequirements;
            vkGetBufferMemoryRequirements(RendererContext.Present.LogicalDevice, srcBuffer, &memoryRequirements);

            uint32_t memoryIndex = findMemoryIndex(RendererContext.Present.PhysicalDevice, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            allocation = allocateDeviceMemory(RendererContext.Present.LogicalDevice, &RendererContext.Allocator, memoryIndex, bufferSize, memoryRequirements.alignment);
            IB_VKCHECK(vkBindBufferMemory(RendererContext.Present.LogicalDevice, srcBuffer, allocation.Memory, allocation.Offset));

            {
                void *memory = mapAllocation(RendererContext.Present.LogicalDevice, &RendererContext.Allocator, allocation);
                memcpy(memory, (uint8_t *)desc.Data, bufferSize);
                unmapAllocation(&RendererContext.Allocator, allocation);
            }
        }

        VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
        commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandBufferAllocateInfo.commandBufferCount = 1;
        commandBufferAllocateInfo.commandPool = RendererContext.Present.Queues[Queue::Graphics].CommandPool;

        VkCommandBuffer commandBuffer;
        IB_VKCHECK(vkAllocateCommandBuffers(RendererContext.Present.LogicalDevice, &commandBufferAllocateInfo, &commandBuffer));

        VkCommandBufferBeginInfo beginBufferInfo = {};
        beginBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        IB_VKCHECK(vkBeginCommandBuffer(commandBuffer, &beginBufferInfo));

        // Image barrier UNDEFINED -> OPTIMAL
        {
            VkAccessFlagBits sourceAccessMask = {};
            VkAccessFlagBits dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

            VkImageSubresourceRange imageSubresource = {};
            imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageSubresource.levelCount = 1;
            imageSubresource.baseMipLevel = 0;
            imageSubresource.baseArrayLayer = 0;
            imageSubresource.layerCount = 1;

            VkImageMemoryBarrier imageBarrier = {};
            imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.image = RendererContext.Textures.Images[imageIndex].Image;
            imageBarrier.subresourceRange = imageSubresource;
            imageBarrier.srcAccessMask = sourceAccessMask;
            imageBarrier.dstAccessMask = dstAccessMask;

            vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, NULL, 0, NULL, 1, &imageBarrier);
        }

        // Image copy
        {
            VkImageSubresourceLayers imageSubresource = {};
            imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageSubresource.mipLevel = 0;
            imageSubresource.baseArrayLayer = 0;
            imageSubresource.layerCount = 1;

            VkExtent3D extent = {};
            extent.width = desc.Width;
            extent.height = desc.Height;
            extent.depth = 1;

            VkBufferImageCopy copyRegion = {};
            copyRegion.bufferOffset = 0;
            copyRegion.bufferRowLength = 0;
            copyRegion.bufferImageHeight = 0;
            copyRegion.imageSubresource = imageSubresource;
            copyRegion.imageOffset = {};
            copyRegion.imageExtent = extent;

            vkCmdCopyBufferToImage(commandBuffer, srcBuffer, RendererContext.Textures.Images[imageIndex].Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
        }

        // Image barrier OPTIMAL -> FRAGMEN_SHADER
        {
            VkAccessFlagBits sourceAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            VkAccessFlagBits dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

            VkImageSubresourceRange imageSubresource = {};
            imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageSubresource.levelCount = 1;
            imageSubresource.baseMipLevel = 0;
            imageSubresource.baseArrayLayer = 0;
            imageSubresource.layerCount = 1;

            VkImageMemoryBarrier imageBarrier = {};
            imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imageBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            imageBarrier.image = RendererContext.Textures.Images[imageIndex].Image;
            imageBarrier.subresourceRange = imageSubresource;
            imageBarrier.srcAccessMask = sourceAccessMask;
            imageBarrier.dstAccessMask = dstAccessMask;

            vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, NULL, 0, NULL, 1, &imageBarrier);
        }

        IB_VKCHECK(vkEndCommandBuffer(commandBuffer));
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        IB_VKCHECK(vkQueueSubmit(RendererContext.Present.Queues[Queue::Graphics].Queue, 1, &submitInfo, RendererContext.Present.ImmediateFence));
        IB_VKCHECK(vkWaitForFences(RendererContext.Present.LogicalDevice, 1, &RendererContext.Present.ImmediateFence, VK_TRUE, UINT64_MAX));
        IB_VKCHECK(vkResetFences(RendererContext.Present.LogicalDevice, 1, &RendererContext.Present.ImmediateFence));

        vkFreeCommandBuffers(RendererContext.Present.LogicalDevice, RendererContext.Present.Queues[Queue::Graphics].CommandPool, 1, &commandBuffer);
        vkDestroyBuffer(RendererContext.Present.LogicalDevice, srcBuffer, NoAllocator);
        freeDeviceMemory(&RendererContext.Allocator, allocation);

        // Update our shader descriptor
        {
            VkDescriptorImageInfo imageInfo = {};
            imageInfo.imageView = RendererContext.Textures.Images[imageIndex].ImageView;
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            VkWriteDescriptorSet writeImageSet = {};
            writeImageSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeImageSet.dstSet = RendererContext.Materials.Forward.ShaderDescriptor;
            writeImageSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            writeImageSet.dstBinding = 2;
            writeImageSet.dstArrayElement = imageIndex;
            writeImageSet.descriptorCount = 1;
            writeImageSet.pImageInfo = &imageInfo;

            VkWriteDescriptorSet writeDescriptorSets[] = {writeImageSet};
            vkUpdateDescriptorSets(RendererContext.Present.LogicalDevice, arrayCount(writeDescriptorSets), writeDescriptorSets, 0, NULL);
        }

        return ImageHandle{imageIndex + 1};
    }

    IB_API MaterialHandle createMaterial(ForwardDesc const &desc)
    {
        struct
        {
            float AlbedoTint[4];
            uint32_t albedoIndex;
        } matData;
        memcpy(matData.AlbedoTint, desc.AlbedoTint, sizeof(float) * 4);
        matData.albedoIndex = desc.AlbedoImage.Value - 1;

        uint32_t instanceIndex = RendererContext.Materials.Forward.InstanceCount++;
        RendererContext.Materials.Forward.Instances[instanceIndex].PipelineIndex = PipelineType::Default;

        VkBuffer dataBuffer;
        {
            VkBufferCreateInfo bufferCreate = {};
            bufferCreate.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreate.size = sizeof(matData);
            bufferCreate.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT; // buffers created through create buffer can always be transfered to

            VkBuffer *buffer = &RendererContext.Materials.Forward.Instances[instanceIndex].FShaderData;
            IB_VKCHECK(vkCreateBuffer(RendererContext.Present.LogicalDevice, &bufferCreate, NoAllocator, buffer));

            VkMemoryRequirements memoryRequirements;
            vkGetBufferMemoryRequirements(RendererContext.Present.LogicalDevice, *buffer, &memoryRequirements);

            unsigned int preferredBits = 0;
            uint32_t memoryIndex = findMemoryIndex(RendererContext.Present.PhysicalDevice, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, preferredBits);

            Allocation *allocation = &RendererContext.Materials.Forward.Instances[instanceIndex].Allocation;
            *allocation = allocateDeviceMemory(RendererContext.Present.LogicalDevice, &RendererContext.Allocator, memoryIndex, sizeof(matData), memoryRequirements.alignment);

            IB_VKCHECK(vkBindBufferMemory(RendererContext.Present.LogicalDevice, *buffer, allocation->Memory, allocation->Offset));
            dataBuffer = *buffer;
        }

        VkBuffer srcBuffer;
        Allocation allocation;
        {
            VkBufferCreateInfo bufferCreate = {};
            bufferCreate.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreate.size = sizeof(matData);
            bufferCreate.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

            IB_VKCHECK(vkCreateBuffer(RendererContext.Present.LogicalDevice, &bufferCreate, NoAllocator, &srcBuffer));

            VkMemoryRequirements memoryRequirements;
            vkGetBufferMemoryRequirements(RendererContext.Present.LogicalDevice, srcBuffer, &memoryRequirements);

            uint32_t memoryIndex = findMemoryIndex(RendererContext.Present.PhysicalDevice, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            allocation = allocateDeviceMemory(RendererContext.Present.LogicalDevice, &RendererContext.Allocator, memoryIndex, sizeof(matData), memoryRequirements.alignment);
            IB_VKCHECK(vkBindBufferMemory(RendererContext.Present.LogicalDevice, srcBuffer, allocation.Memory, allocation.Offset));

            {
                void *memory = mapAllocation(RendererContext.Present.LogicalDevice, &RendererContext.Allocator, allocation);
                memcpy((uint8_t *)memory, &matData, sizeof(matData));
                unmapAllocation(&RendererContext.Allocator, allocation);
            }
        }

        // TODO: We can likely store this in a cleanup structure when we request the dependency
        {
            VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
            commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            commandBufferAllocateInfo.commandBufferCount = 1;
            commandBufferAllocateInfo.commandPool = RendererContext.Present.Queues[Queue::Transfer].CommandPool;

            VkCommandBuffer commandBuffer;
            IB_VKCHECK(vkAllocateCommandBuffers(RendererContext.Present.LogicalDevice, &commandBufferAllocateInfo, &commandBuffer));

            VkCommandBufferBeginInfo beginBufferInfo = {};
            beginBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

            IB_VKCHECK(vkBeginCommandBuffer(commandBuffer, &beginBufferInfo));

            VkBufferCopy copy = {};
            copy.srcOffset = 0;
            copy.dstOffset = 0;
            copy.size = sizeof(matData);
            vkCmdCopyBuffer(commandBuffer, srcBuffer, dataBuffer, 1, &copy);

            IB_VKCHECK(vkEndCommandBuffer(commandBuffer));

            VkSubmitInfo submitInfo = {};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            IB_VKCHECK(vkQueueSubmit(RendererContext.Present.Queues[Queue::Transfer].Queue, 1, &submitInfo, RendererContext.Present.ImmediateFence));
            IB_VKCHECK(vkWaitForFences(RendererContext.Present.LogicalDevice, 1, &RendererContext.Present.ImmediateFence, VK_TRUE, UINT64_MAX));
            IB_VKCHECK(vkResetFences(RendererContext.Present.LogicalDevice, 1, &RendererContext.Present.ImmediateFence));

            vkFreeCommandBuffers(RendererContext.Present.LogicalDevice, RendererContext.Present.Queues[Queue::Transfer].CommandPool, 1, &commandBuffer);
            vkDestroyBuffer(RendererContext.Present.LogicalDevice, srcBuffer, NoAllocator);
            freeDeviceMemory(&RendererContext.Allocator, allocation);
        }

        VkDescriptorSetAllocateInfo descriptorSetAlloc = {};
        descriptorSetAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descriptorSetAlloc.descriptorPool = RendererContext.Present.DescriptorPool;
        descriptorSetAlloc.descriptorSetCount = 1;
        descriptorSetAlloc.pSetLayouts = &RendererContext.Materials.Forward.ShaderInstanceLayout;

        IB_VKCHECK(vkAllocateDescriptorSets(
            RendererContext.Present.LogicalDevice, &descriptorSetAlloc,
            &RendererContext.Materials.Forward.Instances[instanceIndex].ShaderDescriptor));

        VkDescriptorBufferInfo bufferInfo = {};
        bufferInfo.buffer = dataBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(matData);

        VkWriteDescriptorSet writeMaterial = {};
        writeMaterial.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeMaterial.dstSet = RendererContext.Materials.Forward.Instances[instanceIndex].ShaderDescriptor;
        writeMaterial.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writeMaterial.dstBinding = 0;
        writeMaterial.descriptorCount = 1;
        writeMaterial.pBufferInfo = &bufferInfo;

        VkWriteDescriptorSet writeDescriptorSets[] = {writeMaterial};
        vkUpdateDescriptorSets(RendererContext.Present.LogicalDevice, arrayCount(writeDescriptorSets), writeDescriptorSets, 0, NULL);

        return MaterialHandle{instanceIndex + 1};
    }

    IB_API void drawView(ViewDesc const &view)
    {
        // Start the frame
        uint32_t buffer = RendererContext.Present.ActiveFrame;

        uint32_t imageIndex = 0;
        VkResult result = vkAcquireNextImageKHR(RendererContext.Present.LogicalDevice, RendererContext.Present.Swapchain, UINT64_MAX, RendererContext.Present.FrameBuffer[buffer].AcquireSemaphore, VK_NULL_HANDLE, &imageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            vkDeviceWaitIdle(RendererContext.Present.LogicalDevice);
            VkSurfaceCapabilitiesKHR surfaceCapabilities;
            IB_VKCHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(RendererContext.Present.PhysicalDevice, RendererContext.Present.Surface, &surfaceCapabilities));
            IB_ASSERT(surfaceCapabilities.currentExtent.width != UINT32_MAX, "Surface extents are undefined.");
            buildSurfaceSwapchain(surfaceCapabilities.currentExtent);
            return;
        }

        IB_VKCHECK(vkWaitForFences(RendererContext.Present.LogicalDevice, 1, &RendererContext.Present.FrameBuffer[RendererContext.Present.ActiveFrame].FinishedFence, VK_TRUE, UINT64_MAX));
        IB_VKCHECK(vkResetFences(RendererContext.Present.LogicalDevice, 1, &RendererContext.Present.FrameBuffer[buffer].FinishedFence));

        VkCommandBuffer currentCommands = RendererContext.Present.FrameBuffer[buffer].PrimaryCommandBuffer;
        {
            IB_VKCHECK(vkResetCommandBuffer(currentCommands, 0));

            VkCommandBufferBeginInfo beginBufferInfo = {};
            beginBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginBufferInfo.flags = 0;

            IB_VKCHECK(vkBeginCommandBuffer(currentCommands, &beginBufferInfo));

            float color[4] = {0.8f, 0.5f, 0.1f, 0.0f};
            VkClearColorValue clearColor = {};
            memcpy(clearColor.float32, &color, sizeof(float) * 4);

            VkClearValue colorClear = {};
            colorClear.color = clearColor;
            VkClearValue depthClear = {};
            depthClear.depthStencil = {1.0f, 0};
            VkClearValue clearValues[] = {colorClear, depthClear, depthClear };

            VkRect2D renderArea = {};
            renderArea.extent = RendererContext.Present.SurfaceExtents;

            VkRenderPassBeginInfo renderPassBeginInfo = {};
            renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassBeginInfo.renderPass = RendererContext.Present.RenderPass;
            renderPassBeginInfo.framebuffer = RendererContext.Present.FrameBuffer[buffer].Framebuffer;
            renderPassBeginInfo.renderArea = renderArea;
            renderPassBeginInfo.clearValueCount = arrayCount(clearValues);
            renderPassBeginInfo.pClearValues = clearValues;

            // Forward materials
            vkCmdBeginRenderPass(currentCommands, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport = {};
            viewport.x = 0;
            viewport.y = 0;
            viewport.width = (float)RendererContext.Present.SurfaceExtents.width;
            viewport.height = (float)RendererContext.Present.SurfaceExtents.height;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(currentCommands, 0, 1, &viewport);

            VkRect2D scissor = {};
            scissor.extent = RendererContext.Present.SurfaceExtents;
            vkCmdSetScissor(currentCommands, 0, 1, &scissor);

            VkDescriptorSet set = RendererContext.Materials.Forward.ShaderDescriptor;
            vkCmdBindDescriptorSets(
                currentCommands, VK_PIPELINE_BIND_POINT_GRAPHICS, RendererContext.Materials.Forward.PipelineLayout,
                0, 1, &set, 0, VK_NULL_HANDLE);

            for (uint32_t passIndex = 0; passIndex < ViewDesc::Pass::Count; passIndex++)
            {
                ViewDesc::Pass const* pass = &view.Forward.Passes[passIndex];

                if (passIndex > 0)
                {
                    vkCmdNextSubpass(currentCommands, VK_SUBPASS_CONTENTS_INLINE);
                }

                for (uint32_t i = 0; i < pass->BatchCount; i++)
                {
                    ViewDesc::Batch *batch = &pass->Batches[i];
                    uint32_t materialIndex = batch->Material.Value - 1;

                    uint32_t pipelineIndex = RendererContext.Materials.Forward.Instances[materialIndex].PipelineIndex + passIndex;
                    vkCmdBindPipeline(currentCommands, VK_PIPELINE_BIND_POINT_GRAPHICS, RendererContext.Materials.Forward.Pipelines[pipelineIndex]);

                    VkDescriptorSet instanceSet = RendererContext.Materials.Forward.Instances[materialIndex].ShaderDescriptor;
                    vkCmdBindDescriptorSets(
                        currentCommands, VK_PIPELINE_BIND_POINT_GRAPHICS, RendererContext.Materials.Forward.PipelineLayout,
                        1, 1, &instanceSet, 0, VK_NULL_HANDLE);

                    for (uint32_t mesh = 0; mesh < batch->MeshCount && batch->Meshes[mesh].Mesh.Value > 0; mesh++)
                    {
                        uint32_t meshIndex = batch->Meshes[mesh].Mesh.Value - 1;

                        vkCmdBindIndexBuffer(currentCommands, RendererContext.Geometry.MeshDataBuffers, RendererContext.Geometry.Meshes[meshIndex].IndexOffset, VK_INDEX_TYPE_UINT16);
                        for (uint32_t inst = 0; inst < batch->Meshes[mesh].Count; inst++)
                        {
                            struct
                            {
                                Mat4x4 VP;
                                Mat3x4 M;
                                uint32_t VertexOffset;
                            } vertPushConstant;

                            vertPushConstant.VP = view.ViewProj;
                            vertPushConstant.M = batch->Meshes[mesh].Transforms[inst];
                            vertPushConstant.VertexOffset = RendererContext.Geometry.Meshes[meshIndex].VertexOffset / sizeof(Vertex);

                            vkCmdPushConstants(currentCommands, RendererContext.Materials.Forward.PipelineLayout,
                                VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vertPushConstant), &vertPushConstant);

                            vkCmdDrawIndexed(currentCommands, RendererContext.Geometry.Meshes[meshIndex].IndexCount, 1, 0, 0, 0);
                        }
                    }
                }
            }

            vkCmdEndRenderPass(currentCommands);
            vkEndCommandBuffer(currentCommands);
        }

        VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &currentCommands;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &RendererContext.Present.FrameBuffer[buffer].AcquireSemaphore;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &RendererContext.Present.FrameBuffer[buffer].FinishedSemaphore;
        submitInfo.pWaitDstStageMask = &dstStageMask;

        IB_VKCHECK(vkQueueSubmit(RendererContext.Present.Queues[Queue::Graphics].Queue, 1, &submitInfo, RendererContext.Present.FrameBuffer[buffer].FinishedFence));

        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &RendererContext.Present.FrameBuffer[buffer].FinishedSemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &RendererContext.Present.Swapchain;
        presentInfo.pImageIndices = &imageIndex;

        VkResult presentResult = vkQueuePresentKHR(RendererContext.Present.Queues[Queue::Present].Queue, &presentInfo);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
        {
            vkDeviceWaitIdle(RendererContext.Present.LogicalDevice);
            VkSurfaceCapabilitiesKHR surfaceCapabilities;
            IB_VKCHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(RendererContext.Present.PhysicalDevice, RendererContext.Present.Surface, &surfaceCapabilities));
            IB_ASSERT(surfaceCapabilities.currentExtent.width != UINT32_MAX, "Failed to get surface extents.");
            buildSurfaceSwapchain(surfaceCapabilities.currentExtent);
        }

        RendererContext.Present.ActiveFrame = (RendererContext.Present.ActiveFrame + 1) % FrameBufferCount;
    }
} // namespace IB
