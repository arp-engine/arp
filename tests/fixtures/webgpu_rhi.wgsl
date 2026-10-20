struct PushConstants {
  data: array<u32, 16>,
};

@group(3) @binding(0) var<uniform> pc: PushConstants;

struct VertexOutput {
  @builtin(position) position: vec4<f32>,
  @location(0) color: vec4<f32>,
};

@vertex
fn vsMain(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
  var positions = array<vec2<f32>, 3>(
    vec2<f32>(-0.5, -0.5),
    vec2<f32>(0.5, -0.5),
    vec2<f32>(0.0, 0.5)
  );
  var output: VertexOutput;
  output.position = vec4<f32>(positions[vertexIndex], 0.0, 1.0);
  output.color = vec4<f32>(f32(pc.data[0]) * 0.0 + 1.0, 0.0, 0.0, 1.0);
  return output;
}

@fragment
fn fsMain(input: VertexOutput) -> @location(0) vec4<f32> {
  return input.color;
}

@compute @workgroup_size(1)
fn csMain() {
  let unused = pc.data[0];
}
