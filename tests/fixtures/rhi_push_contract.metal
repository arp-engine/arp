#include <metal_stdlib>
#include <metal_math>
#include <metal_texture>
using namespace metal;

#line 16 "/Users/wanghao/Documents/arp/shaders/../tests/fixtures/rhi_push_contract.slang"
struct pixelOutput_0
{
    float4 output_0 [[color(0)]];
};


#line 1
struct RhiPushContractPushConstants_0
{
    float4 data0_0;
    float4 data1_0;
    float4 data2_0;
    float4 data3_0;
    float4 data4_0;
    float4 data5_0;
    float4 data6_0;
    float4 data7_0;
};


struct KernelContext_0
{
    RhiPushContractPushConstants_0 constant* pc_0;
    float4 threadgroup* computeScratch_0;
};


#line 30
[[fragment]] pixelOutput_0 fsMain(float4 position_0 [[position]], RhiPushContractPushConstants_0 constant* pc_1 [[buffer(0)]])
{

#line 30
    thread KernelContext_0 kernelContext_0;

#line 30
    (&kernelContext_0)->pc_0 = pc_1;

#line 30
    threadgroup float4 computeScratch_1;

#line 30
    (&kernelContext_0)->computeScratch_0 = &computeScratch_1;

#line 30
    pixelOutput_0 _S1 = { pc_1->data0_0 };

    return _S1;
}



[[kernel]] void csMain(uint3 dispatchId_0 [[thread_position_in_grid]], RhiPushContractPushConstants_0 constant* pc_2 [[buffer(0)]])
{

#line 37
    thread KernelContext_0 kernelContext_1;

#line 37
    (&kernelContext_1)->pc_0 = pc_2;

#line 37
    threadgroup float4 computeScratch_2;

#line 37
    (&kernelContext_1)->computeScratch_0 = &computeScratch_2;

    computeScratch_2 = pc_2->data0_0;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    return;
}


#line 41
struct vsMain_Result_0
{
    float4 position_1 [[position]];
};


#line 16
struct VertexOutput_0
{
    float4 position_2;
};


#line 16
[[vertex]] vsMain_Result_0 vsMain(uint vertexId_0 [[vertex_id]], RhiPushContractPushConstants_0 constant* pc_3 [[buffer(0)]])
{

#line 16
    thread KernelContext_0 kernelContext_2;

#line 16
    (&kernelContext_2)->pc_0 = pc_3;

#line 16
    threadgroup float4 computeScratch_3;

#line 16
    (&kernelContext_2)->computeScratch_0 = &computeScratch_3;

#line 24
    thread VertexOutput_0 output_1;
    (&output_1)->position_2 = pc_3->data0_0;

#line 25
    thread vsMain_Result_0 _S2;

#line 25
    (&_S2)->position_1 = output_1.position_2;

#line 25
    return _S2;
}

