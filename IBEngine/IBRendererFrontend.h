#pragma once

#include <stdint.h>

#include "IBEngineAPI.h"
#include "IBEntity.h"
#include "IBJobs.h"
#include "IBRenderer.h"

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

    struct MaterialAsset
    {
        char const* AlbedoPath;
        uint32_t AlbedoTint;
    };

    struct MaterialAssetHandle
    {
        uint64_t Value;
    };

    inline Asset::AssetHandle toAssetHandle(MaterialAssetHandle handle) { return { handle.Value }; };

    struct ImageAsset
    {
        ImageFormat::Enum Format;
        uint32_t Width;
        uint32_t Height;
        uint8_t const* Data;
    };

    struct WindowHandle;
    struct RendererFrontendDesc
    {
        WindowHandle *Window;
    };

    IB_API JobHandle initRendererFrontend(RendererFrontendDesc const &frontendDesc);
    IB_API void killRendererFrontend();

    IB_API PropertyHandle createRendererProperty(char const* meshPath, char const* materialPath);
    IB_API MaterialAssetHandle createMaterialAsset(MaterialAsset asset);
    IB_API JobHandle drawCell(CellHandle cell);

    IB_API void toBinary(Serialization::FileStream *stream, MeshAsset const &mesh);
    IB_API void fromBinary(Serialization::MemoryStream *stream, MeshAsset *mesh);

    IB_API void toBinary(Serialization::FileStream *stream, ShaderAsset const &shaders);
    IB_API void fromBinary(Serialization::MemoryStream *stream, ShaderAsset *shaders);

    IB_API void toBinary(Serialization::FileStream *stream, MaterialAsset const &material);
    IB_API void fromBinary(Serialization::MemoryStream *stream, MaterialAsset *material);

    IB_API void toBinary(Serialization::FileStream *stream, ImageAsset const &image);
    IB_API void fromBinary(Serialization::MemoryStream *stream, ImageAsset *image);
} // namespace IB
