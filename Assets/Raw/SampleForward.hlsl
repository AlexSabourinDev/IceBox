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

[[vk::binding(0, 0)]]
StructuredBuffer<Vertex> MeshData;

struct PerDrawBuffer
{
	row_major float4x4 VP;
	row_major float3x4 M;
	uint VertexOffset;
};

[[vk::push_constant]]
PerDrawBuffer PerDraw;

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
	output.NDCPos = mul(PerDraw.VP,mul(M4,float4(MeshData[index].Pos.xyz, 1.0f)));
	output.Normal = mul(MeshData[index].Normal.xyz,rotation);
	output.UV = unpackUV(MeshData[index]);
    output.Color = MeshData[index].Color;
	return output;
}

struct MaterialData
{
	float4 Tint;
	uint TextureIndex;
};

[[vk::binding(0, 1)]]
ConstantBuffer<MaterialData> Material;

[[vk::binding(1, 0)]]
SamplerState TextureSampler;
[[vk::binding(2, 0)]]
Texture2D Textures[];

float4 fragMain(V2F input) : SV_TARGET
{
	return Textures[Material.TextureIndex].Sample(TextureSampler, input.UV) * input.Color;
}
