// GeometryFamily/1. Shared mesh, temporal skinning and wind-deformed instance
// color/depth/shadow entries. Scene lighting and publication belong to renderer.
struct FamilyFrame
{
    float4x4 renderViewProjection;
    float4x4 currentViewProjection;
    float4x4 previousViewProjection;
    float4x4 shadowViewProjection;
    ArpFoliageWindField currentWind;
    ArpFoliageWindField previousWind;
    uint4 outputExtent; // width, height, lanes-per-pixel=23, ABI=1
};
struct FamilyPalette
{
    float4x4 skinMatrices[256];
    float4 worldTranslationAndScale;
    float4 worldRotation;
    uint jointCount;
    uint pad0;
    uint pad1;
    uint pad2;
};
struct FamilyTemporalPalette { FamilyPalette current; FamilyPalette previous; };
[[vk::binding(0, 0)]] ConstantBuffer<FamilyFrame> GeometryFrame;
[[vk::binding(1, 0)]] StructuredBuffer<ArpInstanceDataGpu> GeometryInstances;
[[vk::binding(2, 0)]] ConstantBuffer<FamilyTemporalPalette> GeometryPalettes;
struct FamilyPushConstants { uint material; uint instanceBase; uint reserved0; uint reserved1; };
[[vk::push_constant]] FamilyPushConstants GeometrySelection;

struct FamilyInput
{
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    float4 tangent : TANGENT0;
    float3 windWeights : COLOR0;
    float4 joints : TEXCOORD1;
    float4 weights : TEXCOORD2;
};
struct FamilyVertex
{
    float4 position : SV_Position;
    float3 world : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 tangent : TEXCOORD3;
    float4 currentClip : TEXCOORD4;
    float4 previousClip : TEXCOORD5;
    float4 shadowClip : TEXCOORD6;
    nointerpolation uint material : TEXCOORD7;
    nointerpolation uint4 metadata : TEXCOORD8;
    nointerpolation float lodTime : TEXCOORD9;
};
FamilyVertex familyProject(ArpPbrGeometry geometry, float2 uv, bool shadow)
{
    FamilyVertex result;
    result.position = shadow ? arpProjectPbrGeometry(geometry, GeometryFrame.shadowViewProjection)
        : arpProjectPbrGeometry(geometry, GeometryFrame.renderViewProjection);
    result.world = geometry.worldPosition;
    result.normal = geometry.normal;
    result.uv = uv;
    result.tangent = geometry.tangent;
    result.currentClip = arpProjectPbrGeometry(geometry, GeometryFrame.currentViewProjection);
    result.previousClip = mul(GeometryFrame.previousViewProjection, float4(geometry.previousWorldPosition, 1.0));
    result.shadowClip = arpProjectPbrGeometry(geometry, GeometryFrame.shadowViewProjection);
    result.material = GeometrySelection.material;
    result.metadata = geometry.instanceMetadata;
    result.lodTime = geometry.lodTime;
    return result;
}
ArpPbrGeometry familySkin(FamilyInput input)
{
    ArpSkinningBlend blend = arpEmptySkinningBlend();
    [unroll]
    for (uint lane = 0u; lane < 4u; ++lane) {
        uint joint = min((uint)max(input.joints[lane], 0.0),
            GeometryPalettes.current.jointCount > 0u ? GeometryPalettes.current.jointCount - 1u : 0u);
        if (input.weights[lane] > 0.0 && joint < 256u) {
            uint previous = min((uint)max(input.joints[lane], 0.0),
                GeometryPalettes.previous.jointCount > 0u ? GeometryPalettes.previous.jointCount - 1u : 0u);
            bool valid = previous < 256u;
            arpAccumulateSkinning(blend, GeometryPalettes.current.skinMatrices[joint],
                GeometryPalettes.previous.skinMatrices[valid ? previous : 0u], valid,
                input.weights[lane], input.position, input.normal, input.tangent.xyz);
        }
    }
    return arpPbrSkinnedGeometry(blend,
        GeometryPalettes.current.worldTranslationAndScale, GeometryPalettes.current.worldRotation,
        GeometryPalettes.previous.worldTranslationAndScale, GeometryPalettes.previous.worldRotation,
        input.tangent.w);
}

[shader("vertex")]
FamilyVertex meshColorVertex(FamilyInput input)
{
    return familyProject(arpPbrWorldGeometry(input.position, input.normal, input.tangent), input.uv, false);
}

[shader("vertex")]
FamilyVertex meshDepthVertex(FamilyInput input)
{
    return familyProject(arpPbrWorldGeometry(input.position, input.normal, input.tangent), input.uv, false);
}

[shader("vertex")]
FamilyVertex meshShadowVertex(FamilyInput input)
{
    return familyProject(arpPbrWorldGeometry(input.position, input.normal, input.tangent), input.uv, true);
}

[shader("vertex")]
FamilyVertex skinColorVertex(FamilyInput input)
{
    return familyProject(familySkin(input), input.uv, false);
}

[shader("vertex")]
FamilyVertex skinDepthVertex(FamilyInput input)
{
    return familyProject(familySkin(input), input.uv, false);
}

[shader("vertex")]
FamilyVertex skinShadowVertex(FamilyInput input)
{
    return familyProject(familySkin(input), input.uv, true);
}

[shader("vertex")]
FamilyVertex grassColorVertex(FamilyInput input, uint instanceId : SV_InstanceID)
{
    return familyProject(arpPbrInstanceGeometry(GeometryInstances[GeometrySelection.instanceBase + instanceId],
        input.position, input.normal, input.tangent, input.windWeights,
        GeometryFrame.currentWind, GeometryFrame.previousWind, 0.5), input.uv, false);
}

[shader("vertex")]
FamilyVertex grassDepthVertex(FamilyInput input, uint instanceId : SV_InstanceID)
{
    return familyProject(arpPbrInstanceGeometry(GeometryInstances[GeometrySelection.instanceBase + instanceId],
        input.position, input.normal, input.tangent, input.windWeights,
        GeometryFrame.currentWind, GeometryFrame.previousWind, 0.5), input.uv, false);
}

[shader("vertex")]
FamilyVertex grassShadowVertex(FamilyInput input, uint instanceId : SV_InstanceID)
{
    return familyProject(arpPbrInstanceGeometry(GeometryInstances[GeometrySelection.instanceBase + instanceId],
        input.position, input.normal, input.tangent, input.windWeights,
        GeometryFrame.currentWind, GeometryFrame.previousWind, 0.5), input.uv, true);
}

[shader("vertex")]
FamilyVertex canopyColorVertex(FamilyInput input, uint instanceId : SV_InstanceID)
{
    return familyProject(arpPbrInstanceGeometry(GeometryInstances[GeometrySelection.instanceBase + instanceId],
        input.position, input.normal, input.tangent, input.windWeights,
        GeometryFrame.currentWind, GeometryFrame.previousWind, 2.0), input.uv, false);
}

[shader("vertex")]
FamilyVertex canopyDepthVertex(FamilyInput input, uint instanceId : SV_InstanceID)
{
    return familyProject(arpPbrInstanceGeometry(GeometryInstances[GeometrySelection.instanceBase + instanceId],
        input.position, input.normal, input.tangent, input.windWeights,
        GeometryFrame.currentWind, GeometryFrame.previousWind, 2.0), input.uv, false);
}

[shader("vertex")]
FamilyVertex canopyShadowVertex(FamilyInput input, uint instanceId : SV_InstanceID)
{
    return familyProject(arpPbrInstanceGeometry(GeometryInstances[GeometrySelection.instanceBase + instanceId],
        input.position, input.normal, input.tangent, input.windWeights,
        GeometryFrame.currentWind, GeometryFrame.previousWind, 2.0), input.uv, true);
}

