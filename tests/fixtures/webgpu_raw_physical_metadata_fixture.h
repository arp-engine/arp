#pragma once
#include "shader/shader_artifact_reflection_contract.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace arp::test {
class WebGpuRawPhysicalMetadataFixture final {
    using Y = core::YamlContract;

public:
    static void verify()
    {
        const auto baseline = Y::Node::deserialize(R"(
bindings:
  - {name: Buffer, set: 2, binding: 3, type: UniformBuffer, arrayCount: 1}
webGpuBindingSpaceStatus: Available
webGpuBindingSpaceContractVersion: WebGpuBindingSpace/5
webGpuBindingSpaceGroupCount: 2
webGpuBindingSpace:
  - {name: Buffer, set: 2, binding: 3, active: true, physicalType: UniformBuffer, group: 1, webGpuBinding: 4, capacity: 1, arrayElement: 0}
  - {name: Unused, set: 3, binding: 5, active: false}
pushConstantSizeBytes: 16
pushConstantContractVersion: AtomicFullBlockPush/1
pushConstantWebGpuBinding: {group: 0, binding: 4}
)");
        shader::ShaderArtifactReflectionContract::validateWebGpuPhysicalTable(baseline, 2u);
        const auto rejected = [&](auto edit, const char* expected) {
            auto changed = baseline;
            edit(changed);
            const char* id = nullptr;
            try {
                shader::ShaderArtifactReflectionContract::validateWebGpuPhysicalTable(changed, 2u);
            } catch (const shader::ShaderArtifactLoadError& e) {
                id = e.diagnostic;
            } catch (const core::YamlContractError& e) {
                id = e.diagnostic;
            }
            assert(id && !std::strcmp(id, expected));
        };
        for (const auto* field : {"group", "webGpuBinding", "capacity", "arrayElement"}) {
            for (const auto* bad : {"-1", "4294967296", "true", "null", "1.5", "'0'"})
                rejected(
                    [&](auto& n) {
                        n["webGpuBindingSpace"][0][field] =
                            Y::Node::deserialize(bad, bad + std::strlen(bad));
                    },
                    "YAML-CONTRACT-FIELD-INVALID");
            rejected(
                [&](auto& n) {
                    n["webGpuBindingSpace"][1][field] = 0;
                },
                "SHADER-ENTRY-WEBGPU-INACTIVE-PAYLOAD");
        }
        rejected(
            [](auto& n) {
                n["webGpuBindingSpace"][0]["capacity"] = 0;
            },
            "SHADER-ENTRY-WEBGPU-PHYSICAL-CARDINALITY");
        rejected(
            [](auto& n) {
                n["webGpuBindingSpace"][0]["arrayElement"] = 1;
            },
            "SHADER-ENTRY-WEBGPU-PHYSICAL-CARDINALITY");
        rejected(
            [](auto& n) {
                n["webGpuBindingSpace"][0]["capacity"] = 2;
            },
            "SHADER-ENTRY-WEBGPU-LOGICAL-MISMATCH");
        rejected(
            [](auto& n) {
                n["webGpuBindingSpace"][0]["name"] = "Other";
            },
            "SHADER-ENTRY-WEBGPU-LOGICAL-MISMATCH");
        rejected(
            [](auto& n) {
                n["webGpuBindingSpace"][0]["physicalType"] = "StorageBuffer";
            },
            "SHADER-ENTRY-WEBGPU-LOGICAL-TYPE");
        rejected(
            [](auto& n) {
                n["webGpuBindingSpace"][0]["group"] = 2;
            },
            "SHADER-ENTRY-WEBGPU-PHYSICAL-LOCATION");
        rejected(
            [](auto& n) {
                n["webGpuBindingSpace"][0]["webGpuBinding"] = UINT32_MAX;
            },
            "SHADER-ENTRY-WEBGPU-PHYSICAL-LOCATION");
        rejected(
            [](auto& n) {
                n["webGpuBindingSpaceGroupCount"] = 3;
            },
            "SHADER-ENTRY-WEBGPU-GROUP-EXTENT");
        rejected(
            [](auto& n) {
                n["pushConstantWebGpuBinding"]["group"] = 1;
            },
            "SHADER-ENTRY-WEBGPU-PUSH-COLLISION");
        rejected(
            [](auto& n) {
                n["pushConstantSizeBytes"] = 0;
            },
            "SHADER-ENTRY-WEBGPU-PUSH-MAPPING");
        rejected(
            [](auto& n) {
                n["webGpuBindingSpaceStatus"] = "Unavailable";
            },
            "SHADER-ENTRY-WEBGPU-UNAVAILABLE-MAP");
        rejected(
            [](auto& n) {
                n["webGpuBindingSpace"][1] = n["webGpuBindingSpace"][0];
            },
            "SHADER-ENTRY-WEBGPU-LOGICAL-DUPLICATE");
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        shader::ShaderArtifactReflectionContract::validateWebGpuPhysicalTable(baseline, 2u);
        std::pmr::set_default_resource(previous);
        auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
        auto longNames = baseline;
        longNames["bindings"][0]["name"] = "BufferWithAnExplicitLongFixtureName";
        longNames["webGpuBindingSpace"][0]["name"] = "BufferWithAnExplicitLongFixtureName";
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease exhausted(quota, 1u);
            // Inline string capacity differs between host and Wasm ABIs. Use
            // the authored long-name case to exercise a real allocation.
            bool failed = false;
            try {
                shader::ShaderArtifactReflectionContract::validateWebGpuPhysicalTable(
                    longNames, 2u);
            } catch (const std::bad_alloc&) {
                failed = true;
            }
            assert(failed);
        }
        assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_RHI_WEBGPU_RAW_PHYSICAL_OK typed-fields exact-cardinality logical-physical-uniqueness inactive-empty push-separate hostile-pmr no-minimum-authority");
    }
};
} // namespace arp::test
