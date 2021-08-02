#include "IBRendererFrontend.h"
#include "IBRenderer.h"
#include "IBSerialization.h"
#include "IBAsset.h"
#include "IBLogging.h"
#include "IBAllocator.h"
#include "IBEntity.h"
#include "IBJobs.h"
#include "IBEditor.h"
#include "IBPlatform.h"

#include <imgui/imgui.h>

namespace
{
    constexpr uint32_t InitPassesJobQueueIndex = 1;

    IB::ImageHandle toImageHandle(IB::Asset::Handle asset) { return {static_cast<uint32_t>(IB::Asset::toValue(asset))}; }

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

                IB::Job::Handle imageCreate = IB::launchJob([image, imageHandle = &context->Data]() {
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
                return IB::Asset::complete(IB::Asset::toHandle(context->Data));
            }
        }

        void unloadThreadSafe(IB::Asset::Handle) override
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

                IB::Job::Handle meshCreate = IB::launchJob([mesh, meshHandle = &context->Data]() {
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
                return IB::Asset::complete(IB::Asset::toHandle(context->Data));
            }
        }

        void unloadThreadSafe(IB::Asset::Handle) override
        {
            // TODO: Handle mesh destruction
        }
    };
    MeshAssetStreamer MeshStreamer;

    struct RuntimeMaterial
    {
        IB::MaterialHandle Material;
        IB::MaterialAsset Asset;
        IB::Resource::Handle AlbedoTexture;
    };

    IB::ThreadSafePool<RuntimeMaterial> MaterialPool;
    class MaterialAssetStreamer : public IB::Asset::IStreamer
    {
    public:
        IB::Asset::Handle create() override
        {
            RuntimeMaterial &materialData = MaterialPool.add();
            return IB::Asset::toHandle(reinterpret_cast<uint64_t>(&materialData));
        }
        IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
        {
            enum
            {
                Load = 0,
                Create
            };

            if (context->State == Load)
            {
                RuntimeMaterial& materialData = *reinterpret_cast<RuntimeMaterial*>(IB::Asset::toValue(context->LoadingAsset));
                fromBinary(&context->Stream, &materialData.Asset, context->SaveVersion);

                IB::Job::Handle textureJob = IB::Asset::loadResourceAsync(materialData.Asset.AlbedoPath, { IB::Asset::toFourCC("IMAG") }, &materialData.AlbedoTexture);
                IB::Job::Handle materialJob = IB::continueJob([materialPtr = &materialData, contextData = &context->Data]() {
                    IB::Resource::Handle textureResource = materialPtr->AlbedoTexture;

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
                return IB::Asset::complete(IB::Asset::toHandle(context->Data));
            }
        }

        void saveThreadSafe(IB::Asset::SaveContext *context) override
        {
            RuntimeMaterial *material = reinterpret_cast<RuntimeMaterial *>(IB::Asset::toValue(context->Asset));
            toBinary(context->Stream, material->Asset);
        }

        void unloadThreadSafe(IB::Asset::Handle asset) override
        {
            RuntimeMaterial *material = reinterpret_cast<RuntimeMaterial *>(IB::Asset::toValue(asset));
            IB::Asset::releaseResourceAsync(material->AlbedoTexture);
            // TODO: Handle material destruction in renderer backend
            MaterialPool.remove(*material);
        }
    };
    MaterialAssetStreamer MaterialStreamer;

    IB::ShaderAsset *toShaderAsset(IB::Asset::Handle handle)
    {
        return reinterpret_cast<IB::ShaderAsset *>(IB::Asset::toValue(handle));
    }

    class ShaderAssetStreamer : public IB::Asset::IStreamer
    {
    public:
        IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
        {
            IB::ShaderAsset *shaders = IB::allocate<IB::ShaderAsset>();
            fromBinary(&context->Stream, shaders, context->SaveVersion);

            return IB::Asset::complete(IB::Asset::toHandle(reinterpret_cast<uint64_t>(shaders)));
        }

        void unloadThreadSafe(IB::Asset::Handle asset) override
        {
            IB::deallocate(toShaderAsset(asset));
        }
    };
    ShaderAssetStreamer ShaderStreamer;

    struct RendererProperty
    {
        IB::Resource::Handle MeshResource;
        IB::Resource::Handle MaterialResource;
    };
    RendererProperty constexpr EmptyRendererProperty = {};

    RendererProperty* toRendererProperty(IB::Asset::Handle assetHandle) { return reinterpret_cast<RendererProperty *>(IB::Asset::toValue(assetHandle)); }
    RendererProperty* toRendererProperty(IB::Property::Handle propertyHandle) { return reinterpret_cast<RendererProperty *>(IB::Property::toValue(propertyHandle)); }

    IB::ThreadSafePool<RendererProperty> RendererProperties;

    class RendererPropertyAssetStreamer : public IB::Asset::IStreamer
    {
    public:
        IB::Asset::Handle create() override
        {
            RendererProperty &renderer = RendererProperties.add();
            return IB::Asset::toHandle(reinterpret_cast<uint64_t>(&renderer));
        }

        IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
        {
            enum
            {
                LoadMesh = 0,
                Complete
            };

            if (context->State == LoadMesh)
            {
                RendererProperty const* baseRenderer = &EmptyRendererProperty;
                if (context->BaseAsset != IB::InvalidAsset)
                {
                    baseRenderer = toRendererProperty(context->BaseAsset);
                }

                RendererProperty &renderer = *toRendererProperty(context->LoadingAsset);
                context->Data = reinterpret_cast<uint64_t>(&renderer);

                char const *baseMeshPath = IB::Asset::getResourcePath(baseRenderer->MeshResource);

                char const *meshPath;
                IB::Serialization::Delta::fromBinary(&context->Stream, &meshPath, baseMeshPath);

                char const *baseMaterialPath = IB::Asset::getResourcePath(baseRenderer->MaterialResource);

                char const *materialPath;
                IB::Serialization::Delta::fromBinary(&context->Stream, &materialPath, baseMaterialPath);

                enum
                {
                    MeshJob = 0,
                    MaterialJob,
                    JobCount
                };
                IB::Job::Handle jobs[JobCount];

                jobs[MeshJob] = IB::Asset::loadResourceAsync(meshPath, { IB::Asset::toFourCC("MESH") },
                                                             [](void *data, IB::Resource::Handle resource, IB::Asset::ResourceLoad::State loadState) {
                                                                 // Assign our mesh resource even when it's loading
                                                                 // We want to gracefully handle the resource not being available yet.
                                                                 if (loadState >= IB::Asset::ResourceLoad::Loading)
                                                                 {
                                                                     *reinterpret_cast<IB::Resource::Handle *>(data) = resource;
                                                                 }
                                                             },
                                                             &renderer.MeshResource);

                jobs[MaterialJob] = IB::Asset::loadResourceAsync(materialPath, { IB::Asset::toFourCC("MATE") },
                                                                 [](void *data, IB::Resource::Handle resource, IB::Asset::ResourceLoad::State loadState) {
                                                                     // Assign our mesh resource even when it's loading
                                                                     // We want to gracefully handle the resource not being available yet.
                                                                     if (loadState >= IB::Asset::ResourceLoad::Loading)
                                                                     {
                                                                         *reinterpret_cast<IB::Resource::Handle *>(data) = resource;
                                                                     }
                                                                 },
                                                                 &renderer.MaterialResource);

                return IB::Asset::wait(jobs, JobCount, Complete);
            }
            else
            {
                return IB::Asset::complete(IB::Asset::toHandle(context->Data));
            }
        }

        void saveThreadSafe(IB::Asset::SaveContext *context) override
        {
            RendererProperty const* baseRenderer = &EmptyRendererProperty;
            if (context->BaseAsset != IB::InvalidAsset)
            {
                baseRenderer = toRendererProperty(context->BaseAsset);
            }

            RendererProperty *renderer = toRendererProperty(context->Asset);
            char const *meshPath = IB::Asset::getResourcePath(renderer->MeshResource);
            char const *baseMeshPath = IB::Asset::getResourcePath(baseRenderer->MeshResource);
            IB::Serialization::Delta::toBinary(context->Stream, meshPath, baseMeshPath);

            char const *materialPath = IB::Asset::getResourcePath(renderer->MaterialResource);
            char const *baseMaterialPath = IB::Asset::getResourcePath(baseRenderer->MaterialResource);
            IB::Serialization::Delta::toBinary(context->Stream, materialPath, baseMaterialPath);
        }

        void unloadThreadSafe(IB::Asset::Handle asset) override
        {
            RendererProperty *renderer = toRendererProperty(asset);
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
    namespace Editor
    {
        void rendererEditor(Asset::Handle asset)
        {
            RendererProperty *renderer = toRendererProperty(asset);
            if (ImGui::TreeNode("Renderer"))
            {
                if (ImGui::BeginTable("Inspector Table", 2))
                {
                    ImGui::TableNextColumn();
                    ImGui::Text("Mesh");

                    ImGui::TableNextColumn();
                    ImGui::Text(Asset::getResourcePath(renderer->MeshResource));

                    ImGui::TableNextColumn();
                    ImGui::Text("Material");

                    ImGui::TableNextColumn();
                    ImGui::Text(Asset::getResourcePath(renderer->MaterialResource));

                    ImGui::EndTable();
                }
                ImGui::TreePop();
            }
        }
    }

    void initRendererFrontend(RendererFrontendDesc const &desc)
    {
        Asset::addStreamer(Asset::toFourCC("MESH"), &MeshStreamer);
        Asset::addStreamer(Asset::toFourCC("IMAG"), &ImageStreamer);
        Asset::addStreamer(Asset::toFourCC("MATE"), &MaterialStreamer);
        Asset::addStreamer(Asset::toFourCC("SHDR"), &ShaderStreamer);
        Asset::addStreamer(Asset::toFourCC("RNDR"), &RendererPropertyStreamer);

        RendererDesc rendererDesc = {desc.Window};
        initRenderer(rendererDesc);

        Asset::loadResourceAsync("SampleForward.shdr", { Asset::toFourCC("SHDR") },
                                 [](void*, Resource::Handle resource, IB::Asset::ResourceLoad::State loadState) {
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

        Editor::addAssetEditor(Asset::toFourCC("RNDR"), &Editor::rendererEditor);
    }

    void killRendererFrontend()
    {
        launchJob([]() {
            killRenderer();
            return JobResult::Complete;
        },
                  RendererJobQueueIndex);
    }

    Property::Handle createRendererProperty(char const *meshPath, char const *materialPath)
    {
        RendererProperty &renderer = RendererProperties.add();
        IB::Asset::loadResourceAsync(meshPath, { IB::Asset::toFourCC("MESH") },
                                     [](void *data, IB::Resource::Handle resource, IB::Asset::ResourceLoad::State loadState) {
                                         // Assign our mesh resource even when it's loading
                                         // We want to gracefully handle the resource not being available yet.
                                         if (loadState >= IB::Asset::ResourceLoad::Loading)
                                         {
                                             *reinterpret_cast<IB::Resource::Handle *>(data) = resource;
                                         }
                                     },
                                     &renderer.MeshResource);

        IB::Asset::loadResourceAsync(materialPath, { IB::Asset::toFourCC("MATE") },
                                     [](void *data, IB::Resource::Handle resource, IB::Asset::ResourceLoad::State loadState) {
                                         // Assign our mesh resource even when it's loading
                                         // We want to gracefully handle the resource not being available yet.
                                         if (loadState >= IB::Asset::ResourceLoad::Loading)
                                         {
                                             *reinterpret_cast<IB::Resource::Handle *>(data) = resource;
                                         }
                                     },
                                     &renderer.MaterialResource);

        return Property::toHandle(reinterpret_cast<uint64_t>(&renderer));
    }

    MaterialAssetHandle createMaterialAsset(MaterialAsset asset)
    {
        RuntimeMaterial &materialData = MaterialPool.add();
        materialData.Asset = asset;
        Job::Handle textureJob = Asset::loadResourceAsync(asset.AlbedoPath, { Asset::toFourCC("IMAG") }, &materialData.AlbedoTexture);
        continueJob([materialPtr = &materialData]() {
            IB::Resource::Handle textureResource = materialPtr->AlbedoTexture;
            IB::ForwardDesc materialDesc = {};
            materialDesc.AlbedoTint = materialPtr->Asset.AlbedoTint;
            materialDesc.AlbedoImage = toImageHandle(IB::Asset::getAssetFromResource(textureResource));
            materialPtr->Material = IB::createMaterial(materialDesc);
            return IB::JobResult::Complete;
        },
                    &textureJob, 1, RendererJobQueueIndex);

        return {reinterpret_cast<uint64_t>(&materialData)};
    }

    ViewDesc gatherCellView(Cell::Handle cell, ImDrawData* sourceImguiDrawCommands, StackAllocator* allocator)
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

        Entity::Handle *entities;
        uint32_t entityCount;
        getEntityList(cell, &entities, &entityCount);

        uint32_t rendererCount = 0;
        MeshHandle meshes[32];
        MaterialHandle materials[32];
        for (uint32_t i = 0; i < entityCount; i++)
        {
            Property::Handle handle = getPropertyFromEntity(entities[i], Asset::toFourCC("RNDR"));
            if (handle != InvalidProperty)
            {
                RendererProperty *renderer = toRendererProperty(handle);
                if (Asset::isResourceAssetAvailable(renderer->MeshResource) && Asset::isResourceAssetAvailable(renderer->MaterialResource))
                {
                    Asset::Handle materialAsset = Asset::getAssetFromResource(renderer->MaterialResource);
                    RuntimeMaterial *material = reinterpret_cast<RuntimeMaterial *>(Asset::toValue(materialAsset));
                    // Assure that our material is valid
                    // It's possible our loading thread hasn't finished creating it for us
                    if (material->Material != InvalidMaterial)
                    {
                        meshes[rendererCount] = { static_cast<uint32_t>(Asset::toValue(Asset::getAssetFromResource(renderer->MeshResource))) };
                        materials[rendererCount] = material->Material;
                        rendererCount++;
                    }
                }
            }
        }

        ViewDesc viewDesc = {};
        {
            Float3 viewPos = Float3{ 0.0f, 1.0f, 0.0f };
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
                Mat3x4 meshTransform =
                {
                    {{1.0f, 0.0f, 0.0f, 2.0f * i},
                     {0.0f, 1.0f, 0.0f, 5.0f},
                     {0.0f, 0.0f, 1.0f, 0.0f}},
                };

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

    void toBinary(Serialization::WriteStream *stream, MeshAsset const &mesh)
    {
        Serialization::toBinary(stream, mesh.VertexCount);
        Serialization::toBinary(stream, mesh.IndexCount);

        Serialization::toBinary(stream, mesh.Vertices, mesh.VertexCount * sizeof(Vertex));
        Serialization::toBinary(stream, mesh.Indices, mesh.IndexCount * sizeof(uint16_t));
    }

    void fromBinary(Serialization::ReadStream *stream, MeshAsset *mesh, uint32_t /*versionNumber*/)
    {
        Serialization::fromBinary(stream, &mesh->VertexCount);
        Serialization::fromBinary(stream, &mesh->IndexCount);

        mesh->Vertices = Serialization::fromBinary<Vertex const *>(stream, mesh->VertexCount * sizeof(Vertex));
        mesh->Indices = Serialization::fromBinary<uint16_t const *>(stream, mesh->IndexCount * sizeof(uint16_t));
    }

    void toBinary(Serialization::WriteStream *stream, ShaderAsset const &shaders)
    {
        Serialization::toBinary(stream, shaders.VertexShaderSize);
        Serialization::toBinary(stream, shaders.FragShaderSize);

        Serialization::toBinary(stream, shaders.VertexShader, shaders.VertexShaderSize);
        Serialization::toBinary(stream, shaders.FragShader, shaders.FragShaderSize);
    }

    void fromBinary(Serialization::ReadStream *stream, ShaderAsset *shaders, uint32_t /*versionNumber*/)
    {
        Serialization::fromBinary(stream, &shaders->VertexShaderSize);
        Serialization::fromBinary(stream, &shaders->FragShaderSize);

        shaders->VertexShader = Serialization::fromBinary<uint8_t const *>(stream, shaders->VertexShaderSize);
        shaders->FragShader = Serialization::fromBinary<uint8_t const *>(stream, shaders->FragShaderSize);
    }

    void toBinary(Serialization::WriteStream *stream, MaterialAsset const &material)
    {
        Serialization::toBinary(stream, material.AlbedoTint);
        Serialization::toBinary(stream, material.AlbedoPath);
    }

    void fromBinary(Serialization::ReadStream *stream, MaterialAsset *material, uint32_t /*versionNumber*/)
    {
        Serialization::fromBinary(stream, &material->AlbedoTint);
        Serialization::fromBinary(stream, &material->AlbedoPath);
    }

    void toBinary(Serialization::WriteStream *stream, ImageAsset const &image)
    {
        Serialization::toBinary(stream, image.Format);
        Serialization::toBinary(stream, image.Width);
        Serialization::toBinary(stream, image.Height);

        Serialization::toBinary(stream, image.Data, image.Width * image.Height * FormatStride[image.Format]);
    }

    void fromBinary(Serialization::ReadStream *stream, ImageAsset *image, uint32_t /*versionNumber*/)
    {
        Serialization::fromBinary(stream, &image->Format);
        Serialization::fromBinary(stream, &image->Width);
        Serialization::fromBinary(stream, &image->Height);
        image->Data = Serialization::fromBinary<uint8_t const *>(stream, image->Width * image->Height * FormatStride[image->Format]);
    }
} // namespace IB
