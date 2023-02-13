#include "IBRenderer.h"
#include "IBLogging.h"
#include "IBPlatform.h"
#include "IBBinary.h"

#define IB_GRAPHICS_API_IMPL
#define IB_GRAPHICS_CONSTANTS
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
                    VkCommandBuffer PrimaryCommandBuffer;
                    VkSemaphore FinishedSemaphore;
                    VkFence FinishedFence;
                } FrameBuffer[FrameBufferCount];
                uint32_t ActiveFrame = 0;
            } Present;

            struct
            {
                Texture Images[MaxImageCount];
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
                GraphicsPipeline Pipelines[MaxPipelineCleanups] = {};
            } Cleanup;
        };

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
            GraphicsBuffer MeshDataBuffers;
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
            GraphicsBuffer FrameDraws[FrameBufferCount];
            GraphicsBuffer PerFrameData;
        };
        IndirectDraw IndirectDraw;

        struct ForwardMaterial
        {
            uint32_t AlbedoTint;
            uint32_t AlbedoIndexAndFlags;
        };

        struct ForwardPassInput
        {
            enum
            {
                Meshes,
                Sampler,
                Textures,
                Materials,
                DirectionalLights,
                PerFrameData,
                Count
            };
        };

        ShaderInputDesc ForwardPassInputs[ForwardPassInput::Count] = 
        {
            { ForwardPassInput::Meshes, VK_SHADER_STAGE_VERTEX_BIT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER },
            { ForwardPassInput::Sampler, VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_SAMPLER },
            { ForwardPassInput::Textures, VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, MaxImageCount, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT },
            { ForwardPassInput::Materials, VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER },
            { ForwardPassInput::DirectionalLights, VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER },
            { ForwardPassInput::PerFrameData, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER }
        };

        uint32_t constexpr MaxMaterialInstanceCount = 100;
        struct ForwardPass
        {
            static uint32_t constexpr SubpassCount = 2;

            ShaderBlockLayout ShaderLayout;

            GraphicsPipeline Pipelines[SubpassCount];

            ShaderBlock InputBlock;
            VkSampler Sampler;
            GraphicsBuffer MaterialBuffer;

            struct
            {
                Texture DepthImage;
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
            GraphicsBuffer LightBuffer;
            uint64_t DirectionalLightAllocations[IB::Bitset::bufferSize(MaxDirectionalLightCount)];

            struct
            {
                uint32_t Lights[MaxDirectionalLightCount];
                uint32_t Count = 0;
            } Cleanup[FrameBufferCount];
        };

        GraphicsAPI API;
        Context RendererContext;
        ForwardPass ForwardPass;
        Geometry Geometry;
        Lighting Lighting;
        StagingCore Staging;

        uint32_t constexpr MaxDescriptorUpdateCount = 128;
        struct
        {
            FixedSizedQueue<VkDescriptorImageInfo, MaxDescriptorUpdateCount> ImageInfos;
            FixedSizedQueue<VkWriteDescriptorSet, MaxDescriptorUpdateCount> Writes;
        } DescriptorUpdates;
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
    void initRenderer(RendererDesc const &desc)
    {
        HWND windowHandle;
        HINSTANCE instanceHandle;
        Win32::getWindowHandleAndInstance(desc.Window, &windowHandle, &instanceHandle);
        initGraphicsAPI({windowHandle, instanceHandle}, &API);
        initStaging(&API, {&API.Allocator}, &Staging);

        {
            VkFenceCreateInfo fenceCreateInfo = {};
            fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            IB_VKCHECK(vkCreateFence(API.LogicalDevice, &fenceCreateInfo, NoVkAllocator, &RendererContext.Present.ImmediateFence));
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
                TextureDesc textureAlloc = {};
                textureAlloc.Usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
                textureAlloc.Format = VK_FORMAT_D32_SFLOAT;
                textureAlloc.Width = RendererContext.Present.Surface.Extents.width;
                textureAlloc.Height = RendererContext.Present.Surface.Extents.height;
                textureAlloc.Stride = 4;
                textureAlloc.Aspect = VK_IMAGE_ASPECT_DEPTH_BIT;

                ForwardPass.FrameBuffer[fb].DepthImage = allocTexture(&API, textureAlloc);
            }

            for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
            {
                RendererContext.Present.FrameBuffer[fb].PrimaryCommandBuffer = allocCommandBuffer(&API, GraphicsQueue::Graphics);

                // Semaphore
                VkSemaphoreCreateInfo semaphoreCreateInfo = {};
                semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
                IB_VKCHECK(vkCreateSemaphore(API.LogicalDevice, &semaphoreCreateInfo, NoVkAllocator, &RendererContext.Present.FrameBuffer[fb].FinishedSemaphore));

                VkFenceCreateInfo fenceCreateInfo = {};
                fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
                IB_VKCHECK(vkCreateFence(API.LogicalDevice, &fenceCreateInfo, NoVkAllocator, &RendererContext.Present.FrameBuffer[fb].FinishedFence));
            }
        }

        // Forward pass constant data
        {
            // Graphics pipeline
            ForwardPass.ShaderLayout = allocShaderBlockLayout(&API,
                ShaderBlockLayoutDesc{
                    { ForwardPassInputs, ForwardPassInput::Count }
                });
        }

        // Indirect Draw Buffer
        for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
        {
            IndirectDraw.FrameDraws[fb] = allocGraphicsBuffer(&API,
                {
                    MaxIndirectDraws * sizeof(VkDrawIndexedIndirectCommand),
                    VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    VkMemory::Category::Misc
                });
            }

        IndirectDraw.PerFrameData = allocGraphicsBuffer(&API,
                {
                    FrameBufferCount * MaxPerFrameDrawDataSize,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    VkMemory::Category::Misc
                });

        // Mesh buffer
        {
            for (uint32_t i = 0; i < MaxMeshCount; i++)
            {
                Geometry.FreeIndices[i] = i;
            }
            Geometry.FreeIndexCount = MaxMeshCount;

            Geometry.MeshDataBuffers = allocGraphicsBuffer(&API,
                {
                    MeshBufferSize,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    0,
                    VkMemory::Category::Mesh
                });
        }

        ForwardPass.MaterialBuffer = allocGraphicsBuffer(&API,
            {
                MaxMaterialInstanceCount * sizeof(ForwardMaterial),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                0,
                VkMemory::Category::Misc
            });

        Lighting.LightBuffer = allocGraphicsBuffer(&API,
            {
                MaxDirectionalLightCount * sizeof(DirectionalLight),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                0,
                VkMemory::Category::Misc
            });

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
            ForwardPass.InputBlock = allocShaderBlock(&API, &ForwardPass.ShaderLayout);

            ShaderInputWrite writes[] =
            {
                { &ForwardPassInputs[ForwardPassInput::Meshes], ShaderInputWrite::BufferWrite, VkDescriptorBufferInfo{ Geometry.MeshDataBuffers.VulkanBuffer, 0, MeshBufferSize } },
                { &ForwardPassInputs[ForwardPassInput::Sampler], ShaderInputWrite::SamplerWrite, VkDescriptorBufferInfo{}, VkDescriptorImageInfo{ ForwardPass.Sampler } },
                { &ForwardPassInputs[ForwardPassInput::Materials], ShaderInputWrite::BufferWrite, VkDescriptorBufferInfo{ForwardPass.MaterialBuffer.VulkanBuffer, 0, MaxMaterialInstanceCount * sizeof(ForwardMaterial) } },
                { &ForwardPassInputs[ForwardPassInput::DirectionalLights], ShaderInputWrite::BufferWrite, VkDescriptorBufferInfo{ Lighting.LightBuffer.VulkanBuffer, 0, MaxDirectionalLightCount * sizeof(DirectionalLight) } },
                { &ForwardPassInputs[ForwardPassInput::PerFrameData], ShaderInputWrite::BufferWrite, VkDescriptorBufferInfo{IndirectDraw.PerFrameData.VulkanBuffer, 0, FrameBufferCount * MaxPerFrameDrawDataSize } },
            };

            writeToShaderBlock(&API,
                {
                    &ForwardPass.InputBlock,
                    { writes, arrayCount(writes) }
                });
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
                VkCommandBuffer uploadCommands = allocCommandBuffer(&API, GraphicsQueue::Graphics);

                VkCommandBufferBeginInfo beginBufferInfo = {};
                beginBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginBufferInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

                IB_VKCHECK(vkBeginCommandBuffer(uploadCommands, &beginBufferInfo));
                ImGui_ImplVulkan_CreateFontsTexture(uploadCommands);
                IB_VKCHECK(vkEndCommandBuffer(uploadCommands));

                VkSubmitInfo submitInfo = {};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &uploadCommands;
                IB_VKCHECK(vkQueueSubmit(API.Queues[GraphicsQueue::Graphics].Queue, 1, &submitInfo, RendererContext.Present.ImmediateFence));

                IB_VKCHECK(vkWaitForFences(API.LogicalDevice, 1, &RendererContext.Present.ImmediateFence, VK_TRUE, UINT64_MAX));
                IB_VKCHECK(vkResetFences(API.LogicalDevice, 1, &RendererContext.Present.ImmediateFence));

                freeCommandBuffer(&API, GraphicsQueue::Graphics, uploadCommands);
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
            freeCommandBuffer(&API, GraphicsQueue::Graphics, RendererContext.Present.FrameBuffer[fb].PrimaryCommandBuffer);
            vkDestroySemaphore(API.LogicalDevice, RendererContext.Present.FrameBuffer[fb].FinishedSemaphore, NoVkAllocator);
            vkDestroyFence(API.LogicalDevice, RendererContext.Present.FrameBuffer[fb].FinishedFence, NoVkAllocator);
            freeTexture(&API, &ForwardPass.FrameBuffer[fb].DepthImage);
        }

        freeGraphicsBuffer(&API, &Geometry.MeshDataBuffers);

        for (uint32_t i = 0; i < RendererContext.Textures.ImageCount; i++)
        {
            freeTexture(&API, &RendererContext.Textures.Images[i]);
        }

        freeShaderBlockLayout(&API, &ForwardPass.ShaderLayout);

        for (uint32_t i = 0; i < arrayCount(ForwardPass.Pipelines); i++)
        {
            freeGraphicsPipeline(&API, &ForwardPass.Pipelines[i]);
        }

        vkDestroySampler(API.LogicalDevice, ForwardPass.Sampler, NoVkAllocator);

        freeGraphicsBuffer(&API, &ForwardPass.MaterialBuffer);
        freeGraphicsBuffer(&API, &Lighting.LightBuffer);

        for (uint32_t fb = 0; fb < FrameBufferCount; fb++)
        {
            freeGraphicsBuffer(&API, &IndirectDraw.FrameDraws[fb]);
        }

        freeGraphicsBuffer(&API, &IndirectDraw.PerFrameData);

        vkDestroyFence(API.LogicalDevice, RendererContext.Present.ImmediateFence, NoVkAllocator);
        
        killStaging(&API, &API.Allocator, &Staging);
        killGraphicsAPI(&API);
    }

    void initForwardPass(ForwardPassDesc const &desc)
    {
        // Queue up our pipelines for cleanup by our rendering thread
        for (uint32_t i = 0; i < arrayCount(ForwardPass.Pipelines); i++)
        {
            if (ForwardPass.Pipelines[i].VulkanPipeline != VK_NULL_HANDLE)
            {
                for (uint32_t pip = 0; pip < MaxPipelineCleanups; pip++)
                {
                    if (RendererContext.Cleanup.Pipelines[pip].VulkanPipeline == VK_NULL_HANDLE)
                    {
                        RendererContext.Cleanup.Pipelines[pip] = ForwardPass.Pipelines[i];
                        break;
                    }
                }
            }
        }

        ShaderDesc const shaders[] =
        {
            {
                .EntryPoint = "vertexMain",
                .Code = desc.VShader,
                .CodeSize = desc.VShaderSize,
                .Stage = VK_SHADER_STAGE_VERTEX_BIT
            },
            {
                .EntryPoint = "fragMain",
                .Code = desc.FShader,
                .CodeSize = desc.FShaderSize,
                .Stage = VK_SHADER_STAGE_FRAGMENT_BIT
            }
        };

        RenderTargetDesc const screenRenderTarget = 
        {
            .BlendDesc = BlendDescAlphaBlend,
            .Format = RendererContext.Present.Surface.Format.format
        };

        VkPushConstantRange const pushConstant =
        {
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset = 0,
            .size = 256,
        };

        ShaderBlockLayout const* shaderLayouts[] = {&ForwardPass.ShaderLayout};
        GraphicsPipeline defaultFoward = allocGraphicsPipeline(&API,
        {
            .ShaderDescs = { shaders, arrayCount(shaders) },
            .PushConstants = { &pushConstant, 1 },
            .ShaderBlockLayouts = { shaderLayouts, 1 },
            .VertexDesc = VertexInputNull,
            .InputAssemblyDesc = InputAssemblyTriangleList,
            .RasterizationDesc = RasterizationCullBackFaceCCW,
            .RenderTargetDescs = { &screenRenderTarget, 1 },
            .DepthDesc = { DepthStateLessTestWriteNoStencil, VK_FORMAT_D32_SFLOAT },
        });

        GraphicsPipeline defaultDebugFoward = allocGraphicsPipeline(&API,
        {
            .ShaderDescs = { shaders, arrayCount(shaders) },
            .PushConstants = { &pushConstant, 1 },
            .ShaderBlockLayouts = { shaderLayouts, 1 },
            .VertexDesc = VertexInputNull,
            .InputAssemblyDesc = InputAssemblyTriangleList,
            .RasterizationDesc = RasterizationNoCull,
            .RenderTargetDescs = { &screenRenderTarget, 1 },
            .DepthDesc =  { DepthStateLessTestWriteNoStencil, VK_FORMAT_D32_SFLOAT },
        });
        
        
        // Write our pipelines after our call due to the fact that we expect this function to be called from another thread
        // And we'd like to assure that our writes are done at the right time.
        threadRelease();
        ForwardPass.Pipelines[0] = defaultFoward;
        ForwardPass.Pipelines[1] = defaultDebugFoward;
    }

    namespace
    {
        void writeMesh(uint32_t meshIndex, MeshDesc const& desc)
        {
            uint32_t vertexSize = desc.Vertices.Count * sizeof(Vertex);
            uint32_t indexSize = desc.Indices.Count * sizeof(uint16_t);

            VkCommandBuffer commandBuffer = allocCommandBuffer(&API, GraphicsQueue::Transfer);

            VkCommandBufferBeginInfo beginBufferInfo = {};
            beginBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginBufferInfo.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            IB_VKCHECK(vkBeginCommandBuffer(commandBuffer, &beginBufferInfo));

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
                    vkCmdCopyBuffer(commandBuffer, Geometry.MeshDataBuffers.VulkanBuffer, Geometry.MeshDataBuffers.VulkanBuffer, copyCount, copyRegions);
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

                vkCmdCopyBuffer(commandBuffer, staging.Buffer, Geometry.MeshDataBuffers.VulkanBuffer, 1, &meshCopy);
            }

            // End our command buffer
            {
                IB_VKCHECK(vkEndCommandBuffer(commandBuffer));

                VkSubmitInfo submitInfo = {};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &commandBuffer;
                IB_VKCHECK(vkQueueSubmit(API.Queues[GraphicsQueue::Transfer].Queue, 1, &submitInfo, staging.Fence));

                Staging.Transfers.add(commandBuffer);
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
        TextureDesc textureAlloc = {};
        textureAlloc.Usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        textureAlloc.Format = formats[desc.Format];
        textureAlloc.Width = desc.Width;
        textureAlloc.Height = desc.Height;
        textureAlloc.Stride = formatSize[desc.Format];
        textureAlloc.Aspect = VK_IMAGE_ASPECT_COLOR_BIT;

        RendererContext.Textures.Images[imageIndex] = allocTexture(&API, textureAlloc);

        writeToTexture(&API, &Staging, 
            {
                &RendererContext.Textures.Images[imageIndex],
                desc.Data,
                desc.Width * desc.Height * formatSize[desc.Format],
                formatSize[desc.Format]
            });

        // Queue up our shader descriptor update
        {
            VkDescriptorImageInfo* imageInfo = &DescriptorUpdates.ImageInfos.add();
            VkWriteDescriptorSet* descriptorWrite = &DescriptorUpdates.Writes.add();

            *imageInfo = {};
            imageInfo->imageView = RendererContext.Textures.Images[imageIndex].View;
            imageInfo->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            *descriptorWrite = {};
            descriptorWrite->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite->dstSet = ForwardPass.InputBlock.DescriptorSet;
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

        writeToBuffer(&API, &Staging, 
            {
                &ForwardPass.MaterialBuffer,
                &matData,
                sizeof(ForwardMaterial),
                1,
                instanceIndex * sizeof(ForwardMaterial)
            });

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

        writeToBuffer(&API, &Staging, 
            {
                &Lighting.LightBuffer,
                &lightData,
                sizeof(DirectionalLight),
                1,
                instanceIndex * sizeof(DirectionalLight)
            });

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

    void recordCommands(VkCommandBuffer currentCommands, uint32_t fb, ViewDesc const &view)
    {
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

                VkImageMemoryBarrier depthBarrier = createMemoryBarrier(
                    &ForwardPass.FrameBuffer[fb].DepthImage,
                    VkAccessFlagBits{},
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

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

            vkCmdBindIndexBuffer(currentCommands, Geometry.MeshDataBuffers.VulkanBuffer, 0, VK_INDEX_TYPE_UINT16);

            uint32_t perFrameDataWriteIndex = MaxPerFrameDrawDataSize * fb;
            PerFrameData perFrameData;
            perFrameData.WorldToProjection = view.ViewProj;
            memcpy(IndirectDraw.PerFrameData.Allocation.CPUMemory + perFrameDataWriteIndex, &perFrameData, sizeof(PerFrameData));
            perFrameDataWriteIndex += sizeof(PerFrameData);

            memcpy(IndirectDraw.PerFrameData.Allocation.CPUMemory + perFrameDataWriteIndex, &view.DirectionalLights.Count, sizeof(uint32_t));
            perFrameDataWriteIndex += sizeof(uint32_t);
            for (uint32_t dirLight = 0; dirLight < view.DirectionalLights.Count; dirLight++)
            {
                DirectionalLightInstance directionalLightInstance;

                directionalLightInstance.DirectionAndLightIndex =
                    {view.DirectionalLights.Lights[dirLight].Direction,
                        bitcast<float>(Light::toValue(view.DirectionalLights.Lights[dirLight].Light))};
                
                memcpy(IndirectDraw.PerFrameData.Allocation.CPUMemory + perFrameDataWriteIndex, &directionalLightInstance, sizeof(directionalLightInstance));
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

                if (ForwardPass.Pipelines[passIndex].VulkanPipeline == VK_NULL_HANDLE)
                {
                    continue;
                }

                VkDescriptorSet set = ForwardPass.InputBlock.DescriptorSet;
                vkCmdBindDescriptorSets(
                    currentCommands, VK_PIPELINE_BIND_POINT_GRAPHICS, ForwardPass.Pipelines[passIndex].Layout,
                    0, 1, &set, 0, VK_NULL_HANDLE);

                uint32_t passIndirectDrawCountOffset = indirectDrawCount;
                ViewDesc::ForwardPass const *pass = &view.Forward.Passes[passIndex];
                vkCmdBindPipeline(currentCommands, VK_PIPELINE_BIND_POINT_GRAPHICS, ForwardPass.Pipelines[passIndex].VulkanPipeline);

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

                        memcpy(IndirectDraw.FrameDraws[fb].Allocation.CPUMemory + indirectDrawCount * sizeof(VkDrawIndexedIndirectCommand), &drawIndexed, sizeof(VkDrawIndexedIndirectCommand));
                        indirectDrawCount += 1;

                        passDrawCount += batch->Meshes[mesh].Count;
                        for (uint32_t inst = 0; inst < batch->Meshes[mesh].Count; inst++)
                        {
                            PerInstanceData perInstanceData;
                            perInstanceData.LocalToWorld = batch->Meshes[mesh].Transforms[inst];
                            perInstanceData.VertexOffset = Geometry.Meshes[meshIndex].VertexOffset;
                            perInstanceData.MaterialIndex = materialIndex;
                            memcpy(IndirectDraw.PerFrameData.Allocation.CPUMemory + perFrameDataWriteIndex, &perInstanceData, sizeof(PerInstanceData));
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

                    vkCmdPushConstants(currentCommands, ForwardPass.Pipelines[passIndex].Layout,
                                        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstantBatchData), &pushConstantBatchData);

                    vkCmdDrawIndexedIndirect(currentCommands, IndirectDraw.FrameDraws[fb].VulkanBuffer, passIndirectDrawCountOffset * sizeof(VkDrawIndexedIndirectCommand), passDrawCount, sizeof(VkDrawIndexedIndirectCommand));
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
            if (RendererContext.Cleanup.Pipelines[i].VulkanPipeline != VK_NULL_HANDLE)
            {
                vkDeviceWaitIdle(API.LogicalDevice);
                freeGraphicsPipeline(&API, &RendererContext.Cleanup.Pipelines[i]);
                RendererContext.Cleanup.Pipelines[i] = {};
            }
        }

        // Start the frame
        uint32_t fb = RendererContext.Present.ActiveFrame;

        SurfacePrepareResult prepareResult;
        if(!prepareSurface(&API, { &RendererContext.Present.Surface, fb }, &prepareResult))
        {
            if(prepareResult.SurfaceState == SurfaceState::ShouldRebuild)
            {
                rebuildSurface(&API, &RendererContext.Present.Surface);
            }
        }
        else
        {
            IB_VKCHECK(vkWaitForFences(API.LogicalDevice, 1, &RendererContext.Present.FrameBuffer[fb].FinishedFence, VK_TRUE, UINT64_MAX));
            IB_VKCHECK(vkResetFences(API.LogicalDevice, 1, &RendererContext.Present.FrameBuffer[fb].FinishedFence));

            flushStaging(&API, &Staging);

            // Descriptor updates
            if (DescriptorUpdates.Writes.Count > 0)
            {
                vkUpdateDescriptorSets(API.LogicalDevice, DescriptorUpdates.Writes.Count, DescriptorUpdates.Writes.Data, 0, NULL);
                DescriptorUpdates.Writes.Count = 0;
                DescriptorUpdates.ImageInfos.Count = 0;
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

            recordCommands(RendererContext.Present.FrameBuffer[fb].PrimaryCommandBuffer, fb, view);

            VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            VkSubmitInfo submitInfo = {};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.commandBufferCount = 1;
            submitInfo.pCommandBuffers = &RendererContext.Present.FrameBuffer[fb].PrimaryCommandBuffer;
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &RendererContext.Present.Surface.Framebuffers[fb].AcquireSemaphore;
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &RendererContext.Present.FrameBuffer[fb].FinishedSemaphore;
            submitInfo.pWaitDstStageMask = &dstStageMask;
            IB_VKCHECK(vkQueueSubmit(API.Queues[GraphicsQueue::Graphics].Queue, 1, &submitInfo, RendererContext.Present.FrameBuffer[fb].FinishedFence));

            SurfacePresentResult presentResult;
            presentSurface(&API, { &RendererContext.Present.Surface, prepareResult, RendererContext.Present.FrameBuffer[fb].FinishedSemaphore }, &presentResult);

            if(presentResult.SurfaceState == SurfaceState::ShouldRebuild)
            {
                rebuildSurface(&API, &RendererContext.Present.Surface);
            }

            // Only advance our active frame if we actually rendered anything.
            RendererContext.Present.ActiveFrame = (RendererContext.Present.ActiveFrame + 1) % FrameBufferCount;
        }
    }
} // namespace IB
