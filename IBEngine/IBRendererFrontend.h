#pragma once

#include <stdint.h>

#include "IBEngineAPI.h"
#include "IBRenderer.h"

namespace IB
{
    namespace Serialization
    {
        struct FileStream;
        struct MemoryStream;
    }

    struct MeshAsset
    {
        Vertex *Vertices;
        uint32_t VertexCount;

        uint16_t *Indices;
        uint32_t IndexCount;
    };

    struct ShaderAsset
    {
        uint8_t *VertexShader;
        uint32_t VertexShaderSize;

        uint8_t *FragShader;
        uint32_t FragShaderSize;
    };

    IB_API void toBinary(Serialization::FileStream *stream, MeshAsset const &mesh);
    IB_API void fromBinary(Serialization::MemoryStream *stream, MeshAsset *mesh);

    IB_API void toBinary(Serialization::FileStream *stream, ShaderAsset const &shaders);
    IB_API void fromBinary(Serialization::MemoryStream *stream, ShaderAsset *shaders);
} // namespace IB
