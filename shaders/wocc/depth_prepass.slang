// WoCC Depth Pre-Pass Shader
//
// Renders scene geometry into the depth buffer using the camera view-projection
// matrix. The fragment shader is empty (no color output) - the only output is
// depth. This depth is then available to the scene pass as an input attachment
// (Vulkan SubpassInput / Metal framebuffer fetch) enabling tile-local depth
// reads without a round-trip to main memory on TBDR GPUs.
//
// On the degraded WebGPU path, the FrameGraph compiler splits the subpass group
// into separate render passes; the scene pass reads depth as a sampled texture
// instead, so the pre-pass's stored depth is consumed correctly.
//
// The push constant reuses the PBR scene's WoccPbrPushConstants struct so the
// same per-frame payload can be passed to both passes (only viewProjection is
// read here). See pbr_scene.slang for the struct definition.

struct WoccDepthPrepassPushConstants
{
    float4x4 viewProjection;
    float4 cameraPositionExposure;
    float4 baseColorAlpha;
    float4 pbrParams;
    float4 pbrExtraParams;
    float4 environmentParams;
    float4 lightDirection;
    float4 lightColor;
    float4 shadowParams;
    float4x4 lightViewProjection;
};

[[vk::push_constant]]
WoccDepthPrepassPushConstants gDepthPrepass;

struct VertexInput
{
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT0;
};

struct InstanceVertexInput
{
    float3 position   : POSITION0;
    float3 normal     : NORMAL0;
    float2 uv         : TEXCOORD0;
    float4 tangent    : TANGENT0;
    float3 windWeight : COLOR0;  // present in the vertex buffer but unused here
};

struct InstanceDataInput
{
    float4 positionScale : POSITION1;  // xyz = world pos, w = uniform scale
    float4 yawPhaseIdLod : POSITION2;  // x = yaw, y = phase, z = id, w = lod
    float4 extraTransform : POSITION3; // x = pitchX, y = rollZ, z = scaleY, w = burialY
    // P0 (Track C): packed metadata (species/age/geometryLod/shadowLod/state).
    // Unused by the depth pre-pass today; declared so the instance-rate vertex
    // layout matches pbr_scene.slang across all three instanced entry points
    // (the pipeline binds one shared VertexBufferLayoutDesc). See
    // foliage_instance_batch.h InstanceDataGpu::speciesLodState for bit layout.
    float4 speciesLodState : POSITION4;
};

struct VertexOutput
{
    float4 position : SV_Position;
    // P0 (Track C): Forward packed instance metadata. The depth pre-pass does
    // not consume these, but declaring the output and reading the input is
    // what prevents the Slang compiler from optimizing away the
    // speciesLodState vertex input (location 8). Without this read, the SPV
    // input list would drop location 8 while the C++ VertexBufferLayoutDesc
    // (shared across all three instanced pipelines via makeInstanceDataLayout)
    // still declares it, tripping Vulkan validation. The rasterizer discards
    // unused varyings on a depth-only pipeline, so this is effectively free.
    nointerpolation uint4 instanceMetadata : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(gDepthPrepass.viewProjection, float4(input.position, 1.0));
    // P0 (Track C): non-instanced path has no speciesLodState; zero the
    // metadata varying so the struct is fully initialized (uninitialized
    // varyings are UB in SPIR-V and produce garbage on some drivers).
    output.instanceMetadata = uint4(0u, 0u, 0u, 0u);
    return output;
}

// Instanced depth pre-pass vertex shader. Replicates the instance transform
// from pbr_scene.slang's vertexMainInstanced (scale -> yaw -> tilt ->
// translate + burial) so the depth pre-pass covers instanced trees/rocks at
// the correct world positions. Wind deformation is skipped: the depth pre-pass
// only needs an approximate silhouette, and skipping wind avoids binding the
// wind UBO + reading COLOR0 wind weights in this pipeline.
[shader("vertex")]
VertexOutput vertexMainInstanced(InstanceVertexInput input,
                                 InstanceDataInput instanceData)
{
    // Apply instance transform: scale -> Y-rotate -> tilt -> translate.
    float scale = instanceData.positionScale.w;
    float scaleY = instanceData.extraTransform.z > 0.0
        ? instanceData.extraTransform.z
        : scale;
    float3 scaledPos;
    scaledPos.x = input.position.x * scale;
    scaledPos.y = input.position.y * scaleY;
    scaledPos.z = input.position.z * scale;

    // Yaw rotation (Y axis).
    float yaw = instanceData.yawPhaseIdLod.x;
    float cy = cos(yaw);
    float sy = sin(yaw);
    float3 yawedPos;
    yawedPos.x = scaledPos.x * cy + scaledPos.z * sy;
    yawedPos.y = scaledPos.y;
    yawedPos.z = -scaledPos.x * sy + scaledPos.z * cy;

    // Tilt rotation (pitch around X, roll around Z) - aligns rocks to slope.
    float pitchX = instanceData.extraTransform.x;
    float rollZ = instanceData.extraTransform.y;
    float3 tiltedPos = yawedPos;
    if (abs(pitchX) > 0.001 || abs(rollZ) > 0.001) {
        float cp = cos(pitchX);
        float sp = sin(pitchX);
        float py = tiltedPos.y * cp - tiltedPos.z * sp;
        float pz = tiltedPos.y * sp + tiltedPos.z * cp;
        tiltedPos.y = py;
        tiltedPos.z = pz;
        float cr = cos(rollZ);
        float sr = sin(rollZ);
        float rx = tiltedPos.x * cr - tiltedPos.y * sr;
        float ry = tiltedPos.x * sr + tiltedPos.y * cr;
        tiltedPos.x = rx;
        tiltedPos.y = ry;
    }

    // Translate to world position + burial offset (sink into ground).
    float3 worldPos = tiltedPos + instanceData.positionScale.xyz;
    worldPos.y += instanceData.extraTransform.w;  // burialY

    VertexOutput output;
    output.position = mul(gDepthPrepass.viewProjection, float4(worldPos, 1.0));
    // P0 (Track C): Read speciesLodState so Slang does not optimize away the
    // location-8 vertex input (the C++ VertexBufferLayoutDesc declares it for
    // all three instanced pipelines). See pbr_scene.slang decodeInstanceMetadata
    // for the bit layout; the depth pre-pass does not consume the values.
    output.instanceMetadata = asuint(instanceData.speciesLodState);
    return output;
}

[shader("fragment")]
void fragmentMain(VertexOutput input)
{
    // Depth-only: the rasterizer writes depth automatically. No color output.
}
