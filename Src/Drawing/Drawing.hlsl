cbuffer cbPerObject : register(b0)
{
	float4x4 gWorldViewProj;
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

	// The homogeneous coordinate of a point has a 1 in the 4th dimension.
	vout.PosH = mul(float4(vin.PosL, 1.0f), gWorldViewProj);

	vout.Color = vin.Color;

	return vout;
}

float4 PS(VertexOut pin) : SV_Target{
	return pin.Color;
}