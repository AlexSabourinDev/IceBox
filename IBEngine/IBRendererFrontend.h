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

    namespace Job
    {
        enum Handle : uint64_t;
    }

    struct ViewDesc;
    struct MeshDesc;

    constexpr uint32_t RendererJobQueueIndex = 0;

    namespace Serialization
    {
        struct WriteStream;
        struct ReadStream;
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

    namespace MaterialAsset
    {
        enum Handle : uint64_t;

        struct Desc
        {
            char const* AlbedoPath;
            uint32_t AlbedoTint;
        };

        constexpr Handle toHandle(uint64_t value) { return static_cast<Handle>(value); }
        constexpr uint64_t toValue(Handle handle) { return static_cast<uint64_t>(handle); }
    };

    inline Asset::Handle toAssetHandle(MaterialAsset::Handle handle) { return Asset::toHandle(static_cast<uint64_t>(handle)); };

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

    namespace ImGizmo
    {
        struct DrawData;
    }

    IB_API void initRendererFrontend(RendererFrontendDesc const &frontendDesc);
    IB_API void killRendererFrontend();

    IB_API Property::Handle createRendererProperty(char const* meshPath, char const* materialPath);
    IB_API MaterialAsset::Handle createMaterialAsset(MaterialAsset::Desc asset);

    // Can be called from any thread
    struct GatherCellViewDesc
    {
        StackAllocator* Allocator;

        Cell::Handle Cell;
        ImDrawData const* ImGuiDrawData;
        ImGizmo::DrawData const* ImGizmoDrawData;
    };
    IB_API ViewDesc gatherCellView(GatherCellViewDesc const& gatherDesc);

    // Must be called from our renderer queue
    IB_API void drawCellView(ViewDesc const& viewDesc);

    namespace Serialization
    {
        IB_API void toBinary(Serialization::WriteStream *stream, MeshAsset const &mesh);
        IB_API void fromBinary(Serialization::ReadStream *stream, MeshAsset *mesh, uint32_t versionNumber);

        IB_API void toBinary(Serialization::WriteStream *stream, ShaderAsset const &shaders);
        IB_API void fromBinary(Serialization::ReadStream *stream, ShaderAsset *shaders, uint32_t versionNumber);

        IB_API void toBinary(Serialization::WriteStream *stream, MaterialAsset::Desc const &material);
        IB_API void fromBinary(Serialization::ReadStream *stream, MaterialAsset::Desc *material, uint32_t versionNumber);

        IB_API void toBinary(Serialization::WriteStream *stream, ImageAsset const &image);
        IB_API void fromBinary(Serialization::ReadStream *stream, ImageAsset *image, uint32_t versionNumber);
    }
} // namespace IB
