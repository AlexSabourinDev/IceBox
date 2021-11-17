#include "IBRendererFrontend.h"
#include "IBRenderer.h"
#include "IBSerialization.h"
#include "IBAsset.h"
#include "IBLogging.h"
#include "IBAllocator.h"
#include "IBEntity.h"
#include "IBJobs.h"
#include "IBEditor.h"
#include "IBPlatform.h"

#include <imgui/imgui.h>


namespace
{
    constexpr uint32_t InitPassesJobTypeIndex = 1;

    IB::Image::Handle toImageHandle(IB::Asset::Handle asset) { return IB::Image::toHandle(static_cast<uint32_t>(IB::Asset::toValue(asset))); }

    class ImageAssetStreamer : public IB::Asset::IStreamer
    {
    public:
        IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
        {
            enum
            {
                Load = 0,
                Create
            };

            if (context->State == Load)
            {
                IB::ImageAsset image;
                fromBinary(&context->Stream, &image, context->SaveVersion);

                IB::Job::Handle imageCreate = IB::launchJob([image, imageHandle = &context->Data](IB::Job::Handle /*currentJob*/) {
                    IB::ImageDesc imageDesc = {};
                    imageDesc.Format = image.Format;
                    imageDesc.Width = image.Width;
                    imageDesc.Height = image.Height;
                    imageDesc.Data = image.Data;
                    *imageHandle = IB::Image::toValue(IB::createImage(imageDesc));

                    return IB::JobResult::Complete;
                },
                                                          IB::NoJobType, IB::RendererJobQueueIndex);

                return IB::Asset::wait(&imageCreate, 1, Create);
            }
            else
            {
                return IB::Asset::complete(IB::Asset::toHandle(context->Data));
            }
        }

        void unloadThreadSafe(IB::Asset::Handle) override
        {
            // TODO: Handle image destruction
        }
    };
    ImageAssetStreamer ImageStreamer;

    struct MeshData
    {
        IB::Mesh::Handle RendererMesh = IB::InvalidMesh;
        IB::DynamicArray<IB::SimdTriangleSet> Triangles;
        uint32_t TriangleCount = 0;
    };

    void setCollisionShape(MeshData* meshData, IB::MeshAsset::Desc meshAsset)
    {
        uint32_t triangleCount = meshAsset.IndexCount / 3;
        meshData->Triangles.reserve(triangleCount / IB::SimdLaneCount + 1);
        const bool freeMemory = false;
        meshData->Triangles.clear(freeMemory);
        for (uint32_t t = 0; t < triangleCount; t += IB::SimdLaneCount)
        {
            IB::Float3 p0[IB::SimdLaneCount];
            IB::Float3 p1[IB::SimdLaneCount];
            IB::Float3 p2[IB::SimdLaneCount];

            uint32_t triangleSetCount = IB::min(triangleCount, IB::SimdLaneCount);
            for (uint32_t ti = 0; ti < triangleSetCount; ti++)
            {
                uint32_t i0 = meshAsset.Indices[(t + ti) * 3 + 0];
                uint32_t i1 = meshAsset.Indices[(t + ti) * 3 + 1];
                uint32_t i2 = meshAsset.Indices[(t + ti) * 3 + 2];
                p0[ti] = IB::Float3{ meshAsset.Vertices[i0].Pos[0], meshAsset.Vertices[i0].Pos[1], meshAsset.Vertices[i0].Pos[2] };
                p1[ti] = IB::Float3{ meshAsset.Vertices[i1].Pos[0], meshAsset.Vertices[i1].Pos[1], meshAsset.Vertices[i1].Pos[2] };
                p2[ti] = IB::Float3{ meshAsset.Vertices[i2].Pos[0], meshAsset.Vertices[i2].Pos[1], meshAsset.Vertices[i2].Pos[2] };
            }

            meshData->Triangles.add(IB::SimdTriangleSet{ simdSet(p0), simdSet(p1), simdSet(p2) });
        }
        meshData->TriangleCount = triangleCount;
    }

    MeshData* toMeshData(IB::Asset::Handle asset) { return reinterpret_cast<MeshData*>(IB::Asset::toValue(asset)); }
    IB::Mesh::Handle toMeshHandle(IB::Asset::Handle asset) { return reinterpret_cast<MeshData*>(IB::Asset::toValue(asset))->RendererMesh; }

    IB::ThreadSafePool<MeshData> MeshDataPool;
    class MeshAssetStreamer : public IB::Asset::IStreamer
    {
    public:
        IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
        {
            enum
            {
                Load = 0,
                Create
            };

            if (context->State == Load)
            {
                MeshData& meshData = MeshDataPool.add();

                IB::MeshAsset::Desc meshAsset;
                fromBinary(&context->Stream, &meshAsset, context->SaveVersion);

                IB::Job::Handle jobs[2];

                jobs[0] = IB::launchJob([meshAsset, &meshData](IB::Job::Handle /*currentJob*/) {
                    IB::MeshDesc meshDesc = {};
                    meshDesc.Vertices.Data = meshAsset.Vertices;
                    meshDesc.Vertices.Count = meshAsset.VertexCount;
                    meshDesc.Indices.Data = meshAsset.Indices;
                    meshDesc.Indices.Count = meshAsset.IndexCount;
                    meshData.RendererMesh = createMesh(meshDesc);

                    return IB::JobResult::Complete;
                },
                                        IB::NoJobType, IB::RendererJobQueueIndex);

                // Store mesh for collision detection
                jobs[1] = IB::launchJob([meshAsset, &meshData](IB::Job::Handle /*currentJob*/)
                {
                    setCollisionShape(&meshData, meshAsset);
                    return IB::JobResult::Complete;
                });

                context->Data = reinterpret_cast<uint64_t>(&meshData);
                return IB::Asset::wait(jobs, 2, Create);
            }
            else
            {
                return IB::Asset::complete(IB::Asset::toHandle(context->Data));
            }
        }

        void unloadThreadSafe(IB::Asset::Handle meshAsset) override
        {
            MeshData* meshData = reinterpret_cast<MeshData*>(IB::Asset::toValue(meshAsset));

            IB::launchJob([meshHandle = meshData->RendererMesh](IB::Job::Handle /*currentJob*/)
            {
                IB::destroyMesh(meshHandle);
                return IB::JobResult::Complete;
            },
                IB::NoJobType, IB::RendererJobQueueIndex);

            MeshDataPool.remove(*meshData);
        }
    };
    MeshAssetStreamer MeshStreamer;

    struct RuntimeMaterial
    {
        IB::Material::Handle Material;
        IB::MaterialAsset::Desc Asset;
        IB::Resource::Handle AlbedoTexture;
    };

    IB::ThreadSafePool<RuntimeMaterial> MaterialPool;
    class MaterialAssetStreamer : public IB::Asset::IStreamer
    {
    public:
        IB::Asset::Handle create() override
        {
            RuntimeMaterial &materialData = MaterialPool.add();
            return IB::Asset::toHandle(reinterpret_cast<uint64_t>(&materialData));
        }
        IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
        {
            enum
            {
                Load = 0,
                Create
            };

            if (context->State == Load)
            {
                RuntimeMaterial& materialData = *reinterpret_cast<RuntimeMaterial*>(IB::Asset::toValue(context->LoadingAsset));
                fromBinary(&context->Stream, &materialData.Asset, context->SaveVersion);

                IB::Job::Handle textureJob = IB::Asset::loadResourceAsync(materialData.Asset.AlbedoPath, { IB::ImageType }, &materialData.AlbedoTexture);
                IB::Job::Handle materialJob = IB::continueJob([materialPtr = &materialData, contextData = &context->Data](IB::Job::Handle /*currentJob*/) {
                    IB::Resource::Handle textureResource = materialPtr->AlbedoTexture;

                    IB::ForwardDesc materialDesc = {};
                    materialDesc.AlbedoTint = materialPtr->Asset.AlbedoTint;
                    materialDesc.AlbedoImage = toImageHandle(IB::Asset::getAssetFromResource(textureResource));
                    materialPtr->Material = IB::createMaterial(materialDesc);
                    *contextData = reinterpret_cast<uint64_t>(materialPtr);

                    return IB::JobResult::Complete;
                },
                                                            &textureJob, 1, IB::NoJobType, IB::RendererJobQueueIndex);

                return IB::Asset::wait(&materialJob, 1, Create);
            }
            else
            {
                return IB::Asset::complete(IB::Asset::toHandle(context->Data));
            }
        }

        void saveThreadSafe(IB::Asset::SaveContext *context) override
        {
            RuntimeMaterial *material = reinterpret_cast<RuntimeMaterial *>(IB::Asset::toValue(context->Asset));
            toBinary(context->Stream, material->Asset);
        }

        void unloadThreadSafe(IB::Asset::Handle asset) override
        {
            RuntimeMaterial *material = reinterpret_cast<RuntimeMaterial *>(IB::Asset::toValue(asset));
            IB::Asset::releaseResourceAsync(material->AlbedoTexture);
            // TODO: Handle material destruction in renderer backend
            MaterialPool.remove(*material);
        }
    };
    MaterialAssetStreamer MaterialStreamer;

    IB::ShaderAsset *toShaderAsset(IB::Asset::Handle handle)
    {
        return reinterpret_cast<IB::ShaderAsset *>(IB::Asset::toValue(handle));
    }

    class ShaderAssetStreamer : public IB::Asset::IStreamer
    {
    public:
        IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
        {
            IB::ShaderAsset *shaders = IB::allocate<IB::ShaderAsset>();
            fromBinary(&context->Stream, shaders, context->SaveVersion);

            return IB::Asset::complete(IB::Asset::toHandle(reinterpret_cast<uint64_t>(shaders)));
        }

        void unloadThreadSafe(IB::Asset::Handle asset) override
        {
            IB::deallocate(toShaderAsset(asset));
        }

        bool memoryMapped() const { return true; }
    };
    ShaderAssetStreamer ShaderStreamer;

    constexpr uint32_t FormatStride[IB::ImageFormat::Count] =
    {
        4 };

} // namespace

// Camera property
namespace IB
{
    namespace CameraProperty
    {
        namespace
        {
            struct Data
            {
                IB::Camera Camera;
                IB::Mat4x4 Projection = IB::Mat4x4::identity();
                IB::Mat4x4 InvProjection = IB::Mat4x4::identity();
            };
        }
    }

    namespace
    {
        void updateCameraProjection(IB::CameraProperty::Data* cameraProperty)
        {
            float fov = 1.0f / tanf(cameraProperty->Camera.FOV * IB::DegToRad * 0.5f);
            const float f = cameraProperty->Camera.Far;
            const float n = cameraProperty->Camera.Near;
            const float a = cameraProperty->Camera.Aspect;
            cameraProperty->Projection =
                IB::Mat4x4{
                    {{fov / a, 0.0f, 0.0f, 0.0f},
                     {0.0f, -fov, 0.0f, 0.0f},
                     {0.0f, 0.0f, f / (f - n), -n * f / (f - n)},
                     {0.0f, 0.0f, 1.0, 0.0f}},
            };

            cameraProperty->InvProjection =
                IB::Mat4x4{
                    {{a / fov, 0.0f, 0.0f, 0.0f},
                     {0.0f, -1.0f / fov, 0.0f, 0.0f},
                     {0.0f, 0.0f, 0.0f, 1.0f},
                     {0.0f, 0.0f, (f - n) / (-n * f), f / (n * f)}},
            };
        }

        IB::CameraProperty::Data* toCameraProperty(IB::CameraProperty::Handle handle) { return reinterpret_cast<IB::CameraProperty::Data*>(IB::CameraProperty::toValue(handle)); }
        IB::CameraProperty::Data* toCameraProperty(IB::Asset::Handle asset) { return reinterpret_cast<IB::CameraProperty::Data*>(IB::Asset::toValue(asset)); }

        IB::ThreadSafePool<IB::CameraProperty::Data> CameraProperties;
        class CameraPropertyStreamer : public IB::Asset::IStreamer
        {
        public:
            IB::Asset::Handle create() override
            {
                IB::CameraProperty::Data& camera = CameraProperties.add();
                updateCameraProjection(&camera);

                return IB::Asset::toHandle(reinterpret_cast<uint64_t>(&camera));
            }

            IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
            {
                IB::Camera baseCamera = {};
                if (context->BaseAsset != IB::InvalidAsset)
                {
                    baseCamera = toCameraProperty(context->BaseAsset)->Camera;
                }

                IB::CameraProperty::Data& cameraProperty = *toCameraProperty(context->LoadingAsset);
                IB::Serialization::Delta::fromBinary(&context->Stream, &cameraProperty.Camera, baseCamera);

                updateCameraProjection(&cameraProperty);

                return IB::Asset::complete(IB::Asset::toHandle(reinterpret_cast<uint64_t>(&cameraProperty)));
            }

            void saveThreadSafe(IB::Asset::SaveContext *context) override
            {
                IB::Camera baseCamera = {};
                if (context->BaseAsset != IB::InvalidAsset)
                {
                    baseCamera = toCameraProperty(context->BaseAsset)->Camera;
                }

                IB::CameraProperty::Data& cameraProperty = *toCameraProperty(context->Asset);
                IB::Serialization::Delta::toBinary(context->Stream, cameraProperty.Camera, baseCamera);
            }

            void unloadThreadSafe(IB::Asset::Handle assetHandle) override
            {
                CameraProperties.remove(*toCameraProperty(assetHandle));
            }
        };

        CameraPropertyStreamer CameraStreamer;
    }

    Property::Handle createCameraProperty(Camera camera)
    {
        CameraProperty::Data& cameraProperty = CameraProperties.add();
        cameraProperty.Camera = camera;
        updateCameraProjection(&cameraProperty);
        return Property::toHandle(reinterpret_cast<uint64_t>(&cameraProperty));
    }

    Camera getCamera(CameraProperty::Handle cameraProperty)
    {
        CameraProperty::Data* camera = toCameraProperty(cameraProperty);
        return camera->Camera;
    }

    void setCamera(CameraProperty::Handle cameraProperty, Camera camera)
    {
        CameraProperty::Data* cameraData = toCameraProperty(cameraProperty);
        cameraData->Camera = camera;
        updateCameraProjection(cameraData);
    }

    Mat4x4 getViewProjection(CameraProperty::Handle cameraHandle, Transform const& worldTransform)
    {
        CameraProperty::Data* camera = toCameraProperty(cameraHandle);
        Mat3x4 viewFromWorld = toInvMatrix(worldTransform);
        // Swap Y and Z rows since we want Y to be up in camera space
        // Equivalent to
        // 1 0 0
        // 0 0 1 * invWorld
        // 0 1 0
        // The order of operations we want is:
        // worldPos * viewToWorld * YZflip * projection
        Float4 temp = viewFromWorld[1];
        viewFromWorld[1] = viewFromWorld[2];
        viewFromWorld[2] = temp;

        // world->viewZUp->viewYUp->projection
        return mul(camera->Projection, viewFromWorld);
    }

    Mat4x4 getInvViewProjection(CameraProperty::Handle cameraHandle, Transform const& worldTransform)
    {
        CameraProperty::Data* camera = toCameraProperty(cameraHandle);
        Mat3x4 worldFromViewZUp = toMatrix(worldTransform);
        constexpr Mat4x4 viewZUpFromViewYUp =
        { {
            {1.0f, 0.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 1.0f, 0.0f},
            {0.0f, 1.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, 0.0f, 1.0f}
        } };

        // projection->viewYUp->viewZUp->World
        return mul(mul(worldFromViewZUp, viewZUpFromViewYUp), camera->InvProjection);
    }

    namespace Editor
    {
        void cameraEditor(Asset::Handle asset, void* parentData)
        {
            PropertyEditorState* editorState = reinterpret_cast<PropertyEditorState*>(parentData);

            if (propertyHeader("Camera", &editorState->DestroyProperty))
            {
                CameraProperty::Data *cameraProperty = toCameraProperty(asset);
                if (ImGui::BeginTable("Inspector Table", 2))
                {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableSetupColumn("Data", ImGuiTableColumnFlags_None);

                    bool changed = false;

                    ImGui::TableNextColumn();
                    ImGui::Text("FOV");

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    changed |= ImGui::InputFloat("#FOV", &cameraProperty->Camera.FOV);

                    ImGui::TableNextColumn();
                    ImGui::Text("Near");

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    changed |= ImGui::InputFloat("#Near", &cameraProperty->Camera.Near);

                    ImGui::TableNextColumn();
                    ImGui::Text("Far");

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    changed |= ImGui::InputFloat("#Far", &cameraProperty->Camera.Far);

                    if (changed)
                    {
                        updateCameraProjection(cameraProperty);
                    }

                    ImGui::EndTable();
                }
                ImGui::TreePop();
            }
        }
    }
}

// Renderer property
namespace IB
{
    namespace
    {
        struct RendererProperty
        {
            IB::Resource::Handle MeshResource;
            IB::Resource::Handle MaterialResource;
        };
        RendererProperty constexpr EmptyRendererProperty = {};

        RendererProperty* toRendererProperty(IB::Asset::Handle assetHandle) { return reinterpret_cast<RendererProperty *>(IB::Asset::toValue(assetHandle)); }
        RendererProperty* toRendererProperty(IB::Property::Handle propertyHandle) { return reinterpret_cast<RendererProperty *>(IB::Property::toValue(propertyHandle)); }

        IB::ThreadSafePool<RendererProperty> RendererProperties;

        class RendererPropertyAssetStreamer : public IB::Asset::IStreamer
        {
        public:
            IB::Asset::Handle create() override
            {
                RendererProperty &renderer = RendererProperties.add();
                return IB::Asset::toHandle(reinterpret_cast<uint64_t>(&renderer));
            }

            IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
            {
                enum
                {
                    LoadMesh = 0,
                    Complete
                };

                if (context->State == LoadMesh)
                {
                    RendererProperty const* baseRenderer = &EmptyRendererProperty;
                    if (context->BaseAsset != IB::InvalidAsset)
                    {
                        baseRenderer = toRendererProperty(context->BaseAsset);
                    }

                    RendererProperty &renderer = *toRendererProperty(context->LoadingAsset);

                    // Unload our current resources if we had existing data.
                    {
                        IB::Asset::releaseResourceAsync(renderer.MeshResource);
                        IB::Asset::releaseResourceAsync(renderer.MaterialResource);
                    }

                    context->Data = reinterpret_cast<uint64_t>(&renderer);

                    char const *baseMeshPath = IB::Asset::getResourcePath(baseRenderer->MeshResource);

                    char const *meshPath;
                    IB::Serialization::Delta::fromBinary(&context->Stream, &meshPath, baseMeshPath);

                    char const *baseMaterialPath = IB::Asset::getResourcePath(baseRenderer->MaterialResource);

                    char const *materialPath;
                    IB::Serialization::Delta::fromBinary(&context->Stream, &materialPath, baseMaterialPath);

                    enum
                    {
                        MeshJob = 0,
                        MaterialJob,
                        JobCount
                    };
                    IB::Job::Handle jobs[JobCount];

                    jobs[MeshJob] = IB::Asset::loadResourceAsync(meshPath, { IB::MeshType },
                        [](void *data, IB::Resource::Handle resource, IB::Asset::ResourceLoad::State loadState)
                    {
                        // Assign our mesh resource even when it's loading
                        // We want to gracefully handle the resource not being available yet.
                        if (loadState >= IB::Asset::ResourceLoad::Loading)
                        {
                            *reinterpret_cast<IB::Resource::Handle *>(data) = resource;
                        }
                    },
                        &renderer.MeshResource);

                    jobs[MaterialJob] = IB::Asset::loadResourceAsync(materialPath, { IB::MaterialType },
                        [](void *data, IB::Resource::Handle resource, IB::Asset::ResourceLoad::State loadState)
                    {
                        // Assign our mesh resource even when it's loading
                        // We want to gracefully handle the resource not being available yet.
                        if (loadState >= IB::Asset::ResourceLoad::Loading)
                        {
                            *reinterpret_cast<IB::Resource::Handle *>(data) = resource;
                        }
                    },
                        &renderer.MaterialResource);

                    return IB::Asset::wait(jobs, JobCount, Complete);
                }
                else
                {
                    return IB::Asset::complete(IB::Asset::toHandle(context->Data));
                }
            }

            void saveThreadSafe(IB::Asset::SaveContext *context) override
            {
                RendererProperty const* baseRenderer = &EmptyRendererProperty;
                if (context->BaseAsset != IB::InvalidAsset)
                {
                    baseRenderer = toRendererProperty(context->BaseAsset);
                }

                RendererProperty *renderer = toRendererProperty(context->Asset);
                char const *meshPath = IB::Asset::getResourcePath(renderer->MeshResource);
                char const *baseMeshPath = IB::Asset::getResourcePath(baseRenderer->MeshResource);
                IB::Serialization::Delta::toBinary(context->Stream, meshPath, baseMeshPath);

                char const *materialPath = IB::Asset::getResourcePath(renderer->MaterialResource);
                char const *baseMaterialPath = IB::Asset::getResourcePath(baseRenderer->MaterialResource);
                IB::Serialization::Delta::toBinary(context->Stream, materialPath, baseMaterialPath);
            }

            void unloadThreadSafe(IB::Asset::Handle asset) override
            {
                RendererProperty *renderer = toRendererProperty(asset);
                IB::Asset::releaseResourceAsync(renderer->MeshResource);
                IB::Asset::releaseResourceAsync(renderer->MaterialResource);
                RendererProperties.remove(*renderer);
            }
        };

        RendererPropertyAssetStreamer RendererPropertyStreamer;
    }

    namespace Editor
    {
        void rendererEditor(Asset::Handle asset, void* parentData)
        {
            PropertyEditorState* editorState = reinterpret_cast<PropertyEditorState*>(parentData);

            if (propertyHeader("Renderer", &editorState->DestroyProperty))
            {
                RendererProperty *renderer = toRendererProperty(asset);
                if (ImGui::BeginTable("Inspector Table", 2))
                {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
                    ImGui::TableSetupColumn("Data", ImGuiTableColumnFlags_None);

                    ImGui::TableNextColumn();
                    ImGui::Text("Mesh");

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::Text(Asset::getResourcePath(renderer->MeshResource));

                    ImGui::TableNextColumn();
                    ImGui::Text("Material");

                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::Text(Asset::getResourcePath(renderer->MaterialResource));

                    ImGui::EndTable();
                }
                ImGui::TreePop();
            }
        }
    }

    Property::Handle createRendererProperty(char const *meshPath, char const *materialPath)
    {
        RendererProperty &renderer = RendererProperties.add();
        IB::Asset::loadResourceAsync(meshPath, { IB::MeshType },
            [](void *data, IB::Resource::Handle resource, IB::Asset::ResourceLoad::State loadState)
        {
            // Assign our mesh resource even when it's loading
            // We want to gracefully handle the resource not being available yet.
            if (loadState >= IB::Asset::ResourceLoad::Loading)
            {
                *reinterpret_cast<IB::Resource::Handle *>(data) = resource;
            }
        },
            &renderer.MeshResource);

        IB::Asset::loadResourceAsync(materialPath, { IB::MaterialType },
            [](void *data, IB::Resource::Handle resource, IB::Asset::ResourceLoad::State loadState)
        {
            // Assign our mesh resource even when it's loading
            // We want to gracefully handle the resource not being available yet.
            if (loadState >= IB::Asset::ResourceLoad::Loading)
            {
                *reinterpret_cast<IB::Resource::Handle *>(data) = resource;
            }
        },
            &renderer.MaterialResource);

        return Property::toHandle(reinterpret_cast<uint64_t>(&renderer));
    }

    MeshAsset::Handle getMeshFromRendererProperty(Property::Handle rendererProperty)
    {
        RendererProperty* renderer = toRendererProperty(rendererProperty);
        return MeshAsset::toHandle(Asset::toValue(Asset::getAssetFromResource(renderer->MeshResource)));
    }

    bool tryGetTriangleSet(Property::Handle rendererProperty, SimdTriangleSet** triangles, uint32_t* triangleCount)
    {
        if (rendererProperty == InvalidProperty)
        {
            return false;
        }

        RendererProperty* renderer = toRendererProperty(rendererProperty);

        bool const isMeshAvailable = Asset::isResourceAssetAvailable(renderer->MeshResource);
        if (isMeshAvailable)
        {
            MeshData* meshData = toMeshData(Asset::getAssetFromResource(renderer->MeshResource));
            *triangles = meshData->Triangles.data();
            *triangleCount = meshData->TriangleCount;
        }

        return isMeshAvailable;
    }
}

namespace IB
{

    void initRendererFrontend(RendererFrontendDesc const &desc)
    {
        Asset::addStreamer(MeshType, &MeshStreamer);
        Asset::addStreamer(ImageType, &ImageStreamer);
        Asset::addStreamer(MaterialType, &MaterialStreamer);
        Asset::addStreamer(ShaderType, &ShaderStreamer);
        Asset::addStreamer(RendererPropertyType, &RendererPropertyStreamer);
        Asset::addStreamer(CameraPropertyType, &CameraStreamer);

        RendererDesc rendererDesc = {desc.Window};
        initRenderer(rendererDesc);

        Asset::loadResourceAsync("SampleForward.shdr", { ShaderType },
                                 [](void*, Resource::Handle resource, IB::Asset::ResourceLoad::State loadState) {
                                     if (loadState == Asset::ResourceLoad::Available)
                                     {
                                         launchJob([resource](Job::Handle /*currentJob*/) {
                                             ShaderAsset *shaders = toShaderAsset(Asset::getAssetFromResource(resource));
                                             ForwardPassDesc desc;
                                             desc.VShader = shaders->VertexShader;
                                             desc.VShaderSize = shaders->VertexShaderSize;
                                             desc.FShader = shaders->FragShader;
                                             desc.FShaderSize = shaders->FragShaderSize;
                                             initForwardPass(desc);

                                             Asset::releaseResourceAsync(resource);
                                             return JobResult::Complete;
                                         },
                                               InitPassesJobTypeIndex);
                                     }
                                 });

        Editor::addAssetEditor(RendererPropertyType, &Editor::rendererEditor);
        Editor::addAssetEditor(CameraPropertyType, &Editor::cameraEditor);
    }

    void killRendererFrontend()
    {
        launchJob([](Job::Handle /*currentJob*/) {
            killRenderer();
            return JobResult::Complete;
        },
                  NoJobType, RendererJobQueueIndex);
    }

    MaterialAsset::Handle createMaterialAsset(MaterialAsset::Desc asset)
    {
        RuntimeMaterial &materialData = MaterialPool.add();
        materialData.Asset = asset;
        Job::Handle textureJob = Asset::loadResourceAsync(asset.AlbedoPath, { ImageType }, &materialData.AlbedoTexture);
        continueJob([materialPtr = &materialData](Job::Handle /*currentJob*/) {
            IB::Resource::Handle textureResource = materialPtr->AlbedoTexture;
            IB::ForwardDesc materialDesc = {};
            materialDesc.AlbedoTint = materialPtr->Asset.AlbedoTint;
            materialDesc.AlbedoImage = toImageHandle(IB::Asset::getAssetFromResource(textureResource));
            materialPtr->Material = IB::createMaterial(materialDesc);
            return IB::JobResult::Complete;
        },
                    &textureJob, 1, NoJobType, RendererJobQueueIndex);

        return MaterialAsset::toHandle(reinterpret_cast<uint64_t>(&materialData));
    }

    Job::Handle createMeshAssetAsync(MeshAsset::Desc meshAsset, MeshAsset::Handle* meshHandle)
    {
        MeshData& meshData = MeshDataPool.add();
        Job::Handle job = launchJob([meshAsset, &meshData](Job::Handle /*currentJob*/)
        {
            IB::MeshDesc meshDesc = {};
            meshDesc.Vertices.Data = meshAsset.Vertices;
            meshDesc.Vertices.Count = meshAsset.VertexCount;
            meshDesc.Indices.Data = meshAsset.Indices;
            meshDesc.Indices.Count = meshAsset.IndexCount;
            meshData.RendererMesh = createMesh(meshDesc);

            return IB::JobResult::Complete;
        },
            IB::NoJobType, IB::RendererJobQueueIndex);

        setCollisionShape(&meshData, meshAsset);

        *meshHandle = MeshAsset::toHandle(reinterpret_cast<uint64_t>(&meshData));
        return job;
    }

    Job::Handle updateMeshAssetAsync(MeshAsset::Handle mesh, MeshAsset::Desc meshAsset)
    {
        if (mesh == InvalidMeshAsset)
        {
            return InvalidJob;
        }

        MeshData* meshData = reinterpret_cast<MeshData*>(MeshAsset::toValue(mesh));
        Job::Handle job = launchJob([meshAsset, meshData](Job::Handle /*currentJob*/)
        {
            IB::MeshDesc meshDesc = {};
            meshDesc.Vertices.Data = meshAsset.Vertices;
            meshDesc.Vertices.Count = meshAsset.VertexCount;
            meshDesc.Indices.Data = meshAsset.Indices;
            meshDesc.Indices.Count = meshAsset.IndexCount;
            updateMesh(meshData->RendererMesh, meshDesc);

            return IB::JobResult::Complete;
        },
            IB::NoJobType, IB::RendererJobQueueIndex);

        setCollisionShape(meshData, meshAsset);

        return job;
    }

    void setRendererMesh(Property::Handle rendererProperty, Resource::Handle resource)
    {
        RendererProperty* renderer = toRendererProperty(rendererProperty);
        Asset::acquireResource(resource);
        Asset::releaseResourceAsync(renderer->MeshResource);
        renderer->MeshResource = resource;
    }

    ViewDesc gatherCellView(GatherCellViewDesc const& gatherDesc)
    {
        // Clone our imgui draw data
        ImDrawData* imguiDrawData = nullptr;
        if (gatherDesc.ImGuiDrawData != nullptr && gatherDesc.ImGuiDrawData->CmdListsCount > 0)
        {
            imguiDrawData = allocate<ImDrawData>(gatherDesc.Allocator, *gatherDesc.ImGuiDrawData);
            imguiDrawData->CmdLists = allocateArray<ImDrawList*>(gatherDesc.Allocator, imguiDrawData->CmdListsCount);
            for (int32_t i = 0; i < imguiDrawData->CmdListsCount; i++)
            {
                imguiDrawData->CmdLists[i] = gatherDesc.ImGuiDrawData->CmdLists[i]->CloneOutput();
            }
        }

        Entity::Handle *entities;
        uint32_t entityCount;
        getEntityList(gatherDesc.Cell, &entities, &entityCount);

        uint32_t rendererCount = 0;
        Mesh::Handle meshes[32];
        Material::Handle materials[32];
        Transform transforms[32];
        for (uint32_t i = 0; i < entityCount; i++)
        {
            Property::Handle handle = getPropertyFromEntity(entities[i], RendererPropertyType);
            if (handle != InvalidProperty)
            {
                RendererProperty *renderer = toRendererProperty(handle);
                if (Asset::isResourceAssetAvailable(renderer->MeshResource) && Asset::isResourceAssetAvailable(renderer->MaterialResource))
                {
                    Asset::Handle materialAsset = Asset::getAssetFromResource(renderer->MaterialResource);
                    RuntimeMaterial *material = reinterpret_cast<RuntimeMaterial *>(Asset::toValue(materialAsset));
                    // Assure that our material is valid
                    // It's possible our loading thread hasn't finished creating it for us
                    if (material->Material != InvalidMaterial)
                    {
                        meshes[rendererCount] = toMeshHandle(Asset::getAssetFromResource(renderer->MeshResource));
                        materials[rendererCount] = material->Material;

                        Property::Handle transformHandle = getPropertyFromEntity(entities[i], TransformPropertyType);
                        if (transformHandle != InvalidProperty)
                        {
                            transforms[rendererCount] = getLocalTransform(transformHandle);
                        }

                        rendererCount++;
                    }
                }
            }
        }

        ViewDesc viewDesc = {};
        {
            Transform cameraTransform = {};
            Property::Handle transformProperty = getPropertyFromEntity(gatherDesc.Camera, TransformPropertyType);
            if (transformProperty != InvalidProperty)
            {
                cameraTransform = getLocalTransform(transformProperty);
            }

            CameraProperty::Handle cameraProperty = toCameraHandle(getPropertyFromEntity(gatherDesc.Camera, CameraPropertyType));
            IB_ASSERT(cameraProperty != InvalidCameraProperty, "Camera handle should have a camera property!");

            Mat4x4 viewProj = Mat4x4::identity();
            if (cameraProperty != InvalidCameraProperty)
            {
                viewProj = getViewProjection(cameraProperty, cameraTransform);
            }

            ViewDesc::MeshInstances* meshInstances = allocateArray<ViewDesc::MeshInstances>(gatherDesc.Allocator, rendererCount);

            for (uint32_t i = 0; i < rendererCount; i++)
            {
                meshInstances[i].Mesh = meshes[i];
                Mat3x4 meshTransform = toMatrix(transforms[i]);
                meshInstances[i].Transforms = allocateArray<Mat3x4>(gatherDesc.Allocator, 1, meshTransform);
                meshInstances[i].Count = 1;
            }

            ViewDesc::Batch* meshBatch = allocateArray<ViewDesc::Batch>(gatherDesc.Allocator, 1);
            meshBatch->Material = materials[0]; // TODO
            meshBatch->Meshes = meshInstances;
            meshBatch->MeshCount = rendererCount;

            if (rendererCount > 0)
            {
                ViewDesc::ForwardPass worldPass;
                worldPass.Batches = meshBatch;
                worldPass.BatchCount = 1;
                viewDesc.Forward.Passes[ViewDesc::ForwardPass::Default] = worldPass;
            }

            // Restart our collection for gizmos
            rendererCount = 0;
            for (uint32_t i = 0; i < gatherDesc.ImGizmoDrawData->Instances.count(); i++)
            {
                if (gatherDesc.ImGizmoDrawData->Instances[i].Mesh != InvalidMesh
                    && gatherDesc.ImGizmoDrawData->Instances[i].Material != InvalidMaterial)
                {
                    meshes[rendererCount] = gatherDesc.ImGizmoDrawData->Instances[i].Mesh;
                    materials[rendererCount] = gatherDesc.ImGizmoDrawData->Instances[i].Material;
                    transforms[rendererCount] = gatherDesc.ImGizmoDrawData->Instances[i].Transform;
                    rendererCount++;
                }
            }

            ViewDesc::MeshInstances* gizmoMeshInstances = allocateArray<ViewDesc::MeshInstances>(gatherDesc.Allocator, rendererCount);
            for (uint32_t i = 0; i < rendererCount; i++)
            {
                gizmoMeshInstances[i].Mesh = meshes[i];
                Mat3x4 meshTransform = toMatrix(transforms[i]);
                gizmoMeshInstances[i].Transforms = allocateArray<Mat3x4>(gatherDesc.Allocator, 1, meshTransform);
                gizmoMeshInstances[i].Count = 1;
            }

            ViewDesc::Batch* gizmoMeshBatch = allocateArray<ViewDesc::Batch>(gatherDesc.Allocator, 1);
            gizmoMeshBatch->Material = materials[0]; // TODO
            gizmoMeshBatch->Meshes = gizmoMeshInstances;
            gizmoMeshBatch->MeshCount = rendererCount;

            if (rendererCount > 0)
            {
                IB::ViewDesc::ForwardPass gizmoPass;
                gizmoPass.Batches = gizmoMeshBatch;
                gizmoPass.BatchCount = 1;
                viewDesc.Forward.Passes[ViewDesc::ForwardPass::DebugOverlay] = gizmoPass;
            }

            viewDesc.ViewProj = viewProj;
            viewDesc.ImGuiDrawData = imguiDrawData;
        }

        return viewDesc;
    }

    void drawCellView(StackAllocator* allocator, ViewDesc const& viewDesc)
    {
        drawView(viewDesc);

        // Clean up our ImGui data
        if (viewDesc.ImGuiDrawData != nullptr)
        {
            for (int32_t i = 0; i < viewDesc.ImGuiDrawData->CmdListsCount; i++)
            {
                IM_DELETE(viewDesc.ImGuiDrawData->CmdLists[i]);
                viewDesc.ImGuiDrawData->CmdLists[i] = nullptr;
            }
            deallocateArray(allocator, viewDesc.ImGuiDrawData->CmdLists, viewDesc.ImGuiDrawData->CmdListsCount);
            viewDesc.ImGuiDrawData->CmdLists = nullptr;
            deallocate(allocator, viewDesc.ImGuiDrawData);
        }
    }

    namespace Serialization
    {
        void toBinary(Serialization::WriteStream *stream, MeshAsset::Desc const &mesh)
        {
            Serialization::toBinary(stream, mesh.VertexCount);
            Serialization::toBinary(stream, mesh.IndexCount);

            Serialization::arrayToBinary(stream, mesh.Vertices, mesh.VertexCount);
            Serialization::arrayToBinary(stream, mesh.Indices, mesh.IndexCount);
        }

        void fromBinary(Serialization::ReadStream *stream, MeshAsset::Desc *mesh, uint32_t /*versionNumber*/)
        {
            Serialization::fromBinary(stream, &mesh->VertexCount);
            Serialization::fromBinary(stream, &mesh->IndexCount);

            mesh->Vertices = Serialization::arrayFromBinary<Vertex>(stream, mesh->VertexCount);
            mesh->Indices = Serialization::arrayFromBinary<uint16_t>(stream, mesh->IndexCount);
        }

        void toBinary(Serialization::WriteStream *stream, ShaderAsset const &shaders)
        {
            Serialization::toBinary(stream, shaders.VertexShaderSize);
            Serialization::toBinary(stream, shaders.FragShaderSize);

            Serialization::arrayToBinary(stream, shaders.VertexShader, shaders.VertexShaderSize);
            Serialization::arrayToBinary(stream, shaders.FragShader, shaders.FragShaderSize);
        }

        void fromBinary(Serialization::ReadStream *stream, ShaderAsset *shaders, uint32_t /*versionNumber*/)
        {
            Serialization::fromBinary(stream, &shaders->VertexShaderSize);
            Serialization::fromBinary(stream, &shaders->FragShaderSize);

            shaders->VertexShader = Serialization::arrayFromBinary<uint8_t>(stream, shaders->VertexShaderSize);
            shaders->FragShader = Serialization::arrayFromBinary<uint8_t>(stream, shaders->FragShaderSize);
        }

        void toBinary(Serialization::WriteStream *stream, MaterialAsset::Desc const &material)
        {
            Serialization::toBinary(stream, material.AlbedoTint);
            Serialization::toBinary(stream, material.AlbedoPath);
        }

        void fromBinary(Serialization::ReadStream *stream, MaterialAsset::Desc *material, uint32_t /*versionNumber*/)
        {
            Serialization::fromBinary(stream, &material->AlbedoTint);
            Serialization::fromBinary(stream, &material->AlbedoPath);
        }

        void toBinary(Serialization::WriteStream *stream, ImageAsset const &image)
        {
            Serialization::toBinary(stream, image.Format);
            Serialization::toBinary(stream, image.Width);
            Serialization::toBinary(stream, image.Height);

            Serialization::arrayToBinary(stream, image.Data, image.Width * image.Height * FormatStride[image.Format]);
        }

        void fromBinary(Serialization::ReadStream *stream, ImageAsset *image, uint32_t /*versionNumber*/)
        {
            Serialization::fromBinary(stream, &image->Format);
            Serialization::fromBinary(stream, &image->Width);
            Serialization::fromBinary(stream, &image->Height);
            image->Data = Serialization::arrayFromBinary<uint8_t>(stream, image->Width * image->Height * FormatStride[image->Format]);
        }
    }
} // namespace IB
