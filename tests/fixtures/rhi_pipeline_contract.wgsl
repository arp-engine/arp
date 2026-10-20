struct FrameConstants_std140_0
{
    @align(16) value_0 : vec4<f32>,
};

@binding(0) @group(0) var<uniform> frameConstants_0 : FrameConstants_std140_0;
struct RhiPipelinePushConstants_std430_0
{
    @align(16) data0_0 : vec4<f32>,
    @align(16) data1_0 : vec4<f32>,
    @align(16) data2_0 : vec4<f32>,
    @align(16) data3_0 : vec4<f32>,
    @align(16) data4_0 : vec4<f32>,
    @align(16) data5_0 : vec4<f32>,
    @align(16) data6_0 : vec4<f32>,
    @align(16) data7_0 : vec4<f32>,
};

@binding(0) @group(2) var<uniform> pc_0 : RhiPipelinePushConstants_std430_0;
@binding(0) @group(1) var materialTexture_0 : texture_2d<f32>;

struct VertexOutput_0
{
    @builtin(position) position_0 : vec4<f32>,
};

@vertex
fn vsMain(@builtin(vertex_index) vertexId_0 : u32) -> VertexOutput_0
{
    var output_0 : VertexOutput_0;
    output_0.position_0 = frameConstants_0.value_0 + pc_0.data0_0;
    return output_0;
}

struct pixelOutput_0
{
    @location(0) output_1 : vec4<f32>,
};

@fragment
fn fsMain(@builtin(position) position_1 : vec4<f32>) -> pixelOutput_0
{
    const _S1 : vec3<i32> = vec3<i32>(i32(0), i32(0), i32(0));
    var _S2 : pixelOutput_0 = pixelOutput_0( (textureLoad((materialTexture_0), ((_S1)).xy, ((_S1)).z)) + pc_0.data0_0 );
    return _S2;
}

var<workgroup> computeScratch_0 : u32;

@compute
@workgroup_size(1, 1, 1)
fn csMain(@builtin(global_invocation_id) dispatchId_0 : vec3<u32>)
{
    computeScratch_0 = (bitcast<u32>((pc_0.data0_0.x)));
    workgroupBarrier();
    return;
}

