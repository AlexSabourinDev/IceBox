#define _CRT_SECURE_NO_WARNINGS

#include <IBEngine/IBRenderer.h>
#include <IBEngine/IBRendererFrontend.h>
#include <IBEngine/IBSerialization.h>
#include <IBEngine/Platform/IBPlatform.h>

#include <stdio.h>
#include <string.h>
#include <math.h>

#include <Windows.h>

int main()
{
    bool windowVisible = true;

    IB::WindowDesc winDesc = {};
    winDesc.Name = "Ice Box";
    winDesc.Width = 500;
    winDesc.Height = 500;
    winDesc.OnWindowMessage = [](void *data, IB::WindowMessage message)
    {
        switch (message.Type)
        {
        case IB::WindowMessage::Resize:
            *reinterpret_cast<bool*>(data) = message.Data.Resize.Width > 0;
            break;
        case IB::WindowMessage::Close:
            IB::sendQuitMessage();
            break;
        }
    };
    winDesc.CallbackState = &windowVisible;
    IB::WindowHandle window = IB::createWindow(winDesc);

    IB::File forwardSampleFile = IB::openFile("../Assets/Compiled/SampleForward.c.hlsl", IB::OpenFileOptions::Read);
    void* forwardSample = IB::mapFile(forwardSampleFile);

    IB::ShaderAsset shaders;
    IB::Serialization::MemoryStream shaderReadStream{ reinterpret_cast<uint8_t*>(forwardSample) };
    fromBinary(&shaderReadStream, &shaders);

    IB::RendererDesc rendererDesc = {};
    rendererDesc.Window = &window;
    rendererDesc.Materials.Forward.VShader = shaders.VertexShader;
    rendererDesc.Materials.Forward.VShaderSize = shaders.VertexShaderSize;
    rendererDesc.Materials.Forward.FShader = shaders.FragShader;
    rendererDesc.Materials.Forward.FShaderSize = shaders.FragShaderSize;
    IB::initRenderer(&rendererDesc);

    // Can unmap shaders now
    IB::unmapFile(forwardSampleFile);
    IB::closeFile(forwardSampleFile);

    uint8_t imageTexels[] = {80, 180, 255, 255, 80, 180, 255, 255, 80, 180, 255, 255, 255, 180, 80, 255};

    IB::ImageDesc imageDesc = {};
    imageDesc.Format = IB::ImageFormat::RGBA8;
    imageDesc.Width = 2;
    imageDesc.Height = 2;
    imageDesc.Data = imageTexels;
    IB::ImageHandle albedoImage = IB::createImage(&imageDesc);

    float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    IB::ForwardDesc matDesc = {};
    memcpy(matDesc.AlbedoTint, tint, sizeof(float) * 4);
    matDesc.AlbedoImage = albedoImage;
    IB::MaterialHandle someMaterial = IB::createMaterial(&matDesc);

    IB::File meshFile = IB::openFile("../Assets/Compiled/Box.c.msh", IB::OpenFileOptions::Read);
    void* meshData = IB::mapFile(meshFile);

    IB::MeshAsset mesh = {};
    IB::Serialization::MemoryStream readStream{ reinterpret_cast<uint8_t*>(meshData) };
    fromBinary(&readStream, &mesh);

    IB::MeshDesc meshDesc = {};
    meshDesc.Vertices.Data = mesh.Vertices;
    meshDesc.Vertices.Count = mesh.VertexCount;
    meshDesc.Indices.Data = mesh.Indices;
    meshDesc.Indices.Count = mesh.IndexCount;

    IB::MeshHandle someMesh = IB::createMesh(&meshDesc);

    // Don't need mesh memory anymore, it's fed to the GPU
    IB::unmapFile(meshFile);
    IB::closeFile(meshFile);

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

    IB::PlatformMessage message = IB::PlatformMessage::None;
    while (message != IB::PlatformMessage::Quit)
    {
        IB::consumeMessageQueue([](void *data, IB::PlatformMessage message)
        {
            *reinterpret_cast<IB::PlatformMessage*>(data) = message;
        }, &message);

        if (windowVisible)
        {
            static float spin = -3.14f * 0.5f;
            spin += 0.01f;
            IB::Mat3x4 transform =
            {
                {{cosf(spin), 0.0f, -sinf(spin), 0.0f},
                 {0.0f, 1.0f, 0.0f, 0.0f},
                 {sinf(spin), 0.0f, cosf(spin), 5.0f}},
            };

            IB::ViewDesc::MeshInstances meshInstances = {};
            meshInstances.Mesh = someMesh;
            meshInstances.Transforms = &transform;
            meshInstances.Count = 1;

            IB::ViewDesc::Batch batch = {};
            batch.Material = someMaterial;
            batch.Meshes = &meshInstances;
            batch.MeshCount = 1;

            IB::ViewDesc viewDesc;
            viewDesc.ViewProj = projection;
            viewDesc.Batches[IB::ViewDesc::Materials::Forward] = &batch;
            viewDesc.BatchCounts[IB::ViewDesc::Materials::Forward] = 1;

            IB::drawView(&viewDesc);
        }
    }

    IB::killRenderer();
    IB::destroyWindow(window);
}
