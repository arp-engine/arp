#include "3d/vegetation/foliage_geometry_profile.h"
#include "3d/vegetation/static_instance.h"
#include "3d/vegetation/template.h"

#include <cassert>
#include <cstddef>
#include <limits>

int main()
{
    using namespace arp::vegetation;
    StaticVegetationInstance placement;
    assert(placement.hasValidTransform());
    placement.rotation = {0, 0, 0, std::sqrt(1.00025f)};
    assert(placement.hasValidTransform());
    placement.rotation = {0, 0, 0, 2};
    assert(!placement.hasValidTransform());
    placement.rotation = {};
    assert(!placement.hasValidTransform());
    placement = {};
    placement.scale.z = 0.0f;
    assert(!placement.hasValidTransform());
    placement = {};
    placement.position.x = std::numeric_limits<float>::quiet_NaN();
    assert(!placement.hasValidTransform());
    placement = {};
    placement.rotation[1] = std::numeric_limits<float>::infinity();
    assert(!placement.hasValidTransform());
    static_assert(sizeof(InstanceVertex) == 60u);
    static_assert(sizeof(InstanceDataGpu) == 64u);
    static_assert(offsetof(InstanceDataGpu, speciesLodState) == 48u);
    static_assert(sizeof(WindFieldParameters) == 64u);
    InstanceData source;
    source.x = 13.0f;
    source.speciesId = 255u;
    source.ageClass = 2u;
    source.moisture = 247u;
    source.slopeClass = 3u;
    source.geometryLod = 2u;
    source.shadowLod = 1u;
    source.stateFlags = 0xffffu;
    const auto packed = source.toGpu();
    assert(packed.positionScale[0] == source.x);
    assert(instanceSpeciesId(packed) == source.speciesId);
    assert(instanceAgeClass(packed) == source.ageClass);
    assert(instanceMoisture(packed) == source.moisture);
    assert(instanceSlopeClass(packed) == source.slopeClass);
    assert(instanceGeometryLod(packed) == source.geometryLod);
    assert(instanceShadowLod(packed) == source.shadowLod);
    assert(instanceStateFlags(packed) == source.stateFlags);
    assert(unpackInstanceMetadataFloat(packed.speciesLodState[2]) == 0u);
    assert(unpackInstanceMetadataFloat(packed.speciesLodState[3]) == 0u);
    assert(categoryLodProfile(static_cast<TemplateCategory>(255)).validate() != nullptr);
    assert(categoryLodProfile(TemplateCategory::TreeMaturePine).resolveTier(14.0f, 0) == 1);
    assert(categoryLodProfile(TemplateCategory::FoliageGrass).shouldCull(16.0f, true));
    assert(FoliageGeometryContract::validate(FoliageGeometryContract::grass()) == nullptr);
    auto invalid = FoliageGeometryContract::grass();
    invalid.heightReference = std::numeric_limits<float>::quiet_NaN();
    assert(FoliageGeometryContract::validate(invalid) != nullptr);
}
