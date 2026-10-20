#include <metal_stdlib>
#include <metal_math>
#include <metal_texture>
using namespace metal;

#line 90 "core"
struct pixelOutput_0
{
    float4 output_0 [[color(0)]];
};


#line 1 "/Users/wanghao/Documents/arp/shaders/../tests/fixtures/rhi_pipeline_contract.slang"
struct FrameConstants_0
{
    float4 value_0;
};

struct RhiPipelinePushConstants_0
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


#line 22
struct KernelContext_0
{
    FrameConstants_0 constant* frameConstants_0;
    RhiPipelinePushConstants_0 constant* pc_0;
    texture2d<float, access::sample> materialTexture_0;
    uint threadgroup* computeScratch_0;
};


#line 39
[[fragment]] pixelOutput_0 fsMain(float4 position_0 [[position]], FrameConstants_0 constant* frameConstants_1 [[buffer(0)]], RhiPipelinePushConstants_0 constant* pc_1 [[buffer(1)]], texture2d<float, access::sample> materialTexture_1 [[texture(0)]])
{

#line 39
    thread KernelContext_0 kernelContext_0;

#line 39
    (&kernelContext_0)->frameConstants_0 = frameConstants_1;

#line 39
    (&kernelContext_0)->pc_0 = pc_1;

#line 39
    (&kernelContext_0)->materialTexture_0 = materialTexture_1;

#line 39
    threadgroup uint computeScratch_1;

#line 39
    (&kernelContext_0)->computeScratch_0 = &computeScratch_1;

    int3 _S1 = int3(int(0), int(0), int(0));

#line 41
    pixelOutput_0 _S2 = { ((materialTexture_1).read(vec<uint,2>(((_S1)).xy), uint(((_S1)).z))) + pc_1->data0_0 };

#line 41
    return _S2;
}




[[kernel]] void csMain(uint3 dispatchId_0 [[thread_position_in_grid]], FrameConstants_0 constant* frameConstants_2 [[buffer(0)]], RhiPipelinePushConstants_0 constant* pc_2 [[buffer(1)]], texture2d<float, access::sample> materialTexture_2 [[texture(0)]])
{

#line 47
    thread KernelContext_0 kernelContext_1;

#line 47
    (&kernelContext_1)->frameConstants_0 = frameConstants_2;

#line 47
    (&kernelContext_1)->pc_0 = pc_2;

#line 47
    (&kernelContext_1)->materialTexture_0 = materialTexture_2;

#line 47
    threadgroup uint computeScratch_2;

#line 47
    (&kernelContext_1)->computeScratch_0 = &computeScratch_2;

    computeScratch_2 = (as_type<uint>((pc_2->data0_0.x)));
    threadgroup_barrier(mem_flags::mem_threadgroup);
    return;
}


#line 51
struct vsMain_Result_0
{
    float4 position_1 [[position]];
};


#line 24
struct VertexOutput_0
{
    float4 position_2;
};


#line 24
[[vertex]] vsMain_Result_0 vsMain(uint vertexId_0 [[vertex_id]], FrameConstants_0 constant* frameConstants_3 [[buffer(0)]], RhiPipelinePushConstants_0 constant* pc_3 [[buffer(1)]], texture2d<float, access::sample> materialTexture_3 [[texture(0)]])
{

#line 24
    thread KernelContext_0 kernelContext_2;

#line 24
    (&kernelContext_2)->frameConstants_0 = frameConstants_3;

#line 24
    (&kernelContext_2)->pc_0 = pc_3;

#line 24
    (&kernelContext_2)->materialTexture_0 = materialTexture_3;

#line 24
    threadgroup uint computeScratch_3;

#line 24
    (&kernelContext_2)->computeScratch_0 = &computeScratch_3;

#line 32
    thread VertexOutput_0 output_1;
    (&output_1)->position_2 = frameConstants_3->value_0 + pc_3->data0_0;

#line 33
    thread vsMain_Result_0 _S3;

#line 33
    (&_S3)->position_1 = output_1.position_2;

#line 33
    return _S3;
}

