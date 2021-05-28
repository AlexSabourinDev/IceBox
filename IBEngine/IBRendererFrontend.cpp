#include "IBRendererFrontend.h"
#include "IBRenderer.h"
#include "IBSerialization.h"
#include "IBAsset.h"
#include "IBLogging.h"
#include "IBAllocator.h"

#include <imgui/imgui.h>

namespace
{
    constexpr uint32_t InitPassesJobQueueIndex = 1;

    IB::ImageHandle toImageHandle(IB::Asset::AssetHandle asset) { return {static_cast<uint32_t>(asset.Value)}; }

    class ImageAssetStreamer : public IB::Asset::IStreamer
    {
    public:
        IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
        {
            enum
            {
                Load = 0,
                Create
            };

            if (context->State == Load)
            {
                IB::ImageAsset image;
                fromBinary(&context->Stream, &image, context->SaveVersion);

                IB::JobHandle imageCreate = IB::launchJob([image, imageHandle = &context->Data]() {
                    IB::ImageDesc imageDesc = {};
                    imageDesc.Format = image.Format;
                    imageDesc.Width = image.Width;
                    imageDesc.Height = image.Height;
                    imageDesc.Data = image.Data;
                    *imageHandle = IB::createImage(imageDesc).Value;

                    return IB::JobResult::Complete;
                },
                                                          IB::RendererJobQueueIndex);

                return IB::Asset::wait(&imageCreate, 1, Create);
            }
            else
            {
                return IB::Asset::complete({context->Data});
            }
        }

        void unloadThreadSafe(IB::Asset::AssetHandle) override
        {
            // TODO: Handle image destruction
        }
    };
    ImageAssetStreamer ImageStreamer;

    class MeshAssetStreamer : public IB::Asset::IStreamer
    {
    public:
        IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
        {
            enum
            {
                Load = 0,
                Create
            };

            if (context->State == Load)
            {
                IB::MeshAsset mesh;
                fromBinary(&context->Stream, &mesh, context->SaveVersion);

                IB::JobHandle meshCreate = IB::launchJob([mesh, meshHandle = &context->Data]() {
                    IB::MeshDesc meshDesc = {};
                    meshDesc.Vertices.Data = mesh.Vertices;
                    meshDesc.Vertices.Count = mesh.VertexCount;
                    meshDesc.Indices.Data = mesh.Indices;
                    meshDesc.Indices.Count = mesh.IndexCount;
                    *meshHandle = createMesh(meshDesc).Value;

                    return IB::JobResult::Complete;
                },
                    IB::RendererJobQueueIndex);

                return IB::Asset::wait(&meshCreate, 1, Create);
            }
            else
            {
                return IB::Asset::complete({context->Data});
            }
        }

        void unloadThreadSafe(IB::Asset::AssetHandle) override
        {
            // TODO: Handle mesh destruction
        }
    };
    MeshAssetStreamer MeshStreamer;

    struct RuntimeMaterial
    {
        IB::MaterialHandle Material;
        IB::MaterialAsset Asset;
        IB::Asset::ResourceHandle AlbedoTexture;
    };

    IB::ThreadSafePool<RuntimeMaterial> MaterialPool;
    class MaterialAssetStreamer : public IB::Asset::IStreamer
    {
    public:
        IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
        {
            enum
            {
                Load = 0,
                Create
            };

            if (context->State == Load)
            {
                RuntimeMaterial &materialData = MaterialPool.add();
                fromBinary(&context->Stream, &materialData.Asset, context->SaveVersion);

                IB::JobHandle textureJob = IB::Asset::loadResourceAsync(materialData.Asset.AlbedoPath, IB::Asset::toFourCC("IMAG"), &materialData.AlbedoTexture);
                IB::JobHandle materialJob = IB::continueJob([materialPtr = &materialData, contextData = &context->Data]() {
                    IB::Asset::ResourceHandle textureResource = materialPtr->AlbedoTexture;

                    IB::ForwardDesc materialDesc = {};
                    materialDesc.AlbedoTint = materialPtr->Asset.AlbedoTint;
                    materialDesc.AlbedoImage = toImageHandle(IB::Asset::getAssetFromResource(textureResource));
                    materialPtr->Material = IB::createMaterial(materialDesc);
                    *contextData = reinterpret_cast<uint64_t>(materialPtr);

                    return IB::JobResult::Complete;
                },
                                                            &textureJob, 1, IB::RendererJobQueueIndex);

                return IB::Asset::wait(&materialJob, 1, Create);
            }
            else
            {
                return IB::Asset::complete({context->Data});
            }
        }

        void saveThreadSafe(IB::Asset::SaveContext *context) override
        {
            RuntimeMaterial *material = reinterpret_cast<RuntimeMaterial *>(context->Asset.Value);
            toBinary(context->Stream, material->Asset);
        }

        void unloadThreadSafe(IB::Asset::AssetHandle asset) override
        {
            RuntimeMaterial *material = reinterpret_cast<RuntimeMaterial *>(asset.Value);
            IB::Asset::releaseResourceAsync(material->AlbedoTexture);
            // TODO: Handle material destruction in renderer backend
            MaterialPool.remove(*material);
        }
    };
    MaterialAssetStreamer MaterialStreamer;

    IB::ShaderAsset *toShaderAsset(IB::Asset::AssetHandle handle)
    {
        return reinterpret_cast<IB::ShaderAsset *>(handle.Value);
    }

    class ShaderAssetStreamer : public IB::Asset::IStreamer
    {
    public:
        IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
        {
            IB::ShaderAsset *shaders = IB::allocate<IB::ShaderAsset>();
            fromBinary(&context->Stream, shaders, context->SaveVersion);

            return IB::Asset::complete({reinterpret_cast<uint64_t>(shaders)});
        }

        void unloadThreadSafe(IB::Asset::AssetHandle asset) override
        {
            IB::deallocate(toShaderAsset(asset));
        }
    };
    ShaderAssetStreamer ShaderStreamer;

    struct RendererProperty
    {
        IB::Asset::ResourceHandle MeshResource;
        IB::Asset::ResourceHandle MaterialResource;
    };
    IB::ThreadSafePool<RendererProperty> RendererProperties;

    class RendererPropertyAssetStreamer : public IB::Asset::IStreamer
    {
    public:
        IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
        {
            enum
            {
                LoadMesh = 0,
                Complete
            };

            if (context->State == LoadMesh)
            {
                RendererProperty &renderer = RendererProperties.add();
                context->Data = reinterpret_cast<uint64_t>(&renderer);

                char const *meshPath;
                fromBinary(&context->Stream, &meshPath);

                char const *materialPath;
                fromBinary(&context->Stream, &materialPath);

                enum
                {
                    MeshJob = 0,
                    MaterialJob,
                    JobCount
                };
                IB::JobHandle jobs[JobCount];

                jobs[MeshJob] = IB::Asset::loadResourceAsync(meshPath, IB::Asset::toFourCC("MESH"),
                                                             [](void *data, IB::Asset::ResourceHandle resource, IB::Asset::ResourceLoad::State loadState) {
                                                                 // Assign our mesh resource even when it's loading
                                                                 // We want to gracefully handle the resource not being available yet.
                                                                 if (loadState >= IB::Asset::ResourceLoad::Loading)
                                                                 {
                                                                     *reinterpret_cast<IB::Asset::ResourceHandle *>(data) = resource;
                                                                 }
                                                             },
                                                             &renderer.MeshResource);

                jobs[MaterialJob] = IB::Asset::loadResourceAsync(materialPath, IB::Asset::toFourCC("MATE"),
                                                                 [](void *data, IB::Asset::ResourceHandle resource, IB::Asset::ResourceLoad::State loadState) {
                                                                     // Assign our mesh resource even when it's loading
                                                                     // We want to gracefully handle the resource not being available yet.
                                                                     if (loadState >= IB::Asset::ResourceLoad::Loading)
                                                                     {
                                                                         *reinterpret_cast<IB::Asset::ResourceHandle *>(data) = resource;
                                                                     }
                                                                 },
                                                                 &renderer.MaterialResource);

                return IB::Asset::wait(jobs, JobCount, Complete);
            }
            else
            {
                return IB::Asset::complete({context->Data});
            }
        }

        void saveThreadSafe(IB::Asset::SaveContext *context) override
        {
            RendererProperty *renderer = reinterpret_cast<RendererProperty *>(context->Asset.Value);
            char const *meshPath = IB::Asset::getResourcePath(renderer->MeshResource);
            toBinary(context->Stream, meshPath);

            char const *materialPath = IB::Asset::getResourcePath(renderer->MaterialResource);
            toBinary(context->Stream, materialPath);
        }

        void unloadThreadSafe(IB::Asset::AssetHandle asset) override
        {
            RendererProperty *renderer = reinterpret_cast<RendererProperty *>(asset.Value);
            IB::Asset::releaseResourceAsync(renderer->MeshResource);
            IB::Asset::releaseResourceAsync(renderer->MaterialResource);
            RendererProperties.remove(*renderer);
        }
    };

    RendererPropertyAssetStreamer RendererPropertyStreamer;

    constexpr uint32_t FormatStride[IB::ImageFormat::Count] =
        {
            4};

} // namespace

namespace IB
{
    void initRendererFrontend(RendererFrontendDesc const &desc)
    {
        Asset::addStreamer(Asset::toFourCC("MESH"), &MeshStreamer);
        Asset::addStreamer(Asset::toFourCC("IMAG"), &ImageStreamer);
        Asset::addStreamer(Asset::toFourCC("MATE"), &MaterialStreamer);
        Asset::addStreamer(Asset::toFourCC("SHDR"), &ShaderStreamer);
        Asset::addStreamer(Asset::toFourCC("RNDR"), &RendererPropertyStreamer);

        RendererDesc rendererDesc = {desc.Window};
        initRenderer(rendererDesc);

        Asset::loadResourceAsync("SampleForward.shdr", Asset::toFourCC("SHDR"),
                                 [](void*, Asset::ResourceHandle resource, IB::Asset::ResourceLoad::State loadState) {
                                     if (loadState == Asset::ResourceLoad::Available)
                                     {
                                         launchJob([resource]() {
                                             ShaderAsset *shaders = toShaderAsset(Asset::getAssetFromResource(resource));
                                             ForwardPassDesc desc;
                                             desc.VShader = shaders->VertexShader;
                                             desc.VShaderSize = shaders->VertexShaderSize;
                                             desc.FShader = shaders->FragShader;
                                             desc.FShaderSize = shaders->FragShaderSize;
                                             initForwardPass(desc);

                                             Asset::releaseResourceAsync(resource);
                                             return JobResult::Complete;
                                         },
                                             InitPassesJobQueueIndex);
                                     }
                                 });
    }

    void killRendererFrontend()
    {
        launchJob([]() {
            killRenderer();
            return JobResult::Complete;
        },
                  RendererJobQueueIndex);
    }

    PropertyHandle createRendererProperty(char const *meshPath, char const *materialPath)
    {
        RendererProperty &renderer = RendererProperties.add();
        IB::Asset::loadResourceAsync(meshPath, IB::Asset::toFourCC("MESH"),
                                     [](void *data, IB::Asset::ResourceHandle resource, IB::Asset::ResourceLoad::State loadState) {
                                         // Assign our mesh resource even when it's loading
                                         // We want to gracefully handle the resource not being available yet.
                                         if (loadState >= IB::Asset::ResourceLoad::Loading)
                                         {
                                             *reinterpret_cast<IB::Asset::ResourceHandle *>(data) = resource;
                                         }
                                     },
                                     &renderer.MeshResource);

        IB::Asset::loadResourceAsync(materialPath, IB::Asset::toFourCC("MATE"),
                                     [](void *data, IB::Asset::ResourceHandle resource, IB::Asset::ResourceLoad::State loadState) {
                                         // Assign our mesh resource even when it's loading
                                         // We want to gracefully handle the resource not being available yet.
                                         if (loadState >= IB::Asset::ResourceLoad::Loading)
                                         {
                                             *reinterpret_cast<IB::Asset::ResourceHandle *>(data) = resource;
                                         }
                                     },
                                     &renderer.MaterialResource);

        return {reinterpret_cast<uint64_t>(&renderer)};
    }

    MaterialAssetHandle createMaterialAsset(MaterialAsset asset)
    {
        RuntimeMaterial &materialData = MaterialPool.add();
        materialData.Asset = asset;
        JobHandle textureJob = Asset::loadResourceAsync(asset.AlbedoPath, Asset::toFourCC("IMAG"), &materialData.AlbedoTexture);
        continueJob([materialPtr = &materialData]() {
            IB::Asset::ResourceHandle textureResource = materialPtr->AlbedoTexture;
            IB::ForwardDesc materialDesc = {};
            materialDesc.AlbedoTint = materialPtr->Asset.AlbedoTint;
            materialDesc.AlbedoImage = toImageHandle(IB::Asset::getAssetFromResource(textureResource));
            materialPtr->Material = IB::createMaterial(materialDesc);
            return IB::JobResult::Complete;
        },
                    &textureJob, 1, RendererJobQueueIndex);

        return {reinterpret_cast<uint64_t>(&materialData)};
    }

    ViewDesc gatherCellView(CellHandle cell, ImDrawData* sourceImguiDrawCommands, StackAllocator* allocator)
    {
        // Clone our imgui draw data
        ImDrawData* imguiDrawData = nullptr;
        if (sourceImguiDrawCommands != nullptr && sourceImguiDrawCommands->CmdListsCount > 0)
        {
            imguiDrawData = allocate<ImDrawData>(*sourceImguiDrawCommands);
            imguiDrawData->CmdLists = allocateArray<ImDrawList*>(imguiDrawData->CmdListsCount);
            for (int32_t i = 0; i < imguiDrawData->CmdListsCount; i++)
            {
                imguiDrawData->CmdLists[i] = sourceImguiDrawCommands->CmdLists[i]->CloneOutput();
            }
        }

        EntityHandle *entities;
        uint32_t entityCount;
        getEntityList(cell, &entities, &entityCount);

        uint32_t rendererCount = 0;
        MeshHandle meshes[32];
        MaterialHandle materials[32];
        for (uint32_t i = 0; i < entityCount; i++)
        {
            PropertyHandle handle = getPropertyFromEntity(entities[i], Asset::toFourCC("RNDR"));
            if (handle.Value != InvalidProperty.Value)
            {
                RendererProperty *renderer = reinterpret_cast<RendererProperty *>(handle.Value);
                if (Asset::isResourceAssetAvailable(renderer->MeshResource) && Asset::isResourceAssetAvailable(renderer->MaterialResource))
                {
                    Asset::AssetHandle materialAsset = Asset::getAssetFromResource(renderer->MaterialResource);
                    RuntimeMaterial *material = reinterpret_cast<RuntimeMaterial *>(materialAsset.Value);
                    // Assure that our material is valid
                    // It's possible our loading thread hasn't finished creating it for us
                    if (material->Material.Value != InvalidMaterial.Value)
                    {
                        meshes[rendererCount] = { static_cast<uint32_t>(Asset::getAssetFromResource(renderer->MeshResource).Value) };
                        materials[rendererCount] = material->Material;
                        rendererCount++;
                    }
                }
            }
        }

        ViewDesc viewDesc = {};
        {
            Mat3x4 meshTransform =
            {
                {{1.0f, 0.0f, 0.0f, 0.0f},
                 {0.0f, 1.0f, 0.0f, 5.0f},
                 {0.0f, 0.0f, 1.0f, 0.0f}},
            };

            Float3 viewPos = Float3{ -2.0f, 1.0f, 0.0f };
            Mat4x4 view =
            {
                {{1.0f, 0.0f, 0.0f, -viewPos.x},
                 {0.0f, 0.0f, 1.0f, -viewPos.z},
                 {0.0f, 1.0f, 0.0f, -viewPos.y},
                 {0.0f, 0.0f, 0.0f, 1.0f}},
            };

            float fov = 1.0f / tanf(3.1415f * 0.25);
            const float f = 45.0f;
            const float n = 1.0f;
            const float a = 1.0f;
            Mat4x4 projection =
            {
                {{fov / a, 0.0f, 0.0f, 0.0f},
                 {0.0f, -fov, 0.0f, 0.0f},
                 {0.0f, 0.0f, f / (f - n), -n * f / (f - n)},
                 {0.0f, 0.0f, 1.0, 0.0f}},
            };

            Mat4x4 viewProj = mul(projection, view);

            ViewDesc::MeshInstances* meshInstances = allocateArray<ViewDesc::MeshInstances>(allocator, rendererCount);

            for (uint32_t i = 0; i < rendererCount; i++)
            {
                meshInstances[i].Mesh = meshes[i];
                meshInstances[i].Transforms = allocateArray<Mat3x4>(allocator, 1, meshTransform);
                meshInstances[i].Count = 1;
            }

            ViewDesc::Batch* meshBatch = allocateArray<ViewDesc::Batch>(allocator, 1);
            meshBatch->Material = materials[0]; // TODO
            meshBatch->Meshes = meshInstances;
            meshBatch->MeshCount = rendererCount;

            if (rendererCount > 0)
            {
                ViewDesc::ForwardPass worldPass;
                worldPass.Batches = meshBatch;
                worldPass.BatchCount = 1;
                viewDesc.Forward.Passes[ViewDesc::ForwardPass::Default] = worldPass;
            }

            viewDesc.ViewProj = viewProj;
            viewDesc.ImGuiDrawData = imguiDrawData;
        }

        return viewDesc;
    }

    void drawCellView(ViewDesc const& viewDesc)
    {
        drawView(viewDesc);

        // Clean up our ImGui data
        if (viewDesc.ImGuiDrawData != nullptr)
        {
            for (int32_t i = 0; i < viewDesc.ImGuiDrawData->CmdListsCount; i++)
            {
                IM_DELETE(viewDesc.ImGuiDrawData->CmdLists[i]);
            }
            deallocateArray(viewDesc.ImGuiDrawData->CmdLists, viewDesc.ImGuiDrawData->CmdListsCount);
            deallocate(viewDesc.ImGuiDrawData);
        }
    }

    void toBinary(Serialization::FileStream *stream, MeshAsset const &mesh)
    {
        Serialization::toBinary(stream, mesh.VertexCount);
        Serialization::toBinary(stream, mesh.IndexCount);

        Serialization::toBinary(stream, mesh.Vertices, mesh.VertexCount * sizeof(Vertex));
        Serialization::toBinary(stream, mesh.Indices, mesh.IndexCount * sizeof(uint16_t));
    }

    void fromBinary(Serialization::MemoryStream *stream, MeshAsset *mesh, uint32_t /*versionNumber*/)
    {
        Serialization::fromBinary(stream, &mesh->VertexCount);
        Serialization::fromBinary(stream, &mesh->IndexCount);

        mesh->Vertices = Serialization::fromBinary<Vertex const *>(stream, mesh->VertexCount * sizeof(Vertex));
        mesh->Indices = Serialization::fromBinary<uint16_t const *>(stream, mesh->IndexCount * sizeof(uint16_t));
    }

    void toBinary(Serialization::FileStream *stream, ShaderAsset const &shaders)
    {
        Serialization::toBinary(stream, shaders.VertexShaderSize);
        Serialization::toBinary(stream, shaders.FragShaderSize);

        Serialization::toBinary(stream, shaders.VertexShader, shaders.VertexShaderSize);
        Serialization::toBinary(stream, shaders.FragShader, shaders.FragShaderSize);
    }

    void fromBinary(Serialization::MemoryStream *stream, ShaderAsset *shaders, uint32_t /*versionNumber*/)
    {
        Serialization::fromBinary(stream, &shaders->VertexShaderSize);
        Serialization::fromBinary(stream, &shaders->FragShaderSize);

        shaders->VertexShader = Serialization::fromBinary<uint8_t const *>(stream, shaders->VertexShaderSize);
        shaders->FragShader = Serialization::fromBinary<uint8_t const *>(stream, shaders->FragShaderSize);
    }

    void toBinary(Serialization::FileStream *stream, MaterialAsset const &material)
    {
        Serialization::toBinary(stream, material.AlbedoTint);
        Serialization::toBinary(stream, material.AlbedoPath);
    }

    void fromBinary(Serialization::MemoryStream *stream, MaterialAsset *material, uint32_t /*versionNumber*/)
    {
        Serialization::fromBinary(stream, &material->AlbedoTint);
        Serialization::fromBinary(stream, &material->AlbedoPath);
    }

    void toBinary(Serialization::FileStream *stream, ImageAsset const &image)
    {
        Serialization::toBinary(stream, image.Format);
        Serialization::toBinary(stream, image.Width);
        Serialization::toBinary(stream, image.Height);

        Serialization::toBinary(stream, image.Data, image.Width * image.Height * FormatStride[image.Format]);
    }

    void fromBinary(Serialization::MemoryStream *stream, ImageAsset *image, uint32_t /*versionNumber*/)
    {
        Serialization::fromBinary(stream, &image->Format);
        Serialization::fromBinary(stream, &image->Width);
        Serialization::fromBinary(stream, &image->Height);
        image->Data = Serialization::fromBinary<uint8_t const *>(stream, image->Width * image->Height * FormatStride[image->Format]);
    }
} // namespace IB
