// InstanceLodCrossfade/1. Float storage is bitcast to integer metadata before
// interpolation. Complementary masks preserve opaque depth and shadow writes.
float lodCrossfadeReactive(uint4 metadata) {
    return (metadata.w & 0x80000000u) != 0u ? 0.35 : 0.0;
}
void applyLodCrossfade(float2 pixel, uint4 metadata, float time) {
    if ((metadata.w & 0x80000000u) == 0u) return;
    uint2 p = uint2(pixel);
    uint seed = (metadata.w >> 16) & 0x3fffu;
    uint noise = p.x * 0x1f123bb5u ^ p.y * 0x5f356495u ^ seed * 0x9e3779b9u;
    noise ^= noise >> 16; noise *= 0x7feb352du;
    noise ^= noise >> 15; noise *= 0x846ca68bu; noise ^= noise >> 16;
    // Center bins exclude both endpoints: 0 keeps all outgoing, 65535 all incoming.
    float sample = (float(noise & 0xffffu) + 0.5) / 65536.0;
    float duration = float(metadata.w & 0xffffu) * 0.001;
    float coverage = saturate((time - asfloat(metadata.z)) / duration);
    bool incoming = (metadata.w & 0x40000000u) != 0u;
    if (incoming ? sample >= coverage : sample < coverage) discard;
}
