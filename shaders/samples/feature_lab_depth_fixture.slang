struct VertexOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
VertexOutput vertexMain(uint vertexId : SV_VertexID)
{
    // Two screen-aligned surfaces provide the deterministic depth step used
    // by post-process feature fixtures. Depth comes from the raster pipeline,
    // which is valid for WebGPU D32 attachments unlike buffer-to-depth copies.
    float4 positions[12] = {
        float4(-1.0, -1.0, 0.35, 1.0), float4( 0.0, -1.0, 0.35, 1.0), float4(-1.0,  1.0, 0.35, 1.0),
        float4(-1.0,  1.0, 0.35, 1.0), float4( 0.0, -1.0, 0.35, 1.0), float4( 0.0,  1.0, 0.35, 1.0),
        float4( 0.0, -1.0, 0.41, 1.0), float4( 1.0, -1.0, 0.41, 1.0), float4( 0.0,  1.0, 0.41, 1.0),
        float4( 0.0,  1.0, 0.41, 1.0), float4( 1.0, -1.0, 0.41, 1.0), float4( 1.0,  1.0, 0.41, 1.0),
    };
    VertexOutput output;
    output.position = positions[vertexId];
    return output;
}

[shader("fragment")]
void fragmentMain(VertexOutput input)
{
}
