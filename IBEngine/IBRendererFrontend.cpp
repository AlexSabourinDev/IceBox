#include "IBRendererFrontend.h"
#include "IBRenderer.h"
#include "IBSerialization.h"
#include "IBAsset.h"
#include "IBLogging.h"
#include "IBAllocator.h"

namespace
{
    constexpr uint32_t RendererJobQueueIndex = 0;

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
                fromBinary(&context->Stream, &mesh);

                IB::JobHandle meshCreate = IB::launchJob([mesh, meshHandle = &context->Data]() {
                    IB::MeshDesc meshDesc = {};
                    meshDesc.Vertices.Data = mesh.Vertices;
                    meshDesc.Vertices.Count = mesh.VertexCount;
                    meshDesc.Indices.Data = mesh.Indices;
                    meshDesc.Indices.Count = mesh.IndexCount;
                    *meshHandle = createMesh(meshDesc).Value;

                    return IB::JobResult::Complete;
                },
                                                           RendererJobQueueIndex);

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
            fromBinary(&context->Stream, shaders);

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

                char const *path;
                fromBinary(&context->Stream, &path);

                IB::JobHandle meshJob = IB::Asset::loadResourceAsync(path, IB::Asset::toFourCC("MESH"), &renderer.MeshResource);
                return IB::Asset::wait(&meshJob, 1, Complete);
            }
            else
            {
                return IB::Asset::complete({context->Data});
            }
        }

        void saveThreadSafe(IB::Asset::SaveContext *context) override
        {
            RendererProperty *renderer = reinterpret_cast<RendererProperty *>(context->Asset.Value);
            char const *resourcePath = IB::Asset::GetResourcePath(renderer->MeshResource);
            toBinary(context->Stream, resourcePath);
        }

        void unloadThreadSafe(IB::Asset::AssetHandle asset) override
        {
            RendererProperty *renderer = reinterpret_cast<RendererProperty *>(asset.Value);
            IB::Asset::releaseResourceAsync(renderer->MeshResource);
            RendererProperties.remove(*renderer);
        }
    };

    RendererPropertyAssetStreamer RendererPropertyStreamer;
} // namespace

namespace IB
{
    IB::JobHandle initRendererFrontend(RendererFrontendDesc const &desc)
    {
        Asset::addStreamer(Asset::toFourCC("MESH"), &MeshStreamer);
        Asset::addStreamer(Asset::toFourCC("SHDR"), &ShaderStreamer);
        Asset::addStreamer(Asset::toFourCC("RNDR"), &RendererPropertyStreamer);

        return Asset::loadResourceAsync("SampleForward.shdr", Asset::toFourCC("SHDR"),
                                           [](void *data, Asset::ResourceHandle resource, IB::Asset::ResourceLoad::State loadState) {
                                               if (loadState == Asset::ResourceLoad::Available)
                                               {
                                                   ShaderAsset *shaders = toShaderAsset(Asset::GetAssetFromResource(resource));
                                                   RendererDesc rendererDesc = {};
                                                   rendererDesc.Window = reinterpret_cast<WindowHandle *>(data);
                                                   rendererDesc.Materials.Forward.VShader = shaders->VertexShader;
                                                   rendererDesc.Materials.Forward.VShaderSize = shaders->VertexShaderSize;
                                                   rendererDesc.Materials.Forward.FShader = shaders->FragShader;
                                                   rendererDesc.Materials.Forward.FShaderSize = shaders->FragShaderSize;
                                                   initRenderer(rendererDesc);

                                                   Asset::releaseResourceAsync(resource);
                                               }
                                           },
                                           desc.Window);
    }

    void killRendererFrontend()
    {
        launchJob([]() {
            killRenderer();
            return JobResult::Complete;
        },
                  RendererJobQueueIndex);
    }

    PropertyHandle createRendererProperty(char const *meshPath)
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

        return {reinterpret_cast<uint64_t>(&renderer)};
    }

    void toBinary(Serialization::FileStream *stream, MeshAsset const &mesh)
    {
        Serialization::toBinary(stream, mesh.VertexCount);
        Serialization::toBinary(stream, mesh.IndexCount);

        Serialization::toBinary(stream, mesh.Vertices, mesh.VertexCount * sizeof(Vertex));
        Serialization::toBinary(stream, mesh.Indices, mesh.IndexCount * sizeof(uint16_t));
    }

    void fromBinary(Serialization::MemoryStream *stream, MeshAsset *mesh)
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

    void fromBinary(Serialization::MemoryStream *stream, ShaderAsset *shaders)
    {
        Serialization::fromBinary(stream, &shaders->VertexShaderSize);
        Serialization::fromBinary(stream, &shaders->FragShaderSize);

        shaders->VertexShader = Serialization::fromBinary<uint8_t const *>(stream, shaders->VertexShaderSize);
        shaders->FragShader = Serialization::fromBinary<uint8_t const *>(stream, shaders->FragShaderSize);
    }
} // namespace IB
