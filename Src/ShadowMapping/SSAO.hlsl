// UVs for 2 triangles of quad. Not a vertex attribute, but a static constant.
static const float2 gTexCoords[6] = {
    float2(0.0f, 1.0f),
    float2(0.0f, 0.0f),
    float2(1.0f, 0.0f),
    float2(0.0f, 1.0f),
    float2(1.0f, 0.0f),
    float2(1.0f, 1.0f)
};

struct VertexOut {
    float4 PosH : SV_POSITION;
    float3 PosV : POSITION;
	float2 TexC : TEXCOORD0;
};

cbuffer cbSSAO : register(b0) {
    float4x4 gInvProj;
};

// This vertex shader gets invoked without a vertex buffer and thus without vertex
// attributes (and no input layout). Invoked simply by a DrawInstanced(6, 1, 0, 0)
// with 6 vertices per instance (only 1 instance). Each of the vertices has an
// SV_VertexID id in [0,5], which is used to index into gTexCoords.
VertexOut VS(uint vid : SV_VertexID) {
    VertexOut vout;

    vout.TexC = gTexCoords[vid];

    // Map UV coordinate from [0,1]x[1,0] to NDC space [-1,1]x[-1,1] at the near plane.
    vout.PosH = float4(
        2.0f*vout.TexC.x - 1.0f,
        1 - 2.0f*vout.TexC.y,
        0.0f,
        1.0f
    );

    // From NDC to view space.
    float4 quadVertexInViewSpaceNearPlane = mul(vout.PosH, gInvProj);
    // Perspective divide.
    vout.PosV = quadVertexInViewSpaceNearPlane.xyz / quadVertexInViewSpaceNearPlane.w;

    // vout.PosV - eyeV = vout.PosV - (0, 0, 0, 1) gives us a vector from the origin
    // of view space to the corners of the projection window at the near plane in
    // view space. So in the pixel shader we'll have a vector from the eye to the near
    // plane for each pixel.

    return vout;
}