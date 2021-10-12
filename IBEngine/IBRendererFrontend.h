#pragma once

#include <stdint.h>

#include "IBEngineAPI.h"

struct ImDrawData;
namespace IB
{
    struct Mat4x4;
    struct Transform;

    namespace Asset
    {
        enum class Handle : uint64_t;
        constexpr Handle toHandle(uint64_t value);

        enum FourCC : uint32_t;
        constexpr FourCC toFourCC(char const (&text)[5]);
    }

    namespace Resource
    {
        enum class Handle : uint32_t;
    }

    namespace Property
    {
        enum class Handle : uint64_t;
        constexpr Handle toHandle(uint64_t value);
        constexpr uint64_t toValue(Handle handle);
    }

    namespace Cell
    {
        enum class Handle : uint64_t;
    }

    namespace Entity
    {
        enum class Handle : uint64_t;
    }

    namespace Job
    {
        enum class Handle : uint64_t;
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
    namespace MeshAsset
    {
        enum class Handle : uint64_t;

        struct Desc
        {
            Vertex const *Vertices;
            uint32_t VertexCount;

            uint16_t const *Indices;
            uint32_t IndexCount;
        };

        constexpr Handle toHandle(uint64_t value) { return static_cast<Handle>(value); }
        constexpr uint64_t toValue(Handle handle) { return static_cast<uint64_t>(handle); }
    };
    constexpr MeshAsset::Handle InvalidMeshAsset = MeshAsset::toHandle(UINT64_MAX);
    Asset::FourCC const MeshType = Asset::toFourCC("MESH");
    inline Asset::Handle toAssetHandle(MeshAsset::Handle handle) { return Asset::toHandle(static_cast<uint64_t>(handle)); };

    struct ShaderAsset
    {
        uint8_t const *VertexShader;
        uint32_t VertexShaderSize;

        uint8_t const *FragShader;
        uint32_t FragShaderSize;
    };
    Asset::FourCC const ShaderType = Asset::toFourCC("SHDR");

    namespace MaterialAsset
    {
        enum class Handle : uint64_t;

        struct Desc
        {
            char const* AlbedoPath;
            uint32_t AlbedoTint;
        };

        constexpr Handle toHandle(uint64_t value) { return static_cast<Handle>(value); }
        constexpr uint64_t toValue(Handle handle) { return static_cast<uint64_t>(handle); }
    };

    Asset::FourCC const MaterialType = Asset::toFourCC("MATE");
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
    Asset::FourCC const ImageType = Asset::toFourCC("IMAG");

    namespace Window
    {
        enum class Handle : uintptr_t;
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

    struct SimdTriangleSet;

    IB_API void initRendererFrontend(RendererFrontendDesc const &frontendDesc);
    IB_API void killRendererFrontend();

    Asset::FourCC const RendererPropertyType = Asset::toFourCC("RNDR");
    IB_API Property::Handle createRendererProperty(char const* meshPath, char const* materialPath);
    IB_API MeshAsset::Handle getMeshFromRendererProperty(Property::Handle renderer);
    IB_API MaterialAsset::Handle createMaterialAsset(MaterialAsset::Desc asset);
    Job::Handle createMeshAssetAsync(MeshAsset::Desc meshAsset, MeshAsset::Handle* meshHandle);
    Job::Handle updateMeshAssetAsync(MeshAsset::Handle mesh, MeshAsset::Desc meshAsset);

    void setRendererMesh(Property::Handle rendererProperty, Resource::Handle resource);

    struct Camera
    {
        float FOV = 90.0f;
        float Near = 0.01f;
        float Far = 100.0f;
        float Aspect = 1.0f;
    };
    inline bool operator!=(Camera const& left, Camera const& right)
    {
        return left.FOV != right.FOV || left.Near != right.Near || left.Far != right.Far || left.Aspect != right.Aspect;
    }

    namespace CameraProperty
    {
        enum class Handle : uint64_t;
        constexpr Handle toHandle(uint64_t value) { return static_cast<Handle>(value); }
        constexpr uint64_t toValue(Handle handle) { return static_cast<uint64_t>(handle); }
    }
    constexpr CameraProperty::Handle InvalidCameraProperty = CameraProperty::toHandle(UINT64_MAX);

    inline Property::Handle toPropertyHandle(CameraProperty::Handle cameraHandle) { return Property::toHandle(CameraProperty::toValue(cameraHandle)); }
    inline CameraProperty::Handle toCameraHandle(Property::Handle cameraHandle) { return CameraProperty::toHandle(Property::toValue(cameraHandle)); }

    Asset::FourCC const CameraPropertyType = Asset::toFourCC("CMRA");
    IB_API Property::Handle createCameraProperty(Camera camera);
    IB_API Camera getCamera(CameraProperty::Handle cameraProperty);
    IB_API void setCamera(CameraProperty::Handle cameraProperty, Camera camera);

    IB_API Mat4x4 getViewProjection(CameraProperty::Handle camera, Transform const& worldTransform);
    IB_API Mat4x4 getInvViewProjection(CameraProperty::Handle camera, Transform const& transform);

    // TODO: I suspect we might want a different way to process mesh data for other uses instead of having the renderer frontend own it
    IB_API bool tryGetTriangleSet(Property::Handle rendererProperty, SimdTriangleSet** triangles, uint32_t* triangleCount);

    // Can be called from any thread
    struct GatherCellViewDesc
    {
        StackAllocator* Allocator;

        Entity::Handle Camera;
        Cell::Handle Cell;
        ImDrawData const* ImGuiDrawData;
        ImGizmo::DrawData const* ImGizmoDrawData;
    };
    IB_API ViewDesc gatherCellView(GatherCellViewDesc const& gatherDesc);

    // Must be called from our renderer queue
    IB_API void drawCellView(StackAllocator* allocator, ViewDesc const& viewDesc);

    namespace Serialization
    {
        IB_API void toBinary(Serialization::WriteStream *stream, MeshAsset::Desc const &mesh);
        IB_API void fromBinary(Serialization::ReadStream *stream, MeshAsset::Desc *mesh, uint32_t versionNumber);

        IB_API void toBinary(Serialization::WriteStream *stream, ShaderAsset const &shaders);
        IB_API void fromBinary(Serialization::ReadStream *stream, ShaderAsset *shaders, uint32_t versionNumber);

        IB_API void toBinary(Serialization::WriteStream *stream, MaterialAsset::Desc const &material);
        IB_API void fromBinary(Serialization::ReadStream *stream, MaterialAsset::Desc *material, uint32_t versionNumber);

        IB_API void toBinary(Serialization::WriteStream *stream, ImageAsset const &image);
        IB_API void fromBinary(Serialization::ReadStream *stream, ImageAsset *image, uint32_t versionNumber);
    }
} // namespace IB
