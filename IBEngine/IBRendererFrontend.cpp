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

#include <algorithm>

namespace IB
{
    namespace
    {
        constexpr uint32_t InitPassesJobTypeIndex = 1;

        Image::Handle toImageHandle(Asset::Handle asset) { return Image::toHandle(static_cast<uint32_t>(Asset::toValue(asset))); }

        class ImageAssetStreamer : public Asset::IStreamer
        {
        public:
            Asset::LoadContinuation loadAsync(Asset::LoadContext *context) override
            {
                enum
                {
                    Load = 0,
                    Create
                };

                if (context->State == Load)
                {
                    ImageAsset image;
                    fromBinary(&context->Stream, &image, context->SaveVersion);

                    Job::Handle imageCreate = launchJob([image, imageHandle = &context->Data](Job::Handle /*currentJob*/)
                                                                {
                                                                    ImageDesc imageDesc = {};
                                                                    imageDesc.Format = image.Format;
                                                                    imageDesc.Width = image.Width;
                                                                    imageDesc.Height = image.Height;
                                                                    imageDesc.Data = image.Data;
                                                                    *imageHandle = Image::toValue(createImage(imageDesc));

                                                                    return JobResult::Complete;
                                                                },
                                                                NoJobType, RendererJobQueueIndex);

                    return Asset::wait(&imageCreate, 1, Create);
                }
                else
                {
                    return Asset::complete(Asset::toHandle(context->Data));
                }
            }

            void unloadThreadSafe(Asset::Handle) override
            {
                // TODO: Handle image destruction
            }
        };
        ImageAssetStreamer ImageStreamer;

        struct MeshData
        {
            Mesh::Handle RendererMesh = InvalidMesh;
            DynamicArray<SimdTriangleSet> Triangles;
            uint32_t TriangleCount = 0;
        };

        void setCollisionShape(MeshData *meshData, MeshAsset::Desc meshAsset)
        {
            uint32_t triangleCount = meshAsset.IndexCount / 3;
            meshData->Triangles.reserve(triangleCount / SimdLaneCount + 1);
            const bool freeMemory = false;
            meshData->Triangles.clear(freeMemory);
            for (uint32_t t = 0; t < triangleCount; t += SimdLaneCount)
            {
                Float3 p0[SimdLaneCount];
                Float3 p1[SimdLaneCount];
                Float3 p2[SimdLaneCount];

                uint32_t triangleSetCount = min(triangleCount, SimdLaneCount);
                for (uint32_t ti = 0; ti < triangleSetCount; ti++)
                {
                    uint32_t i0 = meshAsset.Indices[(t + ti) * 3 + 0];
                    uint32_t i1 = meshAsset.Indices[(t + ti) * 3 + 1];
                    uint32_t i2 = meshAsset.Indices[(t + ti) * 3 + 2];
                    p0[ti] = Float3{meshAsset.Vertices[i0].Pos[0], meshAsset.Vertices[i0].Pos[1], meshAsset.Vertices[i0].Pos[2]};
                    p1[ti] = Float3{meshAsset.Vertices[i1].Pos[0], meshAsset.Vertices[i1].Pos[1], meshAsset.Vertices[i1].Pos[2]};
                    p2[ti] = Float3{meshAsset.Vertices[i2].Pos[0], meshAsset.Vertices[i2].Pos[1], meshAsset.Vertices[i2].Pos[2]};
                }

                meshData->Triangles.add(SimdTriangleSet{simdSet(p0), simdSet(p1), simdSet(p2)});
            }
            meshData->TriangleCount = triangleCount;
        }

        MeshData *toMeshData(Asset::Handle asset) { return reinterpret_cast<MeshData *>(Asset::toValue(asset)); }
        Mesh::Handle toMeshHandle(Asset::Handle asset) { return reinterpret_cast<MeshData *>(Asset::toValue(asset))->RendererMesh; }

        ThreadSafePool<MeshData> MeshDataPool;
        class MeshAssetStreamer : public Asset::IStreamer
        {
        public:
            Asset::LoadContinuation loadAsync(Asset::LoadContext *context) override
            {
                enum
                {
                    Load = 0,
                    Create
                };

                if (context->State == Load)
                {
                    MeshData &meshData = MeshDataPool.add();

                    MeshAsset::Desc meshAsset;
                    fromBinary(&context->Stream, &meshAsset, context->SaveVersion);

                    Job::Handle jobs[2];

                    jobs[0] = launchJob([meshAsset, &meshData](Job::Handle /*currentJob*/)
                                            {
                                                MeshDesc meshDesc = {};
                                                meshDesc.Vertices.Data = meshAsset.Vertices;
                                                meshDesc.Vertices.Count = meshAsset.VertexCount;
                                                meshDesc.Indices.Data = meshAsset.Indices;
                                                meshDesc.Indices.Count = meshAsset.IndexCount;
                                                meshData.RendererMesh = createMesh(meshDesc);

                                                return JobResult::Complete;
                                            },
                                            NoJobType, RendererJobQueueIndex);

                    // Store mesh for collision detection
                    jobs[1] = launchJob([meshAsset, &meshData](Job::Handle /*currentJob*/)
                                            {
                                                setCollisionShape(&meshData, meshAsset);
                                                return JobResult::Complete;
                                            });

                    context->Data = reinterpret_cast<uint64_t>(&meshData);
                    return Asset::wait(jobs, 2, Create);
                }
                else
                {
                    return Asset::complete(Asset::toHandle(context->Data));
                }
            }

            void unloadThreadSafe(Asset::Handle meshAsset) override
            {
                MeshData *meshData = reinterpret_cast<MeshData *>(Asset::toValue(meshAsset));

                launchJob([meshHandle = meshData->RendererMesh](Job::Handle /*currentJob*/)
                              {
                                  destroyMesh(meshHandle);
                                  return JobResult::Complete;
                              },
                              NoJobType, RendererJobQueueIndex);

                MeshDataPool.remove(*meshData);
            }
        };
        MeshAssetStreamer MeshStreamer;

        struct RuntimeMaterial
        {
            Material::Handle Material;
            Resource::Handle AlbedoTexture;
            uint32_t AlbedoTint = 0;
        };

        ThreadSafePool<RuntimeMaterial> MaterialPool;
        class MaterialAssetStreamer : public Asset::IStreamer
        {
        public:
            Asset::Handle create() override
            {
                RuntimeMaterial &materialData = MaterialPool.add();
                return Asset::toHandle(reinterpret_cast<uint64_t>(&materialData));
            }
            Asset::LoadContinuation loadAsync(Asset::LoadContext *context) override
            {
                enum
                {
                    Load = 0,
                    Create
                };

                if (context->State == Load)
                {
                    RuntimeMaterial &materialData = *reinterpret_cast<RuntimeMaterial *>(Asset::toValue(context->LoadingAsset));
                   
                    MaterialAsset::Desc materialDesc;
                    fromBinary(&context->Stream, &materialDesc, context->SaveVersion);

                    materialData.AlbedoTint = materialDesc.AlbedoTint;

                    Job::Handle textureJob = Asset::loadResourceAsync(materialDesc.AlbedoPath, {ImageType}, &materialData.AlbedoTexture);
                    Job::Handle materialJob = continueJob([materialPtr = &materialData, contextData = &context->Data](Job::Handle /*currentJob*/)
                                                                  {
                                                                      Resource::Handle textureResource = materialPtr->AlbedoTexture;

                                                                      ForwardDesc materialDesc = {};
                                                                      materialDesc.AlbedoTint = materialPtr->AlbedoTint;
                                                                      materialDesc.AlbedoImage = toImageHandle(Asset::getAssetFromResource(textureResource));
                                                                      materialPtr->Material = createMaterial(materialDesc);
                                                                      *contextData = reinterpret_cast<uint64_t>(materialPtr);

                                                                      return JobResult::Complete;
                                                                  },
                                                                  &textureJob, 1, NoJobType, RendererJobQueueIndex);

                    return Asset::wait(&materialJob, 1, Create);
                }
                else
                {
                    return Asset::complete(Asset::toHandle(context->Data));
                }
            }

            void saveThreadSafe(Asset::SaveContext *context) override
            {
                RuntimeMaterial *material = reinterpret_cast<RuntimeMaterial *>(Asset::toValue(context->Asset));

                MaterialAsset::Desc desc;
                desc.AlbedoPath = Asset::getResourcePath(material->AlbedoTexture);
                desc.AlbedoTint = material->AlbedoTint;
                toBinary(context->Stream, desc);
            }

            void unloadThreadSafe(Asset::Handle asset) override
            {
                RuntimeMaterial *material = reinterpret_cast<RuntimeMaterial *>(Asset::toValue(asset));
                Asset::releaseResourceAsync(material->AlbedoTexture);
                // TODO: Handle material destruction in renderer backend
                MaterialPool.remove(*material);
            }
        };
        MaterialAssetStreamer MaterialStreamer;

        ShaderAsset *toShaderAsset(Asset::Handle handle)
        {
            return reinterpret_cast<ShaderAsset *>(Asset::toValue(handle));
        }

        class ShaderAssetStreamer : public Asset::IStreamer
        {
        public:
            Asset::LoadContinuation loadAsync(Asset::LoadContext *context) override
            {
                ShaderAsset *shaders = allocate<ShaderAsset>();
                fromBinary(&context->Stream, shaders, context->SaveVersion);

                return Asset::complete(Asset::toHandle(reinterpret_cast<uint64_t>(shaders)));
            }

            void unloadThreadSafe(Asset::Handle asset) override
            {
                deallocate(toShaderAsset(asset));
            }

            bool memoryMapped() const { return true; }
        };
        ShaderAssetStreamer ShaderStreamer;

        constexpr uint32_t FormatStride[ImageFormat::Count] =
            {
                4};

    } // namespace

    namespace Editor
    {
        void materialEditor(Asset::Handle asset, void * data)
        {
            bool *assetModified = reinterpret_cast<bool *>(data);

            RuntimeMaterial *material = reinterpret_cast<RuntimeMaterial *>(Asset::toValue(asset));
            if (ImGui::BeginTable("Inspector Table", 2))
            {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("Data", ImGuiTableColumnFlags_None);

                ImGui::TableNextColumn();
                ImGui::Text("Albedo");

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::Text(Asset::getResourcePath(material->AlbedoTexture));

                ImGui::TableNextColumn();
                ImGui::Text("Albedo Tint");

                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-FLT_MIN);

                Float4 color = unpackRGBA(material->AlbedoTint);
                *assetModified |= ImGui::ColorEdit4("", &color[0]);
                material->AlbedoTint = packRGBA(color);

                // TODO: Update the material

                ImGui::EndTable();
            }
        }
    } // namespace Editor
} // namespace IB

// Camera property
namespace IB
{
    namespace CameraProperty
    {
        namespace
        {
            struct Data
            {
                Camera Camera;
                Mat4x4 Projection = Mat4x4::identity();
                Mat4x4 InvProjection = Mat4x4::identity();
            };
        }
    }

    namespace
    {
        void updateCameraProjection(CameraProperty::Data* cameraProperty)
        {
            float fov = 1.0f / tanf(cameraProperty->Camera.FOV * DegToRad * 0.5f);
            const float f = cameraProperty->Camera.Far;
            const float n = cameraProperty->Camera.Near;
            const float a = cameraProperty->Camera.Aspect;
            cameraProperty->Projection =
                Mat4x4{
                    {{fov / a, 0.0f, 0.0f, 0.0f},
                     {0.0f, -fov, 0.0f, 0.0f},
                     {0.0f, 0.0f, f / (f - n), -n * f / (f - n)},
                     {0.0f, 0.0f, 1.0, 0.0f}},
            };

            cameraProperty->InvProjection =
                Mat4x4{
                    {{a / fov, 0.0f, 0.0f, 0.0f},
                     {0.0f, -1.0f / fov, 0.0f, 0.0f},
                     {0.0f, 0.0f, 0.0f, 1.0f},
                     {0.0f, 0.0f, (f - n) / (-n * f), f / (n * f)}},
            };
        }

        CameraProperty::Data* toCameraProperty(CameraProperty::Handle handle) { return reinterpret_cast<CameraProperty::Data*>(CameraProperty::toValue(handle)); }
        CameraProperty::Data* toCameraProperty(Asset::Handle asset) { return reinterpret_cast<CameraProperty::Data*>(Asset::toValue(asset)); }

        ThreadSafePool<CameraProperty::Data> CameraProperties;
        class CameraPropertyStreamer : public Asset::IStreamer
        {
        public:
            Asset::Handle create() override
            {
                CameraProperty::Data& camera = CameraProperties.add();
                updateCameraProjection(&camera);

                return Asset::toHandle(reinterpret_cast<uint64_t>(&camera));
            }

            Asset::LoadContinuation loadAsync(Asset::LoadContext *context) override
            {
                Camera baseCamera = {};
                if (context->BaseAsset != InvalidAsset)
                {
                    baseCamera = toCameraProperty(context->BaseAsset)->Camera;
                }

                CameraProperty::Data& cameraProperty = *toCameraProperty(context->LoadingAsset);
                Serialization::Delta::fromBinary(&context->Stream, &cameraProperty.Camera, baseCamera);

                updateCameraProjection(&cameraProperty);

                return Asset::complete(Asset::toHandle(reinterpret_cast<uint64_t>(&cameraProperty)));
            }

            void saveThreadSafe(Asset::SaveContext *context) override
            {
                Camera baseCamera = {};
                if (context->BaseAsset != InvalidAsset)
                {
                    baseCamera = toCameraProperty(context->BaseAsset)->Camera;
                }

                CameraProperty::Data& cameraProperty = *toCameraProperty(context->Asset);
                Serialization::Delta::toBinary(context->Stream, cameraProperty.Camera, baseCamera);
            }

            void unloadThreadSafe(Asset::Handle assetHandle) override
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
            Resource::Handle MeshResource;
            Resource::Handle MaterialResource;
        };
        RendererProperty constexpr EmptyRendererProperty = {};

        RendererProperty* toRendererProperty(Asset::Handle assetHandle) { return reinterpret_cast<RendererProperty *>(Asset::toValue(assetHandle)); }
        RendererProperty* toRendererProperty(Property::Handle propertyHandle) { return reinterpret_cast<RendererProperty *>(Property::toValue(propertyHandle)); }

        ThreadSafePool<RendererProperty> RendererProperties;

        class RendererPropertyAssetStreamer : public Asset::IStreamer
        {
        public:
            Asset::Handle create() override
            {
                RendererProperty &renderer = RendererProperties.add();
                return Asset::toHandle(reinterpret_cast<uint64_t>(&renderer));
            }

            Asset::LoadContinuation loadAsync(Asset::LoadContext *context) override
            {
                enum
                {
                    LoadMesh = 0,
                    Complete
                };

                if (context->State == LoadMesh)
                {
                    RendererProperty const* baseRenderer = &EmptyRendererProperty;
                    if (context->BaseAsset != InvalidAsset)
                    {
                        baseRenderer = toRendererProperty(context->BaseAsset);
                    }

                    RendererProperty &renderer = *toRendererProperty(context->LoadingAsset);

                    // Unload our current resources if we had existing data.
                    {
                        Asset::releaseResourceAsync(renderer.MeshResource);
                        Asset::releaseResourceAsync(renderer.MaterialResource);
                    }

                    context->Data = reinterpret_cast<uint64_t>(&renderer);

                    char const *baseMeshPath = Asset::getResourcePath(baseRenderer->MeshResource);

                    char const *meshPath;
                    Serialization::Delta::fromBinary(&context->Stream, &meshPath, baseMeshPath);

                    char const *baseMaterialPath = Asset::getResourcePath(baseRenderer->MaterialResource);

                    char const *materialPath;
                    Serialization::Delta::fromBinary(&context->Stream, &materialPath, baseMaterialPath);

                    enum
                    {
                        MeshJob = 0,
                        MaterialJob,
                        JobCount
                    };
                    Job::Handle jobs[JobCount];

                    jobs[MeshJob] = Asset::loadResourceAsync(meshPath, { MeshType },
                        [](void *data, Resource::Handle resource, Asset::ResourceLoad::State loadState)
                    {
                        // Assign our mesh resource even when it's loading
                        // We want to gracefully handle the resource not being available yet.
                        if (loadState >= Asset::ResourceLoad::Loading)
                        {
                            *reinterpret_cast<Resource::Handle *>(data) = resource;
                        }
                    },
                        &renderer.MeshResource);

                    jobs[MaterialJob] = Asset::loadResourceAsync(materialPath, { MaterialType },
                        [](void *data, Resource::Handle resource, Asset::ResourceLoad::State loadState)
                    {
                        // Assign our mesh resource even when it's loading
                        // We want to gracefully handle the resource not being available yet.
                        if (loadState >= Asset::ResourceLoad::Loading)
                        {
                            *reinterpret_cast<Resource::Handle *>(data) = resource;
                        }
                    },
                        &renderer.MaterialResource);

                    return Asset::wait(jobs, JobCount, Complete);
                }
                else
                {
                    return Asset::complete(Asset::toHandle(context->Data));
                }
            }

            void saveThreadSafe(Asset::SaveContext *context) override
            {
                RendererProperty const* baseRenderer = &EmptyRendererProperty;
                if (context->BaseAsset != InvalidAsset)
                {
                    baseRenderer = toRendererProperty(context->BaseAsset);
                }

                RendererProperty *renderer = toRendererProperty(context->Asset);
                char const *meshPath = Asset::getResourcePath(renderer->MeshResource);
                char const *baseMeshPath = Asset::getResourcePath(baseRenderer->MeshResource);
                Serialization::Delta::toBinary(context->Stream, meshPath, baseMeshPath);

                char const *materialPath = Asset::getResourcePath(renderer->MaterialResource);
                char const *baseMaterialPath = Asset::getResourcePath(baseRenderer->MaterialResource);
                Serialization::Delta::toBinary(context->Stream, materialPath, baseMaterialPath);
            }

            void unloadThreadSafe(Asset::Handle asset) override
            {
                RendererProperty *renderer = toRendererProperty(asset);
                Asset::releaseResourceAsync(renderer->MeshResource);
                Asset::releaseResourceAsync(renderer->MaterialResource);
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
        Asset::loadResourceAsync(meshPath, { MeshType },
            [](void *data, Resource::Handle resource, Asset::ResourceLoad::State loadState)
        {
            // Assign our mesh resource even when it's loading
            // We want to gracefully handle the resource not being available yet.
            if (loadState >= Asset::ResourceLoad::Loading)
            {
                *reinterpret_cast<Resource::Handle *>(data) = resource;
            }
        },
            &renderer.MeshResource);

        Asset::loadResourceAsync(materialPath, { MaterialType },
            [](void *data, Resource::Handle resource, Asset::ResourceLoad::State loadState)
        {
            // Assign our mesh resource even when it's loading
            // We want to gracefully handle the resource not being available yet.
            if (loadState >= Asset::ResourceLoad::Loading)
            {
                *reinterpret_cast<Resource::Handle *>(data) = resource;
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
                                 [](void*, Resource::Handle resource, Asset::ResourceLoad::State loadState) {
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
        Editor::addAssetEditor(MaterialType, &Editor::materialEditor);
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
        materialData.AlbedoTint = asset.AlbedoTint;
        Job::Handle textureJob = Asset::loadResourceAsync(asset.AlbedoPath, { ImageType }, &materialData.AlbedoTexture);
        continueJob([materialPtr = &materialData](Job::Handle /*currentJob*/) {
            Resource::Handle textureResource = materialPtr->AlbedoTexture;
            ForwardDesc materialDesc = {};
            materialDesc.AlbedoTint = materialPtr->AlbedoTint;
            materialDesc.AlbedoImage = toImageHandle(Asset::getAssetFromResource(textureResource));
            materialPtr->Material = createMaterial(materialDesc);
            return JobResult::Complete;
        },
                    &textureJob, 1, NoJobType, RendererJobQueueIndex);

        return MaterialAsset::toHandle(reinterpret_cast<uint64_t>(&materialData));
    }

    Job::Handle createMeshAssetAsync(MeshAsset::Desc meshAsset, MeshAsset::Handle* meshHandle)
    {
        MeshData& meshData = MeshDataPool.add();
        Job::Handle job = launchJob([meshAsset, &meshData](Job::Handle /*currentJob*/)
        {
            MeshDesc meshDesc = {};
            meshDesc.Vertices.Data = meshAsset.Vertices;
            meshDesc.Vertices.Count = meshAsset.VertexCount;
            meshDesc.Indices.Data = meshAsset.Indices;
            meshDesc.Indices.Count = meshAsset.IndexCount;
            meshData.RendererMesh = createMesh(meshDesc);

            return JobResult::Complete;
        },
            NoJobType, RendererJobQueueIndex);

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
            MeshDesc meshDesc = {};
            meshDesc.Vertices.Data = meshAsset.Vertices;
            meshDesc.Vertices.Count = meshAsset.VertexCount;
            meshDesc.Indices.Data = meshAsset.Indices;
            meshDesc.Indices.Count = meshAsset.IndexCount;
            updateMesh(meshData->RendererMesh, meshDesc);

            return JobResult::Complete;
        },
            NoJobType, RendererJobQueueIndex);

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

        union SortKey
        {
            struct
            {
                Material::Handle Material;
                Mesh::Handle Mesh;
            } Handles;
            uint64_t Value;
        };

        // TODO: dynamic stack
        SortKey keys[32];
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
                        Mesh::Handle mesh = toMeshHandle(Asset::getAssetFromResource(renderer->MeshResource));
                        if (mesh != InvalidMesh)
                        {
                            keys[rendererCount].Handles.Mesh = mesh;
                            keys[rendererCount].Handles.Material = material->Material;

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
        }

        {
            DynamicArray<ImGeo::DrawData::Instance> const &instances = gatherDesc.ImGeoDrawData->Instances[ViewDesc::ForwardPass::Default];
            for (uint32_t i = 0; i < instances.count(); i++)
            {
                if (instances[i].Mesh != InvalidMesh && instances[i].Material != InvalidMaterial)
                {
                    keys[rendererCount].Handles.Mesh = instances[i].Mesh;
                    keys[rendererCount].Handles.Material = instances[i].Material;
                    transforms[rendererCount] = instances[i].Transform;
                    rendererCount++;
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

            if (rendererCount > 0)
            {
                // TODO: Not 32 max...
                uint16_t indices[32];
                for (uint16_t i = 0; i < 32; i++)
                {
                    indices[i] = i;
                }

                std::sort(indices, indices + rendererCount, [&keys](uint16_t left, uint16_t right)
                          { return keys[left].Value < keys[right].Value; });

                // Count the number of unique materials
                uint32_t uniqueMaterialCount = 1;
                Material::Handle lastMaterial = keys[indices[0]].Handles.Material;
                for (uint32_t i = 1; i < rendererCount; i++)
                {
                    uint16_t index = indices[i];
                    if (keys[index].Handles.Material != lastMaterial)
                    {
                        lastMaterial = keys[index].Handles.Material;
                        uniqueMaterialCount++;
                    }
                }

                ViewDesc::Batch *batches = allocateArray<ViewDesc::Batch>(gatherDesc.Allocator, uniqueMaterialCount);
                Material::Handle currentMaterial = InvalidMaterial;

                // Warning: Lots of nested loops below...
                uint32_t readIndex = 0;
                for (uint32_t batchIndex = 0; readIndex < rendererCount; batchIndex++)
                {
                    uint32_t materialIndex = indices[readIndex];
                    IB_ASSERT(keys[materialIndex].Handles.Material != currentMaterial, "Expecting a new material.");

                    {
                        currentMaterial = keys[materialIndex].Handles.Material;
                        batches[batchIndex].Material = currentMaterial;
                    }

                    // Count our number of unique meshes
                    uint32_t uniqueMeshCount = 1;
                    Mesh::Handle lastMesh = keys[materialIndex].Handles.Mesh;
                    for (uint32_t meshReadIndex = readIndex + 1; meshReadIndex < rendererCount; meshReadIndex++)
                    {
                        uint32_t meshIndex = indices[meshReadIndex];
                        // Only count meshes for our current material
                        if (keys[meshIndex].Handles.Material != currentMaterial)
                        {
                            break;
                        }

                        if (keys[meshIndex].Handles.Mesh != lastMesh)
                        {
                            lastMesh = keys[meshIndex].Handles.Mesh;
                            uniqueMeshCount++;
                        }
                    }

                    ViewDesc::MeshInstances *instanceGroups = allocateArray<ViewDesc::MeshInstances>(gatherDesc.Allocator, uniqueMeshCount);
                    batches[batchIndex].Meshes = instanceGroups;
                    batches[batchIndex].MeshCount = uniqueMeshCount;

                    Mesh::Handle currentMesh = InvalidMesh;
                    for (uint32_t instanceGroupIndex = 0;
                        readIndex < rendererCount && keys[indices[readIndex]].Handles.Material == currentMaterial;
                        instanceGroupIndex++)
                    {
                        uint32_t meshIndex = indices[readIndex];
                        IB_ASSERT(keys[meshIndex].Handles.Mesh != currentMesh, "Expecting a new mesh.");

                        {
                            currentMesh = keys[meshIndex].Handles.Mesh;
                            instanceGroups[instanceGroupIndex].Mesh = currentMesh;
                        }

                        uint32_t instanceCount = 0;
                        for (uint32_t instanceReadIndex = readIndex; instanceReadIndex < rendererCount; instanceReadIndex++)
                        {
                            uint32_t instanceIndex = indices[instanceReadIndex];
                            if (keys[instanceIndex].Handles.Mesh != currentMesh || keys[instanceIndex].Handles.Material != currentMaterial)
                            {
                                break;
                            }

                            instanceCount++;
                        }

                        Mat3x4 *instanceTransforms = allocateArray<Mat3x4>(gatherDesc.Allocator, instanceCount);
                        for (uint32_t instanceIndex = 0; instanceIndex < instanceCount; instanceIndex++)
                        {
                            instanceTransforms[instanceIndex] = toMatrix(transforms[indices[readIndex + instanceIndex]]);
                        }

                        instanceGroups[instanceGroupIndex].Transforms = instanceTransforms;
                        instanceGroups[instanceGroupIndex].Count = instanceCount;

                        readIndex += instanceCount;
                    }
                }

                ViewDesc::ForwardPass worldPass;
                worldPass.Batches = batches;
                worldPass.BatchCount = uniqueMaterialCount;
                viewDesc.Forward.Passes[ViewDesc::ForwardPass::Default] = worldPass;
            }

            // Restart our collection for gizmos
            rendererCount = 0;

            // TODO: Share logic between debug and default passes
            Mesh::Handle meshes[32];
            Material::Handle materials[32];
            DynamicArray<ImGeo::DrawData::Instance> const &instances = gatherDesc.ImGeoDrawData->Instances[ViewDesc::ForwardPass::DebugOverlay];
            for (uint32_t i = 0; i < instances.count(); i++)
            {
                if (instances[i].Mesh != InvalidMesh && instances[i].Material != InvalidMaterial)
                {
                    meshes[rendererCount] = instances[i].Mesh;
                    materials[rendererCount] = instances[i].Material;
                    transforms[rendererCount] = instances[i].Transform;
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
                ViewDesc::ForwardPass gizmoPass;
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
