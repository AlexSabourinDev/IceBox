#pragma once
#include "IBEngineAPI.h"
#include "IBMath.h"
#include "IBCollection.h"

#include <stdint.h>

namespace IB
{
    namespace Mesh
    {
        enum Handle : uint32_t;
    }

    namespace Material
    {
        enum Handle : uint32_t;
    }

    namespace Asset
    {
        enum Handle : uint64_t;
        enum FourCC : uint32_t;
    }

    namespace Resource
    {
        enum Handle : uint32_t;
    }

    namespace Entity
    {
        enum Handle : uint64_t;
    }

    struct WindowMessage;

    namespace Editor
    {
        using AssetEditorCallback = void(Asset::Handle asset, void* parentEditorData);
        IB_API void addAssetEditor(Asset::FourCC type, AssetEditorCallback* frame);
        IB_API void drawAssetEditor(Asset::FourCC type, Asset::Handle handle, void* parentEditorData = nullptr);

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
    }

    namespace ImGizmo
    {
        struct DrawData
        {
            struct Instance
            {
                Mesh::Handle Mesh;
                Material::Handle Material;
                Transform Transform;
            };

            DynamicArray<Instance> Instances;
        };

        IB_API void init();
        IB_API void kill();
        IB_API DrawData const* getDrawData();

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

        IB_API void translation(Float3* position, Quaternion rotation, TranslationGizmo* state);
        IB_API void rotation(Float3 position, Quaternion* rotation, RotationGizmo* state);
        IB_API void transform(Transform* transform, TransformGizmo* state);
    }
}

