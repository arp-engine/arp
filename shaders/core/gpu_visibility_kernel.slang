// VisibilityKernel/1. Compose the value ABI and a typed resource adapter first.
bool visibilityParametersValid(ArpGpuVisibilityParameters p)
{
    return p.capacities.w == 1u && p.policy.w == 1u
        && p.counts.x != 0u && p.counts.y != 0u
        && p.counts.x <= p.capacities.x
        && p.counts.y <= p.capacities.y
        && p.capacities.z != 0u && p.counts.w <= 1u
        && (p.counts.z == 1u || p.counts.z == 3u)
        && p.capacities.x <= 0xffffffffu / p.counts.z
        && p.capacities.y <= (0xffffffffu / 5u) / p.counts.z
        && p.policy.x <= 1u && p.policy.y == 0u && p.policy.z == 0u
        && (p.policy.x == 1u || p.counts.z == 1u)
        && p.depthInfo.w <= 1u;
}

bool visibilityHiZOccluded(ArpGpuVisibilityParameters p, float3 center, float radius)
{
    if (p.depthInfo.w == 0u) return false;
    float4 point = float4(center, 1.0);
    float4 clip = float4(dot(p.viewProjectionRows[0], point),
        dot(p.viewProjectionRows[1], point),
        dot(p.viewProjectionRows[2], point),
        dot(p.viewProjectionRows[3], point));
    // Bound homogeneous numerators and denominator over the complete sphere.
    // Row lengths remain correct under camera rotation and asymmetric frusta.
    float4 extentClip = radius * float4(length(p.viewProjectionRows[0].xyz),
        length(p.viewProjectionRows[1].xyz),
        length(p.viewProjectionRows[2].xyz),
        length(p.viewProjectionRows[3].xyz));
    if (clip.w <= extentClip.w || !all(isfinite(clip)) || !all(isfinite(extentClip))) return false;
    float2 ndc = clip.xy / clip.w;
    float2 radiusNdc = (extentClip.xy + abs(ndc) * extentClip.w) / (clip.w - extentClip.w);
    float2 minUv = (ndc - radiusNdc) * 0.5 + 0.5;
    float2 maxUv = (ndc + radiusNdc) * 0.5 + 0.5;
    if (any(minUv <= 0.0) || any(maxUv >= 1.0)) return false;
    float footprint = max((maxUv.x - minUv.x) * p.depthInfo.x,
        (maxUv.y - minUv.y) * p.depthInfo.y);
    // The rectangle spans at most two cells per axis at ceil(log2(size)).
    // floor can span three cells and miss an interior maximum with four loads.
    uint requested = (uint)ceil(log2(max(footprint, 1.0)));
    if (requested >= p.depthInfo.z) return false;
    uint mip = requested;
    uint2 extent = max(p.depthInfo.xy >> mip, uint2(1u, 1u));
    uint2 lo = min(uint2(minUv * float2(extent)), extent - 1u);
    uint2 hi = min(uint2(maxUv * float2(extent)), extent - 1u);
    float maximum = max(max(arpVisibilityDepth(int3(lo, mip)).r,
        arpVisibilityDepth(int3(uint2(hi.x, lo.y), mip)).r),
        max(arpVisibilityDepth(int3(uint2(lo.x, hi.y), mip)).r,
        arpVisibilityDepth(int3(hi, mip)).r));
    float lowZ = clip.z - extentClip.z;
    float highZ = clip.z + extentClip.z;
    float lowW = clip.w - extentClip.w;
    float highW = clip.w + extentClip.w;
    float nearest = min(min(lowZ / lowW, lowZ / highW), min(highZ / lowW, highZ / highW));
    return nearest > maximum;
}

void arpVisibilityResetKernel(uint3 id)
{
    ArpGpuVisibilityParameters p = arpVisibilityParameters();
    if (id.y != 0u || id.z != 0u) return;
    if (id.x == 0u) arpVisibilityWriteControl(0u, 0u);
    if (!visibilityParametersValid(p)) {
        if (id.x == 0u) arpVisibilityFault();
        return;
    }
    // Reset the full authored section span, including holes and stale draws.
    if (id.x < p.counts.z) arpVisibilityWriteControl(1u + id.x, p.counts.y);
    if (id.x < p.counts.y * p.counts.z) {
        uint output = (id.x / p.counts.y) * p.capacities.y + id.x % p.counts.y;
        arpVisibilityWriteCount(output, 0u);
        arpVisibilityWriteDraw(output, (ArpGpuIndexedIndirectCommand)0);
        arpVisibilityWriteBase(output, (ArpGpuIndirectInstanceBase)0);
    }
}

void arpVisibilityCullKernel(uint3 id)
{
    ArpGpuVisibilityParameters p = arpVisibilityParameters();
    if (!visibilityParametersValid(p)) { arpVisibilityFault(); return; }
    if (id.x >= p.counts.x || id.y >= p.counts.z || id.z != 0u) return;
    uint ownerIndex = arpVisibilityOwner(id.x);
    if (ownerIndex >= p.counts.y) { arpVisibilityFault(); return; }
    ArpGpuVisibilityDrawGroup owner = arpVisibilityGroup(ownerIndex);
    if (owner.materialIndex >= p.capacities.z
        || !arpVisibilityOwnerRange(owner, owner, id.x, p.capacities.x)) {
        arpVisibilityFault(); return;
    }
    bool shadow = p.policy.x == 1u;
    if (shadow && owner.castsShadow == 0u) return;
    ArpInstanceDataGpu instance = arpVisibilityInstance(id.x);
    float scaleY = instance.extraTransform.z > 0.0
        ? instance.extraTransform.z : instance.positionScale.w;
    float3 center = arpTransformInstancePosition(instance, owner.localBoundsCenterRadius.xyz);
    float radius = owner.localBoundsCenterRadius.w
        * max(abs(instance.positionScale.w), abs(scaleY)) + p.cameraAndEnvelope.w;
    if (!all(isfinite(center)) || !isfinite(radius) || radius < 0.0) {
        arpVisibilityFault(); return;
    }
    float3 point = shadow ? instance.positionScale.xyz : center;
    float sphereRadius = shadow ? 0.0 : radius;
    float4 planes[6];
    [unroll]
    for (uint plane = 0u; plane < 6u; ++plane)
        planes[plane] = p.frustumPlanes[id.y * 6u + plane];
    if (!arpVisibilitySphereInFrustum(point, sphereRadius, planes)) return;
    float2 difference = point.xz - p.cameraAndEnvelope.xz;
    float distanceLimit = shadow ? p.selection.x : owner.cullDistance;
    if (dot(difference, difference) > distanceLimit * distanceLimit) return;
    if (!shadow && visibilityHiZOccluded(p, center, radius)) return;
    uint countIndex = id.y * p.capacities.y + ownerIndex;
    uint slot = arpVisibilityReserve(countIndex);
    uint localOutput = 0u, output = 0u;
    if (!arpVisibilityOutputIndex(owner.globalFirstInstance, owner.globalInstanceCount,
            slot, p.capacities.x, localOutput)
        || !arpVisibilitySliceIndex(id.y, p.counts.z, p.capacities.x, localOutput, output)) {
        arpVisibilityFault(); return;
    }
    arpVisibilityWriteInstance(output, instance);
}

void arpVisibilityEmitKernel(uint3 id)
{
    ArpGpuVisibilityParameters p = arpVisibilityParameters();
    if (!visibilityParametersValid(p)) { arpVisibilityFault(); return; }
    if (id.x >= p.counts.y || id.y >= p.counts.z || id.z != 0u) return;
    uint output = id.y * p.capacities.y + id.x;
    ArpGpuVisibilityDrawGroup group = arpVisibilityGroup(id.x);
    ArpGpuIndexedIndirectCommand draw;
    ArpGpuIndirectInstanceBase mapping;
    arpEncodeVisibilityDraw(group, 0u, 0u, id.y, p.counts.w != 0u, draw, mapping);
    bool valid = group.globalInstanceCount != 0u
        && group.globalFirstInstance < p.counts.x
        && group.materialIndex < p.capacities.z;
    if (valid) {
        uint ownerIndex = arpVisibilityOwner(group.globalFirstInstance);
        valid = ownerIndex < p.counts.y;
        if (valid)
            valid = arpVisibilityOwnerRange(group, arpVisibilityGroup(ownerIndex),
                    group.globalFirstInstance, p.capacities.x)
                && arpEncodeVisibilityDraw(group,
                    arpVisibilityCount(id.y * p.capacities.y + ownerIndex),
                    p.capacities.x, id.y, p.counts.w != 0u, draw, mapping);
    }
    if (!valid) {
        arpVisibilityFault();
        draw = (ArpGpuIndexedIndirectCommand)0;
        mapping = (ArpGpuIndirectInstanceBase)0;
    }
    arpVisibilityWriteDraw(output, draw);
    arpVisibilityWriteBase(output, mapping);
    // The separate finalize pass resolves faults discovered by any invocation.
}

// The final pass observes *all* emit errors. Without this pass an invocation
// could publish before a different invocation latched a fault. Draws may only
// consume after the canonical finalize->indirect/vertex dependency.
void arpVisibilityFinalizeKernel(uint3 id)
{
    if (id.y != 0u || id.z != 0u) return;
    // The full immutable output ranges remain available even if Parameters
    // failed its guarded read. This pass uses a flat physical-slot dispatch,
    // independently bounded by the host from those same original ranges.
    uint words = arpVisibilityDrawWords();
    uint draws = words / 5u;
    uint bases = arpVisibilityBaseSlots();
    uint controls = arpVisibilityControlWords();
    ArpGpuVisibilityParameters p = arpVisibilityParameters();
    bool valid = visibilityParametersValid(p);
    if (!valid || words % 5u != 0u || draws != bases || controls == 0u
        || (valid && (draws != p.capacities.y * p.counts.z
            || controls != 1u + p.counts.z)))
        arpVisibilityFault();
    // Query and parameter faults above must be observed before deciding that
    // output is clean. Physical descriptor failures cannot be repaired by a
    // shader; cold admission rejects any unproven output range/authorization.
    if (arpVisibilityFaultBits() == 0u) return;
    if (id.x < draws) arpVisibilityWriteDraw(id.x, (ArpGpuIndexedIndirectCommand)0);
    if (id.x < bases) arpVisibilityWriteBase(id.x, (ArpGpuIndirectInstanceBase)0);
    if (id.x != 0u && id.x < controls) arpVisibilityWriteControl(id.x, 0u);
}
