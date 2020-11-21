#define _CRT_SECURE_NO_WARNINGS

#include <IBEngine/IBRenderer.h>
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
    winDesc.OnCloseRequested = [](void *) { IB::sendQuitMessage(); };
    winDesc.OnWindowMessage = [](void *data, IB::WindowMessage message)
    {
        if (message.Type == IB::WindowMessage::Resize)
        {
            *reinterpret_cast<bool*>(data) = message.Data.Resize.Width > 0;
        }
    };
    winDesc.CallbackState = &windowVisible;
    IB::WindowHandle window = IB::createWindow(winDesc);

    FILE *file = fopen("../Assets/Raw/SampleForwardVert.spv", "rb");
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    void *vertSource = malloc(fileSize);
    unsigned int vertSize = fileSize;
    fread(vertSource, fileSize, 1, file);
    fclose(file);

    file = fopen("../Assets/Raw/SampleForwardFrag.spv", "rb");
    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    void *fragSource = malloc(fileSize);
    unsigned int fragSize = fileSize;
    fread(fragSource, fileSize, 1, file);
    fclose(file);

    IB::RendererDesc rendererDesc = {};
    rendererDesc.Window = &window;
    rendererDesc.Materials.Forward.VShader = reinterpret_cast<uint8_t *>(vertSource);
    rendererDesc.Materials.Forward.VShaderSize = vertSize;
    rendererDesc.Materials.Forward.FShader = reinterpret_cast<uint8_t *>(fragSource);
    rendererDesc.Materials.Forward.FShaderSize = fragSize;
    IB::initRenderer(&rendererDesc);

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

    IB::Vertex vertices[] =
        {
            // Front
            {{-1.0f, -1.0f, -1.0f, 0.0f},
             {0.0f, 0.0f, -1.0f, 0.0f}},
            {{1.0f, -1.0f, -1.0f, 1.0f},
             {0.0f, 0.0f, -1.0f, 0.0f}},
            {{1.0f, 1.0f, -1.0f, 1.0f},
             {0.0f, 0.0f, -1.0f, 1.0f}},
            {{-1.0f, 1.0f, -1.0f, 0.0f},
             {0.0f, 0.0f, -1.0f, 1.0f}},

            // Back
            {{-1.0f, -1.0f, 1.0f, 0.0f},
             {0.0f, 0.0f, 1.0f, 0.0f}},
            {{1.0f, -1.0f, 1.0f, 1.0f},
             {0.0f, 0.0f, 1.0f, 0.0f}},
            {{1.0f, 1.0f, 1.0f, 1.0f},
             {0.0f, 0.0f, 1.0f, 1.0f}},
            {{-1.0f, 1.0f, 1.0f, 0.0f},
             {0.0f, 0.0f, 1.0f, 1.0f}},

            // Left Corners
            {{-1.0f, -1.0f, -1.0f, 0.0f},
             {-1.0f, 0.0f, 0.0f, 0.0f}},
            {{-1.0f, 1.0f, -1.0f, 0.0f},
             {-1.0f, 0.0f, 0.0f, 1.0f}},
            {{-1.0f, -1.0f, 1.0f, 1.0f},
             {-1.0f, 0.0f, 0.0f, 0.0f}},
            {{-1.0f, 1.0f, 1.0f, 1.0f},
             {-1.0f, 0.0f, 0.0f, 1.0f}},

            // Right Corners
            {{1.0f, -1.0f, -1.0f, 0.0f},
             {1.0f, 0.0f, 0.0f, 0.0f}},
            {{1.0f, 1.0f, -1.0f, 0.0f},
             {1.0f, 0.0f, 0.0f, 1.0f}},
            {{1.0f, -1.0f, 1.0f, 1.0f},
             {1.0f, 0.0f, 0.0f, 0.0f}},
            {{1.0f, 1.0f, 1.0f, 1.0f},
             {1.0f, 0.0f, 0.0f, 1.0f}},
        };

    uint16_t indices[] =
        {
            0, 1, 2, 0, 2, 3,      // Front Face
            4, 7, 6, 4, 6, 5,      // Back Face
            8, 9, 10, 10, 9, 11,   // Left Face
            12, 14, 13, 14, 15, 13 // Right Face
        };

    IB::MeshDesc meshDesc = {};
    meshDesc.Vertices.Data = vertices;
    meshDesc.Vertices.Count = 16;
    meshDesc.Indices.Data = indices;
    meshDesc.Indices.Count = 24;

    IB::MeshHandle someMesh = IB::createMesh(&meshDesc);

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
