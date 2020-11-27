#define _CRT_SECURE_NO_WARNINGS
#include <IBEngine/Platform/IBPlatform.h>
#include <IBEngine/IBLogging.h>
#include <IBEngine/IBRendererFrontend.h>
#include <IBEngine/IBSerialization.h>
#include <IBEngine/IBAllocator.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <Windows.h>
#include <wrl.h>
#include <dxc/dxcapi.h>

void processMesh(char const *rawPath, char const *compiledPath)
{
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(rawPath, aiProcess_Triangulate);

    if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || scene->mRootNode == nullptr)
    {
        IB_LOG(IB::LogLevel::Error, "Content Processor", importer.GetErrorString());
        return;
    }

    IB::MeshAsset asset = {};

    IB_ASSERT(scene->mNumMeshes == 1, "Content processor only supports single mesh scenes right now.");
    for (uint32_t i = 0; i < scene->mNumMeshes; i++)
    {
        aiMesh *mesh = scene->mMeshes[i];

        IB::Vertex *vertices = IB::allocateArray<IB::Vertex>(mesh->mNumVertices);
        for (unsigned int i = 0; i < mesh->mNumVertices; i++)
        {
            vertices[i].Pos[0] = mesh->mVertices[i].x;
            vertices[i].Pos[1] = mesh->mVertices[i].y;
            vertices[i].Pos[2] = mesh->mVertices[i].z;
            vertices[i].Pos[3] = mesh->mTextureCoords[0][i].x;

            vertices[i].Normal[0] = mesh->mNormals[i].x;
            vertices[i].Normal[1] = mesh->mNormals[i].y;
            vertices[i].Normal[2] = mesh->mNormals[i].z;
            vertices[i].Normal[3] = mesh->mTextureCoords[0][i].y;

            vertices[i].Color[0] = 1.0f;
            vertices[i].Color[1] = 1.0f;
            vertices[i].Color[2] = 1.0f;
            vertices[i].Color[3] = 1.0f;
        }

        uint16_t *indices = IB::allocateArray<uint16_t>(mesh->mNumFaces * 3);
        for (unsigned int i = 0; i < mesh->mNumFaces; i++)
        {
            indices[i * 3 + 0] = mesh->mFaces[i].mIndices[0];
            indices[i * 3 + 1] = mesh->mFaces[i].mIndices[1];
            indices[i * 3 + 2] = mesh->mFaces[i].mIndices[2];
        }

        asset.Vertices = vertices;
        asset.VertexCount = mesh->mNumVertices;
        asset.Indices = indices;
        asset.IndexCount = mesh->mNumFaces * 3;
    }

    IB::File file = IB::openFile(compiledPath, IB::OpenFileOptions::Overwrite | IB::OpenFileOptions::Write);
    IB::Serialization::FileStream fileStream{ file };
    toBinary(&fileStream, asset);
    flush(&fileStream);

    IB::deallocateArray(asset.Vertices);
    IB::deallocateArray(asset.Indices);
}

void processShader(char const *rawPath, char const *compiledPath)
{
    using namespace Microsoft::WRL;

    ComPtr<IDxcCompiler3> compiler;
    DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler));

    IB::File shaderFile = IB::openFile(rawPath, IB::OpenFileOptions::Read);
    void *shaderData = IB::mapFile(shaderFile);
    DxcBuffer shaderBuffer;
    shaderBuffer.Ptr = shaderData;
    shaderBuffer.Size = IB::fileSize(shaderFile);
    shaderBuffer.Encoding = DXC_CP_ACP;

    ComPtr<IDxcBlob> vertShader;
    ComPtr<IDxcBlob> fragShader;

    {
        wchar_t const* args[] =
        {
            L"-spirv",
            L"-T",
            L"vs_6_6",
            L"-E",
            L"vertexMain",
            L"-fspv-target-env=vulkan1.0",
            L"-WX",
            L"-O3",
        };

        ComPtr<IDxcResult> compileResult;
        compiler->Compile(&shaderBuffer, args, sizeof(args) / sizeof(args[0]), nullptr, IID_PPV_ARGS(&compileResult));

        ComPtr<IDxcBlobUtf16> shaderName;
        compileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&vertShader), &shaderName);

        ComPtr<IDxcBlobUtf8> errors = nullptr;
        compileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
        if (errors != nullptr && errors->GetStringLength() != 0)
        {
            printf("Warnings and Errors:\n%s\n", errors->GetStringPointer());
        }
    }

    {
        wchar_t const* args[] =
        {
            L"-spirv",
            L"-T",
            L"ps_6_6",
            L"-E",
            L"fragMain",
            L"-fspv-target-env=vulkan1.0",
            L"-WX",
            L"-O3",
        };

        ComPtr<IDxcResult> compileResult;
        compiler->Compile(&shaderBuffer, args, sizeof(args) / sizeof(args[0]), nullptr, IID_PPV_ARGS(&compileResult));

        ComPtr<IDxcBlobUtf16> shaderName;
        compileResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&fragShader), &shaderName);

        ComPtr<IDxcBlobUtf8> errors = nullptr;
        compileResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr);
        if (errors != nullptr && errors->GetStringLength() != 0)
        {
            printf("Warnings and Errors:\n%s\n", errors->GetStringPointer());
        }
    }

    IB::ShaderAsset shaderAsset;
    shaderAsset.VertexShader = reinterpret_cast<uint8_t*>(vertShader->GetBufferPointer());
    shaderAsset.VertexShaderSize = static_cast<uint32_t>(vertShader->GetBufferSize());
    shaderAsset.FragShader = reinterpret_cast<uint8_t*>(fragShader->GetBufferPointer());
    shaderAsset.FragShaderSize = static_cast<uint32_t>(fragShader->GetBufferSize());

    IB::File writeFile = IB::openFile(compiledPath, IB::OpenFileOptions::Overwrite | IB::OpenFileOptions::Write);
    IB::Serialization::FileStream fileStream{ writeFile };
    toBinary(&fileStream, shaderAsset);
    flush(&fileStream);

    IB::unmapFile(shaderFile);
    IB::closeFile(shaderFile);
}

int main(int argc, char const *argv[])
{
    if (argc > 2)
    {
        char const* rawDirectory = argv[1];
        char const* compiledDirectory = argv[2];
        IB_ASSERT(IB::isDirectory(rawDirectory), "Raw path is not a directory!");
        IB_ASSERT(IB::isDirectory(compiledDirectory), "Compiled path is not a directory!");

        char const *relativePath = argv[3];

        char rawPath[255];
        sprintf(rawPath, "%s/%s", rawDirectory, relativePath);
        IB_ASSERT(!IB::isDirectory(rawPath), "File is a directory. Support is not in yet.");

        uint32_t extensionIndex = 0;
        {
            uint32_t length = static_cast<uint32_t>(strlen(relativePath));
            extensionIndex = length;
            for (; extensionIndex > 0; extensionIndex--)
            {
                if (relativePath[extensionIndex - 1] == '.')
                {
                    break;
                }
            }

            IB_ASSERT(extensionIndex > 0, "Failed to find extension!");
            extensionIndex--;
        }

        if (strcmp(".obj", relativePath + extensionIndex) == 0 || strcmp(".fbx", relativePath + extensionIndex) == 0)
        {
            char compiledPath[255];
            sprintf(compiledPath, "%s/%.*s.c.msh", compiledDirectory, extensionIndex, relativePath);
            processMesh(rawPath, compiledPath);
        }
        else if (strcmp(".hlsl", relativePath + extensionIndex) == 0)
        {
            char compiledPath[255];
            sprintf(compiledPath, "%s/%.*s.c.hlsl", compiledDirectory, extensionIndex, relativePath);
            processShader(rawPath, compiledPath);
        }
    }
}
