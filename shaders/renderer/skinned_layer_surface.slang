#include "../core/skinning_geometry.slang"

// LayeredSkinnedSurface/3. Layer ordering and blend/depth/cull state belong
// to the caller. All matrices follow ARP's default Slang matrix ABI.
static const uint kMaxSurfaceJoints = 256;

struct SurfaceSkinningPalette
{
    float4x4 skinMatrices[kMaxSurfaceJoints];
    float4 worldTranslationAndScale;
    float4 worldRotation;
    uint jointCount;
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::binding(0, 0)]] ConstantBuffer<SurfaceSkinningPalette> gSkinning;
[[vk::binding(0, 1)]] Texture2D<float4> gLayerTexture;
[[vk::binding(1, 1)]] SamplerState gLayerSampler;

// surfaceParameters = cutoff, material flags/UV palette index, ambient R, ambient G.
// Flags: bit 0 unshaded, bit 1 sphere map, bit 2 UV animation;
// bits 8+ select a matrix beyond the skin joints in the same bounded palette.
// lightDirectionAndAmbient = beam direction XYZ, ambient B.
// lightColorAndIntensity = diffuse RGB, intensity multiplier.
struct SurfacePushConstants
{
    float4x4 viewProjection;
    float4 tintAndAlpha;
    float4 surfaceParameters;
    float4 lightDirectionAndAmbient;
    float4 lightColorAndIntensity;
};

[[vk::push_constant]] SurfacePushConstants gSurface;

struct VertexInput
{
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    float4 joints : TEXCOORD1;
    float4 weights : TEXCOORD2;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 normal : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

void assignSurfaceLayer(inout VertexOutput output, VertexInput input) {}
float4 sampleSurfaceLayer(VertexOutput input) { return gLayerTexture.Sample(gLayerSampler, input.uv); }

#include "layer_surface_body.slang"
[shader("vertex")]
VertexOutput vertexMain(VertexInput input) { return surfaceVertex(input); }
[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target { return surfaceFragment(input); }
