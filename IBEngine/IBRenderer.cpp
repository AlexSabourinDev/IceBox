#include "IBRenderer.h"
#include "IBLogging.h"
#include "IBPlatform.h"
#include "IBBinary.h"

#define IB_GRAPHICS_API_IMPL
#include "GraphicsAPI.h"

#include <stdlib.h>

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


// TODO: Vulkan Validation Layers crash. Investigate if we can debug this somehow.
//#define IB_VKDEBUG

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam); // ImGui window
namespace IB
{
    namespace
    {
        // Main Renderer
        struct ImageAndView
        {
            enum ImageType
            {
                Color,
                Depth
            };

            IB::VkMemory::Allocation Allocation;
            VkImage Image;
            VkImageView View;
            ImageType Type;
        };

        uint32_t constexpr MaxImageCount = 100;
        uint32_t constexpr MaxSamplerCount = 1000;
        uint32_t constexpr FrameBufferCount = 2;
        uint32_t constexpr MaxPhysicalImageCount = 10;
        uint32_t constexpr MaxPipelineCleanups = 10;
        struct Context
        {
            struct
            {
                GraphicsSurface Surface;
                VkFence ImmediateFence;

                struct
                {
                    CommandBuffer PrimaryCommandBuffer;
                    VkFence FinishedFence;
                } FrameBuffer[FrameBufferCount];
                uint32_t ActiveFrame = 0;
            } Present;

            struct
            {
                ImageAndView Images[MaxImageCount];
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

        uint32_t constexpr StagingSize = 32 * 1024 * 1024; // 32MB of staging space
        uint32_t constexpr MaxStagingFenceCount = 128;
        uint32_t constexpr MaxTransferCount = 128;
        uint32_t constexpr MaxDescriptorUpdateCount = 128;
        struct Staging
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

        uint32_t constexpr MeshBufferChunkCount = FrameBufferCount;
        uint32_t constexpr MaxMeshCount = 1000;
        uint32_t constexpr MeshBufferChunkSize = 1024 * 1024 * 16;
        uint32_t constexpr MeshBufferSize = MeshBufferChunkSize * MeshBufferChunkCount;
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
            IB::VkMemory::Allocation Allocation;
        };

        uint32_t constexpr MaxIndirectDraws = 1024;
        uint32_t constexpr MaxDirectionalLightInstances = 32;
        struct PerFrameData
        {
            IB::Mat4x4 WorldToProjection;
        };

        struct PerInstanceData
        {
            IB::Mat3x4 LocalToWorld;
            uint32_t VertexOffset;
            uint32_t MaterialIndex;
        };

        struct DirectionalLightInstance
        {
            IB::Float4 DirectionAndLightIndex;
        };

        uint32_t constexpr MaxPerFrameDrawDataSize = 
            sizeof(PerFrameData)
            + sizeof(PerInstanceData) * MaxIndirectDraws
            + sizeof(DirectionalLightInstance) * MaxDirectionalLightInstances;
        struct IndirectDraw
        {
            struct
            {
                VkBuffer Buffer;
                IB::VkMemory::Allocation Allocation;
                uint8_t *MappedMemory = nullptr;
            } FrameBuffer[FrameBufferCount];

            struct
            {
                VkBuffer Buffer;
                IB::VkMemory::Allocation Allocation;
                uint8_t *MappedMemory = nullptr;
            } PerFrameData;
        };
        IndirectDraw IndirectDraw;

        struct ForwardMaterial
        {
            uint32_t AlbedoTint;
            uint32_t AlbedoIndexAndFlags;
        };

        uint32_t constexpr MaxMaterialInstanceCount = 100;
        struct ForwardPass
        {
            static uint32_t constexpr SubpassCount = 2;

            VkDescriptorSetLayout ShaderLayout;

            VkPipelineLayout PipelineLayout;
            VkPipeline Pipelines[SubpassCount];

            VkDescriptorSet ShaderDescriptor;
            VkSampler Sampler;
            VkBuffer MaterialBuffer;
            IB::VkMemory::Allocation MaterialBufferAllocation;

            struct
            {
                ImageAndView DepthImage;
            } FrameBuffer[FrameBufferCount];
            uint32_t InstanceCount = 0;

            uint64_t InstanceAllocations[IB::Bitset::bufferSize(MaxMaterialInstanceCount)];

            struct
            {
                uint32_t Instances[MaxMaterialInstanceCount];
                uint32_t Count = 0;
            } Cleanup[FrameBufferCount];
        };

        uint32_t constexpr MaxDirectionalLightCount = 32;
        struct DirectionalLight
        {
            float Radiance = 1.0f;
            uint32_t Tint = 0xFFFFFFFF;
        };

        struct Lighting
        {
            VkBuffer LightBuffer;
            IB::VkMemory::Allocation LightBufferAllocation;
            uint64_t DirectionalLightAllocations[IB::Bitset::bufferSize(MaxDirectionalLightCount)];

            struct
            {
                uint32_t Lights[MaxDirectionalLightCount];
                uint32_t Count = 0;
            } Cleanup[FrameBufferCount];
        };

        GraphicsAPI API;
        VkMemory::Allocator Allocator;
        Context RendererContext;
        ForwardPass ForwardPass;
        Geometry Geometry;
        Lighting Lighting;
        Staging Staging;

        struct ImageAllocation
        {
            uint32_t ImageUsage;
            VkFormat Format;
            uint32_t Width = 0;
            uint32_t Height = 0;
            uint32_t Stride = 0;
            ImageAndView::ImageType Type = ImageAndView::Color;
        };

        ImageAndView allocImageAndView(ImageAllocation alloc)
        {
            ImageAndView imageAndView;
            imageAndView.Type = alloc.Type;

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

                IB_VKCHECK(vkCreateImage(API.LogicalDevice, &imageCreate, IB::NoVkAllocator, &imageAndView.Image));
            }

            // Allocation
            {
                VkMemoryRequirements memoryRequirements;
                vkGetImageMemoryRequirements(API.LogicalDevice, imageAndView.Image, &memoryRequirements);

                IB::VkMemory::AllocationRequest request;
                request.Alignment = memoryRequirements.alignment;
                request.Size = alloc.Width * alloc.Height * alloc.Stride;
                request.LogicalDevice = API.LogicalDevice;
                request.PhysicalDevice = API.PhysicalDevice;
                request.RequiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
                request.PreferedFlags = 0;
                request.TypeBits = memoryRequirements.memoryTypeBits;
                request.Category = IB::VkMemory::Category::Image;

                IB::VkMemory::Allocation *allocation = &imageAndView.Allocation;
                *allocation = allocateDeviceMemory(&Allocator, request);

                IB_VKCHECK(vkBindImageMemory(API.LogicalDevice, imageAndView.Image, allocation->Memory, allocation->Offset));
            }

            // Image view
            {
                VkImageSubresourceRange subresourceRange = {};
                subresourceRange.aspectMask = (alloc.Type == ImageAndView::Color) ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
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

                IB_VKCHECK(vkCreateImageView(API.LogicalDevice, &imageCreateViewInfo, IB::NoVkAllocator, &imageAndView.View));
            }

            return imageAndView;
        }
    } // namespace
} // namespace IB

namespace IB
{
    // Forward declared from IBPlatformWin32.cpp
    namespace Win32
    {
        void getWindowHandleAndInstance(Window::Handle handle, HWND *window, HINSTANCE *instance);
        void getPlatformMessageData(WindowMessagePlatformData const *platformData, HWND *hwnd, UINT *msg, WPARAM *wparam, LPARAM *lparam);
    } // namespace Win32
} // namespace IB

namespace IB
{
namespace
{

    VkImageMemoryBarrier createImageBarrier(
        VkAccessFlagBits sourceAccessMask,
        VkAccessFlagBits dstAccessMask,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        IB::ImageAndView image)
    {
        VkImageSubresourceRange imageSubresource = {};
        imageSubresource.aspectMask = (image.Type == ImageAndView::Color) ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
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
        imageBarrier.image = image.Image;
        imageBarrier.subresourceRange = imageSubresource;
        imageBarrier.srcAccessMask = sourceAccessMask;
        imageBarrier.dstAccessMask = dstAccessMask;

        return imageBarrier;
    }
} // namespace
} // namespace IB

namespace IB
{
    void initRenderer(RendererDesc const &desc)
    {
        HWND windowHandle;
        HINSTANCE instanceHandle;
        Win32::getWindowHandleAndInstance(desc.Window, &windowHandle, &instanceHandle);
        initGraphicsAPI({windowHandle, instanceHandle}, &API);
        VkMemory::initAllocator(&Allocator);

        {
            VkFenceCreateInfo fenceCreateInfo = {};
            fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            IB_VKCHECK(vkCreateFence(API.LogicalDevice, &fenceCreateInfo, NoVkAllocator, &RendererContext.Present.ImmediateFence));
        
            for (uint32_t i = 0; i < MaxStagingFenceCount; i++)
            {
                IB_VKCHECK(vkCreateFence(API.LogicalDevice, &fenceCreateInfo, NoVkAllocator, &Staging.Fences[i]));
            }
        }

        {
            initWin32GraphicsSurface(&API,
                {
                    windowHandle,
                    instanceHandle
                },
                &RendererContext.Present.Surface);

            for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
            {
                // Depth image
                {
                    ImageAllocation imageAlloc = {};
                    imageAlloc.ImageUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                    imageAlloc.Format = VK_FORMAT_D32_SFLOAT;
                    imageAlloc.Width = RendererContext.Present.Surface.Extents.width;
                    imageAlloc.Height = RendererContext.Present.Surface.Extents.height;
                    imageAlloc.Stride = 4;
                    imageAlloc.Type = ImageAndView::Depth;

                    ForwardPass.FrameBuffer[fb].DepthImage = allocImageAndView(imageAlloc);
                }
            }

            for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
            {
                initCommandBuffer(&API, { GraphicsQueue::Graphics }, &RendererContext.Present.FrameBuffer[fb].PrimaryCommandBuffer);

                VkFenceCreateInfo fenceCreateInfo = {};
                fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
                IB_VKCHECK(vkCreateFence(API.LogicalDevice, &fenceCreateInfo, NoVkAllocator, &RendererContext.Present.FrameBuffer[fb].FinishedFence));
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
                IB_VKCHECK(vkCreateDescriptorSetLayout(API.LogicalDevice, &createLayout, NoVkAllocator, &ForwardPass.ShaderLayout));
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

                IB_VKCHECK(vkCreatePipelineLayout(API.LogicalDevice, &pipelineLayoutCreate, NoVkAllocator, &ForwardPass.PipelineLayout));
            }
        }

        // Staging Buffer
        {
            VkBufferCreateInfo bufferCreate = {};
            bufferCreate.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreate.size = StagingSize;
            bufferCreate.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

            IB_VKCHECK(vkCreateBuffer(API.LogicalDevice, &bufferCreate, NoVkAllocator, &Staging.Buffer));

            VkMemoryRequirements memoryRequirements;
            vkGetBufferMemoryRequirements(API.LogicalDevice, Staging.Buffer, &memoryRequirements);

            VkMemory::AllocationRequest request;
            request.Alignment = memoryRequirements.alignment;
            request.Size = StagingSize;
            request.LogicalDevice = API.LogicalDevice;
            request.PhysicalDevice = API.PhysicalDevice;
            request.RequiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            request.PreferedFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            request.TypeBits = memoryRequirements.memoryTypeBits;
            request.Category = VkMemory::Category::TransferSource;

            Staging.Allocation = allocateDeviceMemory(&Allocator, request);
            IB_VKCHECK(vkBindBufferMemory(API.LogicalDevice, Staging.Buffer, Staging.Allocation.Memory, Staging.Allocation.Offset));

            Staging.MappedMemory = mapAllocation(API.LogicalDevice, &Allocator, Staging.Allocation);
        }

        // Indirect Draw Buffer
        for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
        {
            VkBufferCreateInfo bufferCreate = {};
            bufferCreate.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreate.size = MaxIndirectDraws * sizeof(VkDrawIndexedIndirectCommand);
            bufferCreate.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;

            IB_VKCHECK(vkCreateBuffer(API.LogicalDevice, &bufferCreate, NoVkAllocator, &IndirectDraw.FrameBuffer[fb].Buffer));

            VkMemoryRequirements memoryRequirements;
            vkGetBufferMemoryRequirements(API.LogicalDevice, IndirectDraw.FrameBuffer[fb].Buffer, &memoryRequirements);

            VkMemory::AllocationRequest request;
            request.Alignment = memoryRequirements.alignment;
            request.Size = MaxIndirectDraws * sizeof(VkDrawIndexedIndirectCommand);
            request.LogicalDevice = API.LogicalDevice;
            request.PhysicalDevice = API.PhysicalDevice;
            request.RequiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            request.PreferedFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            request.TypeBits = memoryRequirements.memoryTypeBits;
            request.Category = VkMemory::Category::Misc;

            IndirectDraw.FrameBuffer[fb].Allocation = allocateDeviceMemory(&Allocator, request);
            IB_VKCHECK(vkBindBufferMemory(API.LogicalDevice, IndirectDraw.FrameBuffer[fb].Buffer, IndirectDraw.FrameBuffer[fb].Allocation.Memory, IndirectDraw.FrameBuffer[fb].Allocation.Offset));

            IndirectDraw.FrameBuffer[fb].MappedMemory = reinterpret_cast<uint8_t*>(mapAllocation(API.LogicalDevice, &Allocator, IndirectDraw.FrameBuffer[fb].Allocation));
        }

        {
            VkBufferCreateInfo bufferCreate = {};
            bufferCreate.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreate.size = FrameBufferCount * MaxPerFrameDrawDataSize;
            bufferCreate.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

            IB_VKCHECK(vkCreateBuffer(API.LogicalDevice, &bufferCreate, NoVkAllocator, &IndirectDraw.PerFrameData.Buffer));

            VkMemoryRequirements memoryRequirements;
            vkGetBufferMemoryRequirements(API.LogicalDevice, IndirectDraw.PerFrameData.Buffer, &memoryRequirements);

            VkMemory::AllocationRequest request;
            request.Alignment = memoryRequirements.alignment;
            request.Size = FrameBufferCount * MaxPerFrameDrawDataSize;
            request.LogicalDevice = API.LogicalDevice;
            request.PhysicalDevice = API.PhysicalDevice;
            request.RequiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            request.PreferedFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            request.TypeBits = memoryRequirements.memoryTypeBits;
            request.Category = VkMemory::Category::Misc;

            IndirectDraw.PerFrameData.Allocation = allocateDeviceMemory(&Allocator, request);
            IB_VKCHECK(vkBindBufferMemory(API.LogicalDevice, IndirectDraw.PerFrameData.Buffer, IndirectDraw.PerFrameData.Allocation.Memory, IndirectDraw.PerFrameData.Allocation.Offset));

            IndirectDraw.PerFrameData.MappedMemory = reinterpret_cast<uint8_t *>(mapAllocation(API.LogicalDevice, &Allocator, IndirectDraw.PerFrameData.Allocation));
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
            IB_VKCHECK(vkCreateBuffer(API.LogicalDevice, &bufferCreate, NoVkAllocator, buffer));

            VkMemoryRequirements memoryRequirements;
            vkGetBufferMemoryRequirements(API.LogicalDevice, *buffer, &memoryRequirements);

            VkMemory::AllocationRequest request;
            request.Alignment = memoryRequirements.alignment;
            request.Size = MeshBufferSize;
            request.LogicalDevice = API.LogicalDevice;
            request.PhysicalDevice = API.PhysicalDevice;
            request.RequiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            request.PreferedFlags = 0;
            request.TypeBits = memoryRequirements.memoryTypeBits;
            request.Category = VkMemory::Category::Mesh;

            VkMemory::Allocation *allocation = &Geometry.Allocation;
            *allocation = allocateDeviceMemory(&Allocator, request);

            IB_VKCHECK(vkBindBufferMemory(API.LogicalDevice, *buffer, allocation->Memory, allocation->Offset));
        }

        // Material buffer
        {
            VkBufferCreateInfo bufferCreate = {};
            bufferCreate.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreate.size = MaxMaterialInstanceCount * sizeof(ForwardMaterial);
            bufferCreate.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT; // buffers created through create buffer can always be transfered to

            VkBuffer *buffer = &ForwardPass.MaterialBuffer;
            IB_VKCHECK(vkCreateBuffer(API.LogicalDevice, &bufferCreate, NoVkAllocator, buffer));

            VkMemoryRequirements memoryRequirements;
            vkGetBufferMemoryRequirements(API.LogicalDevice, *buffer, &memoryRequirements);

            VkMemory::AllocationRequest request;
            request.Alignment = memoryRequirements.alignment;
            request.Size = MaxMaterialInstanceCount * sizeof(ForwardMaterial);
            request.LogicalDevice = API.LogicalDevice;
            request.PhysicalDevice = API.PhysicalDevice;
            request.RequiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            request.PreferedFlags = 0;
            request.TypeBits = memoryRequirements.memoryTypeBits;
            request.Category = VkMemory::Category::Misc;

            VkMemory::Allocation *allocation = &ForwardPass.MaterialBufferAllocation;
            *allocation = allocateDeviceMemory(&Allocator, request);

            IB_VKCHECK(vkBindBufferMemory(API.LogicalDevice, *buffer, allocation->Memory, allocation->Offset));
        }

        // Lights buffer
        {
            VkBufferCreateInfo bufferCreate = {};
            bufferCreate.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferCreate.size = MaxDirectionalLightCount * sizeof(DirectionalLight);
            bufferCreate.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT; // buffers created through create buffer can always be transfered to

            VkBuffer *buffer = &Lighting.LightBuffer;
            IB_VKCHECK(vkCreateBuffer(API.LogicalDevice, &bufferCreate, NoVkAllocator, buffer));

            VkMemoryRequirements memoryRequirements;
            vkGetBufferMemoryRequirements(API.LogicalDevice, *buffer, &memoryRequirements);

            VkMemory::AllocationRequest request;
            request.Alignment = memoryRequirements.alignment;
            request.Size = MaxDirectionalLightCount * sizeof(DirectionalLight);
            request.LogicalDevice = API.LogicalDevice;
            request.PhysicalDevice = API.PhysicalDevice;
            request.RequiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            request.PreferedFlags = 0;
            request.TypeBits = memoryRequirements.memoryTypeBits;
            request.Category = VkMemory::Category::Misc;

            VkMemory::Allocation *allocation = &Lighting.LightBufferAllocation;
            *allocation = allocateDeviceMemory(&Allocator, request);

            IB_VKCHECK(vkBindBufferMemory(API.LogicalDevice, *buffer, allocation->Memory, allocation->Offset));
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

            IB_VKCHECK(vkCreateSampler(API.LogicalDevice, &samplerCreate, NoVkAllocator, &ForwardPass.Sampler));
        }

        // Write to our template descriptor set
        {
            VkDescriptorSetAllocateInfo descriptorSetAlloc = {};
            descriptorSetAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            descriptorSetAlloc.descriptorPool = API.DescriptorPool;
            descriptorSetAlloc.descriptorSetCount = 1;
            descriptorSetAlloc.pSetLayouts = &ForwardPass.ShaderLayout;
            IB_VKCHECK(vkAllocateDescriptorSets(
                API.LogicalDevice, &descriptorSetAlloc,
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
            lightBufferInfo.range = MaxDirectionalLightCount * sizeof(DirectionalLight);

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
            vkUpdateDescriptorSets(API.LogicalDevice, arrayCount(writeDescriptorSets), writeDescriptorSets, 0, NULL);
        }

        // ImGui
        {
            ImGui::CreateContext();
            ImGui_ImplWin32_Init(windowHandle);

            ImGui_ImplVulkan_InitInfo initInfo = {};
            initInfo.Instance = API.Instance;
            initInfo.PhysicalDevice = API.PhysicalDevice;
            initInfo.Device = API.LogicalDevice;
            initInfo.QueueFamily = API.Queues[GraphicsQueue::Graphics].Index;
            initInfo.Queue = API.Queues[GraphicsQueue::Graphics].Queue;
            initInfo.PipelineCache = API.PipelineCache;
            initInfo.DescriptorPool = API.DescriptorPool;
            initInfo.Allocator = NoVkAllocator;
            initInfo.MinImageCount = FrameBufferCount;
            initInfo.ImageCount = FrameBufferCount;
            initInfo.CheckVkResultFn = &vkCheck;
            ImGui_ImplVulkan_Init(&initInfo, RendererContext.Present.Surface.Format.format);

            // Upload ImGui font
            {
                CommandBuffer uploadCommands;
                initCommandBuffer(&API, { GraphicsQueue::Graphics }, &uploadCommands);

                VkCommandBufferBeginInfo beginBufferInfo = {};
                beginBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginBufferInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

                IB_VKCHECK(vkBeginCommandBuffer(uploadCommands.Commands, &beginBufferInfo));
                ImGui_ImplVulkan_CreateFontsTexture(uploadCommands.Commands);
                IB_VKCHECK(vkEndCommandBuffer(uploadCommands.Commands));

                submitCommandBuffer(&API, { &uploadCommands, VK_NULL_HANDLE, RendererContext.Present.ImmediateFence });
                IB_VKCHECK(vkWaitForFences(API.LogicalDevice, 1, &RendererContext.Present.ImmediateFence, VK_TRUE, UINT64_MAX));
                IB_VKCHECK(vkResetFences(API.LogicalDevice, 1, &RendererContext.Present.ImmediateFence));

                killCommandBuffer(&API, &uploadCommands);
                ImGui_ImplVulkan_DestroyFontUploadObjects();
            }
        }
    }

    void killRenderer()
    {
        vkDeviceWaitIdle(API.LogicalDevice);

        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        killGraphicsSurface(&API, &RendererContext.Present.Surface);
        for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
        {
            killCommandBuffer(&API, &RendererContext.Present.FrameBuffer[fb].PrimaryCommandBuffer);
            vkDestroyFence(API.LogicalDevice, RendererContext.Present.FrameBuffer[fb].FinishedFence, NoVkAllocator);
            vkDestroyImage(API.LogicalDevice, ForwardPass.FrameBuffer[fb].DepthImage.Image, NoVkAllocator);
            vkDestroyImageView(API.LogicalDevice, ForwardPass.FrameBuffer[fb].DepthImage.View, NoVkAllocator);
            freeDeviceMemory(&Allocator, ForwardPass.FrameBuffer[fb].DepthImage.Allocation);
        }

        vkDestroyBuffer(API.LogicalDevice, Geometry.MeshDataBuffers, NoVkAllocator);
        freeDeviceMemory(&Allocator, Geometry.Allocation);

        for (uint32_t i = 0; i < RendererContext.Textures.ImageCount; i++)
        {
            vkDestroyImage(API.LogicalDevice, RendererContext.Textures.Images[i].Image, NoVkAllocator);
            vkDestroyImageView(API.LogicalDevice, RendererContext.Textures.Images[i].View, NoVkAllocator);
            freeDeviceMemory(&Allocator, RendererContext.Textures.Images[i].Allocation);
        }

        vkDestroyDescriptorSetLayout(API.LogicalDevice, ForwardPass.ShaderLayout, NoVkAllocator);

        vkDestroyPipelineLayout(API.LogicalDevice, ForwardPass.PipelineLayout, NoVkAllocator);

        for (uint32_t i = 0; i < arrayCount(ForwardPass.Pipelines); i++)
        {
            vkDestroyPipeline(API.LogicalDevice, ForwardPass.Pipelines[i], NoVkAllocator);
        }

        vkDestroySampler(API.LogicalDevice, ForwardPass.Sampler, NoVkAllocator);
        vkDestroyBuffer(API.LogicalDevice, ForwardPass.MaterialBuffer, NoVkAllocator);
        freeDeviceMemory(&Allocator, ForwardPass.MaterialBufferAllocation);

        vkDestroyBuffer(API.LogicalDevice, Lighting.LightBuffer, NoVkAllocator);
        freeDeviceMemory(&Allocator, Lighting.LightBufferAllocation);

        vkDestroyBuffer(API.LogicalDevice, Staging.Buffer, NoVkAllocator);
        freeDeviceMemory(&Allocator, Staging.Allocation);

        for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
        {
            vkDestroyBuffer(API.LogicalDevice, IndirectDraw.FrameBuffer[fb].Buffer, NoVkAllocator);
            freeDeviceMemory(&Allocator, IndirectDraw.FrameBuffer[fb].Allocation);
        }

        vkDestroyBuffer(API.LogicalDevice, IndirectDraw.PerFrameData.Buffer, NoVkAllocator);
        freeDeviceMemory(&Allocator, IndirectDraw.PerFrameData.Allocation);

        vkDestroyFence(API.LogicalDevice, RendererContext.Present.ImmediateFence, NoVkAllocator);

        for (uint32_t i = 0; i < MaxStagingFenceCount; i++)
        {
            vkDestroyFence(API.LogicalDevice, Staging.Fences[i], NoVkAllocator);
        }

        VkMemory::killAllocator(API.LogicalDevice, &Allocator);
        killGraphicsAPI(&API);
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
        IB_VKCHECK(vkCreateShaderModule(API.LogicalDevice, &createVShader, NoVkAllocator, &vertexShader));

        VkShaderModule fragShader = {};

        VkShaderModuleCreateInfo createFShader = {};
        createFShader.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createFShader.pCode = (const uint32_t *)desc.FShader;
        createFShader.codeSize = desc.FShaderSize;
        IB_VKCHECK(vkCreateShaderModule(API.LogicalDevice, &createFShader, NoVkAllocator, &fragShader));

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
        scissor.extent = RendererContext.Present.Surface.Extents;

        VkViewport viewport = {};
        viewport.x = 0;
        viewport.y = 0;
        viewport.width = (float)RendererContext.Present.Surface.Extents.width;
        viewport.height = (float)RendererContext.Present.Surface.Extents.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkPipelineViewportStateCreateInfo viewportStateCreate = {};
        viewportStateCreate.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportStateCreate.viewportCount = 1;
        viewportStateCreate.pViewports = &viewport;
        viewportStateCreate.scissorCount = 1;
        viewportStateCreate.pScissors = &scissor;

        VkPipelineRenderingCreateInfoKHR pipelineRenderingCreateInfo = {};
        pipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
        pipelineRenderingCreateInfo.colorAttachmentCount = 1;
        pipelineRenderingCreateInfo.pColorAttachmentFormats = &RendererContext.Present.Surface.Format.format;
        pipelineRenderingCreateInfo.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;

        VkGraphicsPipelineCreateInfo baseGraphicsPipelineCreate = {};
        baseGraphicsPipelineCreate.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        baseGraphicsPipelineCreate.pNext = &pipelineRenderingCreateInfo;
        baseGraphicsPipelineCreate.layout = ForwardPass.PipelineLayout;
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
        IB_VKCHECK(vkCreateGraphicsPipelines(API.LogicalDevice, API.PipelineCache, arrayCount(pipelineCreateInfo), pipelineCreateInfo, NoVkAllocator, createdPipelines));

        // Write our pipelines after our call due to the fact that we expect this function to be called from another thread
        // And we'd like to assure that our writes are done at the right time.
        threadRelease();
        for (uint32_t i = 0; i < ForwardPass::SubpassCount; i++)
        {
            ForwardPass.Pipelines[i] = createdPipelines[i];
        }

        vkDestroyShaderModule(API.LogicalDevice, vertexShader, NoVkAllocator);
        vkDestroyShaderModule(API.LogicalDevice, fragShader, NoVkAllocator);
    }

    namespace
    {
        void writeMesh(uint32_t meshIndex, MeshDesc const& desc)
        {
            uint32_t vertexSize = desc.Vertices.Count * sizeof(Vertex);
            uint32_t indexSize = desc.Indices.Count * sizeof(uint16_t);

            CommandBuffer commandBuffer;
            initCommandBuffer(&API, { GraphicsQueue::Transfer }, &commandBuffer);

            VkCommandBufferBeginInfo beginBufferInfo = {};
            beginBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginBufferInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            IB_VKCHECK(vkBeginCommandBuffer(commandBuffer.Commands, &beginBufferInfo));

            uint32_t chunkOffset = Geometry.NextOffset % MeshBufferChunkSize;
            // We're out of memory. Try to pack the empty space caused by freed geometry and try again
            if (chunkOffset + vertexSize + indexSize > MeshBufferChunkSize)
            {
                // We store our mesh buffers as essentially 2 buffers
                // The active buffer and the packing buffer
                // The packing buffer should never overlap with the active buffer
                // Once the packing buffer is in use, we know that the previous active
                // buffer is free to be written to.
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
                    vkCmdCopyBuffer(commandBuffer.Commands, Geometry.MeshDataBuffers, Geometry.MeshDataBuffers, copyCount, copyRegions);
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
            if (vertexSize + indexSize > 0)
            {
                memcpy(staging.Memory, desc.Vertices.Data, vertexSize);
                memcpy(reinterpret_cast<uint8_t *>(staging.Memory) + vertexSize, desc.Indices.Data, indexSize);

                VkBufferCopy meshCopy = {};
                meshCopy.srcOffset = staging.Offset;
                meshCopy.dstOffset = Geometry.Meshes[meshIndex].VertexOffset;
                meshCopy.size = vertexSize + indexSize;

                vkCmdCopyBuffer(commandBuffer.Commands, staging.Buffer, Geometry.MeshDataBuffers, 1, &meshCopy);
            }

            // End our command buffer
            {
                IB_VKCHECK(vkEndCommandBuffer(commandBuffer.Commands));
                submitCommandBuffer(&API, {&commandBuffer, VK_NULL_HANDLE, staging.Fence});
                Staging.Transfers.Commands[Staging.Transfers.ActiveCount++] = commandBuffer;
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

    Mesh::Handle updateMesh(Mesh::Handle mesh, MeshDesc const &desc)
    {
        writeMesh(Mesh::toValue(mesh), desc);
        // Meshes are modified in-place
        return mesh;
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
        imageAlloc.ImageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageAlloc.Format = formats[desc.Format];
        imageAlloc.Width = desc.Width;
        imageAlloc.Height = desc.Height;
        imageAlloc.Stride = formatSize[desc.Format];

        RendererContext.Textures.Images[imageIndex] = allocImageAndView(imageAlloc);

        StagingBuffer staging = requestStagingBuffer(&Staging, desc.Width * desc.Height * formatSize[desc.Format], formatSize[desc.Format]);
        memcpy(staging.Memory, desc.Data, desc.Width * desc.Height * formatSize[desc.Format]);

        CommandBuffer commandBuffer;
        initCommandBuffer(&API, {GraphicsQueue::Transfer}, &commandBuffer);

        VkCommandBufferBeginInfo beginBufferInfo = {};
        beginBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginBufferInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        IB_VKCHECK(vkBeginCommandBuffer(commandBuffer.Commands, &beginBufferInfo));

        // Image barrier UNDEFINED -> TRANSFER
        {
            VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            VkImageMemoryBarrier imageBarrier = createImageBarrier(
                VkAccessFlagBits{},
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                RendererContext.Textures.Images[imageIndex]);
            vkCmdPipelineBarrier(commandBuffer.Commands, sourceStage, destinationStage, 0, 0, NULL, 0, NULL, 1, &imageBarrier);
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

            vkCmdCopyBufferToImage(commandBuffer.Commands, staging.Buffer, RendererContext.Textures.Images[imageIndex].Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
        }

        // Image barrier TRANSFER -> FRAGMENT_SHADER
        {
            VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            VkImageMemoryBarrier imageBarrier = createImageBarrier(
                VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                RendererContext.Textures.Images[imageIndex]);
            vkCmdPipelineBarrier(commandBuffer.Commands, sourceStage, destinationStage, 0, 0, NULL, 0, NULL, 1, &imageBarrier);
        }

        IB_VKCHECK(vkEndCommandBuffer(commandBuffer.Commands));
        submitCommandBuffer(&API, {&commandBuffer, VK_NULL_HANDLE, staging.Fence});
        Staging.Transfers.Commands[Staging.Transfers.ActiveCount++] = commandBuffer;

        // Queue up our shader descriptor update
        {
            uint32_t descriptorUpdateIndex = Staging.DescriptorUpdates.ActiveCount++;
            VkDescriptorImageInfo* imageInfo = &Staging.DescriptorUpdates.DescriptorInfos[descriptorUpdateIndex];
            VkWriteDescriptorSet* descriptorWrite = &Staging.DescriptorUpdates.DescriptorWrites[descriptorUpdateIndex];

            *imageInfo = {};
            imageInfo->imageView = RendererContext.Textures.Images[imageIndex].View;
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
        matData.AlbedoIndexAndFlags = Image::toValue(desc.AlbedoImage);
        matData.AlbedoIndexAndFlags |= desc.IsLit ? 0x80000000 : 0;

        uint64_t instanceIndex = IB::Bitset::findClearedSlot(ForwardPass.InstanceAllocations, MaxMaterialInstanceCount);
        IB_ASSERT(instanceIndex != IB::Bitset::NoSlot, "Too many materials!");
        IB::Bitset::setSlot(ForwardPass.InstanceAllocations, instanceIndex);

        StagingBuffer staging = requestStagingBuffer(&Staging, sizeof(ForwardMaterial));
        memcpy(staging.Memory, &matData, sizeof(ForwardMaterial));

        {
            CommandBuffer commandBuffer;
            initCommandBuffer(&API, {GraphicsQueue::Transfer}, &commandBuffer);

            VkCommandBufferBeginInfo beginBufferInfo = {};
            beginBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginBufferInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            IB_VKCHECK(vkBeginCommandBuffer(commandBuffer.Commands, &beginBufferInfo));

            VkBufferCopy copy = {};
            copy.srcOffset = staging.Offset;
            copy.dstOffset = instanceIndex * sizeof(ForwardMaterial);
            copy.size = sizeof(ForwardMaterial);
            vkCmdCopyBuffer(commandBuffer.Commands, staging.Buffer, ForwardPass.MaterialBuffer, 1, &copy);

            IB_VKCHECK(vkEndCommandBuffer(commandBuffer.Commands));
            submitCommandBuffer(&API, {&commandBuffer, VK_NULL_HANDLE, staging.Fence });
            Staging.Transfers.Commands[Staging.Transfers.ActiveCount++] = commandBuffer;
        }

        return Material::toHandle(static_cast<uint32_t>(instanceIndex));
    }

    Material::Handle updateMaterial(Material::Handle handle, ForwardDesc const& desc)
    {
        // Materials are simply recreated in a new block
        destroyMaterial(handle);
        return createMaterial(desc);
    }

    void destroyMaterial(Material::Handle handle)
    {
        uint32_t materialIndex = Material::toValue(handle);
        // Our release frame is the last frame we had in flight
        uint32_t releaseFrame = (RendererContext.Present.ActiveFrame - 1) % FrameBufferCount;
        IB_ASSERT(ForwardPass.Cleanup[releaseFrame].Count < MaxMaterialInstanceCount, "Releasing too many materials. Are we releasing a material more than once?");
        ForwardPass.Cleanup[releaseFrame].Instances[ForwardPass.Cleanup[releaseFrame].Count++] = materialIndex;
    }

    Light::Handle createLight(DirectionalLightDesc const& desc)
    {
        DirectionalLight lightData = {};
        lightData.Radiance = desc.Radiance;
        lightData.Tint = desc.Tint;

        uint64_t instanceIndex = IB::Bitset::findClearedSlot(Lighting.DirectionalLightAllocations, MaxDirectionalLightCount);
        IB_ASSERT(instanceIndex != IB::Bitset::NoSlot, "Too many directional lights!");
        IB::Bitset::setSlot(Lighting.DirectionalLightAllocations, instanceIndex);

        StagingBuffer staging = requestStagingBuffer(&Staging, sizeof(lightData));
        memcpy(staging.Memory, &lightData, sizeof(lightData));

        {
            CommandBuffer commandBuffer;
            initCommandBuffer(&API, { GraphicsQueue::Transfer }, &commandBuffer);

            VkCommandBufferBeginInfo beginBufferInfo = {};
            beginBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginBufferInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            IB_VKCHECK(vkBeginCommandBuffer(commandBuffer.Commands, &beginBufferInfo));

            VkBufferCopy copy = {};
            copy.srcOffset = staging.Offset;
            copy.dstOffset = instanceIndex * sizeof(DirectionalLight);
            copy.size = sizeof(DirectionalLight);
            vkCmdCopyBuffer(commandBuffer.Commands, staging.Buffer, Lighting.LightBuffer, 1, &copy);

            IB_VKCHECK(vkEndCommandBuffer(commandBuffer.Commands));
            submitCommandBuffer(&API, { &commandBuffer, VK_NULL_HANDLE, staging.Fence});
            Staging.Transfers.Commands[Staging.Transfers.ActiveCount++] = commandBuffer;
        }

        return Light::toHandle(static_cast<uint32_t>(instanceIndex));
    }

    Light::Handle updateLight(Light::Handle light, DirectionalLightDesc const &desc)
    {
        // Lights are simply recreated in a new block
        destroyLight(light);
        return createLight(desc);
    }

    void destroyLight(Light::Handle light)
    {
        uint32_t lightIndex = Light::toValue(light);
        // Our release frame is the last frame we had in flight
        uint32_t releaseFrame = (RendererContext.Present.ActiveFrame - 1) % FrameBufferCount;
        IB_ASSERT(Lighting.Cleanup[releaseFrame].Count < MaxDirectionalLightCount, "Releasing too many lights. Are we releasing a light more than once?");
        Lighting.Cleanup[releaseFrame].Lights[Lighting.Cleanup[releaseFrame].Count++] = lightIndex;
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

    void recordCommands(CommandBuffer* commandBuffer, uint32_t fb, ViewDesc const &view)
    {
        VkCommandBuffer currentCommands = commandBuffer->Commands;

        IB_VKCHECK(vkResetCommandBuffer(currentCommands, 0));

        VkCommandBufferBeginInfo beginBufferInfo = {};
        beginBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginBufferInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        IB_VKCHECK(vkBeginCommandBuffer(currentCommands, &beginBufferInfo));

        // Forward pass
        {
            {
                VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                VkImageMemoryBarrier colorBarrier = createMemoryBarrier(
                    &RendererContext.Present.Surface.Framebuffers[fb].Texture,
                    VkAccessFlagBits{},
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                vkCmdPipelineBarrier(currentCommands, sourceStage, destinationStage, 0, 0, NULL, 0, NULL, 1, &colorBarrier);
            }

            {
                VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;

                VkImageMemoryBarrier depthBarrier = createImageBarrier(
                    VkAccessFlagBits{},
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    ForwardPass.FrameBuffer[fb].DepthImage);

                vkCmdPipelineBarrier(currentCommands, sourceStage, destinationStage, 0, 0, NULL, 0, NULL, 1, &depthBarrier);
            }

            float color[4] = {0.8f, 0.5f, 0.1f, 0.0f};
            VkClearColorValue clearColor = {};
            memcpy(clearColor.float32, &color, sizeof(float) * 4);

            VkClearValue colorClear = {};
            colorClear.color = clearColor;

            VkRect2D renderArea = {};
            renderArea.extent = RendererContext.Present.Surface.Extents;

            VkRenderingAttachmentInfo forwardImageAttachment = {};
            forwardImageAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            forwardImageAttachment.imageView = RendererContext.Present.Surface.Framebuffers[fb].Texture.View;
            forwardImageAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            forwardImageAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            forwardImageAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            forwardImageAttachment.clearValue = colorClear;

            VkClearValue depthClear = {};
            depthClear.depthStencil = {1.0f, 0};

            VkRenderingAttachmentInfo forwardDepthAttachment = {};
            forwardDepthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            forwardDepthAttachment.imageView = ForwardPass.FrameBuffer[fb].DepthImage.View;
            forwardDepthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            forwardDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            forwardDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            forwardDepthAttachment.clearValue = depthClear;

            VkRenderingInfo renderInfo = {};
            renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderInfo.renderArea = renderArea;
            renderInfo.layerCount = 1;
            renderInfo.colorAttachmentCount = 1;
            renderInfo.pColorAttachments = &forwardImageAttachment;
            renderInfo.pDepthAttachment = &forwardDepthAttachment;

            vkCmdBeginRendering(currentCommands, &renderInfo);

            VkViewport viewport = {};
            viewport.x = 0;
            viewport.y = 0;
            viewport.width = (float)RendererContext.Present.Surface.Extents.width;
            viewport.height = (float)RendererContext.Present.Surface.Extents.height;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(currentCommands, 0, 1, &viewport);

            VkRect2D scissor = {};
            scissor.extent = RendererContext.Present.Surface.Extents;
            vkCmdSetScissor(currentCommands, 0, 1, &scissor);

            VkDescriptorSet set = ForwardPass.ShaderDescriptor;
            vkCmdBindDescriptorSets(
                currentCommands, VK_PIPELINE_BIND_POINT_GRAPHICS, ForwardPass.PipelineLayout,
                0, 1, &set, 0, VK_NULL_HANDLE);

            vkCmdBindIndexBuffer(currentCommands, Geometry.MeshDataBuffers, 0, VK_INDEX_TYPE_UINT16);

            uint32_t perFrameDataWriteIndex = MaxPerFrameDrawDataSize * fb;
            PerFrameData perFrameData;
            perFrameData.WorldToProjection = view.ViewProj;
            memcpy(IndirectDraw.PerFrameData.MappedMemory + perFrameDataWriteIndex, &perFrameData, sizeof(PerFrameData));
            perFrameDataWriteIndex += sizeof(PerFrameData);

            memcpy(IndirectDraw.PerFrameData.MappedMemory + perFrameDataWriteIndex, &view.DirectionalLights.Count, sizeof(uint32_t));
            perFrameDataWriteIndex += sizeof(uint32_t);
            for (uint32_t dirLight = 0; dirLight < view.DirectionalLights.Count; dirLight++)
            {
                DirectionalLightInstance directionalLightInstance;

                directionalLightInstance.DirectionAndLightIndex =
                    {view.DirectionalLights.Lights[dirLight].Direction,
                        bitcast<float>(Light::toValue(view.DirectionalLights.Lights[dirLight].Light))};
                
                memcpy(IndirectDraw.PerFrameData.MappedMemory + perFrameDataWriteIndex, &directionalLightInstance, sizeof(directionalLightInstance));
                perFrameDataWriteIndex += sizeof(directionalLightInstance);
            }

            uint32_t const lightWriteEnd = perFrameDataWriteIndex;

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
                            perInstanceData.LocalToWorld = batch->Meshes[mesh].Transforms[inst];
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
                        uint32_t InstanceDataOffsetAndFlags;
                    } pushConstantBatchData;
                    pushConstantBatchData.InstanceOffset = passIndirectDrawCountOffset;
                    pushConstantBatchData.FrameOffset = MaxPerFrameDrawDataSize * fb;
                    pushConstantBatchData.InstanceDataOffsetAndFlags = lightWriteEnd;
                    // Use the last bit for some flag info
                    if (passIndex == 0)
                    {
                        pushConstantBatchData.InstanceDataOffsetAndFlags = pushConstantBatchData.InstanceDataOffsetAndFlags | 0x80000000;
                    }

                    vkCmdPushConstants(currentCommands, ForwardPass.PipelineLayout,
                                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstantBatchData), &pushConstantBatchData);

                    vkCmdDrawIndexedIndirect(currentCommands, IndirectDraw.FrameBuffer[fb].Buffer, passIndirectDrawCountOffset * sizeof(VkDrawIndexedIndirectCommand), passDrawCount, sizeof(VkDrawIndexedIndirectCommand));
                }
            }

            vkCmdEndRendering(currentCommands);
        }

        // ImGui Pass
        if (view.ImGuiDrawData != nullptr)
        {
            VkRect2D renderArea = {};
            renderArea.extent = RendererContext.Present.Surface.Extents;

            VkRenderingAttachmentInfo forwardImageAttachment = {};
            forwardImageAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            forwardImageAttachment.imageView = RendererContext.Present.Surface.Framebuffers[fb].Texture.View;
            forwardImageAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            forwardImageAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            forwardImageAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

            VkRenderingInfo renderInfo = {};
            renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderInfo.renderArea = renderArea;
            renderInfo.layerCount = 1;
            renderInfo.colorAttachmentCount = 1;
            renderInfo.pColorAttachments = &forwardImageAttachment;

            vkCmdBeginRendering(currentCommands, &renderInfo);

            ImGui_ImplVulkan_RenderDrawData(view.ImGuiDrawData, currentCommands);

            vkCmdEndRendering(currentCommands);
        }

        {
            VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            VkImageMemoryBarrier colorBarrier = createMemoryBarrier(
                &RendererContext.Present.Surface.Framebuffers[fb].Texture,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                {},
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

            vkCmdPipelineBarrier(currentCommands, sourceStage, destinationStage, 0, 0, NULL, 0, NULL, 1, &colorBarrier);
        }

        vkEndCommandBuffer(currentCommands);
    }

    void drawView(ViewDesc const &view)
    {
        // Cleanup our pipelines
        for (uint32_t i = 0; i < MaxPipelineCleanups; i++)
        {
            if (RendererContext.Cleanup.Pipelines[i] != VK_NULL_HANDLE)
            {
                vkDeviceWaitIdle(API.LogicalDevice);
                vkDestroyPipeline(API.LogicalDevice, RendererContext.Cleanup.Pipelines[i], NoVkAllocator);
                RendererContext.Cleanup.Pipelines[i] = VK_NULL_HANDLE;
            }
        }

        // Start the frame
        uint32_t fb = RendererContext.Present.ActiveFrame;

        IB_VKCHECK(vkWaitForFences(API.LogicalDevice, 1, &RendererContext.Present.FrameBuffer[fb].FinishedFence, VK_TRUE, UINT64_MAX));
        IB_VKCHECK(vkResetFences(API.LogicalDevice, 1, &RendererContext.Present.FrameBuffer[fb].FinishedFence));

        // Wait for the staging we've queued up before we've submitted this frame to complete before we move on.
        {
            if (Staging.ActiveFenceCount > 0)
            {
                IB_VKCHECK(vkWaitForFences(API.LogicalDevice, Staging.ActiveFenceCount, Staging.Fences, VK_TRUE, UINT64_MAX));
                IB_VKCHECK(vkResetFences(API.LogicalDevice, Staging.ActiveFenceCount, Staging.Fences));
                Staging.ActiveFenceCount = 0;
            }

            if (Staging.Transfers.ActiveCount > 0)
            {
                for(uint32_t i = 0; i < Staging.Transfers.ActiveCount; i++)
                {
                    killCommandBuffer(&API, &Staging.Transfers.Commands[i]);
                }
                Staging.Transfers.ActiveCount = 0;
            }

            if (Staging.DescriptorUpdates.ActiveCount > 0)
            {
                vkUpdateDescriptorSets(API.LogicalDevice, Staging.DescriptorUpdates.ActiveCount, Staging.DescriptorUpdates.DescriptorWrites, 0, NULL);
                Staging.DescriptorUpdates.ActiveCount = 0;
            }

            Staging.WriteOffset = 0;
        }

        // Cleanup our released material instances once we're sure they aren't in use anymore.
        {
            for (uint32_t i = 0; i < ForwardPass.Cleanup[fb].Count; i++)
            {
                IB::Bitset::clearSlot(ForwardPass.InstanceAllocations, ForwardPass.Cleanup[fb].Instances[i]);
            }
            ForwardPass.Cleanup[fb].Count = 0;
        }

        // Cleanup our released lights once we're sure they aren't in use anymore.
        {
            for (uint32_t i = 0; i < Lighting.Cleanup[fb].Count; i++)
            {
                IB::Bitset::clearSlot(Lighting.DirectionalLightAllocations, Lighting.Cleanup[fb].Lights[i]);
            }
            Lighting.Cleanup[fb].Count = 0;
        }

        SurfacePrepareResult prepareResult;
        if(prepareSurface(&API, { &RendererContext.Present.Surface, fb }, &prepareResult))
        {
            recordCommands(&RendererContext.Present.FrameBuffer[fb].PrimaryCommandBuffer, fb, view);

            submitCommandBuffer(&API, 
                {
                    &RendererContext.Present.FrameBuffer[fb].PrimaryCommandBuffer,
                    RendererContext.Present.Surface.Framebuffers[fb].AcquireSemaphore,
                    RendererContext.Present.FrameBuffer[fb].FinishedFence
                });

            SurfacePresentResult presentResult;
            presentSurface(&API, { &RendererContext.Present.Surface, prepareResult, RendererContext.Present.FrameBuffer[fb].PrimaryCommandBuffer }, &presentResult);

            if(presentResult.SurfaceState == SurfaceState::ShouldRebuild)
            {
                rebuildSurface(&API, &RendererContext.Present.Surface);
            }
        }
        else
        {
            if(prepareResult.SurfaceState == SurfaceState::ShouldRebuild)
            {
                rebuildSurface(&API, &RendererContext.Present.Surface);
            }
        }
        
        RendererContext.Present.ActiveFrame = (RendererContext.Present.ActiveFrame + 1) % FrameBufferCount;
    }
} // namespace IB
