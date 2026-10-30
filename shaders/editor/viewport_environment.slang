// EditorViewportEnvironment/2. Rays come from the editor camera's inverse VP.
// The depth rows are dot products with world position, not matrix declarations.
// grid: fine spacing, major spacing, fade distance, pass (0 sky / 1 grid).
// Sky colors and sun inputs come from the active scene SkyComponent.
struct EnvironmentPushConstants
{
    float4 camera;
    float4 rayOrigin;
    float4 rayHorizontal;
    float4 rayVertical;
    float4 depthNumerator;
    float4 depthDenominator;
    float4 grid;
    float4 zenith;
    float4 horizon;
    float4 sunDirection;
    float4 sunColor;
};
[[vk::push_constant]] EnvironmentPushConstants environment;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(float2 uv : POSITION0)
{
    VertexOutput output;
    output.position = float4(uv * 2 - 1, 1, 1);
    output.uv = uv;
    return output;
}

struct FragmentOutput
{
    float4 color : SV_Target;
    float depth : SV_Depth;
};

float gridLine(float2 world, float spacing, float2 footprint)
{
    const float2 distanceToLine = abs(frac(world / spacing + 0.5) - 0.5) * spacing;
    const float2 coverage = 1 - smoothstep(0.0, footprint * 1.35, distanceToLine);
    return max(coverage.x, coverage.y);
}

[shader("fragment")]
FragmentOutput fragmentMain(VertexOutput input)
{
    const float3 ray = environment.rayOrigin.xyz
        + environment.rayHorizontal.xyz * input.uv.x
        + environment.rayVertical.xyz * input.uv.y;
    const float3 direction = normalize(ray);
    FragmentOutput output;
    output.depth = 1;
    if (environment.grid.w < 0.5)
    {
        const float skyHeight = pow(saturate(direction.y), 0.45);
        float3 sky = lerp(environment.horizon.rgb, environment.zenith.rgb, skyHeight);
        const float3 belowHorizon = lerp(
            float3(0.12, 0.15, 0.19), environment.horizon.rgb, environment.horizon.w);
        sky = lerp(sky, belowHorizon, smoothstep(0.0, 0.35, -direction.y));
        const float sun = pow(saturate(dot(direction, normalize(environment.sunDirection.xyz))), 512);
        output.color = float4(sky + environment.sunColor.rgb * sun, 1);
    }
    else
    {
        // Derivatives are evaluated before divergent horizon/near-plane rejection.
        const float safeY = abs(ray.y) < 1e-6 ? (ray.y < 0 ? -1e-6 : 1e-6) : ray.y;
        const float t = -environment.camera.y / safeY;
        const float3 hit = environment.camera.xyz + ray * t;
        const float2 footprint = max((abs(ddx(hit.xz)) + abs(ddy(hit.xz))), float2(1e-5));
        const float fine = gridLine(hit.xz, environment.grid.x, footprint);
        const float major = gridLine(hit.xz, environment.grid.y, footprint);
        const float xAxis = 1 - smoothstep(0.0, footprint.y * 1.75, abs(hit.z));
        const float zAxis = 1 - smoothstep(0.0, footprint.x * 1.75, abs(hit.x));
        const float denominator = dot(environment.depthDenominator, float4(hit, 1));
        const float depth = dot(environment.depthNumerator, float4(hit, 1)) / denominator;
        if (abs(ray.y) < 1e-6 || t <= 0 || denominator <= 0 || depth < 0 || depth > 1)
            discard;
        const float fade = 1 - smoothstep(environment.grid.z * 0.4, environment.grid.z,
            length(hit - environment.camera.xyz));
        // Suppress subpixel fine grids to avoid moire at grazing angles.
        const float fineFade = 1 - smoothstep(environment.grid.x * 0.5, environment.grid.x * 2,
            max(footprint.x, footprint.y));
        float3 color = lerp(float3(0.46, 0.52, 0.57), float3(0.68, 0.73, 0.78), major);
        color = lerp(color, float3(0.9, 0.22, 0.19), xAxis);
        color = lerp(color, float3(0.22, 0.47, 0.95), zAxis);
        output.color = float4(color, max(max(fine * fineFade * 0.5, major * 0.7), max(xAxis, zAxis)) * fade);
        output.depth = depth;
    }
    return output;
}
