#define _CRT_SECURE_NO_WARNINGS
#include "IBGeometryToolkit.h"

#include "IBAllocator.h"
#include "IBAsset.h"
#include "IBEntity.h"
#include "IBSerialization.h"
#include "IBRendererFrontend.h"
#include "IBLogging.h"
#include "IBPlatform.h"
#include "IBRenderer.h"
#include "IBJobs.h"
#include "IBEditor.h"

#include <inttypes.h>
#include <stdio.h>
#include <imgui/imgui.h>

namespace IB
{
    namespace
    {
        struct LineSweep
        {
            LineSweepDesc Desc;
        };

        LineSweep* toLineSweep(Asset::Handle asset) { return reinterpret_cast<LineSweep*>(Asset::toValue(asset)); }
        LineSweep* toLineSweep(Property::Handle propertyHandle) { return reinterpret_cast<LineSweep*>(Property::toValue(propertyHandle)); }

        struct LineSweepMesh
        {
            DynamicArray<Vertex> Vertices;
            DynamicArray<uint16_t> Indices;
        };

        LineSweepMesh createLineSweepMesh(LineSweepDesc const& lineSweep)
        {
            LineSweepMesh mesh;

            for (uint32_t i = 0; i < lineSweep.Points.count(); i++)
            {
                Transform point = lineSweep.Points[i];

                Float3 rectanglePoints[] =
                {
                    { -0.5f, 0.0f, -0.5f },
                    {  0.5f, 0.0f, -0.5f },
                    {  0.5f, 0.0f,  0.5f },
                    { -0.5f, 0.0f,  0.5f }
                };

                float UVs[] = 
                {
                    bitcast<float>(toHalf2(Float2{0.0f, 0.0f})),
                    bitcast<float>(toHalf2(Float2{1.0f, 0.0f})),
                    bitcast<float>(toHalf2(Float2{1.0f, 1.0f})),
                    bitcast<float>(toHalf2(Float2{0.0f, 1.0f})),
                };

                for (uint32_t p = 0; p < 4; p++)
                {
                    Vertex v;
                    v.Pos = Float4{ transformPoint(point, rectanglePoints[p] * Float3{ lineSweep.ShapeDimension.x, 1.0f, lineSweep.ShapeDimension.y }), UVs[p] };
                    v.Normal = Float4{ 0.0f, 1.0f, 0.0f, bitcast<float>(0xFFFFFFFF) };
                    mesh.Vertices.add(v);
                }
            }

            // Starting face
            uint32_t constexpr quadIndexCount = 6;
            {
                uint16_t indices[] =
                {
                    0, 1, 2,
                    0, 2, 3
                };

                for (uint32_t pi = 0; pi < quadIndexCount; pi++)
                {
                    mesh.Indices.add(indices[pi]);
                }
            }

            // Tubes

            /*
              7---6
            3---2 |
            | 4 | 5
            0---1
            */
            for (uint32_t i = 1; i < lineSweep.Points.count(); i++)
            {
                uint16_t indices[] =
                {
                    // bottom
                    0, 5, 1,
                    0, 4, 5,

                    // right
                    1, 5, 2,
                    2, 5, 6,

                    // top
                    3, 2, 6,
                    3, 6, 7,

                    // left
                    0, 3, 4,
                    4, 3, 7,
                };

                uint16_t indexOffset = static_cast<uint16_t>((i - 1) * 4); // Advance our tube by one set of points each iteration
                for (uint32_t pi = 0; pi < quadIndexCount * 4; pi++)
                {
                    mesh.Indices.add(static_cast<uint16_t>(indices[pi] + indexOffset));
                }
            }

            // Ending face
            {
                uint16_t indices[] =
                {
                    0, 2, 1,
                    0, 3, 2
                };

                uint16_t indexOffset = static_cast<uint16_t>((lineSweep.Points.count() - 1) * 4); // Advance our tube by one set of points each iteration
                for (uint32_t pi = 0; pi < quadIndexCount; pi++)
                {
                    mesh.Indices.add(static_cast<uint16_t>(indices[pi] + indexOffset));
                }
            }

            return mesh;
        }

        class LineSweepPropertyStreamer : public IB::Asset::IStreamer
        {
        public:
            IB::Asset::Handle create() override
            {
                LineSweep* lineSweep = allocate<LineSweep>();
                // TODO: temporary line sweep test
                lineSweep->Desc.ShapeDimension = Float2{ 1.0f, 1.0f };
                lineSweep->Desc.Points.add(Transform{ Float3{ 0.0f, 0.0f, 0.0f } });
                lineSweep->Desc.Points.add(Transform{ Float3{ 0.0f, 1.0f, 0.0f } });
                lineSweep->Desc.Points.add(Transform{ Float3{ 0.0f, 2.0f, 0.0f } });
                lineSweep->Desc.Points.add(Transform{ Float3{ 0.0f, 3.0f, 0.0f } });

                return Asset::toHandle(reinterpret_cast<uint64_t>(lineSweep));
            }

            IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
            {
                LineSweep& lineSweep = *toLineSweep(context->LoadingAsset);

                uint32_t count = 0;
                Serialization::fromBinary(&context->Stream, &count);

                Transform const* pointBuffer = Serialization::arrayFromBinary<Transform>(&context->Stream, count);
                lineSweep.Desc.Points.setBuffer(pointBuffer, count);

                Serialization::fromBinary(&context->Stream, &lineSweep.Desc.ShapeDimension);

                return Asset::complete(Asset::toHandle(reinterpret_cast<uint64_t>(&lineSweep)));
            }

            void saveThreadSafe(IB::Asset::SaveContext *context) override
            {
                LineSweep& lineSweep = *toLineSweep(context->Asset);
                Serialization::toBinary(context->Stream, lineSweep.Desc.Points.count());
                Serialization::arrayToBinary(context->Stream, lineSweep.Desc.Points.data(), lineSweep.Desc.Points.count());
                Serialization::toBinary(context->Stream, lineSweep.Desc.ShapeDimension);
            }

            void unloadThreadSafe(IB::Asset::Handle assetHandle) override
            {
                deallocate(toLineSweep(assetHandle));
            }
        };
        LineSweepPropertyStreamer LineSweepStreamer;

        class LineSweepActivator : public IPropertyActivator
        {
        public:
            Job::Handle onLoadAsync(Entity::Handle entity) const override
            {
                Property::Handle rendererProperty = getPropertyFromEntity(entity, RendererPropertyType);
                if (rendererProperty == InvalidProperty)
                {
                    char const* noResourcePath = "";
                    rendererProperty = createRendererProperty(noResourcePath, noResourcePath);
                    addPropertyToEntity(entity, RendererPropertyType, rendererProperty);
                }

                Property::Handle lineSweepProperty = getPropertyFromEntity(entity, LineSweepPropertyType);
                IB_ASSERT(lineSweepProperty != InvalidProperty, "Line sweep property should be valid!");

                LineSweep* lineSweep = toLineSweep(lineSweepProperty);

                struct LineSweepData
                {
                    Resource::Handle Resource;
                    LineSweepMesh Mesh;
                };

                LineSweepData* lineSweepData = allocate<LineSweepData>();

                char path[MaxFilePath];
                sprintf(path, "Dynamic_LineSweep_%" PRIu64, Entity::toValue(entity));

                Job::Handle loadJob = loadResourceAsync(path, MeshType,
                    [](void *data, Resource::Handle resource, Asset::ResourceLoad::State)
                    {
                        LineSweepData* lineSweepData = reinterpret_cast<LineSweepData*>(data);
                        lineSweepData->Resource = resource;
                    }, lineSweepData);

                Job::Handle assetGenerationResource = launchJob([lineSweep, lineSweepData](Job::Handle /*currentJob*/)
                {
                    lineSweepData->Mesh = createLineSweepMesh(lineSweep->Desc);
                    return JobResult::Complete;
                });

                Job::Handle jobDependencies[] = { loadJob, assetGenerationResource };
                Job::Handle meshAssetCreation = continueJob([lineSweep, lineSweepData, rendererProperty](Job::Handle /*currentJob*/)
                {
                    MeshAsset::Desc meshDesc = {};
                    meshDesc.Vertices = lineSweepData->Mesh.Vertices.data();
                    meshDesc.VertexCount = lineSweepData->Mesh.Vertices.count();
                    meshDesc.Indices = lineSweepData->Mesh.Indices.data();
                    meshDesc.IndexCount = lineSweepData->Mesh.Indices.count();

                    MeshAsset::Handle lineSweepMesh;
                    Job::Handle createMeshJob = createMeshAssetAsync(meshDesc, &lineSweepMesh);

                    setResourceAsset(lineSweepData->Resource, toAssetHandle(lineSweepMesh));
                    setRendererMesh(rendererProperty, lineSweepData->Resource);
                    Asset::releaseResourceAsync(lineSweepData->Resource);

                    // Only deallocate our sweep data once the data is consumed.
                    continueJob([lineSweepData](Job::Handle /*currentJob*/)
                    {
                        deallocate(lineSweepData);
                        return JobResult::Complete;
                    }, &createMeshJob, 1);
                    return JobResult::Complete;
                }, jobDependencies, 2);

                return meshAssetCreation;
            }
        };
        LineSweepActivator LineSweepActivator;
    }

    LineSweepDesc* getLineSweep(Property::Handle lineSweepProperty)
    {
        return &reinterpret_cast<LineSweep*>(Property::toValue(lineSweepProperty))->Desc;
    }

    Job::Handle updateLineSweepAsync(Entity::Handle entity, LineSweepDesc const& lineSweep)
    {
        struct JobState
        {
            enum
            {
                CreatingMesh,
                CleanupMesh
            };

            uint32_t State = CreatingMesh;
            LineSweepMesh Mesh;
        };

        Property::Handle rendererProperty = getPropertyFromEntity(entity, RendererPropertyType);
        return launchJob([lineSweep, rendererProperty, jobState = JobState{}](Job::Handle currentJob) mutable
        {
            if (jobState.State == JobState::CreatingMesh)
            {
                jobState.Mesh = createLineSweepMesh(lineSweep);

                MeshAsset::Desc meshDesc = {};
                meshDesc.Vertices = jobState.Mesh.Vertices.data();
                meshDesc.VertexCount = jobState.Mesh.Vertices.count();
                meshDesc.Indices = jobState.Mesh.Indices.data();
                meshDesc.IndexCount = jobState.Mesh.Indices.count();

                MeshAsset::Handle lineSweepMesh = getMeshFromRendererProperty(rendererProperty);
                Job::Handle createMeshJob = updateMeshAssetAsync(lineSweepMesh, meshDesc);

                jobState.State = JobState::CleanupMesh;

                // Put our job to sleep, it will be complete once our mesh job is complete. (This will also clean up all our data)
                continueJob(currentJob, &createMeshJob, 1);
                return JobResult::Sleep;
            }
            else
            {
                return JobResult::Complete;
            }
        });
    }

    namespace Editor
    {
        void lineSweepEditor(Asset::Handle asset, void *parentData)
        {
            PropertyEditorState *editorState = reinterpret_cast<PropertyEditorState *>(parentData);

            if (propertyHeader("Line Sweep", &editorState->DestroyProperty))
            {
                LineSweepDesc* linesweep = getLineSweep(toPropertyHandle(asset));
                for (uint32_t i = 0; i < linesweep->Points.count(); i++)
                {
                    ImGui::PushID(i);
                    if (ImGui::TreeNode("", "[%u]", i))
                    {
                        imguiTransform(&linesweep->Points[i]);
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        }
    } // namespace Editor

    void initGeometryToolkit()
    {
        Asset::addStreamer(LineSweepPropertyType, &LineSweepStreamer);
        addPropertyActivator(LineSweepPropertyType, &LineSweepActivator);
        Editor::addAssetEditor(LineSweepPropertyType, &Editor::lineSweepEditor);
    }

    void killGeometryToolkit()
    {

    }
}

