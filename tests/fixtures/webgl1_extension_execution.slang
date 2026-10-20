[[vk::binding(0, 0)]] Texture2D<float4> DepthImage;
[[vk::binding(1, 0)]] SamplerState DepthSampler;
struct ExtensionConstants {
    float4 scale;
};
[[vk::binding(2, 0)]] ConstantBuffer<ExtensionConstants> constants;
struct Input {
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
    float2 instanceOffset : TEXCOORD1;
    float4 instanceColor : COLOR0;
};
struct Output {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};
[shader("vertex")] Output vertexMain(Input input) {
    Output output;
    output.position = float4(input.position + input.instanceOffset, constants.scale.z, 1.0);
    output.uv = input.uv;
    output.color = input.instanceColor;
    return output;
}
[shader("fragment")] float4 fragmentMain(Output input) : SV_Target0 {
    float depth = DepthImage.Sample(DepthSampler, input.uv).r;
    float derivative = abs(ddx(input.uv.x));
    return float4(input.color.rgb * depth * constants.scale.x * derivative * constants.scale.y, input.color.a);
}


