#ifndef ARP_BOX_GEOMETRY_SLANG
#define ARP_BOX_GEOMETRY_SLANG
// Non-indexed, outward-facing box, 36 vertices. Half-extents are in world units.
void arpBoxVertex(uint id, float3 center, float3 halfExtent, out float3 position, out float3 normal)
{
    static const float2 corners[6]={float2(-1,-1),float2(1,-1),float2(1,1),float2(-1,-1),float2(1,1),float2(-1,1)};
    uint face=id/6;
    float2 uv=corners[id%6];
    float3 axis=face<2 ? float3(1,0,0) : face<4 ? float3(0,1,0) : float3(0,0,1);
    normal=axis*(face%2==0?1.0:-1.0);
    float3 u=face<2 ? float3(0,0,-normal.x) : face<4 ? float3(1,0,0) : float3(normal.z,0,0);
    float3 v=cross(normal,u);
    position=center+(normal+u*uv.x+v*uv.y)*halfExtent;
}
#endif
