#pragma once
#include "IBEngineAPI.h"
#include "IBMath.h"
#include "IBCollection.h"
#include "IBRenderer.h"

#include <stdint.h>

namespace IB
{
    struct PathString;
    namespace Mesh
    {
        enum class Handle : uint32_t;
    }

    namespace Material
    {
        enum class Handle : uint32_t;
    }

    namespace Asset
    {
        enum class Handle : uint64_t;
        enum class FourCC : uint32_t;
    }

    namespace Resource
    {
        enum class Handle : uint32_t;
    }

    namespace Entity
    {
        enum class Handle : uint64_t;
    }

    namespace Job
    {
        enum class Handle : uint64_t;
    }

    struct WindowMessage;

    namespace Editor
    {
        using AssetEditorCallback = void(Asset::Handle asset, void* parentEditorData);
        IB_API void addAssetEditor(Asset::FourCC type, AssetEditorCallback* frame);
        IB_API void drawAssetEditor(Asset::FourCC type, Asset::Handle handle, void* parentEditorData = nullptr);

        IB_API void addLooseResourceType(Asset::FourCC type, char const *name, char const *extension, bool allowCreation);

        struct FrameState
        {
            Resource::Handle* CellResource;
        };
        IB_API void init();
        IB_API void kill();
        IB_API void frame(FrameState* state);
        IB_API bool onWindowMessage(IB::WindowMessage message);
        IB_API Entity::Handle getCamera();

        IB_API bool isSelected(Entity::Handle entity);

        IB_API bool resourceCombo(char const* label, Asset::FourCC type, Resource::Handle* resource, Job::Handle* loadingJob);
        IB_API bool resourcePathCombo(char const *label, Asset::FourCC type, PathString *resourcePath);
    }

    namespace ImGizmo
    {
        IB_API void init();
        IB_API void kill();

        struct TranslationGizmo
        {
            uint32_t ActiveAxis = 3;
        };

        struct RotationGizmo
        {
            uint32_t ActiveAxis = 3;
        };

        struct TransformGizmo
        {
            enum class Type
            {
                Translation,
                Rotation,
            };

            TranslationGizmo Translation;
            RotationGizmo Rotation;
            Type GizmoType = Type::Translation;
        };

        IB_API bool translation(Float3* position, Quaternion rotation, TranslationGizmo* state, uint32_t id = 0);
        IB_API bool rotation(Float3 position, Quaternion *rotation, RotationGizmo *state, uint32_t id = 0);
        IB_API bool transform(Transform *transform, TransformGizmo *state, uint32_t id = 0);
    }

    struct Vertex;
    namespace ImGeo
    {
        struct DrawData
        {
            struct Instance
            {
                Mesh::Handle Mesh;
                Material::Handle Material;
                Transform Transform;
            };

            DynamicArray<Instance> Instances[ViewDesc::ForwardPass::Count];
        };

        struct State
        {
            Vertex *Vertices = nullptr;
            uint16_t *Indices = nullptr;
            uint16_t VertexCount = 0;
            uint16_t IndexCount = 0;

            Transform Transform = {};
            Float4 VertexColor = {1.0f, 1.0f, 1.0f, 1.0f};
        };

        State *state();
        IB_API void init();
        IB_API void kill();
        IB_API void newFrame();
        IB_API void endFrame();

        IB_API DrawData const *getDrawData();
        IB_API void addDrawInstance(DrawData::Instance const &instance, ViewDesc::ForwardPass::Type type);

        IB_API void tube(State *state, float length, float radius, uint16_t segmentCount);
        IB_API void cone(State *state, float height, float radius, uint16_t segmentCount);
        IB_API void grid(State *state, uint32_t rowCount, uint32_t columnCount, float spacing);
    }
}

