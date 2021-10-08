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
    v.Pos = asfloat(buffer.Load4(offset));
    v.Normal = asfloat(buffer.Load4(offset + sizeof(float4)));
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

struct PerDrawBuffer
{
    row_major float4x4 VP;
    row_major float3x4 M;
    uint VertexOffset;
    uint MaterialIndex;
    uint LightIndex;
};

[[vk::push_constant]] PerDrawBuffer PerDraw;

struct VertexInput
{
    uint VertexID : SV_VertexID;
};

struct V2F
{
    float4 NDCPos : SV_POSITION;
    [[vk::location(0)]] float3 Normal : NORMAL0;
    [[vk::location(1)]] float2 UV : TEXCOORD0;
    [[vk::location(2)]] float4 Color : COLOR0;
};

V2F vertexMain(VertexInput input)
{
    V2F output = (V2F)0;
    row_major float4x4 M4 = float4x4(PerDraw.M, float4(0.0f, 0.0f, 0.0f, 1.0f));
    row_major float3x3 rotation = (float3x3)M4;

    Vertex vertex = loadVertex(MeshData, PerDraw.VertexOffset + sizeof(Vertex) * input.VertexID);
    output.NDCPos = mul(PerDraw.VP, mul(M4, float4(vertex.Pos.xyz, 1.0f)));
    output.Normal = mul(vertex.Normal.xyz, rotation);
    output.UV = unpackUV(vertex);
    output.Color = unpackColor(vertex);
    return output;
}

struct Material
{
    uint Tint;
    uint TextureIndex;
};

struct DirectionalLight
{
    float4 DirectionAndRadiance; // Radiance is our 4th element
    uint Tint;
};

float unpackRadiance(DirectionalLight light)
{
    return light.DirectionAndRadiance.w;
}

float3 unpackDirection(DirectionalLight light)
{
    return light.DirectionAndRadiance.xyz;
}

[[vk::binding(1, 0)]] SamplerState TextureSampler;
[[vk::binding(2, 0)]] Texture2D Textures[];
[[vk::binding(3, 0)]] StructuredBuffer<Material> Materials;
[[vk::binding(4, 0)]] StructuredBuffer<DirectionalLight> Lights;

float4 fragMain(V2F input) : SV_TARGET
{
    Material material = Materials[PerDraw.MaterialIndex];
    float4 tint = float4(fromRGBA(material.Tint).rgb, 1.0f);

    float4 surfaceColor = Textures[material.TextureIndex].Sample(TextureSampler, input.UV) * input.Color * tint;

    [branch]
    if (PerDraw.LightIndex < 0xFFFFFFFF)
    {
        DirectionalLight light = Lights[PerDraw.LightIndex];
        float3 albedo = surfaceColor.rgb;
        float3 lambertianBRDF = albedo / Pi;
        float3 lightRadiance = fromRGBA(light.Tint).rgb * unpackRadiance(light);

        surfaceColor.rgb = lambertianBRDF * saturate(dot(normalize(input.Normal), -unpackDirection(light))) * lightRadiance;
    }

    return surfaceColor;
}
