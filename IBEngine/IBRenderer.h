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

namespace IB
{
    struct WindowHandle;
    struct RendererDesc
    {
        WindowHandle *Window;
        struct
        {
            struct
            {
                uint8_t *VShader = nullptr;
                uint32_t VShaderSize = 0;
                uint8_t *FShader = nullptr;
                uint32_t FShaderSize = 0;
            } Forward;
        } Materials;
    };

    struct Vertex
    {
        // UV in pos[3], normal[3]
        float Pos[4];
        float Normal[4];
        float Color[4];
    };

    struct MeshDesc
    {
        struct
        {
            Vertex *Data = nullptr;
            uint32_t Count = 0; // number of vertices
        } Vertices;

        struct
        {
            uint16_t *Data = nullptr;
            uint32_t Count = 0; // number of indices
        } Indices;
    };

    struct MeshHandle
    {
        uint32_t Value;
    };

    struct ImageFormat
    {
        enum Enum
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
        uint8_t *Data = nullptr;
    };

    struct ImageHandle
    {
        uint32_t Value;
    };

    struct ForwardDesc
    {
        float AlbedoTint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        ImageHandle AlbedoImage = {};
    };

    struct MaterialHandle
    {
        uint32_t Value;
    };

    struct ViewDesc
    {
        struct MeshInstances
        {
            MeshHandle Mesh;
            Mat3x4 const *Transforms = nullptr;
            uint32_t Count = 0;
        };

        struct Batch
        {
            MaterialHandle Material;
            MeshInstances *Meshes;
            uint32_t MeshCount = 0;
        };

        Mat4x4 ViewProj = Mat4x4::identity();

        struct Pass
        {
            enum
            {
                Default,
                DebugOverlay,
                Count
            };

            Batch *Batches;
            uint32_t BatchCount;
        };

        struct
        {
            Pass Passes[Pass::Count];
        } Forward;
    };

    IB_API void initRenderer(RendererDesc const *desc);
    IB_API void killRenderer();
    IB_API MeshHandle createMesh(MeshDesc const *desc);
    IB_API ImageHandle createImage(ImageDesc const *desc);
    IB_API MaterialHandle createMaterial(ForwardDesc const *desc);
    IB_API void drawView(ViewDesc const *view);
} // namespace IB
