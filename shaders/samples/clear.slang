struct SceneRuntimeDemoPushConstants
{
    float4 params;
};

[[vk::push_constant]]
SceneRuntimeDemoPushConstants gDemo;

struct VertexOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

float2 cubePosition(uint vertexId)
{
    uint v = vertexId % 18;
    float2 top = float2(-0.24, 0.54);
    float2 left = float2(-0.62, 0.24);
    float2 right = float2(0.12, 0.22);
    float2 center = float2(-0.24, -0.02);
    float2 leftBottom = float2(-0.62, -0.30);
    float2 rightBottom = float2(0.12, -0.32);
    float2 bottom = float2(-0.24, -0.58);

    if (v == 0 || v == 3) return top;
    if (v == 1) return right;
    if (v == 2 || v == 4 || v == 7 || v == 12 || v == 15) return center;
    if (v == 5 || v == 6 || v == 11) return left;
    if (v == 8 || v == 10 || v == 17) return bottom;
    if (v == 9) return leftBottom;
    if (v == 13 || v == 16) return rightBottom;
    return right;
}

float3 cubeColor(uint vertexId)
{
    uint face = (vertexId % 18) / 6;
    if (face == 0) return float3(0.96, 0.76, 0.30);
    if (face == 1) return float3(0.24, 0.58, 0.90);
    return float3(0.92, 0.34, 0.30);
}

float2 spritePosition(uint vertexId)
{
    uint v = vertexId % 6;
    float2 minPoint = float2(0.34, -0.62);
    float2 maxPoint = float2(0.74, -0.22);
    if (v == 0 || v == 3) return minPoint;
    if (v == 1) return float2(maxPoint.x, minPoint.y);
    if (v == 2 || v == 4) return maxPoint;
    if (v == 5) return float2(minPoint.x, maxPoint.y);
    return minPoint;
}

float3 spriteColor(uint vertexId)
{
    uint v = vertexId % 6;
    return v < 3 ? float3(0.20, 0.82, 0.90) : float3(0.10, 0.46, 0.78);
}

float2 uiPosition(uint vertexId)
{
    uint v = vertexId % 6;
    float2 minPoint = float2(-0.88, 0.62);
    float2 maxPoint = float2(-0.18, 0.78);
    if (v == 0 || v == 3) return minPoint;
    if (v == 1) return float2(maxPoint.x, minPoint.y);
    if (v == 2 || v == 4) return maxPoint;
    if (v == 5) return float2(minPoint.x, maxPoint.y);
    return minPoint;
}

float3 uiColor(uint vertexId)
{
    uint v = vertexId % 6;
    return v < 3 ? float3(0.96, 0.42, 0.78) : float3(0.60, 0.32, 0.92);
}

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    uint drawKind = uint(gDemo.params.x + 0.5);
    float2 position = drawKind == 0
        ? cubePosition(vertexId)
        : (drawKind == 1 ? spritePosition(vertexId) : uiPosition(vertexId));
    float3 color = drawKind == 0
        ? cubeColor(vertexId)
        : (drawKind == 1 ? spriteColor(vertexId) : uiColor(vertexId));

    VertexOutput output;
    output.position = float4(position, 0.0, 1.0);
    output.color = color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target
{
    return float4(input.color, 1.0);
}
