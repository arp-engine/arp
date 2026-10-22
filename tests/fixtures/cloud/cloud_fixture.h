#pragma once

#include "assets/cloud/cloud_asset.h"
#include "renderer/volumetric_cloud/cloud_execution_plan.h"

#include <cassert>

namespace arp::tests::cloud {

inline assets::cloud::CloudAsset asset()
{
    assets::cloud::CloudCookDesc d;
    d.algorithmVersion = assets::cloud::kCloudCookAlgorithmVersion;
    d.seed = 137u;
    d.weatherDimension = d.shapeDimension = 8u;
    d.detailDimension = 4u;
    d.weatherFrequency = d.shapeFrequency = d.detailFrequency = 2u;
    d.octaves = 2u;
    d.maxOutputBytes = 65536u;
    d.maxNoiseEvaluations = 1000000u;
    d.heightProfiles = {core::Vec4{0, .08f, .2f, .35f},
        core::Vec4{0, .12f, .55f, .85f},
        core::Vec4{0, .15f, .75f, 1}};
    assets::cloud::CloudAsset result;
    assert(assets::cloud::CloudAssetCooker::cook(d, result).success);
    return result;
}

inline renderer::cloud::CloudRenderConfig config()
{
    renderer::cloud::CloudRenderConfig c;
    c.resolutionDivisor = 1u;
    c.viewSteps = 24u;
    c.lightSteps = 4u;
    c.framesInFlight = 3u;
    c.gpuBudgetBytes = 128u * 1024u * 1024u;
    c.maxDensityEvaluations = 1000000000u;
    c.maxDistance = 10000;
    c.historyWeight = .9f;
    c.historyDepthTolerance = 10;
    c.temporal = true;
    return c;
}

inline scene3d::CloudLayerDesc layer()
{
    scene3d::CloudLayerDesc d;
    d.planetCenter = {0, -6371000, 0};
    d.planetRadius = 6371000;
    d.baseAltitude = 1000;
    d.thickness = 1500;
    d.extinction = .002f;
    d.scatteringAlbedo = {1, .97f, .92f};
    d.anisotropy = .5f;
    d.windVelocity = {10, 0, 3};
    d.weatherPeriod = 16000;
    d.shapePeriod = 4000;
    d.detailPeriod = 600;
    d.coverage = 1;
    d.erosion = .1f;
    return d;
}

} // namespace arp::tests::cloud
