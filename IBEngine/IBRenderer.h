#pragma once

#include "IBEngineAPI.h"
#include "IBMath.h"
#include <stdint.h>

/*
Hello, welcome to the redenderer. I will be your guide.

Using the IceBox renderer should be straightforward and have as minimal an API as possible.

Arguments should generally be passed as a "Desc" type object, this allows for
simple support of default arguments and adding new arguments to your types without
breaking previous use of the system.

Handles should be returned instead of pointers or raw integer types.
The reason we prefer handles is simple. Our internal implementation of our objects can change
as well as where they are currently in use by the renderer. As a result, minimizing access to them
is essential. If we were to simply allow a pointer to say an interface, it would facilitate adding
modifier functions to the objects. I would like to avoid this since it would introduce
more API surface area as well as potentially introducing challenges in multi threading.

The API should also require minimal work to get running and it should
facilitate efficient use of the graphics API underneath.

You'll notice how ViewDesc is arranged in a hierarchical structure
Starting first with a material handle
And then a mesh handle
And finally a list of tranforms

This assures that we minimize our state changes while maximizing the potential for
batching instances. (It's also easier to think about for me)
*/

struct ImDrawData;
namespace IB
{
    struct WindowMessagePlatformData;
    namespace Window
    {
        enum class Handle : uintptr_t;
    }
    struct RendererDesc
    {
        Window::Handle Window;
    };

    struct ForwardPassDesc
    {
        uint8_t const*VShader = nullptr;
        uint32_t VShaderSize = 0;
        uint8_t const*FShader = nullptr;
        uint32_t FShaderSize = 0;
    };

    struct alignas(16) Vertex
    {
        // UV in pos[3] as 2 halves
        Float4 Pos;
        // Color in Normal[3] as uint
        Float4 Normal;
    };

    struct MeshDesc
    {
        struct
        {
            Vertex const*Data = nullptr;
            uint32_t Count = 0; // number of vertices
        } Vertices;

        struct
        {
            uint16_t const*Data = nullptr;
            uint32_t Count = 0; // number of indices
        } Indices;
    };

    namespace Mesh
    {
        enum class Handle : uint32_t;
        Handle constexpr toHandle(uint32_t value) { return static_cast<Handle>(value); }
        uint32_t constexpr toValue(Handle handle) { return static_cast<uint32_t>(handle); }
    };

    Mesh::Handle constexpr InvalidMesh = Mesh::toHandle(UINT32_MAX);


    namespace ImageFormat
    {
        enum Enum : uint32_t
        {
            RGBA8 = 0,
            Count
        };
    };

    struct ImageDesc
    {
        ImageFormat::Enum Format = {};
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint8_t const*Data = nullptr;
    };

    namespace Image
    {
        enum class Handle : uint32_t;
        Handle constexpr toHandle(uint32_t value) { return static_cast<Handle>(value); }
        uint32_t constexpr toValue(Handle handle) { return static_cast<uint32_t>(handle); }
    };

    Image::Handle constexpr InvalidImage = Image::toHandle(UINT32_MAX);

    uint32_t constexpr toRGBA(float r, float g, float b, float a)
    { 
        return (static_cast<uint32_t>(r * 255.99f) << 24)
             | (static_cast<uint32_t>(g * 255.99f) << 16)
             | (static_cast<uint32_t>(b * 255.99f) << 8)
             | (static_cast<uint32_t>(a * 255.99f));
    }

    struct ForwardDesc
    {
        uint32_t AlbedoTint = 0;
        Image::Handle AlbedoImage = {};
    };

    namespace Material
    {
        enum class Handle : uint32_t;
        Handle constexpr toHandle(uint32_t value) { return static_cast<Handle>(value); }
        uint32_t constexpr toValue(Handle handle) { return static_cast<uint32_t>(handle); }
    };

    Material::Handle constexpr InvalidMaterial = Material::toHandle(UINT32_MAX);

    struct DirectionalLightDesc
    {
        Float3 Direction = { 0.0f, 0.0f, -1.0f };
        float Radiance = 1.0f;
        uint32_t Tint = 0xFFFFFFFF;
    };

    namespace Light
    {
        enum class Handle : uint32_t;
        Handle constexpr toHandle(uint32_t value) { return static_cast<Handle>(value); }
        uint32_t constexpr toValue(Handle handle) { return static_cast<uint32_t>(handle); }
    };

    Light::Handle constexpr InvalidLight = Light::toHandle(UINT32_MAX);

    struct ViewDesc
    {
        struct MeshInstances
        {
            Mesh::Handle Mesh = InvalidMesh;
            Mat3x4 const *Transforms = nullptr;
            uint32_t Count = 0;
        };

        struct Batch
        {
            Material::Handle Material;
            MeshInstances *Meshes;
            uint32_t MeshCount = 0;
        };

        Mat4x4 ViewProj = Mat4x4::identity();
        Light::Handle DirectionalLight = InvalidLight;

        struct ForwardPass
        {
            enum Type
            {
                Default,
                DebugOverlay,
                Count
            };

            Batch *Batches;
            uint32_t BatchCount = 0;
        };

        struct
        {
            ForwardPass Passes[ForwardPass::Count];
        } Forward;

        ImDrawData *ImGuiDrawData = nullptr;
    };

    IB_API void initRenderer(RendererDesc const &desc);
    IB_API void killRenderer();

    // Can be called multiple times to reset the pipeline.
    // This function is safe to be called from a separate shader loading thread.
    // Make sure to only call pass initialization functions from this thread and no other.
    IB_API void initForwardPass(ForwardPassDesc const &desc);

    IB_API Mesh::Handle createMesh(MeshDesc const &desc);
    IB_API Mesh::Handle updateMesh(Mesh::Handle mesh, MeshDesc const &desc);
    IB_API void destroyMesh(Mesh::Handle mesh);

    IB_API Image::Handle createImage(ImageDesc const &desc);
    IB_API Material::Handle createMaterial(ForwardDesc const &desc);
    IB_API Material::Handle updateMaterial(Material::Handle material, ForwardDesc const &desc);
    IB_API void destroyMaterial(Material::Handle material);
    IB_API Light::Handle createLight(DirectionalLightDesc const& desc);

    IB_API void newImGuiFrame();
    IB_API bool imGuiPlatformMessage(WindowMessagePlatformData const* platformData);
    IB_API void drawView(ViewDesc const &view);
} // namespace IB
