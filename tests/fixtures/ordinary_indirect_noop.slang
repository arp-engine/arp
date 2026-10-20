// ComputeFixedIndirectProducer/1 no-op controls, with hostile unused fetch words.
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> Output;

[shader("compute")]
[numthreads(1, 1, 1)]
void noopIndexMain()
{
    Output[0] = 0u;
    Output[1] = 4294967295u;
    Output[2] = 4294967295u;
    Output[3] = 2147483648u;
    Output[4] = 4294967295u;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void noopInstanceMain()
{
    Output[0] = 4294967295u;
    Output[1] = 0u;
    Output[2] = 4294967295u;
    Output[3] = 2147483648u;
    Output[4] = 4294967295u;
}
