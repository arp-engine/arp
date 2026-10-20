#pragma once

#include "core/cpu_allocator.h"

#include <cstdint>
#include <limits>

namespace arp::tools::render_feature_lab {

struct FeatureLabCamera {
    float position[3]{0.0f, 1.5f, -4.0f};
    float target[3]{0.0f, 0.0f, 0.0f};
    float verticalFovDegrees = 60.0f;
    bool declared = false;
};

struct FeatureLabAcceptanceThreshold {
    core::pmr::String attachment;
    core::pmr::String metric;
    float minimum = -std::numeric_limits<float>::infinity();
    float maximum = std::numeric_limits<float>::infinity();
};

struct FeatureLabCase {
    core::pmr::String id;
    core::pmr::String parent;
    core::pmr::Vector<core::pmr::String> features;
    core::pmr::Vector<core::pmr::String> requiredCapabilities;
    core::pmr::Vector<core::pmr::String> attachments;
    core::pmr::Vector<FeatureLabAcceptanceThreshold> acceptance;
    FeatureLabCamera camera;
    std::uint32_t defaultFrames = 1u;
};

struct ResolvedFeatureLabCase {
    core::pmr::String id;
    core::pmr::Vector<core::pmr::String> features;
    core::pmr::Vector<core::pmr::String> requiredCapabilities;
    core::pmr::Vector<core::pmr::String> attachments;
    core::pmr::Vector<FeatureLabAcceptanceThreshold> acceptance;
    FeatureLabCamera camera;
    std::uint32_t defaultFrames = 1u;
};

struct FeatureLabManifest {
    std::uint32_t version = 0u;
    core::pmr::Vector<FeatureLabCase> cases;
};

struct FeatureLabManifestResult {
    bool success = false;
    FeatureLabManifest manifest;
    core::pmr::String diagnostics;
};

FeatureLabManifestResult loadFeatureLabManifest(const char* path);
const FeatureLabCase* findFeatureLabCase(
    const FeatureLabManifest& manifest, const core::pmr::String& id) noexcept;
bool resolveFeatureLabCase(const FeatureLabManifest& manifest,
    const FeatureLabCase& selected,
    core::pmr::Vector<core::pmr::String>& features,
    core::pmr::String& diagnostics);
bool resolveFeatureLabCase(const FeatureLabManifest& manifest,
    const FeatureLabCase& selected,
    ResolvedFeatureLabCase& resolved,
    core::pmr::String& diagnostics);

} // namespace arp::tools::render_feature_lab
