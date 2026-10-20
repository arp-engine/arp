#include <common/subpass_input.slang>

struct ProbeOutput
{
    float4 position : SV_Position;
};

SUBPASS_INPUT_COLOR(SceneInput, 4);

[shader("vertex")]
ProbeOutput vsMain(uint vertexId : SV_VertexID)
{
    ProbeOutput output;
    output.position = float4(0.0f, 0.0f, 0.0f, 1.0f);
    return output;
}

[shader("fragment")]
float4 fsMain() : SV_Target0
{
    return subpassLoadColor(SceneInput);
}
