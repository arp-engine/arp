#pragma once
#include "pcg/vegetation_generation_result.h"
// Legacy source aliases; generation results remain PCG-owned.
#include "3d/vegetation/mesh_adapter.h"
#include "3d/vegetation/template.h"
#include "assets/asset_pipeline.h"

namespace arp::pcg {
using vegetation::categoryLodProfile;
using vegetation::computeInstanceVertexLocalBounds;
using vegetation::convertToInstanceVertices;
using vegetation::familyLodStride;
using vegetation::familyTemplateBase;
using vegetation::familyVariantCount;
using vegetation::grassLodProfile;
using vegetation::instanceAgeClass;
using vegetation::InstanceBatch;
using vegetation::InstanceData;
using vegetation::InstanceDataGpu;
using vegetation::instanceGeometryLod;
using vegetation::instanceMoisture;
using vegetation::instanceShadowLod;
using vegetation::instanceSlopeClass;
using vegetation::instanceSpeciesId;
using vegetation::instanceStateFlags;
using vegetation::InstanceVertex;
using vegetation::InstanceVertexLocalBounds;
using vegetation::isGroundCoverFoliage;
using vegetation::matchesFamilyTemplateAddress;
using vegetation::packInstanceMetadataFloat;
using vegetation::rockLodProfile;
using vegetation::setInstanceMetadata;
using vegetation::TemplateCategory;
using vegetation::treeLodProfile;
using vegetation::understoryLodProfile;
using vegetation::unpackInstanceMetadataFloat;
using vegetation::VegetationTemplate;
using vegetation::VegetationTemplateConfig;
} // namespace arp::pcg
