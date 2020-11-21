#include "IBRendererFrontend.h"
#include "IBSerialization.h"

namespace IB
{
    void toBinary(FileStream *stream, MeshAsset const &mesh)
    {
        toBinary(stream, mesh.VertexCount);
        toBinary(stream, mesh.IndexCount);

        toBinary(stream, mesh.Vertices, mesh.VertexCount * sizeof(Vertex));
        toBinary(stream, mesh.Indices, mesh.IndexCount * sizeof(uint16_t));
    }

    void fromBinary(MemoryStream *stream, MeshAsset *mesh)
    {
        fromBinary(stream, &mesh->VertexCount);
        fromBinary(stream, &mesh->IndexCount);

        mesh->Vertices = fromBinary<Vertex>(stream, mesh->VertexCount * sizeof(Vertex));
        mesh->Indices = fromBinary<uint16_t>(stream, mesh->IndexCount * sizeof(uint16_t));
    }
} // namespace IB
