#define _CRT_SECURE_NO_WARNINGS

#include <IBEngine/IBRenderer.h>
#include <IBEngine/IBRendererFrontend.h>
#include <IBEngine/IBSerialization.h>
#include <IBEngine/IBLogging.h>
#include <IBEngine/IBPlatform.h>

#include <stdio.h>
#include <string.h>
#include <math.h>

#include <Windows.h>

int main()
{
    struct SampleState
    {
        bool WindowVisible = true;
        bool CullGizmo = true;
        uint32_t Width = 500;
        uint32_t Height = 500;
        IB::Mat4x4 ViewProj;
        IB::Mat4x4 InvViewProj;
        IB::Float3 ViewPos;

        IB::AABB MeshAabb;
        IB::Float3 MeshPos;

        IB::Float3 GizmoPos;
        uint32_t PreviousMouseX = 0;
        uint32_t PreviousMouseY = 0;
        uint32_t ActiveAxis = 3;
    };

    SampleState sampleState;

    IB::WindowDesc winDesc = {};
    winDesc.Name = "Ice Box";
    winDesc.Width = sampleState.Width;
    winDesc.Height = sampleState.Height;
    winDesc.OnWindowMessage = [](void *data, IB::WindowMessage message) {
        SampleState *sampleState = reinterpret_cast<SampleState *>(data);
        switch (message.Type)
        {
        case IB::WindowMessage::Resize:
            sampleState->WindowVisible = message.Data.Resize.Width > 0;
            sampleState->Width = message.Data.Resize.Width;
            sampleState->Height = message.Data.Resize.Height;
            break;
        case IB::WindowMessage::Close:
            IB::sendQuitMessage();
            break;
        case IB::WindowMessage::MouseClick:
            if (message.Data.MouseClick.State == IB::WindowMessage::Mouse::Pressed &&
                message.Data.MouseClick.Button == IB::WindowMessage::Mouse::Left)
            {
                sampleState->PreviousMouseX = message.Data.MouseClick.X;
                sampleState->PreviousMouseY = message.Data.MouseClick.Y;
                float u = static_cast<float>(message.Data.MouseClick.X) / static_cast<float>(sampleState->Width)*2.0f - 1.0f;
                float v = static_cast<float>(message.Data.MouseClick.Y) / static_cast<float>(sampleState->Height)*2.0f - 1.0f;

                IB::Float4 ndcRayStart = IB::Float4{u, v, 0.0f, 1.0f};
                IB::Float4 rayStart4 = IB::mul(sampleState->InvViewProj, ndcRayStart);
                IB::Float3 rayStart = IB::Float3{rayStart4.x, rayStart4.y, rayStart4.z}/ rayStart4.w;

                IB::Float4 ndcRayEnd = IB::Float4{ u, v, 1.0f, 1.0f };
                IB::Float4 rayEnd4 = IB::mul(sampleState->InvViewProj, ndcRayEnd);
                IB::Float3 rayEnd = IB::Float3{ rayEnd4.x, rayEnd4.y, rayEnd4.z } / rayEnd4.w;

                if (doesLineAABBIntersect(rayStart - sampleState->MeshPos, rayEnd - sampleState->MeshPos, sampleState->MeshAabb))
                {
                    sampleState->CullGizmo = false;
                }

                if (!sampleState->CullGizmo)
                {
                    for (uint32_t i = 0; i < 3; i++)
                    {
                        IB::Float3 direction;
                        direction[i] = 1.0f;
                        direction[(i + 1) % 3] = 0.0f;
                        direction[(i + 2) % 3] = 0.0f;

                        if (doesLineCylinderIntersect(rayStart, rayEnd, sampleState->GizmoPos, sampleState->GizmoPos + direction, 0.1f))
                        {
                            char const* name[3] = { "X axis!", "Y axis!", "Z axis!" };
                            IB_LOG(IB::LogLevel::Log, "Sample", name[i]);
                            sampleState->ActiveAxis = i;
                            break;
                        }

                    }
                }
            }
            else if (message.Data.MouseClick.State == IB::WindowMessage::Mouse::Released)
            {
                sampleState->ActiveAxis = 3;
            }
            break;
        case IB::WindowMessage::MouseMove:
            {
                if (!sampleState->CullGizmo && sampleState->ActiveAxis < 3)
                {
                    auto mouseToWorldPos = [sampleState](uint32_t x, uint32_t y)
                    {
                        float u = static_cast<float>(x) / static_cast<float>(sampleState->Width)*2.0f - 1.0f;
                        float v = static_cast<float>(y) / static_cast<float>(sampleState->Height)*2.0f - 1.0f;

                        IB::Float4 ndcRayStart = IB::Float4{ u, v, 0.0f, 1.0f };
                        IB::Float4 rayStart4 = IB::mul(sampleState->InvViewProj, ndcRayStart);
                        return IB::Float3{ rayStart4.x, rayStart4.y, rayStart4.z } / rayStart4.w;
                    };

                    IB::Float3 previousPos = mouseToWorldPos(sampleState->PreviousMouseX, sampleState->PreviousMouseY);
                    IB::Float3 nextPos = mouseToWorldPos(message.Data.MouseMove.X, message.Data.MouseMove.Y);

                    IB::Float3 previousRayDir = previousPos - sampleState->ViewPos;
                    IB::Float3 nextRayDir = nextPos - sampleState->ViewPos;

                    // Intersect our ray with the plane defined by our axies

                    IB::Float3 normals[3] = 
                    {
                        {-1.0f, 0.0f, 0.0f},
                        {0.0f, -1.0f, 0.0f},
                        {0.0f, 0.0f, -1.0f},
                    };
                    // Given our current ray direction, pick our axis normal that is most parallel
                    // This will give us the most perpendicular plane
                    float angles[] =
                    {
                        fabsf(dot(nextRayDir, normals[(sampleState->ActiveAxis + 1) % 3])),
                        fabsf(dot(nextRayDir, normals[(sampleState->ActiveAxis + 2) % 3]))
                    };

                    uint32_t largest = angles[0] > angles[1] ? 0 : 1;
                    uint32_t planeAxis = (sampleState->ActiveAxis + largest + 1) % 3;

                    float distance = sampleState->MeshPos[planeAxis];

                    previousPos = intersectRayPlane(normals[planeAxis], distance, previousRayDir, previousPos);
                    nextPos = intersectRayPlane(normals[planeAxis], distance, nextRayDir, nextPos);

                    float axisDelta = nextPos[sampleState->ActiveAxis] - previousPos[sampleState->ActiveAxis];
                    sampleState->MeshPos[sampleState->ActiveAxis] += axisDelta;

                    sampleState->PreviousMouseX = message.Data.MouseMove.X;
                    sampleState->PreviousMouseY = message.Data.MouseMove.Y;
                }
            }
            break;
        }
    };
    winDesc.CallbackState = &sampleState;
    IB::WindowHandle window = IB::createWindow(winDesc);

    IB::File forwardSampleFile = IB::openFile("../Assets/Compiled/SampleForward.c.shdr", IB::OpenFileOptions::Read);
    void *forwardSample = IB::mapFile(forwardSampleFile);

    IB::ShaderAsset shaders;
    IB::Serialization::MemoryStream shaderReadStream{reinterpret_cast<uint8_t *>(forwardSample)};
    fromBinary(&shaderReadStream, &shaders);

    IB::RendererDesc rendererDesc = {};
    rendererDesc.Window = &window;
    rendererDesc.Materials.Forward.VShader = shaders.VertexShader;
    rendererDesc.Materials.Forward.VShaderSize = shaders.VertexShaderSize;
    rendererDesc.Materials.Forward.FShader = shaders.FragShader;
    rendererDesc.Materials.Forward.FShaderSize = shaders.FragShaderSize;
    IB::initRenderer(rendererDesc);

    // Can unmap shaders now
    IB::unmapFile(forwardSampleFile);
    IB::closeFile(forwardSampleFile);

    uint8_t imageTexels[] = {80, 180, 255, 255, 80, 180, 255, 255, 80, 180, 255, 255, 255, 180, 80, 255};

    IB::ImageDesc imageDesc = {};
    imageDesc.Format = IB::ImageFormat::RGBA8;
    imageDesc.Width = 2;
    imageDesc.Height = 2;
    imageDesc.Data = imageTexels;
    IB::ImageHandle albedoImage = IB::createImage(imageDesc);

    float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    IB::ForwardDesc matDesc = {};
    memcpy(matDesc.AlbedoTint, tint, sizeof(float) * 4);
    matDesc.AlbedoImage = albedoImage;
    IB::MaterialHandle someMaterial = IB::createMaterial(matDesc);

    uint8_t whiteImageTexels[] = {255, 255, 255, 255};

    IB::ImageDesc whiteImageDesc = {};
    whiteImageDesc.Format = IB::ImageFormat::RGBA8;
    whiteImageDesc.Width = 1;
    whiteImageDesc.Height = 1;
    whiteImageDesc.Data = whiteImageTexels;
    IB::ImageHandle whiteImage = IB::createImage(whiteImageDesc);

    IB::ForwardDesc gizmoMatDesc = {};
    memcpy(gizmoMatDesc.AlbedoTint, tint, sizeof(float) * 4);
    gizmoMatDesc.AlbedoImage = whiteImage;
    IB::MaterialHandle gizmoMaterial = IB::createMaterial(gizmoMatDesc);

    IB::File meshFile = IB::openFile("../Assets/Compiled/Box.c.msh", IB::OpenFileOptions::Read);
    void *meshData = IB::mapFile(meshFile);

    IB::MeshAsset mesh = {};
    IB::Serialization::MemoryStream readStream{reinterpret_cast<uint8_t *>(meshData)};
    fromBinary(&readStream, &mesh);

    IB::AABB meshAABB = { {1000.0f, 1000.0f, 1000.0f }, { -1000.0f, -1000.0f, -1000.0f } };
    for (uint32_t i = 0; i < mesh.VertexCount; i++)
    {
        IB::Float3 p = { mesh.Vertices[i].Pos[0], mesh.Vertices[i].Pos[1], mesh.Vertices[i].Pos[2] };
        meshAABB = consume(meshAABB, p);
    }
    sampleState.MeshAabb = meshAABB;

    IB::MeshDesc meshDesc = {};
    meshDesc.Vertices.Data = mesh.Vertices;
    meshDesc.Vertices.Count = mesh.VertexCount;
    meshDesc.Indices.Data = mesh.Indices;
    meshDesc.Indices.Count = mesh.IndexCount;

    IB::MeshHandle someMesh = IB::createMesh(meshDesc);

    // Don't need mesh memory anymore, it's fed to the GPU
    IB::unmapFile(meshFile);
    IB::closeFile(meshFile);

    // Create our gizmo
    IB::MeshHandle gizmoMesh;
    {
        constexpr uint32_t cylinderSegments = 8;
        constexpr float cylinderLength = 0.75f;
        constexpr float cylinderRadius = 0.035f;

        constexpr uint32_t cylinderVertexCount = cylinderSegments * 2;
        constexpr uint32_t cylinderIndexCount = cylinderSegments * 6;

        constexpr uint32_t coneSegmentCount = 8;
        constexpr float coneHeight = 0.25f;
        constexpr float coneRadius = 0.1f;

        constexpr uint32_t coneVertexCount = coneSegmentCount + 2;
        constexpr uint32_t coneIndexCount = coneSegmentCount * 3 + coneSegmentCount * 3;

        constexpr uint32_t axisVertexCount = (cylinderVertexCount + coneVertexCount);
        constexpr uint32_t axisIndexCount = (cylinderIndexCount + coneIndexCount);

        IB::Vertex vertices[axisVertexCount * 3];
        uint16_t indices[axisIndexCount * 3];

        for (uint32_t axis = 0; axis < 3; axis++)
        {
            float colors[3][4] = {
                {1.0f, 0.0f, 0.0f, 1.0f},
                {0.0f, 1.0f, 0.0f, 1.0f},
                {0.0f, 0.0f, 1.0f, 1.0f}};

            IB::Vertex *axisVerts = vertices + axisVertexCount * axis;
            uint16_t *axisIndices = indices + axisIndexCount * axis;

            float cylinderAngleDelta = IB::Tao / static_cast<float>(cylinderSegments);
            for (uint32_t i = 0; i < cylinderSegments; i++)
            {
                float start[3] = {};
                float end[3] = {};

                {
                    float angle = cylinderAngleDelta * i;
                    start[axis] = 0.0f;
                    end[axis] = cylinderLength;
                    end[(axis + 1) % 3] = start[(axis + 1) % 3] = sinf(angle) * cylinderRadius;
                    end[(axis + 2) % 3] = start[(axis + 2) % 3] = cosf(angle) * cylinderRadius;
                }

                axisVerts[i * 2] = IB::Vertex{{start[0], start[1], start[2]}};
                memcpy(&axisVerts[i * 2].Color, colors[axis], sizeof(float) * 4);
                axisVerts[i * 2 + 1] = IB::Vertex{{end[0], end[1], end[2]}};
                memcpy(axisVerts[i * 2 + 1].Color, colors[axis], sizeof(float) * 4);
            }

            axisVerts += cylinderVertexCount;
            float coneAngleDelta = IB::Tao / static_cast<float>(cylinderSegments);
            for (uint32_t i = 0; i < coneSegmentCount; i++)
            {
                float angle = coneAngleDelta * i;
                float pos[3];
                pos[axis] = cylinderLength;
                pos[(axis + 1) % 3] = sinf(angle) * coneRadius;
                pos[(axis + 2) % 3] = cosf(angle) * coneRadius;
                axisVerts[i] = IB::Vertex{{pos[0], pos[1], pos[2]}};
                memcpy(axisVerts[i].Color, colors[axis], sizeof(float) * 4);
            }

            float pos[3];
            pos[axis] = cylinderLength;
            pos[(axis + 1) % 3] = 0.0f;
            pos[(axis + 2) % 3] = 0.0f;
            axisVerts[coneSegmentCount] = IB::Vertex{{pos[0], pos[1], pos[2]}};
            memcpy(axisVerts[coneSegmentCount].Color, colors[axis], sizeof(float) * 4);

            pos[axis] = cylinderLength + coneHeight;
            axisVerts[coneSegmentCount + 1] = IB::Vertex{{pos[0], pos[1], pos[2]}};
            memcpy(axisVerts[coneSegmentCount + 1].Color, colors[axis], sizeof(float) * 4);

            for (uint32_t i = 0; i < cylinderSegments; i++)
            {
                uint16_t startEdge = axisVertexCount * axis + i * 2;
                uint16_t endEdge = axisVertexCount * axis + (i + 1) * 2 % cylinderVertexCount;

                axisIndices[i * 6] = startEdge;
                axisIndices[i * 6 + 1] = startEdge + 1;
                axisIndices[i * 6 + 2] = endEdge + 1;
                axisIndices[i * 6 + 3] = endEdge + 1;
                axisIndices[i * 6 + 4] = endEdge;
                axisIndices[i * 6 + 5] = startEdge;
            }

            axisIndices += cylinderIndexCount;
            for (uint32_t i = 0; i < coneSegmentCount; i++)
            {
                uint16_t centerVertex = axisVertexCount * axis + cylinderVertexCount + coneSegmentCount;
                uint16_t topVertex = axisVertexCount * axis + cylinderVertexCount + coneSegmentCount + 1;

                uint16_t startVertex = axisVertexCount * axis + cylinderVertexCount + i;
                uint16_t endVertex = axisVertexCount * axis + cylinderVertexCount + (i + 1) % coneSegmentCount;

                axisIndices[i * 6] = startVertex;
                axisIndices[i * 6 + 1] = topVertex;
                axisIndices[i * 6 + 2] = endVertex;
                axisIndices[i * 6 + 3] = centerVertex;
                axisIndices[i * 6 + 4] = startVertex;
                axisIndices[i * 6 + 5] = endVertex;
            }
        }

        IB::MeshDesc meshDesc = {};
        meshDesc.Vertices.Data = vertices;
        meshDesc.Vertices.Count = axisVertexCount * 3;
        meshDesc.Indices.Data = indices;
        meshDesc.Indices.Count = axisIndexCount * 3;

        gizmoMesh = IB::createMesh(meshDesc);
    }

    float fov = 1.0f / tanf(3.1415f * 0.25);
    const float f = 45.0f;
    const float n = 1.0f;
    const float a = 1.0f;
    IB::Mat4x4 projection =
        {
            {{fov / a, 0.0f, 0.0f, 0.0f},
             {0.0f, -fov, 0.0f, 0.0f},
             {0.0f, 0.0f, f / (f - n), -n * f / (f - n)},
             {0.0f, 0.0f, 1.0, 0.0f}},
        };

    IB::Mat4x4 invProjection =
        {
            {{a / fov, 0.0f, 0.0f, 0.0f},
             {0.0f, -1.0f / fov, 0.0f, 0.0f},
             {0.0f, 0.0f, 0.0f, 1.0f},
             {0.0f, 0.0f, (f-n)/(-n*f), f/(n*f)}},
        };

    sampleState.MeshPos = IB::Float3{ 0.0f, 5.0f, 0.0f };

    IB::PlatformMessage message = IB::PlatformMessage::None;
    while (message != IB::PlatformMessage::Quit)
    {
        IB::consumeMessageQueue([](void *data, IB::PlatformMessage message) {
            *reinterpret_cast<IB::PlatformMessage *>(data) = message;
        },
                                &message);

        if (sampleState.WindowVisible)
        {
            static float spin = -3.14f * 0.5f;
            spin += 0.01f;
            IB::Mat3x4 meshTransform =
                {
                    {{1.0f, 0.0f, 0.0f, sampleState.MeshPos.x},
                     {0.0f, 1.0f, 0.0f, sampleState.MeshPos.y},
                     {0.0f, 0.0f, 1.0f, sampleState.MeshPos.z}},
                };

            sampleState.ViewPos = IB::Float3{ -2.0f, 1.0f, 0.0f };
            IB::Mat4x4 view =
                {
                    {{1.0f, 0.0f, 0.0f, -sampleState.ViewPos.x},
                     {0.0f, 0.0f, 1.0f, -sampleState.ViewPos.z},
                     {0.0f, 1.0f, 0.0f, -sampleState.ViewPos.y},
                     {0.0f, 0.0f, 0.0f, 1.0f}},
                };

            IB::Mat4x4 invView = 
            {
                {{1.0f, 0.0f, 0.0f, sampleState.ViewPos.x},
                 {0.0f, 0.0f, 1.0f, sampleState.ViewPos.y},
                 {0.0f, 1.0f, 0.0f, sampleState.ViewPos.z},
                 {0.0f, 0.0f, 0.0f, 1.0f}},
            };
            sampleState.InvViewProj = IB::mul(invView, invProjection);
            sampleState.ViewProj = mul(projection, view);

            IB::ViewDesc::MeshInstances meshInstance = {};
            meshInstance.Mesh = someMesh;
            meshInstance.Transforms = &meshTransform;
            meshInstance.Count = 1;

            IB::ViewDesc::Batch meshBatch = {};
            meshBatch.Material = someMaterial;
            meshBatch.Meshes = &meshInstance;
            meshBatch.MeshCount = 1;

            IB::ViewDesc::Pass worldPass;
            worldPass.Batches = &meshBatch;
            worldPass.BatchCount = 1;

            IB::ViewDesc viewDesc = {};
            viewDesc.ViewProj = sampleState.ViewProj;
            viewDesc.Forward.Passes[IB::ViewDesc::Pass::Default] = worldPass;

            IB::ViewDesc::MeshInstances gizmoInstance = {};
            IB::ViewDesc::Batch gizmoBatch = {};
            IB::ViewDesc::Pass gizmoPass;
            IB::Mat3x4 gizmoTransform;
            if (!sampleState.CullGizmo)
            {
                sampleState.GizmoPos = sampleState.MeshPos;
                IB::Float3 cameraPos = { -view[0][3], -view[2][3], -view[1][3] };

                // Calculate constant distance from camera
                float similarTrianglesScale = 5.0f / (sampleState.GizmoPos.y - cameraPos.y);
                sampleState.GizmoPos.x = sampleState.GizmoPos.x * similarTrianglesScale + cameraPos.x * (1.0f - similarTrianglesScale);
                sampleState.GizmoPos.y = 5.0f + cameraPos.y;
                sampleState.GizmoPos.z = sampleState.GizmoPos.z * similarTrianglesScale + cameraPos.z * (1.0f - similarTrianglesScale);

                gizmoTransform =
                {
                    {{1.0f, 0.0f, 0.0f, sampleState.GizmoPos.x},
                     {0.0f, 1.0f, 0.0f, sampleState.GizmoPos.y},
                     {0.0f, 0.0f, 1.0f, sampleState.GizmoPos.z}},
                };

                gizmoInstance.Mesh = gizmoMesh;
                gizmoInstance.Transforms = &gizmoTransform;
                gizmoInstance.Count = 1;

                gizmoBatch.Material = gizmoMaterial;
                gizmoBatch.Meshes = &gizmoInstance;
                gizmoBatch.MeshCount = 1;

                gizmoPass.Batches = &gizmoBatch;
                gizmoPass.BatchCount = 1;

                viewDesc.Forward.Passes[IB::ViewDesc::Pass::DebugOverlay] = gizmoPass;
            }

            IB::drawView(viewDesc);
        }
    }

    IB::killRenderer();
    IB::destroyWindow(window);
}
