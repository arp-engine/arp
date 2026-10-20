// ConformanceStorageMrtWorkload/1: complete pixel writes from original input element two.
[[vk::binding(0, 0)]] StructuredBuffer<float4> Input;
[[vk::binding(1, 0)]] RWTexture2D<float4> StorageOutput;
[shader("compute")]
[numthreads(1, 1, 1)]
void computeMain(uint3 thread : SV_DispatchThreadID)
{
    StorageOutput[thread.xy] = Input[2];
}
