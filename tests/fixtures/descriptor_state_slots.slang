// Test-only scalar buffer actor with two distinct logical descriptor sets.
[[vk::binding(0,0)]] StructuredBuffer<uint> Input;
[[vk::binding(0,1)]] RWStructuredBuffer<uint> Output;
[shader("compute")]
[numthreads(1,1,1)]
void computeMain() { Output[0] = Input[0] + 7u; }
