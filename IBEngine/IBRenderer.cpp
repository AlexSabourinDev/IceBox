#include "IBRenderer.h"
#include "IBLogging.h"
#include "IBPlatform.h"

#include <stdlib.h>

#define VK_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <imgui/imgui_impl_vulkan.h>
#include <imgui/imgui_impl_win32.h>
#include <imgui/imgui.h>

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

#define IB_VKDEBUG

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam); // ImGui window
namespace
{
    void VulkanCheck(VkResult result)
    {
        if (VK_SUCCESS != result)
        {
            IB_ASSERT(false, "Failed VK Check.");
        }
    }

#define IB_VKCHECK(call) VulkanCheck(call);

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

    struct AllocationCategory
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

    constexpr uint32_t MaxAllocatorPoolSizes[AllocationCategory::Count] = {
        1024 * 1024 * 16,
        1024 * 1024 * 16,
        1024 * 1024 * 64,
        1024 * 1024 * 512,
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
        AllocationCategory::Enum Category;
        void *Map = nullptr;
    };

    struct Allocation
    {
        VkDeviceMemory Memory = {};
        VkDeviceSize Offset = {};
        uint32_t Id = 0;
        uint32_t PoolIndex = 0;
    };

    struct VulkanAllocator
    {
        MemoryPool MemoryPools[MaxAllocatorPools] = {};
        MemoryBlock BlockPool[MaxMemoryBlocks] = {};
        uint32_t FreeBlocks[MaxMemoryBlocks] = {};
        uint32_t FreeBlockCount = 0;
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
        AllocationCategory::Enum Category;
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

    void createAllocator(VulkanAllocator *allocator)
    {
        allocator->FreeBlockCount = MaxMemoryBlocks;
        for (uint32_t freeBlockIndex = 0; freeBlockIndex < allocator->FreeBlockCount; freeBlockIndex++)
        {
            allocator->FreeBlocks[freeBlockIndex] = freeBlockIndex;
        }
    }

    void destroyAllocator(VkDevice device, VulkanAllocator *allocator)
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

    void *mapAllocation(VkDevice logicalDevice, VulkanAllocator *allocator, Allocation allocation)
    {
        if (allocator->MemoryPools[allocation.PoolIndex].Map == nullptr)
        {
            unsigned int flags = 0;
            IB_VKCHECK(vkMapMemory(logicalDevice, allocator->MemoryPools[allocation.PoolIndex].Memory, 0, VK_WHOLE_SIZE, flags, &allocator->MemoryPools[allocation.PoolIndex].Map));
        }

        return reinterpret_cast<uint8_t *>(allocator->MemoryPools[allocation.PoolIndex].Map) + allocation.Offset;
    }

    void unmapAllocation(VulkanAllocator * /*allocator*/, Allocation /*allocation*/)
    {
        // Does nothing
    }

    Allocation allocateDeviceMemory(VulkanAllocator *allocator, AllocationRequest request)
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

    void freeMemoryBlock(VulkanAllocator *allocator, MemoryPool *memoryPool, uint32_t currentBlockIndex)
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

    void freeDeviceMemory(VulkanAllocator *allocator, Allocation allocation)
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

    constexpr uint32_t MaxImageCount = 100;
    constexpr uint32_t MaxSamplerCount = 1000;
    constexpr uint32_t FrameBufferCount = 2;
    constexpr uint32_t MaxPhysicalImageCount = 10;
    constexpr uint32_t MaxPipelineCleanups = 10;
    struct Context
    {
        VkInstance VulkanInstance;
        VkPhysicalDevice PhysicalDevice;
        VkDevice LogicalDevice;

        VkDescriptorPool DescriptorPool;
        VkPipelineCache PipelineCache;

        struct
        {
            VkSurfaceKHR Surface;
            VkSurfaceFormatKHR SurfaceFormat;
            VkExtent2D SurfaceExtents;
            VkPresentModeKHR PresentMode;
            VkSwapchainKHR Swapchain;
            VkImage SwapchainImages[MaxPhysicalImageCount];
            uint32_t SwapchainImageCount;

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
                VkCommandBuffer PrimaryCommandBuffer;
                VkFence FinishedFence;
            } FrameBuffer[FrameBufferCount];
            uint32_t ActiveFrame = 0;
        } Present;

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
            // Written to by our pipeline loading thread
            // And during cleanup (rendering) thread.
            // Pipeline loading thread only writes valid pipelines
            // And the cleanup thread only writes null handles
            // The thread communication strategy isn't perfect here
            // But it's fine for our use case since we just need our pipelines to be cleaned up "at some point"
            VkPipeline Pipelines[MaxPipelineCleanups] = {};
        } Cleanup;
    };

    constexpr uint32_t StagingSize = 10 * 1024 * 1024; // 10MB of staging space
    constexpr uint32_t MaxStagingFenceCount = 128;
    constexpr uint32_t MaxTransferCount = 128;
    constexpr uint32_t MaxDescriptorUpdateCount = 128;
    struct Staging
    {
        VkBuffer Buffer;
        Allocation Allocation;
        void* MappedMemory = nullptr;
        uint32_t WriteOffset = 0;

        VkFence Fences[MaxStagingFenceCount];
        uint32_t ActiveFenceCount = 0;

        struct
        {
            VkCommandBuffer Commands[MaxTransferCount];
            uint32_t ActiveCount = 0;
        } Transfers[Queue::Count];

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

    StagingBuffer requestStagingBuffer(Staging* staging, uint32_t size, uint32_t alignment = 1)
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

    constexpr uint32_t MeshBufferChunkCount = FrameBufferCount;
    constexpr uint32_t MaxMeshCount = 1000;
    constexpr uint32_t MeshBufferChunkSize = 1024 * 1024 * 16;
    constexpr uint32_t MeshBufferSize = MeshBufferChunkSize * MeshBufferChunkCount;
    struct Geometry
    {
        struct
        {
            uint32_t VertexOffset = 0;
            uint32_t VertexSize = 0;
            uint32_t IndexOffset = 0;
            uint32_t IndexCount = 0;
        } Meshes[MaxMeshCount];
        bool MeshAllocations[MaxMeshCount] = {}; // Can likely be a bitset if needed

        uint32_t NextOffset = 0;

        uint32_t FreeIndices[MaxMeshCount];
        uint32_t FreeIndexCount = 0;

        // Maintained as a series of chunks
        VkBuffer MeshDataBuffers;
        Allocation Allocation;
    };

    constexpr uint32_t MaxIndirectDraws = 1024;
    struct PerFrameData
    {
        IB::Mat4x4 VP;
        uint32_t LightIndex = 0xFFFFFFFF;
    };

    struct PerInstanceData
    {
        IB::Mat3x4 M;
        uint32_t VertexOffset;
        uint32_t MaterialIndex;
    };

    constexpr uint32_t MaxPerFrameDrawDataSize = sizeof(PerFrameData) + sizeof(PerInstanceData) * MaxIndirectDraws;
    struct IndirectDraw
    {
        struct
        {
            VkBuffer Buffer;
            Allocation Allocation;
            uint8_t *MappedMemory = nullptr;
        } FrameBuffer[FrameBufferCount];

        struct
        {
            VkBuffer Buffer;
            Allocation Allocation;
            uint8_t *MappedMemory = nullptr;
        } PerFrameData;
    };
    IndirectDraw IndirectDraw;

    struct ForwardMaterial
    {
        uint32_t AlbedoTint;
        uint32_t AlbedoIndex;
    };

    constexpr uint32_t MaxMaterialInstanceCount = 100;
    struct ForwardPass
    {
        static constexpr uint32_t SubpassCount = 2;

        VkDescriptorSetLayout ShaderLayout;

        VkPipelineLayout PipelineLayout;
        VkPipeline Pipelines[SubpassCount];

        VkDescriptorSet ShaderDescriptor;
        VkSampler Sampler;
        VkBuffer MaterialBuffer;
        Allocation MaterialBufferAllocation;

        VkRenderPass RenderPass;

        struct
        {
            Allocation DepthImageAllocation;
            VkImage DepthImage;
            VkImageView DepthImageView;

            VkFramebuffer Framebuffer;
        } FrameBuffer[FrameBufferCount];
        uint32_t InstanceCount = 0;
    };

    struct ImGuiPass
    {
        VkRenderPass RenderPass;

        struct
        {
            VkFramebuffer Framebuffer;
        } FrameBuffer[FrameBufferCount];
    };

    constexpr uint32_t MaxLightCount = 32;
    struct DirectionalLight
    {
        IB::Float4 DirectionAndRadiance;
        uint32_t Tint;
    };

    struct Lighting
    {
        VkBuffer LightBuffer;
        Allocation LightBufferAllocation;
        uint32_t InstanceCount = 0;
    };

    VulkanAllocator Allocator;
    Context RendererContext;
    ForwardPass ForwardPass;
    Geometry Geometry;
    ImGuiPass ImGuiPass;
    Lighting Lighting;
    Staging Staging;

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
        VulkanAllocator *Allocator = nullptr;
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

            AllocationRequest request;
            request.Alignment = memoryRequirements.alignment;
            request.Size = alloc.Width * alloc.Height * alloc.Stride;
            request.LogicalDevice = alloc.LogicalDevice;
            request.PhysicalDevice = alloc.PhysicalDevice;
            request.RequiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            request.PreferedFlags = 0;
            request.TypeBits = memoryRequirements.memoryTypeBits;
            request.Category = AllocationCategory::Image;

            Allocation *allocation = &imageAndView.Allocation;
            *allocation = allocateDeviceMemory(alloc.Allocator, request);

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
            vkDestroySwapchainKHR(RendererContext.LogicalDevice, RendererContext.Present.Swapchain, NoAllocator);
            for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
            {
                vkDestroyImageView(RendererContext.LogicalDevice, RendererContext.Present.FrameBuffer[fb].SwapchainImageView, NoAllocator);
                vkDestroyImage(RendererContext.LogicalDevice, ForwardPass.FrameBuffer[fb].DepthImage, NoAllocator);
                vkDestroyImageView(RendererContext.LogicalDevice, ForwardPass.FrameBuffer[fb].DepthImageView, NoAllocator);
                freeDeviceMemory(&Allocator, ForwardPass.FrameBuffer[fb].DepthImageAllocation);
                vkDestroyFramebuffer(RendererContext.LogicalDevice, ForwardPass.FrameBuffer[fb].Framebuffer, NoAllocator);
                vkDestroyFramebuffer(RendererContext.LogicalDevice, ImGuiPass.FrameBuffer[fb].Framebuffer, NoAllocator);
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
            swapchainCreate.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
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

            IB_VKCHECK(vkCreateSwapchainKHR(RendererContext.LogicalDevice, &swapchainCreate, NoAllocator, &RendererContext.Present.Swapchain));

            IB_VKCHECK(vkGetSwapchainImagesKHR(RendererContext.LogicalDevice, RendererContext.Present.Swapchain, &RendererContext.Present.SwapchainImageCount, NULL));
            RendererContext.Present.SwapchainImageCount = RendererContext.Present.SwapchainImageCount < MaxPhysicalImageCount ? RendererContext.Present.SwapchainImageCount : MaxPhysicalImageCount;
            IB_VKCHECK(vkGetSwapchainImagesKHR(RendererContext.LogicalDevice, RendererContext.Present.Swapchain, &RendererContext.Present.SwapchainImageCount, RendererContext.Present.SwapchainImages));

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

                IB_VKCHECK(vkCreateImageView(RendererContext.LogicalDevice, &imageViewCreate, NoAllocator, &RendererContext.Present.FrameBuffer[fb].SwapchainImageView));
            }
        }

        for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
        {
            // Depth image
            {
                ImageAllocation imageAlloc = {};
                imageAlloc.LogicalDevice = RendererContext.LogicalDevice;
                imageAlloc.PhysicalDevice = RendererContext.PhysicalDevice;
                imageAlloc.ImageUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                imageAlloc.Format = VK_FORMAT_D32_SFLOAT;
                imageAlloc.ImageAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
                imageAlloc.Width = surfaceExtents.width;
                imageAlloc.Height = surfaceExtents.height;
                imageAlloc.Stride = 4;
                imageAlloc.Allocator = &Allocator;

                ImageAndView imageAndView = allocImageAndView(imageAlloc);
                ForwardPass.FrameBuffer[fb].DepthImage = imageAndView.Image;
                ForwardPass.FrameBuffer[fb].DepthImageView = imageAndView.ImageView;
                ForwardPass.FrameBuffer[fb].DepthImageAllocation = imageAndView.Allocation;
            }

            // Create our forward pass framebuffers
            {
                VkImageView images[] =
                    {
                        RendererContext.Present.FrameBuffer[fb].SwapchainImageView,
                        ForwardPass.FrameBuffer[fb].DepthImageView,
                    };

                VkFramebufferCreateInfo framebufferCreate = {};
                framebufferCreate.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                framebufferCreate.attachmentCount = arrayCount(images);
                framebufferCreate.width = surfaceExtents.width;
                framebufferCreate.height = surfaceExtents.height;
                framebufferCreate.layers = 1;
                framebufferCreate.renderPass = ForwardPass.RenderPass;
                framebufferCreate.pAttachments = images;

                IB_VKCHECK(vkCreateFramebuffer(RendererContext.LogicalDevice, &framebufferCreate, NoAllocator, &ForwardPass.FrameBuffer[fb].Framebuffer));
            }

            // Create our imgui pass framebuffers
            {
                VkImageView images[] =
                    {
                        RendererContext.Present.FrameBuffer[fb].SwapchainImageView,
                    };

                VkFramebufferCreateInfo framebufferCreate = {};
                framebufferCreate.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                framebufferCreate.attachmentCount = arrayCount(images);
                framebufferCreate.width = RendererContext.Present.SurfaceExtents.width;
                framebufferCreate.height = RendererContext.Present.SurfaceExtents.height;
                framebufferCreate.layers = 1;
                framebufferCreate.renderPass = ImGuiPass.RenderPass;
                framebufferCreate.pAttachments = images;

                IB_VKCHECK(vkCreateFramebuffer(RendererContext.LogicalDevice, &framebufferCreate, NoAllocator, &ImGuiPass.FrameBuffer[fb].Framebuffer));
            }
        }
    }
} // namespace

namespace IB
{
    // Forward declared from IBPlatformWin32.cpp
    namespace Win32
    {
        void getWindowHandleAndInstance(Window::Handle handle, HWND *window, HINSTANCE *instance);
        void getPlatformMessageData(WindowMessagePlatformData const *platformData, HWND *hwnd, UINT *msg, WPARAM *wparam, LPARAM *lparam);
    } // namespace Win32
} // namespace IB

namespace
{
    VkSurfaceKHR createSurface(IB::Window::Handle window)
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
    void initRenderer(RendererDesc const &desc)
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

        RendererContext.Present.Surface = createSurface(desc.Window);

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
                RendererContext.PhysicalDevice = physicalDevices[physicalDeviceIndex];
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
                physicalDeviceFeatures.multiDrawIndirect = VK_TRUE;
                physicalDeviceFeatures.drawIndirectFirstInstance = VK_TRUE;

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

                IB_VKCHECK(vkCreateDevice(RendererContext.PhysicalDevice, &deviceCreateInfo, NoAllocator, &RendererContext.LogicalDevice));
                for (uint32_t i = 0; i < Queue::Count; i++)
                {
                    vkGetDeviceQueue(RendererContext.LogicalDevice, RendererContext.Present.Queues[i].Index, 0, &RendererContext.Present.Queues[i].Queue);
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

            IB_VKCHECK(vkCreateDescriptorPool(RendererContext.LogicalDevice, &descriptorPoolCreate, NoAllocator, &RendererContext.DescriptorPool));
        }

        {
            VkPipelineCacheCreateInfo pipelineCacheCreate = {};
            pipelineCacheCreate.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

            IB_VKCHECK(vkCreatePipelineCache(RendererContext.LogicalDevice, &pipelineCacheCreate, NoAllocator, &RendererContext.PipelineCache));
        }

        for (uint32_t i = 0; i < Queue::Count; i++)
        {
            VkCommandPoolCreateInfo commandPoolCreateInfo = {};
            commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            commandPoolCreateInfo.queueFamilyIndex = RendererContext.Present.Queues[i].Index;

            IB_VKCHECK(vkCreateCommandPool(RendererContext.LogicalDevice, &commandPoolCreateInfo, NoAllocator, &RendererContext.Present.Queues[i].CommandPool));
        }

        {
            VkFenceCreateInfo fenceCreateInfo = {};
            fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            IB_VKCHECK(vkCreateFence(RendererContext.LogicalDevice, &fenceCreateInfo, NoAllocator, &RendererContext.Present.ImmediateFence));
        
            for (uint32_t i = 0; i < MaxStagingFenceCount; i++)
            {
                IB_VKCHECK(vkCreateFence(RendererContext.LogicalDevice, &fenceCreateInfo, NoAllocator, &Staging.Fences[i]));
            }
        }

        createAllocator(&Allocator);

        {
            VkSemaphoreCreateInfo semaphoreCreateInfo = {};
            semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            for (uint32_t i = 0; i < FrameBufferCount; i++)
            {
                IB_VKCHECK(vkCreateSemaphore(RendererContext.LogicalDevice, &semaphoreCreateInfo, NoAllocator, &RendererContext.Present.FrameBuffer[i].AcquireSemaphore));
            }
        }

        {
            constexpr uint32_t maxSurfaceFormatCount = 100;
            uint32_t surfaceFormatCount;
            VkSurfaceFormatKHR surfaceFormats[maxSurfaceFormatCount];

            IB_VKCHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(RendererContext.PhysicalDevice, RendererContext.Present.Surface, &surfaceFormatCount, NULL));
            IB_ASSERT(surfaceFormatCount > 0, "Failed to find any surface formats.");
            surfaceFormatCount = surfaceFormatCount < maxSurfaceFormatCount ? surfaceFormatCount : maxSurfaceFormatCount;
            IB_VKCHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(RendererContext.PhysicalDevice, RendererContext.Present.Surface, &surfaceFormatCount, surfaceFormats));

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
            IB_VKCHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(RendererContext.PhysicalDevice, RendererContext.Present.Surface, &surfaceCapabilities));

            IB_ASSERT(surfaceCapabilities.currentExtent.width != UINT32_MAX, "Surface has invalid width.");
            RendererContext.Present.SurfaceExtents = surfaceCapabilities.currentExtent;
        }

        RendererContext.Present.PresentMode = VK_PRESENT_MODE_FIFO_KHR;

        constexpr bool useVSync = true;
        if (!useVSync)
        {
            constexpr uint32_t maxPresentModes = 100;
            uint32_t presentModeCount;
            VkPresentModeKHR presentModes[maxPresentModes];

            IB_VKCHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(RendererContext.PhysicalDevice, RendererContext.Present.Surface, &presentModeCount, NULL));
            IB_ASSERT(presentModeCount > 0, "Failed to find a present mode.");
            presentModeCount = presentModeCount < maxPresentModes ? presentModeCount : maxPresentModes;
            IB_VKCHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(RendererContext.PhysicalDevice, RendererContext.Present.Surface, &presentModeCount, presentModes));

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
            // Forward Renderpass
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

                VkAttachmentDescription attachmentDescriptions[] = {output, depth};

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

                VkSubpassDescription subpasses[] = {renderSubpass};

                VkRenderPassCreateInfo createRenderPass = {};
                createRenderPass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
                createRenderPass.attachmentCount = arrayCount(attachmentDescriptions);
                createRenderPass.subpassCount = arrayCount(subpasses);
                createRenderPass.dependencyCount = 0;
                createRenderPass.pAttachments = attachmentDescriptions;
                createRenderPass.pSubpasses = subpasses;

                IB_VKCHECK(vkCreateRenderPass(RendererContext.LogicalDevice, &createRenderPass, NoAllocator, &ForwardPass.RenderPass));
            }

            // ImGui Renderpass
            {
                VkAttachmentDescription output = {};
                output.samples = VK_SAMPLE_COUNT_1_BIT;
                output.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                output.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                output.initialLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                output.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                output.format = RendererContext.Present.SurfaceFormat.format;

                VkAttachmentDescription attachmentDescriptions[] = {output};

                // Attachments
                VkAttachmentReference outputAttachment = {};
                outputAttachment.attachment = 0;
                outputAttachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                VkSubpassDescription renderSubpass = {};
                renderSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
                renderSubpass.colorAttachmentCount = 1;
                renderSubpass.pColorAttachments = &outputAttachment;

                VkSubpassDescription subpasses[] = {renderSubpass};

                VkRenderPassCreateInfo createRenderPass = {};
                createRenderPass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
                createRenderPass.attachmentCount = arrayCount(attachmentDescriptions);
                createRenderPass.subpassCount = arrayCount(subpasses);
                createRenderPass.dependencyCount = 0;
                createRenderPass.pAttachments = attachmentDescriptions;
                createRenderPass.pSubpasses = subpasses;

                IB_VKCHECK(vkCreateRenderPass(RendererContext.LogicalDevice, &createRenderPass, NoAllocator, &ImGuiPass.RenderPass));
            }

            buildSurfaceSwapchain(RendererContext.Present.SurfaceExtents);

            {
                VkSemaphoreCreateInfo semaphoreCreateInfo = {};
                semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

                for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
                {
                    IB_VKCHECK(vkCreateSemaphore(RendererContext.LogicalDevice, &semaphoreCreateInfo, NoAllocator, &RendererContext.Present.FrameBuffer[fb].FinishedSemaphore));
                }
            }

            for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
            {
                VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
                commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                commandBufferAllocateInfo.commandBufferCount = 1;
                commandBufferAllocateInfo.commandPool = RendererContext.Present.Queues[Queue::Graphics].CommandPool;

                IB_VKCHECK(vkAllocateCommandBuffers(RendererContext.LogicalDevice, &commandBufferAllocateInfo, &RendererContext.Present.FrameBuffer[fb].PrimaryCommandBuffer));
            }

            {
                VkFenceCreateInfo fenceCreateInfo = {};
                fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

                for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
                {
                    IB_VKCHECK(vkCreateFence(RendererContext.LogicalDevice, &fenceCreateInfo, NoAllocator, &RendererContext.Present.FrameBuffer[fb].FinishedFence));
                }
            }
        }

        // Forward pass constant data
        {
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

                VkDescriptorSetLayoutBinding fragBinding2 = {};
                fragBinding2.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                fragBinding2.binding = 3;
                fragBinding2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                fragBinding2.descriptorCount = 1;

                VkDescriptorSetLayoutBinding fragBinding3 = {};
                fragBinding3.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                fragBinding3.binding = 4;
                fragBinding3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                fragBinding3.descriptorCount = 1;

                VkDescriptorSetLayoutBinding perFrameBufferBinding = {};
                perFrameBufferBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
                perFrameBufferBinding.binding = 5;
                perFrameBufferBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                perFrameBufferBinding.descriptorCount = 1;

                VkDescriptorSetLayoutBinding layoutBindings[] = {vertexBinding0, fragBinding0, fragBinding1, fragBinding2, fragBinding3, perFrameBufferBinding};

                VkDescriptorBindingFlagsEXT bindFlags[] = {0, 0, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT, 0, 0, 0};
                VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extendedInfo = {};
                extendedInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT;
                extendedInfo.bindingCount = arrayCount(bindFlags);
                extendedInfo.pBindingFlags = bindFlags;
                static_assert(arrayCount(bindFlags) == arrayCount(layoutBindings), "Binding counts must match!");

                VkDescriptorSetLayoutCreateInfo createLayout = {};
                createLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                createLayout.pNext = &extendedInfo;
                createLayout.bindingCount = arrayCount(layoutBindings);
                createLayout.pBindings = layoutBindings;
                IB_VKCHECK(vkCreateDescriptorSetLayout(RendererContext.LogicalDevice, &createLayout, NoAllocator, &ForwardPass.ShaderLayout));
            }

            {
                VkPushConstantRange pushConstant = {};
                pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
                pushConstant.offset = 0;
                pushConstant.size = 256;

                VkPushConstantRange pushConstantRanges[] = { pushConstant };

                VkDescriptorSetLayout layouts[] =
                    {
                        ForwardPass.ShaderLayout,
                    };

                VkPipelineLayoutCreateInfo pipelineLayoutCreate = {};
                pipelineLayoutCreate.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
                pipelineLayoutCreate.setLayoutCount = arrayCount(layouts);
                pipelineLayoutCreate.pSetLayouts = layouts;
                pipelineLayoutCreate.pushConstantRangeCount = arrayCount(pushConstantRanges);
                pipelineLayoutCreate.pPushConstantRanges = pushConstantRanges;

                IB_VKCHECK(vkCreatePipelineLayout(RendererContext.LogicalDevice, &pipelineLayoutCreate, NoAllocator, &ForwardPass.PipelineLayout));
            }
        }

        // Staging Buffer
        {
            VkBufferCreateInfo bufferCreate = {};
            bufferCreate.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreate.size = StagingSize;
            bufferCreate.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

            IB_VKCHECK(vkCreateBuffer(RendererContext.LogicalDevice, &bufferCreate, NoAllocator, &Staging.Buffer));

            VkMemoryRequirements memoryRequirements;
            vkGetBufferMemoryRequirements(RendererContext.LogicalDevice, Staging.Buffer, &memoryRequirements);

            AllocationRequest request;
            request.Alignment = memoryRequirements.alignment;
            request.Size = StagingSize;
            request.LogicalDevice = RendererContext.LogicalDevice;
            request.PhysicalDevice = RendererContext.PhysicalDevice;
            request.RequiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            request.PreferedFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            request.TypeBits = memoryRequirements.memoryTypeBits;
            request.Category = AllocationCategory::TransferSource;

            Staging.Allocation = allocateDeviceMemory(&Allocator, request);
            IB_VKCHECK(vkBindBufferMemory(RendererContext.LogicalDevice, Staging.Buffer, Staging.Allocation.Memory, Staging.Allocation.Offset));

            Staging.MappedMemory = mapAllocation(RendererContext.LogicalDevice, &Allocator, Staging.Allocation);
        }

        // Indirect Draw Buffer
        for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
        {
            VkBufferCreateInfo bufferCreate = {};
            bufferCreate.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreate.size = MaxIndirectDraws * sizeof(VkDrawIndexedIndirectCommand);
            bufferCreate.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

            IB_VKCHECK(vkCreateBuffer(RendererContext.LogicalDevice, &bufferCreate, NoAllocator, &IndirectDraw.FrameBuffer[fb].Buffer));

            VkMemoryRequirements memoryRequirements;
            vkGetBufferMemoryRequirements(RendererContext.LogicalDevice, IndirectDraw.FrameBuffer[fb].Buffer, &memoryRequirements);

            AllocationRequest request;
            request.Alignment = memoryRequirements.alignment;
            request.Size = MaxIndirectDraws * sizeof(VkDrawIndexedIndirectCommand);
            request.LogicalDevice = RendererContext.LogicalDevice;
            request.PhysicalDevice = RendererContext.PhysicalDevice;
            request.RequiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            request.PreferedFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            request.TypeBits = memoryRequirements.memoryTypeBits;
            request.Category = AllocationCategory::Misc;

            IndirectDraw.FrameBuffer[fb].Allocation = allocateDeviceMemory(&Allocator, request);
            IB_VKCHECK(vkBindBufferMemory(RendererContext.LogicalDevice, IndirectDraw.FrameBuffer[fb].Buffer, IndirectDraw.FrameBuffer[fb].Allocation.Memory, IndirectDraw.FrameBuffer[fb].Allocation.Offset));

            IndirectDraw.FrameBuffer[fb].MappedMemory = reinterpret_cast<uint8_t*>(mapAllocation(RendererContext.LogicalDevice, &Allocator, IndirectDraw.FrameBuffer[fb].Allocation));
        }

        {
            VkBufferCreateInfo bufferCreate = {};
            bufferCreate.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreate.size = FrameBufferCount * MaxPerFrameDrawDataSize;
            bufferCreate.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

            IB_VKCHECK(vkCreateBuffer(RendererContext.LogicalDevice, &bufferCreate, NoAllocator, &IndirectDraw.PerFrameData.Buffer));

            VkMemoryRequirements memoryRequirements;
            vkGetBufferMemoryRequirements(RendererContext.LogicalDevice, IndirectDraw.PerFrameData.Buffer, &memoryRequirements);

            AllocationRequest request;
            request.Alignment = memoryRequirements.alignment;
            request.Size = FrameBufferCount * MaxPerFrameDrawDataSize;
            request.LogicalDevice = RendererContext.LogicalDevice;
            request.PhysicalDevice = RendererContext.PhysicalDevice;
            request.RequiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            request.PreferedFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            request.TypeBits = memoryRequirements.memoryTypeBits;
            request.Category = AllocationCategory::Misc;

            IndirectDraw.PerFrameData.Allocation = allocateDeviceMemory(&Allocator, request);
            IB_VKCHECK(vkBindBufferMemory(RendererContext.LogicalDevice, IndirectDraw.PerFrameData.Buffer, IndirectDraw.PerFrameData.Allocation.Memory, IndirectDraw.PerFrameData.Allocation.Offset));

            IndirectDraw.PerFrameData.MappedMemory = reinterpret_cast<uint8_t *>(mapAllocation(RendererContext.LogicalDevice, &Allocator, IndirectDraw.PerFrameData.Allocation));
        }

        // Mesh buffer
        {
            for (uint32_t i = 0; i < MaxMeshCount; i++)
            {
                Geometry.FreeIndices[i] = i;
            }
            Geometry.FreeIndexCount = MaxMeshCount;

            VkBufferCreateInfo bufferCreate = {};
            bufferCreate.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreate.size = MeshBufferSize;
            bufferCreate.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT; // buffers created through create buffer can always be transfered to

            VkBuffer *buffer = &Geometry.MeshDataBuffers;
            IB_VKCHECK(vkCreateBuffer(RendererContext.LogicalDevice, &bufferCreate, NoAllocator, buffer));

            VkMemoryRequirements memoryRequirements;
            vkGetBufferMemoryRequirements(RendererContext.LogicalDevice, *buffer, &memoryRequirements);

            AllocationRequest request;
            request.Alignment = memoryRequirements.alignment;
            request.Size = MeshBufferSize;
            request.LogicalDevice = RendererContext.LogicalDevice;
            request.PhysicalDevice = RendererContext.PhysicalDevice;
            request.RequiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            request.PreferedFlags = 0;
            request.TypeBits = memoryRequirements.memoryTypeBits;
            request.Category = AllocationCategory::Mesh;

            Allocation *allocation = &Geometry.Allocation;
            *allocation = allocateDeviceMemory(&Allocator, request);

            IB_VKCHECK(vkBindBufferMemory(RendererContext.LogicalDevice, *buffer, allocation->Memory, allocation->Offset));
        }

        // Material buffer
        {
            VkBufferCreateInfo bufferCreate = {};
            bufferCreate.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreate.size = MaxMaterialInstanceCount * sizeof(ForwardMaterial);
            bufferCreate.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT; // buffers created through create buffer can always be transfered to

            VkBuffer *buffer = &ForwardPass.MaterialBuffer;
            IB_VKCHECK(vkCreateBuffer(RendererContext.LogicalDevice, &bufferCreate, NoAllocator, buffer));

            VkMemoryRequirements memoryRequirements;
            vkGetBufferMemoryRequirements(RendererContext.LogicalDevice, *buffer, &memoryRequirements);

            AllocationRequest request;
            request.Alignment = memoryRequirements.alignment;
            request.Size = MaxMaterialInstanceCount * sizeof(ForwardMaterial);
            request.LogicalDevice = RendererContext.LogicalDevice;
            request.PhysicalDevice = RendererContext.PhysicalDevice;
            request.RequiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            request.PreferedFlags = 0;
            request.TypeBits = memoryRequirements.memoryTypeBits;
            request.Category = AllocationCategory::Misc;

            Allocation *allocation = &ForwardPass.MaterialBufferAllocation;
            *allocation = allocateDeviceMemory(&Allocator, request);

            IB_VKCHECK(vkBindBufferMemory(RendererContext.LogicalDevice, *buffer, allocation->Memory, allocation->Offset));
        }

        // Lights buffer
        {
            VkBufferCreateInfo bufferCreate = {};
            bufferCreate.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreate.size = MaxLightCount * sizeof(DirectionalLight);
            bufferCreate.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT; // buffers created through create buffer can always be transfered to

            VkBuffer *buffer = &Lighting.LightBuffer;
            IB_VKCHECK(vkCreateBuffer(RendererContext.LogicalDevice, &bufferCreate, NoAllocator, buffer));

            VkMemoryRequirements memoryRequirements;
            vkGetBufferMemoryRequirements(RendererContext.LogicalDevice, *buffer, &memoryRequirements);

            AllocationRequest request;
            request.Alignment = memoryRequirements.alignment;
            request.Size = MaxLightCount * sizeof(DirectionalLight);
            request.LogicalDevice = RendererContext.LogicalDevice;
            request.PhysicalDevice = RendererContext.PhysicalDevice;
            request.RequiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            request.PreferedFlags = 0;
            request.TypeBits = memoryRequirements.memoryTypeBits;
            request.Category = AllocationCategory::Misc;

            Allocation *allocation = &Lighting.LightBufferAllocation;
            *allocation = allocateDeviceMemory(&Allocator, request);

            IB_VKCHECK(vkBindBufferMemory(RendererContext.LogicalDevice, *buffer, allocation->Memory, allocation->Offset));
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

            IB_VKCHECK(vkCreateSampler(RendererContext.LogicalDevice, &samplerCreate, NoAllocator, &ForwardPass.Sampler));
        }

        // Write to our template descriptor set
        {
            VkDescriptorSetAllocateInfo descriptorSetAlloc = {};
            descriptorSetAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptorSetAlloc.descriptorPool = RendererContext.DescriptorPool;
            descriptorSetAlloc.descriptorSetCount = 1;
            descriptorSetAlloc.pSetLayouts = &ForwardPass.ShaderLayout;
            IB_VKCHECK(vkAllocateDescriptorSets(
                RendererContext.LogicalDevice, &descriptorSetAlloc,
                &ForwardPass.ShaderDescriptor));

            VkDescriptorBufferInfo meshBufferInfo = {};
            meshBufferInfo.buffer = Geometry.MeshDataBuffers;
            meshBufferInfo.offset = 0;
            meshBufferInfo.range = MeshBufferSize;

            VkWriteDescriptorSet writeGeometry = {};
            writeGeometry.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeGeometry.dstSet = ForwardPass.ShaderDescriptor;
            writeGeometry.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writeGeometry.dstBinding = 0;
            writeGeometry.descriptorCount = 1;
            writeGeometry.pBufferInfo = &meshBufferInfo;

            VkDescriptorImageInfo samplerInfo = {};
            samplerInfo.sampler = ForwardPass.Sampler;

            VkWriteDescriptorSet writeSamplerSet = {};
            writeSamplerSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeSamplerSet.dstSet = ForwardPass.ShaderDescriptor;
            writeSamplerSet.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
            writeSamplerSet.dstBinding = 1;
            writeSamplerSet.descriptorCount = 1;
            writeSamplerSet.pImageInfo = &samplerInfo;

            VkDescriptorBufferInfo materialBufferInfo = {};
            materialBufferInfo.buffer = ForwardPass.MaterialBuffer;
            materialBufferInfo.offset = 0;
            materialBufferInfo.range = MaxMaterialInstanceCount * sizeof(ForwardMaterial);

            VkWriteDescriptorSet writeMaterials = {};
            writeMaterials.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeMaterials.dstSet = ForwardPass.ShaderDescriptor;
            writeMaterials.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writeMaterials.dstBinding = 3;
            writeMaterials.descriptorCount = 1;
            writeMaterials.pBufferInfo = &materialBufferInfo;

            VkDescriptorBufferInfo lightBufferInfo = {};
            lightBufferInfo.buffer = Lighting.LightBuffer;
            lightBufferInfo.offset = 0;
            lightBufferInfo.range = MaxLightCount * sizeof(DirectionalLight);

            VkWriteDescriptorSet writeLights = {};
            writeLights.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeLights.dstSet = ForwardPass.ShaderDescriptor;
            writeLights.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writeLights.dstBinding = 4;
            writeLights.descriptorCount = 1;
            writeLights.pBufferInfo = &lightBufferInfo;

            VkDescriptorBufferInfo perFrameBufferInfo = {};
            perFrameBufferInfo.buffer = IndirectDraw.PerFrameData.Buffer;
            perFrameBufferInfo.offset = 0;
            perFrameBufferInfo.range = FrameBufferCount * MaxPerFrameDrawDataSize;

            VkWriteDescriptorSet writePerFrame = {};
            writePerFrame.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writePerFrame.dstSet = ForwardPass.ShaderDescriptor;
            writePerFrame.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writePerFrame.dstBinding = 5;
            writePerFrame.descriptorCount = 1;
            writePerFrame.pBufferInfo = &perFrameBufferInfo;

            VkWriteDescriptorSet writeDescriptorSets[] = {writeSamplerSet, writeGeometry, writeMaterials, writeLights, writePerFrame};
            vkUpdateDescriptorSets(RendererContext.LogicalDevice, arrayCount(writeDescriptorSets), writeDescriptorSets, 0, NULL);
        }

        // ImGui
        {
            ImGui::CreateContext();

            HWND windowHandle;
            HINSTANCE instanceHandle;
            IB::Win32::getWindowHandleAndInstance(desc.Window, &windowHandle, &instanceHandle);
            ImGui_ImplWin32_Init(windowHandle);

            ImGui_ImplVulkan_InitInfo initInfo = {};
            initInfo.Instance = RendererContext.VulkanInstance;
            initInfo.PhysicalDevice = RendererContext.PhysicalDevice;
            initInfo.Device = RendererContext.LogicalDevice;
            initInfo.QueueFamily = RendererContext.Present.Queues[Queue::Graphics].Index;
            initInfo.Queue = RendererContext.Present.Queues[Queue::Graphics].Queue;
            initInfo.PipelineCache = RendererContext.PipelineCache;
            initInfo.DescriptorPool = RendererContext.DescriptorPool;
            initInfo.Allocator = NoAllocator;
            initInfo.MinImageCount = FrameBufferCount;
            initInfo.ImageCount = FrameBufferCount;
            initInfo.CheckVkResultFn = &VulkanCheck;
            ImGui_ImplVulkan_Init(&initInfo, ImGuiPass.RenderPass);

            // Upload ImGui font
            {
                VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
                commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                commandBufferAllocateInfo.commandBufferCount = 1;
                commandBufferAllocateInfo.commandPool = RendererContext.Present.Queues[Queue::Graphics].CommandPool;

                VkCommandBuffer commandBuffer;
                IB_VKCHECK(vkAllocateCommandBuffers(RendererContext.LogicalDevice, &commandBufferAllocateInfo, &commandBuffer));

                VkCommandBufferBeginInfo beginBufferInfo = {};
                beginBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginBufferInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

                IB_VKCHECK(vkBeginCommandBuffer(commandBuffer, &beginBufferInfo));

                ImGui_ImplVulkan_CreateFontsTexture(commandBuffer);

                VkSubmitInfo submitInfo = {};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &commandBuffer;
                IB_VKCHECK(vkEndCommandBuffer(commandBuffer));

                IB_VKCHECK(vkQueueSubmit(RendererContext.Present.Queues[Queue::Graphics].Queue, 1, &submitInfo, RendererContext.Present.ImmediateFence));
                IB_VKCHECK(vkWaitForFences(RendererContext.LogicalDevice, 1, &RendererContext.Present.ImmediateFence, VK_TRUE, UINT64_MAX));
                IB_VKCHECK(vkResetFences(RendererContext.LogicalDevice, 1, &RendererContext.Present.ImmediateFence));

                vkFreeCommandBuffers(RendererContext.LogicalDevice, RendererContext.Present.Queues[Queue::Graphics].CommandPool, 1, &commandBuffer);
                ImGui_ImplVulkan_DestroyFontUploadObjects();
            }
        }
    }

    void killRenderer()
    {
        vkDeviceWaitIdle(RendererContext.LogicalDevice);

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
        {
            vkDestroySemaphore(RendererContext.LogicalDevice, RendererContext.Present.FrameBuffer[fb].AcquireSemaphore, NoAllocator);
            vkDestroySemaphore(RendererContext.LogicalDevice, RendererContext.Present.FrameBuffer[fb].FinishedSemaphore, NoAllocator);
            vkDestroyImageView(RendererContext.LogicalDevice, RendererContext.Present.FrameBuffer[fb].SwapchainImageView, NoAllocator);
            vkDestroyFramebuffer(RendererContext.LogicalDevice, ForwardPass.FrameBuffer[fb].Framebuffer, NoAllocator);
            vkDestroyFramebuffer(RendererContext.LogicalDevice, ImGuiPass.FrameBuffer[fb].Framebuffer, NoAllocator);
            vkDestroyFence(RendererContext.LogicalDevice, RendererContext.Present.FrameBuffer[fb].FinishedFence, NoAllocator);
            vkDestroyImage(RendererContext.LogicalDevice, ForwardPass.FrameBuffer[fb].DepthImage, NoAllocator);
            vkDestroyImageView(RendererContext.LogicalDevice, ForwardPass.FrameBuffer[fb].DepthImageView, NoAllocator);
            freeDeviceMemory(&Allocator, ForwardPass.FrameBuffer[fb].DepthImageAllocation);
        }

        vkDestroyBuffer(RendererContext.LogicalDevice, Geometry.MeshDataBuffers, NoAllocator);
        freeDeviceMemory(&Allocator, Geometry.Allocation);

        for (uint32_t i = 0; i < RendererContext.Textures.ImageCount; i++)
        {
            vkDestroyImage(RendererContext.LogicalDevice, RendererContext.Textures.Images[i].Image, NoAllocator);
            vkDestroyImageView(RendererContext.LogicalDevice, RendererContext.Textures.Images[i].ImageView, NoAllocator);
            freeDeviceMemory(&Allocator, RendererContext.Textures.Images[i].Allocation);
        }

        vkDestroyDescriptorSetLayout(RendererContext.LogicalDevice, ForwardPass.ShaderLayout, NoAllocator);

        vkDestroyPipelineLayout(RendererContext.LogicalDevice, ForwardPass.PipelineLayout, NoAllocator);

        for (uint32_t i = 0; i < arrayCount(ForwardPass.Pipelines); i++)
        {
            vkDestroyPipeline(RendererContext.LogicalDevice, ForwardPass.Pipelines[i], NoAllocator);
        }

        vkDestroySampler(RendererContext.LogicalDevice, ForwardPass.Sampler, NoAllocator);
        vkDestroyBuffer(RendererContext.LogicalDevice, ForwardPass.MaterialBuffer, NoAllocator);
        freeDeviceMemory(&Allocator, ForwardPass.MaterialBufferAllocation);

        vkDestroyBuffer(RendererContext.LogicalDevice, Lighting.LightBuffer, NoAllocator);
        freeDeviceMemory(&Allocator, Lighting.LightBufferAllocation);

        vkDestroyBuffer(RendererContext.LogicalDevice, Staging.Buffer, NoAllocator);
        freeDeviceMemory(&Allocator, Staging.Allocation);

        for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
        {
            vkDestroyBuffer(RendererContext.LogicalDevice, IndirectDraw.FrameBuffer[fb].Buffer, NoAllocator);
            freeDeviceMemory(&Allocator, IndirectDraw.FrameBuffer[fb].Allocation);
        }

        vkDestroyBuffer(RendererContext.LogicalDevice, IndirectDraw.PerFrameData.Buffer, NoAllocator);
        freeDeviceMemory(&Allocator, IndirectDraw.PerFrameData.Allocation);

        for (uint32_t i = 0; i < Queue::Count; i++)
        {
            vkDestroyCommandPool(RendererContext.LogicalDevice, RendererContext.Present.Queues[i].CommandPool, NoAllocator);
        }

        vkDestroyFence(RendererContext.LogicalDevice, RendererContext.Present.ImmediateFence, NoAllocator);

        for (uint32_t i = 0; i < MaxStagingFenceCount; i++)
        {
            vkDestroyFence(RendererContext.LogicalDevice, Staging.Fences[i], NoAllocator);
        }

        vkDestroyRenderPass(RendererContext.LogicalDevice, ForwardPass.RenderPass, NoAllocator);
        vkDestroyRenderPass(RendererContext.LogicalDevice, ImGuiPass.RenderPass, NoAllocator);
        vkDestroySwapchainKHR(RendererContext.LogicalDevice, RendererContext.Present.Swapchain, NoAllocator);
        vkDestroyPipelineCache(RendererContext.LogicalDevice, RendererContext.PipelineCache, NoAllocator);
        vkDestroyDescriptorPool(RendererContext.LogicalDevice, RendererContext.DescriptorPool, NoAllocator);
        vkDestroySurfaceKHR(RendererContext.VulkanInstance, RendererContext.Present.Surface, NoAllocator);

        destroyAllocator(RendererContext.LogicalDevice, &Allocator);
        vkDestroyDevice(RendererContext.LogicalDevice, NoAllocator);
        vkDestroyInstance(RendererContext.VulkanInstance, NoAllocator);
    }

    void initForwardPass(ForwardPassDesc const &desc)
    {
        // Queue up our pipelines for cleanup by our rendering thread
        for (uint32_t i = 0; i < arrayCount(ForwardPass.Pipelines); i++)
        {
            if (ForwardPass.Pipelines[i] != VK_NULL_HANDLE)
            {
                for (uint32_t pip = 0; pip < MaxPipelineCleanups; pip++)
                {
                    if (RendererContext.Cleanup.Pipelines[pip] == VK_NULL_HANDLE)
                    {
                        RendererContext.Cleanup.Pipelines[pip] = ForwardPass.Pipelines[i];
                        break;
                    }
                }
            }
        }

        VkShaderModule vertexShader = {};

        VkShaderModuleCreateInfo createVShader = {};
        createVShader.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createVShader.pCode = (const uint32_t *)desc.VShader;
        createVShader.codeSize = desc.VShaderSize;
        IB_VKCHECK(vkCreateShaderModule(RendererContext.LogicalDevice, &createVShader, NoAllocator, &vertexShader));

        VkShaderModule fragShader = {};

        VkShaderModuleCreateInfo createFShader = {};
        createFShader.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createFShader.pCode = (const uint32_t *)desc.FShader;
        createFShader.codeSize = desc.FShaderSize;
        IB_VKCHECK(vkCreateShaderModule(RendererContext.LogicalDevice, &createFShader, NoAllocator, &fragShader));

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

        VkPipelineRasterizationStateCreateInfo rasterizationCreateNoCull = rasterizationCreate;
        rasterizationCreateNoCull.cullMode = VK_CULL_MODE_NONE;

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
        vertexStage.module = vertexShader;
        vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;

        VkPipelineShaderStageCreateInfo fragStage = {};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.pName = "fragMain";
        fragStage.module = fragShader;
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
        baseGraphicsPipelineCreate.layout = ForwardPass.PipelineLayout;
        baseGraphicsPipelineCreate.renderPass = ForwardPass.RenderPass;
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

        VkGraphicsPipelineCreateInfo debugGeoPipeline = baseGraphicsPipelineCreate;
        debugGeoPipeline.pRasterizationState = &rasterizationCreateNoCull;
        debugGeoPipeline.subpass = 0;

        VkGraphicsPipelineCreateInfo pipelineCreateInfo[] = {subpass0Pipeline, debugGeoPipeline};
        static_assert(arrayCount(pipelineCreateInfo) == ForwardPass::SubpassCount, "Number of created pipelines is not correct.");
        VkPipeline createdPipelines[ForwardPass::SubpassCount];
        IB_VKCHECK(vkCreateGraphicsPipelines(RendererContext.LogicalDevice, RendererContext.PipelineCache, arrayCount(pipelineCreateInfo), pipelineCreateInfo, NoAllocator, createdPipelines));

        // Write our pipelines after our call due to the fact that we expect this function to be called from another thread
        // And we'd like to assure that our writes are done at the right time.
        threadRelease();
        for (uint32_t i = 0; i < ForwardPass::SubpassCount; i++)
        {
            ForwardPass.Pipelines[i] = createdPipelines[i];
        }

        vkDestroyShaderModule(RendererContext.LogicalDevice, vertexShader, NoAllocator);
        vkDestroyShaderModule(RendererContext.LogicalDevice, fragShader, NoAllocator);
    }

    namespace
    {
        void writeMesh(uint32_t meshIndex, MeshDesc const& desc)
        {
            uint32_t vertexSize = desc.Vertices.Count * sizeof(Vertex);
            uint32_t indexSize = desc.Indices.Count * sizeof(uint16_t);

            VkCommandBuffer commandBuffer;

            // Begin our command buffer
            {
                VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
                commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                commandBufferAllocateInfo.commandBufferCount = 1;
                commandBufferAllocateInfo.commandPool = RendererContext.Present.Queues[Queue::Transfer].CommandPool;

                IB_VKCHECK(vkAllocateCommandBuffers(RendererContext.LogicalDevice, &commandBufferAllocateInfo, &commandBuffer));

                VkCommandBufferBeginInfo beginBufferInfo = {};
                beginBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginBufferInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

                IB_VKCHECK(vkBeginCommandBuffer(commandBuffer, &beginBufferInfo));
            }

            uint32_t chunkOffset = Geometry.NextOffset % MeshBufferChunkSize;
            // We're out of memory. Try to pack the empty space caused by freed geometry and try again
            if (chunkOffset + vertexSize + indexSize > MeshBufferChunkSize)
            {
                uint32_t chunkIndex = Geometry.NextOffset / MeshBufferChunkSize;
                chunkIndex = (chunkIndex + 1) % MeshBufferChunkCount;
                Geometry.NextOffset = chunkIndex * MeshBufferChunkSize;

                VkBufferCopy copyRegions[MaxMeshCount];
                uint32_t copyCount = 0;
                for (uint32_t i = 0; i < MaxMeshCount; i++)
                {
                    if (Geometry.MeshAllocations[i])
                    {
                        // Vertex and indices are packed together
                        VkBufferCopy meshCopy = {};
                        meshCopy.srcOffset = Geometry.Meshes[i].VertexOffset;
                        meshCopy.dstOffset = Geometry.NextOffset;
                        meshCopy.size = Geometry.Meshes[i].VertexSize + Geometry.Meshes[i].IndexCount * sizeof(uint16_t);
                        copyRegions[copyCount++] = meshCopy;

                        // Relocate our mesh data
                        Geometry.Meshes[i].VertexOffset = Geometry.NextOffset;
                        Geometry.Meshes[i].IndexOffset = Geometry.NextOffset + Geometry.Meshes[i].VertexSize;

                        Geometry.NextOffset += Geometry.Meshes[i].VertexSize + Geometry.Meshes[i].IndexCount * sizeof(uint16_t);
                        uint32_t alignmentBump = Geometry.NextOffset % alignof(Vertex);
                        Geometry.NextOffset += alignmentBump == 0 ? 0 : alignof(Vertex) - alignmentBump;
                    }
                }

                if (copyCount > 0)
                {
                    vkCmdCopyBuffer(commandBuffer, Geometry.MeshDataBuffers, Geometry.MeshDataBuffers, copyCount, copyRegions);
                }
            }

            IB_ASSERT((Geometry.NextOffset % MeshBufferChunkSize) + vertexSize + indexSize <= MeshBufferChunkSize, "After packing we're still out of memory. Our buffer is likely not large enough.");

            // Allocate our mesh
            {
                Geometry.Meshes[meshIndex].VertexSize = vertexSize;
                Geometry.Meshes[meshIndex].VertexOffset = Geometry.NextOffset;
                Geometry.Meshes[meshIndex].IndexOffset = vertexSize + Geometry.NextOffset;
                Geometry.Meshes[meshIndex].IndexCount = desc.Indices.Count;
                Geometry.MeshAllocations[meshIndex] = true;

                Geometry.NextOffset += vertexSize + indexSize;
                uint32_t alignmentBump = Geometry.NextOffset % alignof(Vertex);
                Geometry.NextOffset += alignmentBump == 0 ? 0 : alignof(Vertex) - alignmentBump;
            }

            // Staging
            StagingBuffer staging = requestStagingBuffer(&Staging, indexSize + vertexSize);
            memcpy(staging.Memory, desc.Vertices.Data, vertexSize);
            memcpy(reinterpret_cast<uint8_t*>(staging.Memory) + vertexSize, desc.Indices.Data, indexSize);

            VkBufferCopy meshCopy = {};
            meshCopy.srcOffset = staging.Offset;
            meshCopy.dstOffset = Geometry.Meshes[meshIndex].VertexOffset;
            meshCopy.size = vertexSize + indexSize;

            vkCmdCopyBuffer(commandBuffer, staging.Buffer, Geometry.MeshDataBuffers, 1, &meshCopy);

            // End our command buffer
            {
                IB_VKCHECK(vkEndCommandBuffer(commandBuffer));
                VkSubmitInfo submitInfo = {};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &commandBuffer;

                IB_VKCHECK(vkQueueSubmit(RendererContext.Present.Queues[Queue::Transfer].Queue, 1, &submitInfo, staging.Fence));
                Staging.Transfers[Queue::Transfer].Commands[Staging.Transfers[Queue::Transfer].ActiveCount++] = commandBuffer;
            }
        }
    }

    Mesh::Handle createMesh(MeshDesc const &desc)
    {
        IB_ASSERT(Geometry.FreeIndexCount > 0, "We've run out of mesh slots! Perhaps we should use a dynamic array?");
        uint32_t meshIndex = Geometry.FreeIndices[--Geometry.FreeIndexCount];
        IB_ASSERT(Geometry.MeshAllocations[meshIndex] == false, "Mesh already allocated!");
        writeMesh(meshIndex, desc);
        return Mesh::toHandle(meshIndex);
    }

    void updateMesh(Mesh::Handle mesh, MeshDesc const &desc)
    {
        writeMesh(Mesh::toValue(mesh), desc);
    }

    void destroyMesh(Mesh::Handle mesh)
    {
        uint32_t meshIndex = Mesh::toValue(mesh);
        Geometry.MeshAllocations[meshIndex] = false;
        Geometry.FreeIndices[Geometry.FreeIndexCount++] = meshIndex;
    }

    Image::Handle createImage(ImageDesc const &desc)
    {
        VkFormat formats[ImageFormat::Count] = {};
        formats[ImageFormat::RGBA8] = VK_FORMAT_R8G8B8A8_UNORM;

        uint32_t formatSize[ImageFormat::Count] = {};
        formatSize[ImageFormat::RGBA8] = 4;

        // Image
        uint32_t imageIndex = RendererContext.Textures.ImageCount++;
        ImageAllocation imageAlloc = {};
        imageAlloc.LogicalDevice = RendererContext.LogicalDevice;
        imageAlloc.PhysicalDevice = RendererContext.PhysicalDevice;
        imageAlloc.ImageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageAlloc.Format = formats[desc.Format];
        imageAlloc.ImageAspect = VK_IMAGE_ASPECT_COLOR_BIT;
        imageAlloc.Width = desc.Width;
        imageAlloc.Height = desc.Height;
        imageAlloc.Stride = formatSize[desc.Format];
        imageAlloc.Allocator = &Allocator;

        ImageAndView imageAndView = allocImageAndView(imageAlloc);
        RendererContext.Textures.Images[imageIndex].Image = imageAndView.Image;
        RendererContext.Textures.Images[imageIndex].ImageView = imageAndView.ImageView;
        RendererContext.Textures.Images[imageIndex].Allocation = imageAndView.Allocation;

        StagingBuffer staging = requestStagingBuffer(&Staging, desc.Width * desc.Height * formatSize[desc.Format], formatSize[desc.Format]);
        memcpy(staging.Memory, desc.Data, desc.Width * desc.Height * formatSize[desc.Format]);

        VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
        commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        commandBufferAllocateInfo.commandBufferCount = 1;
        commandBufferAllocateInfo.commandPool = RendererContext.Present.Queues[Queue::Transfer].CommandPool;

        VkCommandBuffer commandBuffer;
        IB_VKCHECK(vkAllocateCommandBuffers(RendererContext.LogicalDevice, &commandBufferAllocateInfo, &commandBuffer));

        VkCommandBufferBeginInfo beginBufferInfo = {};
        beginBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginBufferInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        IB_VKCHECK(vkBeginCommandBuffer(commandBuffer, &beginBufferInfo));

        // Image barrier UNDEFINED -> TRANSFER
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
            copyRegion.bufferOffset = staging.Offset;
            copyRegion.bufferRowLength = 0;
            copyRegion.bufferImageHeight = 0;
            copyRegion.imageSubresource = imageSubresource;
            copyRegion.imageOffset = {};
            copyRegion.imageExtent = extent;

            vkCmdCopyBufferToImage(commandBuffer, staging.Buffer, RendererContext.Textures.Images[imageIndex].Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
        }

        // Image barrier TRANSFER -> FRAGMENT_SHADER
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

        IB_VKCHECK(vkQueueSubmit(RendererContext.Present.Queues[Queue::Transfer].Queue, 1, &submitInfo, staging.Fence));
        Staging.Transfers[Queue::Transfer].Commands[Staging.Transfers[Queue::Transfer].ActiveCount++] = commandBuffer;

        // Queue up our shader descriptor update
        {
            uint32_t descriptorUpdateIndex = Staging.DescriptorUpdates.ActiveCount++;
            VkDescriptorImageInfo* imageInfo = &Staging.DescriptorUpdates.DescriptorInfos[descriptorUpdateIndex];
            VkWriteDescriptorSet* descriptorWrite = &Staging.DescriptorUpdates.DescriptorWrites[descriptorUpdateIndex];

            *imageInfo = {};
            imageInfo->imageView = RendererContext.Textures.Images[imageIndex].ImageView;
            imageInfo->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            *descriptorWrite = {};
            descriptorWrite->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite->dstSet = ForwardPass.ShaderDescriptor;
            descriptorWrite->descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            descriptorWrite->dstBinding = 2;
            descriptorWrite->dstArrayElement = imageIndex;
            descriptorWrite->descriptorCount = 1;
            descriptorWrite->pImageInfo = imageInfo;
        }

        return Image::toHandle(imageIndex);
    }

    Material::Handle createMaterial(ForwardDesc const &desc)
    {
        ForwardMaterial matData = {};
        matData.AlbedoTint = desc.AlbedoTint;
        matData.AlbedoIndex = Image::toValue(desc.AlbedoImage);

        uint32_t instanceIndex = ForwardPass.InstanceCount++;
        IB_ASSERT(instanceIndex < MaxMaterialInstanceCount, "Too many materials!");

        StagingBuffer staging = requestStagingBuffer(&Staging, sizeof(ForwardMaterial));
        memcpy(staging.Memory, &matData, sizeof(ForwardMaterial));

        {
            VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
            commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            commandBufferAllocateInfo.commandBufferCount = 1;
            commandBufferAllocateInfo.commandPool = RendererContext.Present.Queues[Queue::Transfer].CommandPool;


            VkCommandBuffer commandBuffer;
            IB_VKCHECK(vkAllocateCommandBuffers(RendererContext.LogicalDevice, &commandBufferAllocateInfo, &commandBuffer));

            VkCommandBufferBeginInfo beginBufferInfo = {};
            beginBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginBufferInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            IB_VKCHECK(vkBeginCommandBuffer(commandBuffer, &beginBufferInfo));

            VkBufferCopy copy = {};
            copy.srcOffset = staging.Offset;
            copy.dstOffset = instanceIndex * sizeof(ForwardMaterial);
            copy.size = sizeof(ForwardMaterial);
            vkCmdCopyBuffer(commandBuffer, staging.Buffer, ForwardPass.MaterialBuffer, 1, &copy);

            IB_VKCHECK(vkEndCommandBuffer(commandBuffer));

            VkSubmitInfo submitInfo = {};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            IB_VKCHECK(vkQueueSubmit(RendererContext.Present.Queues[Queue::Transfer].Queue, 1, &submitInfo, staging.Fence));
            Staging.Transfers[Queue::Transfer].Commands[Staging.Transfers[Queue::Transfer].ActiveCount++] = commandBuffer;
        }

        return Material::toHandle(instanceIndex);
    }

    Light::Handle createLight(DirectionalLightDesc const& desc)
    {
        DirectionalLight lightData = {};
        lightData.DirectionAndRadiance = Float4{ desc.Direction.x, desc.Direction.y, desc.Direction.z, desc.Radiance };
        lightData.Tint = desc.Tint;

        uint32_t instanceIndex = Lighting.InstanceCount++;
        IB_ASSERT(instanceIndex < MaxLightCount, "Too many lights!");

        StagingBuffer staging = requestStagingBuffer(&Staging, sizeof(lightData));
        memcpy(staging.Memory, &lightData, sizeof(lightData));

        {
            VkCommandBufferAllocateInfo commandBufferAllocateInfo = {};
            commandBufferAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            commandBufferAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            commandBufferAllocateInfo.commandBufferCount = 1;
            commandBufferAllocateInfo.commandPool = RendererContext.Present.Queues[Queue::Transfer].CommandPool;

            VkCommandBuffer commandBuffer;
            IB_VKCHECK(vkAllocateCommandBuffers(RendererContext.LogicalDevice, &commandBufferAllocateInfo, &commandBuffer));

            VkCommandBufferBeginInfo beginBufferInfo = {};
            beginBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginBufferInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            IB_VKCHECK(vkBeginCommandBuffer(commandBuffer, &beginBufferInfo));

            VkBufferCopy copy = {};
            copy.srcOffset = staging.Offset;
            copy.dstOffset = instanceIndex * sizeof(DirectionalLight);
            copy.size = sizeof(DirectionalLight);
            vkCmdCopyBuffer(commandBuffer, staging.Buffer, Lighting.LightBuffer, 1, &copy);

            IB_VKCHECK(vkEndCommandBuffer(commandBuffer));

            VkSubmitInfo submitInfo = {};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &commandBuffer;

            IB_VKCHECK(vkQueueSubmit(RendererContext.Present.Queues[Queue::Transfer].Queue, 1, &submitInfo, staging.Fence));
            Staging.Transfers[Queue::Transfer].Commands[Staging.Transfers[Queue::Transfer].ActiveCount++] = commandBuffer;
        }

        return Light::toHandle(instanceIndex);
    }

    void newImGuiFrame()
    {
        ImGui_ImplWin32_NewFrame();
        ImGui_ImplVulkan_NewFrame();
        ImGui::NewFrame();
    }

    bool imGuiPlatformMessage(WindowMessagePlatformData const *platformData)
    {
        if (platformData == nullptr)
        {
            return false;
        }

        HWND hwnd;
        UINT msg;
        WPARAM wparam;
        LPARAM lparam;
        Win32::getPlatformMessageData(platformData, &hwnd, &msg, &wparam, &lparam);

        return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
    }

    void drawView(ViewDesc const &view)
    {
        // Cleanup our pipelines
        for (uint32_t i = 0; i < MaxPipelineCleanups; i++)
        {
            if (RendererContext.Cleanup.Pipelines[i] != VK_NULL_HANDLE)
            {
                vkDeviceWaitIdle(RendererContext.LogicalDevice);
                vkDestroyPipeline(RendererContext.LogicalDevice, RendererContext.Cleanup.Pipelines[i], NoAllocator);
                RendererContext.Cleanup.Pipelines[i] = VK_NULL_HANDLE;
            }
        }

        // Start the frame
        uint32_t fb = RendererContext.Present.ActiveFrame;

        uint32_t imageIndex = 0;
        VkResult result = vkAcquireNextImageKHR(RendererContext.LogicalDevice, RendererContext.Present.Swapchain, UINT64_MAX, RendererContext.Present.FrameBuffer[fb].AcquireSemaphore, VK_NULL_HANDLE, &imageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            vkDeviceWaitIdle(RendererContext.LogicalDevice);
            VkSurfaceCapabilitiesKHR surfaceCapabilities;
            IB_VKCHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(RendererContext.PhysicalDevice, RendererContext.Present.Surface, &surfaceCapabilities));
            IB_ASSERT(surfaceCapabilities.currentExtent.width != UINT32_MAX, "Surface extents are undefined.");
            buildSurfaceSwapchain(surfaceCapabilities.currentExtent);
            return;
        }

        IB_VKCHECK(vkWaitForFences(RendererContext.LogicalDevice, 1, &RendererContext.Present.FrameBuffer[fb].FinishedFence, VK_TRUE, UINT64_MAX));
        IB_VKCHECK(vkResetFences(RendererContext.LogicalDevice, 1, &RendererContext.Present.FrameBuffer[fb].FinishedFence));

        // Wait for the staging we've queued up before we've submitted this frame to complete before we move on.
        {
            if (Staging.ActiveFenceCount > 0)
            {
                IB_VKCHECK(vkWaitForFences(RendererContext.LogicalDevice, Staging.ActiveFenceCount, Staging.Fences, VK_TRUE, UINT64_MAX));
                IB_VKCHECK(vkResetFences(RendererContext.LogicalDevice, Staging.ActiveFenceCount, Staging.Fences));
                Staging.ActiveFenceCount = 0;
            }

            for (uint32_t queue = 0; queue < Queue::Count; queue++)
            {
                if (Staging.Transfers[queue].ActiveCount > 0)
                {
                    vkFreeCommandBuffers(RendererContext.LogicalDevice, RendererContext.Present.Queues[queue].CommandPool, Staging.Transfers[queue].ActiveCount, Staging.Transfers[queue].Commands);
                    Staging.Transfers[queue].ActiveCount = 0;
                }
            }

            if (Staging.DescriptorUpdates.ActiveCount > 0)
            {
                vkUpdateDescriptorSets(RendererContext.LogicalDevice, Staging.DescriptorUpdates.ActiveCount, Staging.DescriptorUpdates.DescriptorWrites, 0, NULL);
                Staging.DescriptorUpdates.ActiveCount = 0;
            }

            Staging.WriteOffset = 0;
        }

        VkCommandBuffer currentCommands = RendererContext.Present.FrameBuffer[fb].PrimaryCommandBuffer;
        {
            IB_VKCHECK(vkResetCommandBuffer(currentCommands, 0));

            VkCommandBufferBeginInfo beginBufferInfo = {};
            beginBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginBufferInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            IB_VKCHECK(vkBeginCommandBuffer(currentCommands, &beginBufferInfo));

            // Forward pass
            {
                float color[4] = {0.8f, 0.5f, 0.1f, 0.0f};
                VkClearColorValue clearColor = {};
                memcpy(clearColor.float32, &color, sizeof(float) * 4);

                VkClearValue colorClear = {};
                colorClear.color = clearColor;
                VkClearValue depthClear = {};
                depthClear.depthStencil = {1.0f, 0};
                VkClearValue clearValues[] = {colorClear, depthClear};

                VkRect2D renderArea = {};
                renderArea.extent = RendererContext.Present.SurfaceExtents;

                VkRenderPassBeginInfo renderPassBeginInfo = {};
                renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                renderPassBeginInfo.renderPass = ForwardPass.RenderPass;
                renderPassBeginInfo.framebuffer = ForwardPass.FrameBuffer[fb].Framebuffer;
                renderPassBeginInfo.renderArea = renderArea;
                renderPassBeginInfo.clearValueCount = arrayCount(clearValues);
                renderPassBeginInfo.pClearValues = clearValues;

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

                VkDescriptorSet set = ForwardPass.ShaderDescriptor;
                vkCmdBindDescriptorSets(
                    currentCommands, VK_PIPELINE_BIND_POINT_GRAPHICS, ForwardPass.PipelineLayout,
                    0, 1, &set, 0, VK_NULL_HANDLE);

                vkCmdBindIndexBuffer(currentCommands, Geometry.MeshDataBuffers, 0, VK_INDEX_TYPE_UINT16);

                uint32_t perFrameDataWriteIndex = MaxPerFrameDrawDataSize * fb;
                PerFrameData perFrameData;
                perFrameData.VP = view.ViewProj;
                perFrameData.LightIndex = Light::toValue(view.DirectionalLight);
                memcpy(IndirectDraw.PerFrameData.MappedMemory + perFrameDataWriteIndex, &perFrameData, sizeof(PerFrameData));
                perFrameDataWriteIndex += sizeof(PerFrameData);

                uint32_t indirectDrawCount = 0;
                for (uint32_t passIndex = 0; passIndex < ViewDesc::ForwardPass::Count; passIndex++)
                {
                    if (passIndex > 0)
                    {
                        VkClearAttachment depthAttachment = {};
                        depthAttachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
                        depthAttachment.clearValue = depthClear;

                        VkClearRect clearArea = {};
                        clearArea.rect = renderArea;
                        clearArea.baseArrayLayer = 0;
                        clearArea.layerCount = 1;

                        vkCmdClearAttachments(currentCommands, 1, &depthAttachment, 1, &clearArea);
                    }

                    if (ForwardPass.Pipelines[passIndex] == VK_NULL_HANDLE)
                    {
                        continue;
                    }

                    uint32_t passIndirectDrawCountOffset = indirectDrawCount;
                    ViewDesc::ForwardPass const *pass = &view.Forward.Passes[passIndex];
                    vkCmdBindPipeline(currentCommands, VK_PIPELINE_BIND_POINT_GRAPHICS, ForwardPass.Pipelines[passIndex]);

                    uint32_t passDrawCount = 0;
                    for (uint32_t i = 0; i < pass->BatchCount; i++)
                    {
                        ViewDesc::Batch *batch = &pass->Batches[i];
                        uint32_t materialIndex = Material::toValue(batch->Material);

                        for (uint32_t mesh = 0; mesh < batch->MeshCount && batch->Meshes[mesh].Mesh != InvalidMesh; mesh++)
                        {
                            uint32_t meshIndex = Mesh::toValue(batch->Meshes[mesh].Mesh);

                            VkDrawIndexedIndirectCommand drawIndexed = {};
                            drawIndexed.indexCount = Geometry.Meshes[meshIndex].IndexCount;
                            drawIndexed.instanceCount = batch->Meshes[mesh].Count;
                            drawIndexed.firstIndex = Geometry.Meshes[meshIndex].IndexOffset / sizeof(uint16_t);
                            drawIndexed.vertexOffset = 0;
                            drawIndexed.firstInstance = passDrawCount;

                            memcpy(IndirectDraw.FrameBuffer[fb].MappedMemory + indirectDrawCount * sizeof(VkDrawIndexedIndirectCommand), &drawIndexed, sizeof(VkDrawIndexedIndirectCommand));
                            indirectDrawCount += 1;

                            passDrawCount += batch->Meshes[mesh].Count;
                            for (uint32_t inst = 0; inst < batch->Meshes[mesh].Count; inst++)
                            {
                                PerInstanceData perInstanceData;
                                perInstanceData.M = batch->Meshes[mesh].Transforms[inst];
                                perInstanceData.VertexOffset = Geometry.Meshes[meshIndex].VertexOffset;
                                perInstanceData.MaterialIndex = materialIndex;
                                memcpy(IndirectDraw.PerFrameData.MappedMemory + perFrameDataWriteIndex, &perInstanceData, sizeof(PerInstanceData));
                                perFrameDataWriteIndex += sizeof(PerInstanceData);
                            }
                        }
                    }

                    if (passDrawCount > 0)
                    {
                        struct
                        {
                            uint32_t InstanceOffset;
                            uint32_t FrameOffset;
                            uint32_t Lit;
                        } pushConstantBatchData;
                        pushConstantBatchData.InstanceOffset = passIndirectDrawCountOffset;
                        pushConstantBatchData.FrameOffset = MaxPerFrameDrawDataSize * fb;
                        pushConstantBatchData.Lit = passIndex == 0 ? 1 : 0;

                        vkCmdPushConstants(currentCommands, ForwardPass.PipelineLayout,
                                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstantBatchData), &pushConstantBatchData);

                        vkCmdDrawIndexedIndirect(currentCommands, IndirectDraw.FrameBuffer[fb].Buffer, passIndirectDrawCountOffset * sizeof(VkDrawIndexedIndirectCommand), passDrawCount, sizeof(VkDrawIndexedIndirectCommand));
                    }
                }

                vkCmdEndRenderPass(currentCommands);
            }

            // ImGui Pass
            if (view.ImGuiDrawData != nullptr)
            {
                VkRenderPassBeginInfo renderPassBeginInfo = {};
                renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                renderPassBeginInfo.renderPass = ImGuiPass.RenderPass;
                renderPassBeginInfo.framebuffer = ImGuiPass.FrameBuffer[fb].Framebuffer;

                VkRect2D renderArea = {};
                renderArea.extent = RendererContext.Present.SurfaceExtents;

                renderPassBeginInfo.renderArea = renderArea;
                vkCmdBeginRenderPass(currentCommands, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

                ImGui_ImplVulkan_RenderDrawData(view.ImGuiDrawData, currentCommands);

                vkCmdEndRenderPass(currentCommands);
            }

            vkEndCommandBuffer(currentCommands);
        }

        VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &currentCommands;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &RendererContext.Present.FrameBuffer[fb].AcquireSemaphore;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &RendererContext.Present.FrameBuffer[fb].FinishedSemaphore;
        submitInfo.pWaitDstStageMask = &dstStageMask;

        IB_VKCHECK(vkQueueSubmit(RendererContext.Present.Queues[Queue::Graphics].Queue, 1, &submitInfo, RendererContext.Present.FrameBuffer[fb].FinishedFence));

        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &RendererContext.Present.FrameBuffer[fb].FinishedSemaphore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &RendererContext.Present.Swapchain;
        presentInfo.pImageIndices = &imageIndex;

        VkResult presentResult = vkQueuePresentKHR(RendererContext.Present.Queues[Queue::Present].Queue, &presentInfo);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
        {
            vkDeviceWaitIdle(RendererContext.LogicalDevice);
            VkSurfaceCapabilitiesKHR surfaceCapabilities;
            IB_VKCHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(RendererContext.PhysicalDevice, RendererContext.Present.Surface, &surfaceCapabilities));
            IB_ASSERT(surfaceCapabilities.currentExtent.width != UINT32_MAX, "Failed to get surface extents.");
            buildSurfaceSwapchain(surfaceCapabilities.currentExtent);
        }

        RendererContext.Present.ActiveFrame = (RendererContext.Present.ActiveFrame + 1) % FrameBufferCount;
    }
} // namespace IB
