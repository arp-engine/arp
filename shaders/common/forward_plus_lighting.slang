#ifndef ARP_FORWARD_PLUS_LIGHTING_SLANG
#define ARP_FORWARD_PLUS_LIGHTING_SLANG

static const uint ARP_FORWARD_PLUS_TILE_SIZE = 16;

struct GpuDirectionalLight
{
    float4 directionAndShadow;
    float4 colorAndIntensity;
};

// positionAndRange.xyz is world-space position. The cull pass transforms its
// conservative sphere to view space; fragment consumers evaluate the same
// record against world-space surfaces. Spot lights use their conservative
// bounding sphere for culling and exact cone attenuation in fragment shading.
struct GpuLocalLight
{
    float4 positionAndRange;
    float4 colorAndIntensity;
    float4 directionAndOuterConeCos;
    float4 innerConeCosAndType;
};

struct ForwardPlusTileHeader
{
    uint offset;
    uint count;
};

uint forwardPlusTileIndex(float4 svPosition, uint2 tileCount)
{
    uint2 pixel = uint2(svPosition.xy);
    uint2 tile = min(pixel / ARP_FORWARD_PLUS_TILE_SIZE, tileCount - 1);
    return tile.y * tileCount.x + tile.x;
}

#endif
