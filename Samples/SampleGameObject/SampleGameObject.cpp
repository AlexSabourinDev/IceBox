#define _CRT_SECURE_NO_WARNINGS
#include <IBEngine/IBMath.h>
#include <IBEngine/IBSerialization.h>
#include <IBEngine/IBJobs.h>
#include <IBEngine/IBRendererFrontend.h>
#include <stddef.h>
#include <string.h>
#include <Windows.h>

struct TransformProperty
{
    IB::Mat3x4 Local;
};

struct EntityID
{
    uint32_t Value;
};

uint32_t EntityTable[1024];

IB::Mat3x4 LocalTransforms[1024];
IB::Mat3x4 WorldTransforms[1024];
EntityID EntityMap[1024];
uint32_t ActiveTransforms = 0;

struct LoadContext
{
    IB::Serialization::MemoryStream Stream;
    uint64_t ParentAsset;
};

void loadAssetAsync(IB::Serialization::MemoryStream stream, uint32_t type, uint64_t parentAsset);
void loadAssetAsync(char const *assetPath, uint32_t type, uint64_t parentAsset);

void onMeshAssetLoad(LoadContext context)
{
    IB::MeshAsset mesh;
    fromBinary(&context.Stream, &mesh);
}

void onRendererPropertyLoad(LoadContext context)
{
    uint32_t stringSize;
    fromBinary(&context.Stream, &stringSize);

    char const* path = reinterpret_cast<char const*>(fromBinary(&context.Stream, stringSize));

    loadAssetAsync(path, 'MESH', context.ParentAsset);
}

void onTransformPropertyLoad(LoadContext context)
{
    TransformProperty asset = {};
    fromBinary(&context.Stream, &asset);

    LocalTransforms[ActiveTransforms] = asset.Local;
    WorldTransforms[ActiveTransforms] = asset.Local;
    EntityMap[ActiveTransforms] = {static_cast<uint32_t>(context.ParentAsset)};
    ActiveTransforms++;
}

void onEntityAssetLoad(LoadContext context)
{
    EntityID entity;
    fromBinary(&context.Stream, &entity);
    EntityTable[0] = entity.Value;

    uint32_t propertyCount;
    fromBinary(&context.Stream, &propertyCount);

    for (uint32_t i = 0; i < propertyCount; i++)
    {
        uint32_t type;
        fromBinary(&context.Stream, &type);
        uint32_t offset;
        fromBinary(&context.Stream, &offset);
        loadAssetAsync(context.Stream, type, entity.Value);
        advance(&context.Stream, offset);
    }
}

void loadAsset(IB::Serialization::MemoryStream stream, uint32_t type, uint64_t parentAsset)
{
    LoadContext context = { stream, parentAsset };

    switch (type)
    {
    case 'ENTT':
        onEntityAssetLoad(context);
        break;
    case 'TFRM':
        onTransformPropertyLoad(context);
        break;
    case 'RNDR':
        onRendererPropertyLoad(context);
        break;
    case 'MESH':
        onMeshAssetLoad(context);
        break;
    }
}

void loadAssetAsync(IB::Serialization::MemoryStream stream, uint32_t type, uint64_t parentAsset)
{
    IB::launchJob([stream, type, parentAsset]() {
        loadAsset(stream, type, parentAsset);
    });
}

void loadAssetAsync(char const *assetPath, uint32_t type, uint64_t parentAsset)
{
    IB::launchJob([assetPath, type, parentAsset]() {
        IB::File file = IB::openFile(assetPath, IB::OpenFileOptions::Read);
        void *memory = IB::mapFile(file);
        IB::Serialization::MemoryStream stream = {reinterpret_cast<uint8_t *>(memory)};

        loadAsset(stream, type, parentAsset);
    });
}

int main()
{
    IB::initJobSystem();

#pragma pack(1)
    struct EntityAssetFile
    {
        EntityID Entity;
        uint32_t PropertyCount = 2;
        uint32_t TFRM = 'TFRM';
        uint32_t NextOffset1 = sizeof(TransformProperty);
        TransformProperty Transform;
        uint32_t RNDR = 'RNDR';
        uint32_t NextOffset2 = sizeof(uint32_t) + 29;
        uint32_t StringSize = 29;
        char Path[29] = "../Assets/Compiled/Box.c.msh";
    };

    EntityAssetFile asset;
    asset.Entity = {5};
    asset.Transform = {{{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}}}};
    IB::Serialization::MemoryStream stream = {reinterpret_cast<uint8_t *>(&asset)};
    loadAssetAsync(stream, 'ENTT', 0);

    Sleep(100000);
    IB::killJobSystem();
}
