float CalculateCylinderShaftBrightness(
    float3 pobject,
    float2 pixelCoord,
    float3 cameraPosition,
    float rx2,
    float ry2,
    float rx2ry2
) {
    float3 vdir = cameraPosition − pobject;
    float2 v2 = vdir.xy * vdir.xy;
    float2 p2 = pobject.xy * pobject.xy;
    // Calculate quadratic coefficients.
    float a = ry2 * v2.x + rx2 * v2.y;
    float b = −ry2 * pobject.x * vdir.x − rx2 * pobject.y * vdir.y;
    float c = ry2 * p2.x + rx2 * p2.y − rx2ry2;
    float m = sqrt(max(b * b − a * c, 0.0));
    // Calculate limits and integrate.
    float t1 = max((b − m) / a, 0.0);
    float t2 = max((b + m) / a, 0.0);
    return (CalculateShaftBrightness(pobject.z, vdir, pixelCoord, t1, t2));
}