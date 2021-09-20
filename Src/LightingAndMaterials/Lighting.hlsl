#define MaxLights 16

// Corresponds to struct Light in d3dUtil.h.
struct Light {
  // TODO: what's the range of each component?
  float3 Strength;
  // Parameter of the linear falloff function; a distance from the light source.
  float FalloffStart;

  // For directional lights and spotlights.
  float3 Direction;
  // Parameter of the linear falloff function; a distance from the light source.
  float FalloffEnd;

  // For point and spotlights.
  float3 Position;
  // Exponent of the angular decay function of a spotlight's intensity.
  float SpotPower;
};

struct Material {
  // The fraction of radiance that gets reflected per color component.
  float4 DiffuseAlbedo;
  float3 FresnelR0;
  float Shininess;
};

// Linear falloff function. Approximates the inverse squared decay law of light
// attenuation. falloffStart and falloffEnd is a range of distance from the light
// source; the function maps the interval [falloffStart, falloffEnd] linearly to
// [0, 1], and d too, so long as falloffStart <= d <= falloffEnd; if d is outside
// this range, the function maps it to 0 or 1.
float Attenuation(float d, float falloffStart, float falloffEnd) {
  return saturate((falloffEnd - d)/(falloffEnd - falloffStart));
}

// Schlick approximation of the Fresnel reflectance equations. Computes the fraction
// of incident light arriving from the -L direction that the surface will reflect at
// the point with normal N.
// TODO: what's R0?
float SchlickFresnel(float R0, float3 N, float3 L) {
  return R0 + (1.0f - R0) * pow(1.0f - saturate(dot(N, L)), 5);
}

float3 BlinnPhong(float3 lightStrength, float3 L, float3 N, float3 E, Material mat) {
  // The larger m is, the smoother the surface is and larger the bias of H is towards N.
  const float m = mat.Shininess * 256.0f;

  // Halfway vector. Characterizes the orientation of a subset of the microfacets about
  // the normal.
  float3 H = normalize(E + L);

  // The microfacet distribution function rho(thetaH) = cos^m(H, N) determines the fraction
  // of microfacets about the normal that form a thetaH angle with the normal. m characterizes
  // the material's roughness; as m becomes larger, the orientation of a larger fraction of
  // microfacets will tend towards the direction of the normal, making the surface look smoother.
  float roughnessFactor = (m + 8.0f) * pow(max(dot(H, N), 0.0f), m) / 8.0f;

  float3 fresnelFactor = SchlickFresnel(mat.FresnelR0, H, L);

  // Cap the reflected radiance's value; we are doing low dynamic range rendering.
  float specularAlbedo = roughnessFactor * fresnelFactor;
  specularAlbedo /= (specularAlbedo + 1.0f);

  return (mat.DiffuseAlbedo.rgb + specularAlbedo) * lightStrength;
}