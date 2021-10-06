static float const Pi = 3.1415926535f;

struct Vertex
{
    float4 Pos;
    float4 Normal;
    float4 Color;
};

float2 unpackUV(Vertex v)
{
    return float2(v.Pos.w, v.Normal.w);
}

[[vk::binding(0, 0)]] StructuredBuffer<Vertex> MeshData;

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
    uint index = (PerDraw.VertexOffset + input.VertexID);
    row_major float4x4 M4 = float4x4(PerDraw.M, float4(0.0f, 0.0f, 0.0f, 1.0f));
    row_major float3x3 rotation = (float3x3)M4;
    output.NDCPos = mul(PerDraw.VP, mul(M4, float4(MeshData[index].Pos.xyz, 1.0f)));
    output.Normal = mul(MeshData[index].Normal.xyz, rotation);
    output.UV = unpackUV(MeshData[index]);
    output.Color = MeshData[index].Color;
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

float4 fromRGBA(uint tint)
{
    return float4(
        (float)((tint & 0xFF000000) >> 24) / 255.0f,
        (float)((tint & 0x00FF0000) >> 16) / 255.0f,
        (float)((tint & 0x0000FF00) >> 8) / 255.0f,
        (float)((tint & 0x000000FF) >> 0) / 255.0f);
}

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
