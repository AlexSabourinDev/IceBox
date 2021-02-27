#pragma once

#include <stdint.h>

#include "IBEngineAPI.h"
#include "IBEntity.h"
#include "IBJobs.h"

namespace IB
{
    namespace Serialization
    {
        struct FileStream;
        struct MemoryStream;
    } // namespace Serialization

    struct Vertex;
    struct MeshAsset
    {
        Vertex const *Vertices;
        uint32_t VertexCount;

        uint16_t const *Indices;
        uint32_t IndexCount;
    };

    struct ShaderAsset
    {
        uint8_t const *VertexShader;
        uint32_t VertexShaderSize;

        uint8_t const *FragShader;
        uint32_t FragShaderSize;
    };

    struct WindowHandle;
    struct RendererFrontendDesc
    {
        WindowHandle *Window;
    };

    IB_API IB::JobHandle initRendererFrontend(RendererFrontendDesc const &frontendDesc);
    IB_API void killRendererFrontend();

    IB_API IB::PropertyHandle createRendererProperty(char const* meshPath);

    IB_API void toBinary(Serialization::FileStream *stream, MeshAsset const &mesh);
    IB_API void fromBinary(Serialization::MemoryStream *stream, MeshAsset *mesh);

    IB_API void toBinary(Serialization::FileStream *stream, ShaderAsset const &shaders);
    IB_API void fromBinary(Serialization::MemoryStream *stream, ShaderAsset *shaders);
} // namespace IB
