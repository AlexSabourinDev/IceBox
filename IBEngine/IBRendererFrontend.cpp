#include "IBRendererFrontend.h"
#include "IBRenderer.h"
#include "IBSerialization.h"
#include "IBAsset.h"
#include "IBLogging.h"

namespace
{
    constexpr uint32_t MaxShaderCount = 10;
    constexpr uint32_t RendererJobQueueIndex = 0;
    IB::ShaderAsset Shaders[MaxShaderCount];
    uint32_t ActiveShaderCount = 0;

    IB::Asset::ResourceHandle GlobalShaderResource;
    IB::JobHandle InitJob;

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

                IB::JobHandle meshCreate = IB::continueJob([mesh, meshHandle = &context->Data]() {
                    IB::MeshDesc meshDesc = {};
                    meshDesc.Vertices.Data = mesh.Vertices;
                    meshDesc.Vertices.Count = mesh.VertexCount;
                    meshDesc.Indices.Data = mesh.Indices;
                    meshDesc.Indices.Count = mesh.IndexCount;
                    *meshHandle = createMesh(meshDesc).Value;

                    return IB::JobResult::Complete;
                },
                                                           &InitJob, 1, RendererJobQueueIndex);

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

    class ShaderAssetStreamer : public IB::Asset::IStreamer
    {
    public:
        IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
        {
            IB::ShaderAsset shaders;
            fromBinary(&context->Stream, &shaders);

            uint32_t shaderID = IB::atomicIncrement(&ActiveShaderCount);
            Shaders[shaderID] = shaders;
            return IB::Asset::complete({shaderID});
        }

        void unloadThreadSafe(IB::Asset::AssetHandle asset) override
        {
            Shaders[asset.Value] = {};
            IB::atomicDecrement(&ActiveShaderCount);
        }
    };
    ShaderAssetStreamer ShaderStreamer;

    struct RendererProperty
    {
        IB::MeshHandle MeshHandle;
    };

    constexpr uint32_t MaxRendererPropertyCount = 1024;
    RendererProperty RendererProperties[MaxRendererPropertyCount];
} // namespace

namespace IB
{
    void initRendererFrontend(RendererFrontendDesc const &desc)
    {
        Asset::addStreamer(Asset::toFourCC("MESH"), &MeshStreamer);
        Asset::addStreamer(Asset::toFourCC("SHDR"), &ShaderStreamer);

        JobHandle jobHandle = Asset::loadResourceAsync("SampleForward.shdr", Asset::toFourCC("SHDR"), &GlobalShaderResource);
        InitJob = continueJob([window = *desc.Window]() {
            ShaderAsset shaders = Shaders[Asset::GetAssetFromResource(GlobalShaderResource).Value];

            RendererDesc rendererDesc = {};
            rendererDesc.Window = &window;
            rendererDesc.Materials.Forward.VShader = shaders.VertexShader;
            rendererDesc.Materials.Forward.VShaderSize = shaders.VertexShaderSize;
            rendererDesc.Materials.Forward.FShader = shaders.FragShader;
            rendererDesc.Materials.Forward.FShaderSize = shaders.FragShaderSize;
            initRenderer(rendererDesc);
            return JobResult::Complete;
        },
                              &jobHandle, 1, RendererJobQueueIndex);
    }

    void killRendererFrontend()
    {
        IB::Asset::releaseResourceAsync(GlobalShaderResource);
        launchJob([]() {
            killRenderer();
            return JobResult::Complete;
        },
                  RendererJobQueueIndex);
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
