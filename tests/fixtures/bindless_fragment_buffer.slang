[shader("fragment")]
float4 fragmentBufferMain(float4 position : SV_Position) : SV_Target
{
    bool fault;
    uint value = Inputs_load(uint(position.x) & 1u, 0u, fault);
    return fault ? float4(1.0, 0.0, 1.0, 1.0)
        : float4(float(value) / 255.0, 0.0, 0.0, 1.0);
}
