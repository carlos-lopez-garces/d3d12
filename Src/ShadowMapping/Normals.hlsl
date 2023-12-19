#include "Common.hlsl"

struct VertexIn {
    float3 PosL     : POSITION;
    float3 NormalL  : NORMAL;
    float2 TexC     : TEXCOORD;
    float3 TangentU : TANGENT;
};

struct VertexOut {
    float4 PosH     : SV_POSITION;
    float3 NormalW  : NORMAL;
    float3 TangentW : TANGENT;
    float2 TexC     : TEXCOORD;
};

VertexOut VS(VertexIn vin) {
    VertexOut vout = (VertexOut) 0.0f;

    MaterialData material = gMaterialData[gMaterialIndex];

    // gWorld is a 4x4 homogeneous matrix. Normals and tangents are
    // 3x1 and don't need translation.
    vout.NormalW = mul(vin.NormalL, (float3x3) gWorld);
    vout.TangentW = mul(vin.TangentU, (float3x3) gWorld);
    vout.PosH = mul(mul(float4(vin.PosL, 1.0f), gWorld), gViewProj);
    vout.TexC = mul(mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform), material.MatTransform).xy;

    return vout;
}

float4 PS(VertexOut pin) : SV_Target {
    MaterialData material = gMaterialData[gMaterialIndex];

    // Normal in view space.
    return float4(mul(normalize(pin.NormalW), (float3x3) gView), 0.0f);
}