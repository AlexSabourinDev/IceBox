static float const Pi = 3.1415926535f;

float4 fromRGBA(uint tint)
{
    return float4(
        (float)((tint & 0xFF000000) >> 24) / 255.0f,
        (float)((tint & 0x00FF0000) >> 16) / 255.0f,
        (float)((tint & 0x0000FF00) >> 8) / 255.0f,
        (float)((tint & 0x000000FF) >> 0) / 255.0f);
}

struct Vertex
{
    float4 Pos;
    float4 Normal;
};

Vertex loadVertex(ByteAddressBuffer buffer, uint offset)
{
    Vertex v;
    v.Pos = buffer.Load<float4>(offset);
    v.Normal = buffer.Load<float4>(offset + sizeof(float4));
    return v;
}

float2 unpackUV(Vertex v)
{
    uint packedUV = asuint(v.Pos.w);
    uint uvX = packedUV & 0xFFFF;
    uint uvY = packedUV >> 16;
    return float2(f16tof32(uvX), f16tof32(uvY));
}

float4 unpackColor(Vertex v)
{
    uint packedColor = asuint(v.Normal.w);
    return fromRGBA(packedColor);
}

[[vk::binding(0, 0)]] ByteAddressBuffer MeshData;

struct PerFrameData
{
    row_major float4x4 WorldToProjection;
};

static uint const PerFrameDataSize = (sizeof(float4) * 4);
PerFrameData loadPerFrameData(ByteAddressBuffer buffer, uint frameOffset)
{
    PerFrameData pf;
    pf.WorldToProjection = float4x4(
        buffer.Load<float4>(frameOffset + sizeof(float4) * 0),
        buffer.Load<float4>(frameOffset + sizeof(float4) * 1),
        buffer.Load<float4>(frameOffset + sizeof(float4) * 2),
        buffer.Load<float4>(frameOffset + sizeof(float4) * 3));
    return pf;
}

struct PerInstanceData
{
    row_major float3x4 LocalToWorld;
    uint VertexOffset;
    uint MaterialIndex;
};

static uint const PerInstanceDataSize = (sizeof(float4) * 3 + sizeof(uint) * 2);
PerInstanceData loadPerInstanceData(ByteAddressBuffer buffer, uint instanceDataOffset, uint instanceIndex)
{
    uint instanceOffset = instanceDataOffset + PerInstanceDataSize * instanceIndex;

    PerInstanceData pd;
    pd.LocalToWorld = float3x4(
        buffer.Load<float4>(instanceOffset + sizeof(float4) * 0),
        buffer.Load<float4>(instanceOffset + sizeof(float4) * 1),
        buffer.Load<float4>(instanceOffset + sizeof(float4) * 2));
    pd.VertexOffset = buffer.Load(instanceOffset + sizeof(float4) * 3);
    pd.MaterialIndex = buffer.Load(instanceOffset + sizeof(float4) * 3 + sizeof(uint));
    return pd;
}

[[vk::binding(5, 0)]] ByteAddressBuffer PerFrameBuffer;

struct BatchData
{
    uint InstanceOffset;
    uint FrameOffset;
    uint InstanceDataOffsetAndFlags;
};

uint unpackInstanceDataOffset(BatchData batchData)
{
    return batchData.InstanceDataOffsetAndFlags & 0x7FFFFFFF;
}

uint isLit(BatchData batchData)
{
    return (batchData.InstanceDataOffsetAndFlags & 0x80000000) != 0;
}

[[vk::push_constant]] BatchData Batch;


struct VertexInput
{
    uint VertexID : SV_VertexID;
    uint InstanceOffset : SV_InstanceID;
    [[vk::builtin("BaseInstance")]] uint BaseInstance : INSTANCE_ID;
};

struct V2F
{
    float4 NDCPos : SV_POSITION;
    [[vk::location(0)]] float3 Normal : VARYING0;
    [[vk::location(1)]] float2 UV : VARYING1;
    [[vk::location(2)]] float4 Color : VARYING2;
    [[vk::location(3)]] uint InstanceIndex : VARYING3;
};

V2F vertexMain(VertexInput input)
{
    uint instanceIndex = input.BaseInstance + input.InstanceOffset;
    PerFrameData perFrame = loadPerFrameData(PerFrameBuffer, Batch.FrameOffset);
    PerInstanceData perInstance = loadPerInstanceData(PerFrameBuffer, unpackInstanceDataOffset(Batch), Batch.InstanceOffset + instanceIndex);

    V2F output = (V2F)0;
    row_major float4x4 localToWorld = float4x4(perInstance.LocalToWorld, float4(0.0f, 0.0f, 0.0f, 1.0f));
    row_major float3x3 rotation = (float3x3)localToWorld;

    Vertex vertex = loadVertex(MeshData, perInstance.VertexOffset + sizeof(Vertex) * input.VertexID);
    output.NDCPos = mul(perFrame.WorldToProjection, mul(localToWorld, float4(vertex.Pos.xyz, 1.0f)));
    output.Normal = mul(vertex.Normal.xyz, rotation);
    output.UV = unpackUV(vertex);
    output.Color = unpackColor(vertex);
    output.InstanceIndex = instanceIndex;
    return output;
}

struct Material
{
    uint Tint;
    uint TextureIndex;
};

struct DirectionalLight
{
    float Radiance;
    uint Tint;
};

struct DirectionalLightInstance
{
    float3 Direction;
    DirectionalLight LightData;
};

[[vk::binding(1, 0)]] SamplerState TextureSampler;
[[vk::binding(2, 0)]] Texture2D Textures[];
[[vk::binding(3, 0)]] StructuredBuffer<Material> Materials;
[[vk::binding(4, 0)]] StructuredBuffer<DirectionalLight> DirectionalLights;

static uint const DirectionalLightInstanceSize = sizeof(float4);
DirectionalLightInstance loadLightInstanceData(ByteAddressBuffer buffer, uint lightDataOffset, uint instanceIndex)
{
    uint instanceOffset = lightDataOffset + DirectionalLightInstanceSize * instanceIndex;

    float4 directionalLightInstanceData = buffer.Load<float4>(instanceOffset);
    uint lightIndex = asuint(directionalLightInstanceData.w);

    DirectionalLightInstance lightInstance;
    lightInstance.Direction = directionalLightInstanceData.xyz;
    lightInstance.LightData = DirectionalLights[lightIndex];

    return lightInstance;
}

float4 fragMain(V2F input) : SV_TARGET
{
    PerFrameData perFrame = loadPerFrameData(PerFrameBuffer, Batch.FrameOffset);
    PerInstanceData perInstance = loadPerInstanceData(PerFrameBuffer, unpackInstanceDataOffset(Batch), Batch.InstanceOffset + input.InstanceIndex);

    Material material = Materials[perInstance.MaterialIndex];
    float4 tint = float4(fromRGBA(material.Tint).rgb, 1.0f);

    float4 surfaceColor = Textures[material.TextureIndex].Sample(TextureSampler, input.UV) * input.Color * tint;

    if (isLit(Batch))
    {
        uint lightDataOffset = Batch.FrameOffset + PerFrameDataSize;
        uint directionalLightCount = PerFrameBuffer.Load<uint>(lightDataOffset);
        lightDataOffset += sizeof(uint);
        for (uint i = 0; i < directionalLightCount; i++)
        {
            DirectionalLightInstance lightInstance = loadLightInstanceData(PerFrameBuffer, lightDataOffset, i);

            DirectionalLight light = lightInstance.LightData;
            float3 albedo = surfaceColor.rgb;
            float3 lambertianBRDF = albedo / Pi;
            float3 lightRadiance = fromRGBA(light.Tint).rgb * light.Radiance;

            surfaceColor.rgb = lambertianBRDF * saturate(dot(normalize(input.Normal), -lightInstance.Direction)) * lightRadiance;
        }
    }

    return surfaceColor;
}
