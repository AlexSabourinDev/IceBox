#pragma once

#include <stdint.h>

#include "IBEngineAPI.h"

struct ImDrawData;
namespace IB
{
    namespace Asset
    {
        enum Handle : uint64_t;
        constexpr Handle toHandle(uint64_t value);
    }

    namespace Property
    {
        enum Handle : uint64_t;
    }

    namespace Cell
    {
        enum Handle : uint64_t;
    }

    struct ViewDesc;

    constexpr uint32_t RendererJobQueueIndex = 0;

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

    inline Asset::Handle toAssetHandle(MaterialAssetHandle handle) { return Asset::toHandle(handle.Value); };

    namespace ImageFormat
    {
        enum Enum : uint32_t;
    }

    struct ImageAsset
    {
        ImageFormat::Enum Format;
        uint32_t Width;
        uint32_t Height;
        uint8_t const* Data;
    };

    namespace Window
    {
        enum Handle : uintptr_t;
    }
    struct RendererFrontendDesc
    {
        Window::Handle Window;
    };

    struct StackAllocator;

    IB_API void initRendererFrontend(RendererFrontendDesc const &frontendDesc);
    IB_API void killRendererFrontend();

    IB_API Property::Handle createRendererProperty(char const* meshPath, char const* materialPath);
    IB_API MaterialAssetHandle createMaterialAsset(MaterialAsset asset);

    // Can be called from any thread
    IB_API ViewDesc gatherCellView(Cell::Handle cell, ImDrawData* imguiDrawCommands, StackAllocator* allocator);

    // Must be called from our renderer queue
    IB_API void drawCellView(ViewDesc const& viewDesc);

    IB_API void toBinary(Serialization::FileStream *stream, MeshAsset const &mesh);
    IB_API void fromBinary(Serialization::MemoryStream *stream, MeshAsset *mesh, uint32_t versionNumber);

    IB_API void toBinary(Serialization::FileStream *stream, ShaderAsset const &shaders);
    IB_API void fromBinary(Serialization::MemoryStream *stream, ShaderAsset *shaders, uint32_t versionNumber);

    IB_API void toBinary(Serialization::FileStream *stream, MaterialAsset const &material);
    IB_API void fromBinary(Serialization::MemoryStream *stream, MaterialAsset *material, uint32_t versionNumber);

    IB_API void toBinary(Serialization::FileStream *stream, ImageAsset const &image);
    IB_API void fromBinary(Serialization::MemoryStream *stream, ImageAsset *image, uint32_t versionNumber);
} // namespace IB
