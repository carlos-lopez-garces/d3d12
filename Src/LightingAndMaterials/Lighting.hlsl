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

float3 BlinnPhong(float3 strength, float3 L, float3 N, float3 E, Material mat) {
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

  // Remap the reflected radiance's value to the range [0.0,1.0]; we are doing low dynamic range
  // rendering because our render target expects values in the range [0.0,1.0]. If we didn't do
  // this remapping, radiance values higher than 1.0 would get clamped.
  float specularAlbedo = roughnessFactor * fresnelFactor;
  specularAlbedo /= (specularAlbedo + 1.0f);

  return (mat.DiffuseAlbedo.rgb + specularAlbedo) * strength;
}

float3 ComputeDirectionalLight(Light light, Material mat, float3 N, float3 E) {
  float3 L = -light.Direction;

  // Light strength decays as the angle of incidence increases and according to
  // Lambert's cosine law.
  float3 strength = light.Strength * max(dot(L, N), 0.0f);

  return BlinnPhong(strength, L, N, E, mat);
}

float3 ComputePointLight(Light L, Material mat, float3 P, float3 N, float3 E) {
  float3 L = light.Position - P;

  // Distance from surface point to point light.
  float d = length(L);

  if (d > light.FalloffEnd) {
    // Light from this point light source doesn't reach the surface point.
    return 0.0f;
  }

  // Normalize.
  L /= d;

  // Light strength decays as the angle of incidence increases and according to
  // Lambert's cosine law.
  float3 strength = light.Strength * max(dot(L, N), 0.0f);

  // Light strength decays linearly with distance.
  strength *= Attenuation(d, light.FalloffStart, light.FalloffEnd);

  return BlinnPhong(strength, L, N, E, mat);
}

float3 ComputeSpotLight(Light light, Material mat, float3 P, float3 N, float3 E) {
  float3 L = light.Position - P;

  // Distance from surface point to point light.
  float d = length(L);

  if (d > light.FalloffEnd) {
    // Light from this point light source doesn't reach the surface point.
    return 0.0f;
  }

  // Normalize.
  L /= d;

  // Light strength decays as the angle of incidence increases and according to
  // Lambert's cosine law.
  float3 strength = light.Strength * max(dot(L, N), 0.0f);

  // Light strength decays linearly with distance.
  strength *= Attenuation(d, light.FalloffStart, light.FalloffEnd);

  // Light strength decays as the light direction L departs from the spot light
  // source cone's axis.
  float spotFactor = pow(max(dot(-L, light.Direction), 0.0f), light.SpotPower);
  strength *= spotFactor;

  return BlinnPhong(strength, L, N, E, mat);
}

float4 ComputeLighting(
  Light lights[MaxLights],
  Material mat,
  float3 P,
  float3 N,
  float3 E,
  float3 shadowFactor
) {
  float3 radiance = 0.0f;

  int i = 0;

#if (NUM_DIR_LIGHTS > 0)
  for (i = 0; i < NUM_DIR_LIGHTS; ++i) {
    radiance += shadowFactor[i] * ComputeDirectionalLight(lights[i], mat, N, E);
  }
#endif

#if (NUM_POINT_LIGHTS > 0)
  for (i = NUM_DIR_LIGHTS; i < NUM_DIR_LIGHTS+NUM_POINT_LIGHTS; ++i) {
    radiance += ComputePointLight(lights[i], mat, P, N, E);
  }
#endif

#if (NUM_SPOT_LIGHTS > 0)
  for (i = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; ++i) {
    radiance += ComputeSpotLight(lights[i], mat, P, N, E);
  }
#endif

  return float4(radiance, 0.0f);
}