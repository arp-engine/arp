struct DemoConstants { float4x4 viewProjection; float4 lightDirection; };
struct VertexOutput {float4 position:SV_Position;float3 world:TEXCOORD0;};
VertexOutput fogGlassVertex(uint id:SV_VertexID)
{
    static const float2 points[6]={float2(-1,-1),float2(1,-1),float2(1,1),float2(-1,-1),float2(1,1),float2(-1,1)};
    VertexOutput o;o.world=float3(points[id].x*2-4,points[id].y*2+3,14);
    o.position=mul(DemoParams.viewProjection,float4(o.world,1));return o;
}
