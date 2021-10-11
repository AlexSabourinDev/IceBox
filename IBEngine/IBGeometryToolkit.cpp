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

#include <inttypes.h>

namespace IB
{
    namespace
    {
        LineSweep* toLineSweep(Asset::Handle asset) { return reinterpret_cast<LineSweep*>(Asset::toValue(asset)); }
        LineSweep* toLineSweep(Property::Handle propertyHandle) { return reinterpret_cast<LineSweep*>(Property::toValue(propertyHandle)); }

        class LineSweepPropertyStreamer : public IB::Asset::IStreamer
        {
        public:
            IB::Asset::Handle create() override
            {
                LineSweep* lineSweep = allocate<LineSweep>();
                return Asset::toHandle(reinterpret_cast<uint64_t>(lineSweep));
            }

            IB::Asset::LoadContinuation loadAsync(IB::Asset::LoadContext *context) override
            {
                LineSweep& lineSweep = *toLineSweep(context->LoadingAsset);

                uint32_t count = 0;
                Serialization::fromBinary(&context->Stream, &count);

                Float3 const* pointBuffer = Serialization::arrayFromBinary<Float3>(&context->Stream, count);
                lineSweep.Points.setBuffer(pointBuffer, count);

                Serialization::fromBinary(&context->Stream, &lineSweep.ShapeDimension);

                return Asset::complete(Asset::toHandle(reinterpret_cast<uint64_t>(&lineSweep)));
            }

            void saveThreadSafe(IB::Asset::SaveContext *context) override
            {
                LineSweep& lineSweep = *toLineSweep(context->Asset);
                Serialization::toBinary(context->Stream, lineSweep.Points.count());
                Serialization::arrayToBinary(context->Stream, lineSweep.Points.data(), lineSweep.Points.count());
                Serialization::toBinary(context->Stream, lineSweep.ShapeDimension);
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

                char fullPath[MaxFilePath] = {};
                sprintf(fullPath, "Temporary_LineSweep_%" PRIu64, Entity::toValue(entity));
                Resource::Handle lineSweepResource = Asset::createResourceThreadSafe(fullPath, MeshType, InvalidAsset);

                struct LineSweepMesh
                {
                    DynamicArray<Vertex> Vertices;
                    DynamicArray<uint16_t> Indices;
                };

                LineSweepMesh* mesh = allocate<LineSweepMesh>();

                Job::Handle assetGenerationResource = launchJob([lineSweep, mesh]()
                {
                    for (uint32_t i = 0; i < lineSweep->Points.count(); i++)
                    {
                        Float3 point = lineSweep->Points[i];

                        Float3 rectanglePoints[] = 
                        {
                            { -0.5f, 0.0f, -0.5f },
                            {  0.5f, 0.0f, -0.5f },
                            {  0.5f, 0.0f,  0.5f },
                            { -0.5f, 0.0f,  0.5f }
                        };

                        uint16_t indices[] = 
                        {
                            0, 1, 2,
                            0, 1, 3
                        };

                        for (uint32_t pi = 0; pi < 6; pi++)
                        {
                            mesh->Indices.add(static_cast<uint16_t>(indices[pi] + mesh->Vertices.count()));
                        }

                        for (uint32_t p = 0; p < 4; p++)
                        {
                            Vertex v;
                            v.Pos = Float4{ rectanglePoints[p] * Float3{ lineSweep->ShapeDimension.x, 1.0f, lineSweep->ShapeDimension.y } +point, 0.0f };
                            v.Normal = Float3{ 0.0f, 1.0f, 0.0f };
                            mesh->Vertices.add(v);
                        }
                    }

                    return JobResult::Complete;
                });

                Job::Handle meshAssetCreation = continueJob([mesh, lineSweepResource]()
                {
                    MeshAsset::Desc meshDesc = {};
                    meshDesc.Vertices = mesh->Vertices.data();
                    meshDesc.VertexCount = mesh->Vertices.count();
                    meshDesc.Indices = mesh->Indices.data();
                    meshDesc.IndexCount = mesh->Indices.count();

                    MeshAsset::Handle lineSweepMesh = createMeshAsset(meshDesc);
                    setResourceAsset(lineSweepResource, toAssetHandle(lineSweepMesh));

                    deallocate(mesh);
                    return JobResult::Complete;
                }, &assetGenerationResource, 1, RendererJobQueueIndex);

                return meshAssetCreation;
            }
        };
        LineSweepActivator LineSweepActivator;
    }

    void initGeometryToolkit()
    {
        Asset::addStreamer(LineSweepPropertyType, &LineSweepStreamer);
        addPropertyActivator(LineSweepPropertyType, &LineSweepActivator);
    }

    void killGeometryToolkit()
    {

    }
}

