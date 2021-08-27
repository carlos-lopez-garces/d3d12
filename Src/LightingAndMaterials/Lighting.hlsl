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