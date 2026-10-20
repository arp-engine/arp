// Test-only original runtime operation custody, no native length authority.
[[vk::binding(0,0)]] StructuredBuffer<float3> Input;
[[vk::binding(1,0)]] RWStructuredBuffer<uint> Output;
[shader("compute")] [numthreads(1,1,1)] void computeMain(){uint count,stride; Input.GetDimensions(count,stride); Output[0]=count; Output[1]=stride;}
