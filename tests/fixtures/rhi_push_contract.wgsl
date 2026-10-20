struct RhiPushContractPushConstants_std430_0
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

@binding(0) @group(0) var<uniform> pc_0 : RhiPushContractPushConstants_std430_0;
struct VertexOutput_0
{
    @builtin(position) position_0 : vec4<f32>,
};

@vertex
fn vsMain(@builtin(vertex_index) vertexId_0 : u32) -> VertexOutput_0
{
    var output_0 : VertexOutput_0;
    output_0.position_0 = pc_0.data0_0;
    return output_0;
}

struct pixelOutput_0
{
    @location(0) output_1 : vec4<f32>,
};

@fragment
fn fsMain(@builtin(position) position_1 : vec4<f32>) -> pixelOutput_0
{
    var _S1 : pixelOutput_0 = pixelOutput_0( pc_0.data0_0 );
    return _S1;
}

var<workgroup> computeScratch_0 : vec4<f32>;

@compute
@workgroup_size(1, 1, 1)
fn csMain(@builtin(global_invocation_id) dispatchId_0 : vec3<u32>)
{
    computeScratch_0 = pc_0.data0_0;
    workgroupBarrier();
    return;
}

