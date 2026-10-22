#include "core/box_geometry.slang"
float fogDemoTerrainHeight(float2 xz) { return 0.5*sin(xz.x*0.14)+0.7*sin(xz.y*0.10); }
void fogDemoVertex(uint id,out float3 world,out float3 normal,out float3 color, bool slits=false)
{
    if(slits) {
        uint object=id/36;
        static const float2 spans[4]={float2(-32,-5.175),float2(-4.825,-0.5),float2(0.5,4),float2(6,32)};
        float3 center=object<4 ? float3((spans[object].x+spans[object].y)*0.5,8,20) : float3(0,-5.1,0);
        float3 halfExtent=object<4 ? float3((spans[object].y-spans[object].x)*0.5,24,0.025) : float3(60,0.1,60);
        arpBoxVertex(id%36,center,halfExtent,world,normal);
        color=object<4 ? float3(0.16,0.17,0.18) : float3(0.07,0.08,0.09);
        return;
    }
    if(id<6144) {
        static const uint2 offsets[6]={uint2(0,0),uint2(1,1),uint2(1,0),uint2(0,0),uint2(0,1),uint2(1,1)};
        uint cell=id/6;
        float2 xz=(float2(uint2(cell%32,cell/32)+offsets[id%6])/32.0)*float2(80,100)+float2(-40,-20);
        world=float3(xz.x,fogDemoTerrainHeight(xz),xz.y);
        normal=normalize(float3(-0.07*cos(xz.x*0.14),1,-0.07*cos(xz.y*0.10)));
        color=float3(0.16,0.24,0.08);
    } else {
        uint object=(id-6144)/36;
        uint tree=object%16;
        float x=(float(tree%4)-1.5)*8;
        float z=8+float(tree/4)*12;
        float height=fogDemoTerrainHeight(float2(x,z));
        bool crown=object>=16;
        float3 center=float3(x,height+(crown?8.5:4),z);
        arpBoxVertex((id-6144)%36,center,crown?float3(3.2,2.5,3.2):float3(0.45,4,0.45),world,normal);
        color=crown?float3(0.08,0.20,0.06):float3(0.22,0.13,0.05);
    }
}
