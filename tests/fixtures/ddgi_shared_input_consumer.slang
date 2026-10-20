[[vk::binding(0, 0)]] StructuredBuffer<float4> Input;
[[vk::binding(1, 0)]] RWStructuredBuffer<uint> Output;
[[vk::binding(2, 0)]] StructuredBuffer<uint> Influences;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    for (uint row = 0; row < 2; ++row) {
        uint4 words = asuint(Input[row]);
        for (uint column = 0; column < 4; ++column)
            Output[row * 4 + column] = words[column];
    }
    // Fixture: 2 x 2 x 2 probes, eight local-light indices per probe.
    for (uint index = 0; index < 64; ++index)
        Output[8 + index] = Influences[index];
}
