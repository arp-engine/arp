// ComputeFixedIndirectProducer/1: active signed/instance boundaries and unsafe
// literal producers. Unsafe fetch must reject cold output preparation.
[[vk::binding(3, 0)]] RWStructuredBuffer<uint> Output;

[shader("compute")]
[numthreads(1, 1, 1)]
void positiveBaseMain()
{
    Output[0] = 3u;
    Output[1] = 1u;
    Output[2] = 1u;
    Output[3] = 1u;
    Output[4] = 2u;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void zeroInstanceBaseMain()
{
    Output[0] = 3u;
    Output[1] = 1u;
    Output[2] = 1u;
    Output[3] = 4294967294u;
    Output[4] = 0u;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void outOfRangeIndexMain()
{
    Output[0] = 4u;
    Output[1] = 2u;
    Output[2] = 1u;
    Output[3] = 4294967294u;
    Output[4] = 1u;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void outOfRangeInstanceMain()
{
    Output[0] = 3u;
    Output[1] = 4294967295u;
    Output[2] = 1u;
    Output[3] = 4294967294u;
    Output[4] = 1u;
}
