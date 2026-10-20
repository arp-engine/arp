#include "tools/render_feature_lab/feature_lab_manifest.h"

#include <cassert>
#include <filesystem>
#include <iostream>

namespace lab = arp::tools::render_feature_lab;

namespace {

bool contains(const arp::core::pmr::Vector<arp::core::pmr::String>& values, const char* expected)
{
    for (const auto& value : values)
        if (value == expected)
            return true;
    return false;
}

void manifest_resolves_cumulative_contract()
{
#ifdef ARP_SOURCE_DIR
    const auto path = std::filesystem::path(ARP_SOURCE_DIR) / "tests/render_feature_lab/cases.yaml";
#else
    const auto path = std::filesystem::path("tests/render_feature_lab/cases.yaml");
#endif
    const auto loaded = lab::loadFeatureLabManifest(path.string().c_str());
    if (!loaded.success)
        std::cerr << loaded.diagnostics << '\n';
    assert(loaded.success);
    assert(loaded.manifest.version == 1u);

    const auto* full = lab::findFeatureLabCase(loaded.manifest, arp::core::pmr::String("L10-full"));
    assert(full != nullptr);
    lab::ResolvedFeatureLabCase resolved;
    arp::core::pmr::String diagnostic;
    assert(lab::resolveFeatureLabCase(loaded.manifest, *full, resolved, diagnostic));
    assert(resolved.camera.declared);
    assert(resolved.features.size() == 11u);
    assert(contains(resolved.features, "forward-plus"));
    assert(contains(resolved.requiredCapabilities, "sampled-depth"));
    assert(contains(resolved.attachments, "overflow-telemetry"));
    assert(contains(resolved.attachments, "final-ldr"));
    assert(!resolved.acceptance.empty());
}

void manifest_rejects_parent_cycles_and_undeclared_threshold_attachments()
{
    lab::FeatureLabManifest cycle;
    cycle.version = 1u;
    lab::FeatureLabCase first;
    first.id = "first";
    first.parent = "second";
    lab::FeatureLabCase second;
    second.id = "second";
    second.parent = "first";
    cycle.cases.push_back(std::move(first));
    cycle.cases.push_back(std::move(second));
    lab::ResolvedFeatureLabCase resolved;
    arp::core::pmr::String diagnostic;
    assert(!lab::resolveFeatureLabCase(cycle, cycle.cases.front(), resolved, diagnostic));
    assert(diagnostic == "FEATURE-LAB-PARENT-CYCLE");

    lab::FeatureLabManifest invalid;
    invalid.version = 1u;
    lab::FeatureLabCase testCase;
    testCase.id = "invalid-attachment";
    testCase.camera.declared = true;
    lab::FeatureLabAcceptanceThreshold threshold;
    threshold.attachment = "missing";
    threshold.metric = "finite_fraction";
    threshold.minimum = 1.0f;
    testCase.acceptance.push_back(std::move(threshold));
    invalid.cases.push_back(std::move(testCase));
    diagnostic.clear();
    assert(!lab::resolveFeatureLabCase(invalid, invalid.cases.front(), resolved, diagnostic));
    assert(diagnostic == "FEATURE-LAB-ACCEPTANCE-ATTACHMENT-NOT-DECLARED");
}

} // namespace

int main()
{
    manifest_resolves_cumulative_contract();
    manifest_rejects_parent_cycles_and_undeclared_threshold_attachments();
    std::cout << "feature_lab_manifest_tests: all assertions passed\n";
    return 0;
}
