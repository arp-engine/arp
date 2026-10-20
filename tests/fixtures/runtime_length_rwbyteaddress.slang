// Test-only original runtime operation custody, no native length authority.
[[vk::binding(0,0)]] RWByteAddressBuffer Input;
[[vk::binding(1,0)]] RWStructuredBuffer<uint> Output;
[shader("compute")] [numthreads(1,1,1)] void computeMain(){uint count; Input.GetDimensions(count); Output[0]=count;}
