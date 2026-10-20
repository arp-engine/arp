struct ProbeOutput
{
    float4 position : SV_Position;
};

[[vk::binding(0, 0)]] Texture2D<float4> SceneInput;

float4 arpSubpassLoadColor(Texture2D<float4> inputTexture)
{
    return inputTexture.Load(int3(0, 0, 0));
}

[shader("vertex")]
ProbeOutput vsMain(uint vertexId : SV_VertexID)
{
    ProbeOutput output;
    output.position = float4(0.0f, 0.0f, 0.0f, 1.0f);
    return output;
}

[shader("fragment")]
float4 fsMain(ProbeOutput input) : SV_Target0
{
    return arpSubpassLoadColor(SceneInput);
}
