// ComputeFixedIndirectProducer/1: exact five-word DrawIndexedIndirectCommand.
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> Output;

[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain()
{
    Output[0] = 3u;
    Output[1] = 2u;
    Output[2] = 1u;
    Output[3] = 4294967294u;
    Output[4] = 1u;
}
