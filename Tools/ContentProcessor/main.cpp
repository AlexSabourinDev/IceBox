#define _CRT_SECURE_NO_WARNINGS
#include <IBEngine/Platform/IBPlatform.h>
#include <IBEngine/IBLogging.h>
#include <IBEngine/IBRendererFrontend.h>
#include <IBEngine/IBSerialization.h>
#include <IBEngine/IBAllocator.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

constexpr uint32_t toFourCC(char const *string)
{
    return (string[0] << 24) | (string[1] << 16) | (string[2] << 8) | string[3];
}

void processMesh(char const *rawPath, char const *compiledPath)
{
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(rawPath, aiProcess_Triangulate | aiProcess_FlipUVs);

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
            vertices[i].pos[0] = mesh->mVertices[i].x;
            vertices[i].pos[1] = mesh->mVertices[i].y;
            vertices[i].pos[2] = mesh->mVertices[i].z;
            vertices[i].pos[3] = mesh->mTextureCoords[0][i].x;

            vertices[i].normal[0] = mesh->mNormals[i].x;
            vertices[i].normal[1] = mesh->mNormals[i].y;
            vertices[i].normal[2] = mesh->mNormals[i].z;
            vertices[i].normal[3] = mesh->mTextureCoords[0][i].y;
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
    IB::FileStream fileStream{ file };
    toBinary(&fileStream, asset);
    flush(&fileStream);

    IB::deallocateArray(asset.Vertices);
    IB::deallocateArray(asset.Indices);
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

        uint32_t extension = 0;
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

            if (length - extensionIndex == 4)
            {
                extension = toFourCC(relativePath + extensionIndex);
            }
        }

        switch (extension)
        {
        case toFourCC(".obj"):
        case toFourCC(".fbx"):
        {
            char compiledPath[255];
            sprintf(compiledPath, "%s/%.*s.c.msh", compiledDirectory, extensionIndex, relativePath);
            processMesh(rawPath, compiledPath);
        }
        break;
        }
    }
}
