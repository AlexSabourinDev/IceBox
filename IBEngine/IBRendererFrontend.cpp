#include "IBRendererFrontend.h"
#include "IBSerialization.h"

namespace IB
{
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

        mesh->Vertices = Serialization::fromBinary<Vertex*>(stream, mesh->VertexCount * sizeof(Vertex));
        mesh->Indices = Serialization::fromBinary<uint16_t*>(stream, mesh->IndexCount * sizeof(uint16_t));
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

        shaders->VertexShader = Serialization::fromBinary<uint8_t*>(stream, shaders->VertexShaderSize);
        shaders->FragShader = Serialization::fromBinary<uint8_t*>(stream, shaders->FragShaderSize);
    }
} // namespace IB
