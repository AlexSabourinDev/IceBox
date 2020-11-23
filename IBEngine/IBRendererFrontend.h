#pragma once

#include <stdint.h>

#include "IBEngineAPI.h"
#include "IBRenderer.h"

namespace IB
{
    struct FileStream;
    struct MemoryStream;

    struct MeshAsset
    {
        Vertex *Vertices;
        uint32_t VertexCount;

        uint16_t *Indices;
        uint32_t IndexCount;
    };

    IB_API void toBinary(FileStream *stream, MeshAsset const &mesh);
    IB_API void fromBinary(MemoryStream *stream, MeshAsset *mesh);
} // namespace IB
