[[vk::binding(0, 0)]] Texture2D<float4> Image;
[[vk::binding(1, 0)]] SamplerState ImageSampler;

struct Input {
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct Output {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")] Output vertexMain(Input input) {
    Output output;
    output.position = float4(input.position, 0.5, 1.0);
    output.uv = input.uv;
    return output;
}[shader("fragment")] float4 fragmentMain(Output input)
    : SV_Target0
{
    float derivative = abs(ddx(input.uv.x)) + abs(ddy(input.uv.y));
    return Image.SampleLevel(ImageSampler, input.uv, 0.0)
        * float4(derivative, derivative, derivative, 1.0);
}
