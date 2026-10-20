// HSV saturation preserves hue and value, unlike a luminance-based blend.
float3 leafTranslucentColor(float3 color, float saturation)
{
    float maximum = max(color.r, max(color.g, color.b));
    float minimum = min(color.r, min(color.g, color.b));
    float chroma = maximum - minimum;
    if (chroma <= 1e-8 || maximum <= 0.0) return color;
    float newChroma = min(chroma * saturation, maximum);
    return max(float3(0), maximum - (maximum - color) * (newChroma / chroma));
}

// Blender's added Translucent BSDF is Lambertian on the opposite hemisphere.
float3 leafTransmittedDiffuse(float3 color, float weight, float nDotL)
{
    return color * weight * max(-nDotL, 0.0);
}
