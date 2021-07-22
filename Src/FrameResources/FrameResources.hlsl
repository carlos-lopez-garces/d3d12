// Constant buffer with object-specific data.
cbuffer cbPerObject : register(b0) {
	// The composite view and projection transformation come in the render
	// pass buffer.
	float4x4 gWorld;
};

// Constant buffer with data that applies to all draw calls.
cbuffer cbPass : register(b1) {
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
};

struct VertexIn {
	float3 PosL : POSITION;
	float4 Color : COLOR;
};

struct VertexOut {
	// Vertex coordnate in homogeneous clip space.
	float4 PosH : SV_POSITION;
	float4 Color : COLOR;
};

VertexOut VS(VertexIn vin) {
	VertexOut vout;

	// Transform vertex to homogeneous clip space.
  float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
  vout.PosH = mul(posW, gViewProj);

	vout.Color = vin.Color;

	return vout;
}

float4 PS(VertexOut pin) : SV_Target{
	return pin.Color;
}