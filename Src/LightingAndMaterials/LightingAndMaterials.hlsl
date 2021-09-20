#ifndef NUM_DIR_LIGHTS
#define NUM_DIR_LIGHTS 1
#endif

#ifndef NUM_POINT_LIGHTS
#define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
#define NUM_SPOT_LIGHTS 0
#endif

#include "Lighting.hlsl"

cbuffer cbPerObject : register(b0) {
  float4x4 gWorld;
};

cbuffer cbPass : register(b2) {
  float4x4 gView;
  float4x4 gInvView;
  float4x4 gProj;
  float4x4 gInvProj;
  float4x4 gViewProj;
  float4x4 gInvViewProj;
  float3 gEyePosW;
  float cbPerObjectPad1;
  float2 gRenderTargetSize;
  float2 gInvRenderTargetSize;
  float gNearZ;
  float gFarZ;
  float gTotalTime;
  float gDeltaTime;
  float4 gAmbientLight;

  // Indices [0, NUM_DIR_LIGHTS) are directional lights;
  // indices [NUM_DIR_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHTS) are point lights;
  // indices [NUM_DIR_LIGHTS+NUM_POINT_LIGHTS, NUM_DIR_LIGHTS+NUM_POINT_LIGHT+NUM_SPOT_LIGHTS)
  // are spot lights for a maximum of MaxLights per object.
  Light gLights[MaxLights];
};

struct VertexIn {
  // L stands for local.
  float3 PosL : POSITION;
  float3 NormalL : NORMAL;
};

struct VertexOut {
  // H stands for homogeneous clip space.
  float4 PosH : SV_POSITION;
  // W stands for world.
  float3 PosW : POSITION;
  float3 NormalW : NORMAL;
};

VertexOut VS(VertexIn vin) {
  VertexOut vout = (VertexOut)0.0f;

  // Transform position local coordinate to world coordinate.
  float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
  vout.PosW = posW.xyz;

  // Transform local normal to world space.
  // TODO: normals are usually transformed using the inverse transpose of the
  // world matrix, unless the world matrix is orthogonal.
  vout.NormalW = mul(vin.NormalL, (float3x3) gWorld);

  // Transform world space position to homogeneous clip space.
  vout.PosH = mult(posW, gViewProj);

  return vout;
}