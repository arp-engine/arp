#pragma once
#include "../rhi_shader_package_test_helpers.h"
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/pipeline_push_constant_abi_identity.h"
#include "rhi/frontend/pipeline_state_copy.h"
#include "rhi/gpu_pipeline.h"

#include <cstdio>

namespace arp::test {
class GraphicsShaderInterfaceMergeFixture final {
    static auto selected(rhi::ShaderStage stage)
    {
        auto result = rhi::test::makePushContractShaderPackage();
        auto entry = result.entrypoints[stage == rhi::ShaderStage::Vertex ? 0u : 1u];
        result.entrypoints.clear();
        result.entrypoints.push_back(std::move(entry));
        result.reflection.sourceSha256 = "shared-original-source-identity-for-selected-stages";
        result.reflection.pushConstantStages = stage == rhi::ShaderStage::Vertex ? "VS" : "FS";
        result.reflection.descriptorBindings.push_back(
            {0u, 3u, rhi::DescriptorType::ReadOnlyStorageBuffer, stage, 2u});
        return result;
    }

public:
    static void verify()
    {
        using namespace rhi;
        const auto vertex = selected(ShaderStage::Vertex),
                   fragment = selected(ShaderStage::Fragment);
        assert(validateShaderPackage(vertex).success && validateShaderPackage(fragment).success);
        ShaderPackageDesc merged;
        assert(GraphicsShaderInterfaceMerger::merge(1u, vertex, fragment, merged).success);
        assert(merged.entrypoints.size() == 2u && merged.reflection.pushConstantStages == "VS|FS");
        assert(merged.reflection.descriptorBindings.size() == 1u);
        assert(merged.reflection.descriptorBindings[0].stages
            == (ShaderStage::Vertex | ShaderStage::Fragment));
        const auto reject = [&](auto change, const char* id) {
            auto changed = ShaderPackageStateCopy::copy(fragment);
            change(changed);
            auto output = ShaderPackageStateCopy::copy(merged);
            const auto result = GraphicsShaderInterfaceMerger::merge(1u, vertex, changed, output);
            if (result.success || result.diagnostics != id)
                std::fprintf(stderr, "merge expected %s got %s\n", id, result.diagnostics.c_str());
            assert(!result.success && result.diagnostics == id && output.entrypoints.empty()
                && output.reflection.descriptorBindings.empty()
                && !output.reflection.pushConstantByteCount);
        };
        assert(!GraphicsShaderInterfaceMerger::merge(0u, vertex, fragment, merged).success
            && merged.entrypoints.empty());
        reject(
            [](auto& p) {
                p.entrypoints[0].stage = ShaderStage::Compute;
            },
            "RHI-GRAPHICS-MERGE-SELECTED-STAGE");
        reject(
            [](auto& p) {
                p.reflection.sourceSha256.clear();
            },
            "RHI-GRAPHICS-MERGE-SOURCE-IDENTITY");
        reject(
            [](auto& p) {
                p.reflection.sourceSha256 += "changed";
            },
            "RHI-GRAPHICS-MERGE-SOURCE-IDENTITY");
        reject(
            [](auto& p) {
                p.reflection.reflectionContractVersion += "changed";
            },
            "RHI-GRAPHICS-MERGE-SOURCE-IDENTITY");
        reject(
            [](auto& p) {
                ++p.reflection.pushConstantWebGpuBindingGroup;
            },
            "RHI-GRAPHICS-MERGE-PUSH-ABI");
        reject(
            [](auto& p) {
                ++p.reflection.pushConstantWebGpuBindingIndex;
            },
            "RHI-GRAPHICS-MERGE-PUSH-ABI");
        reject(
            [](auto& p) {
                ++p.reflection.pushConstantMetalBufferIndex;
            },
            "RHI-GRAPHICS-MERGE-PUSH-ABI");
        reject(
            [](auto& p) {
                p.reflection.pushConstantMembers[0].name += "changed";
            },
            "RHI-GRAPHICS-MERGE-PUSH-ABI");
        reject(
            [](auto& p) {
                p.reflection.pushConstantMembers[0].rowMajor = true;
            },
            "RHI-GRAPHICS-MERGE-PUSH-ABI");
        reject(
            [](auto& p) {
                ++p.reflection.pushConstantMembers[0].decorations;
            },
            "RHI-GRAPHICS-MERGE-PUSH-ABI");
        reject(
            [](auto& p) {
                p.reflection.descriptorBindings[0].type = DescriptorType::StorageBuffer;
            },
            "RHI-GRAPHICS-MERGE-LOGICAL-BINDING");
        reject(
            [](auto& p) {
                ++p.reflection.descriptorBindings[0].arrayCount;
            },
            "RHI-GRAPHICS-MERGE-LOGICAL-BINDING");
        reject(
            [](auto& p) {
                p.reflection.descriptorBindings[0].depthTexture = true;
            },
            "RHI-GRAPHICS-MERGE-LOGICAL-BINDING");
        reject(
            [](auto& p) {
                p.reflection.descriptorBindings[0].subpassAttachmentIndex = 1u;
            },
            "RHI-GRAPHICS-MERGE-LOGICAL-BINDING");
        reject(
            [](auto& p) {
                p.reflection.descriptorBindings.push_back(p.reflection.descriptorBindings[0]);
            },
            "shader reflection binding is duplicated");
        auto changed = ShaderPackageStateCopy::copy(fragment);
        changed.reflection.pushConstantStages =
            core::pmr::String(80u, 'X', &core::defaultCpuMemoryResource());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        const auto invalid = GraphicsShaderInterfaceMerger::merge(1u, vertex, changed, merged);
        std::pmr::set_default_resource(previous);
        assert(!invalid.success && merged.entrypoints.empty());
        changed = ShaderPackageStateCopy::copy(fragment);
        changed.reflection.descriptorBindings[0].binding = 9u;
        assert(GraphicsShaderInterfaceMerger::merge(1u, vertex, changed, merged).success);
        assert(merged.reflection.descriptorBindings.size() == 2u);
        // One selected stage can legitimately have no push declaration.
        changed = ShaderPackageStateCopy::copy(vertex);
        changed.reflection.pushConstantByteCount = 0u;
        changed.reflection.pushConstantMembers.clear();
        changed.reflection.pushConstantStages.clear();
        assert(GraphicsShaderInterfaceMerger::merge(1u, changed, fragment, merged).success);
        assert(merged.reflection.pushConstantStages == "FS"
            && merged.reflection.pushConstantByteCount == 128u);
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        auto factory = [&] {
            try {
                core::CpuMemoryQuotaScope scope(quota);
                auto result = core::makeUnique<ShaderPackageDesc>();
                if (GraphicsShaderInterfaceMerger::merge(1u, vertex, fragment, *result).success)
                    return result;
            } catch (const std::bad_alloc&) {
            }
            return core::UniquePtr<ShaderPackageDesc>{};
        };
        CpuQuotaFactoryFixture pressure(quota);
        const auto peak = pressure.measure(factory);
        pressure.verify(peak, factory);
        previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        auto exact = factory();
        std::pmr::set_default_resource(previous);
        assert(exact);
        exact.reset();
        assert(!quota->liveBytes());
        const auto mismatch = [&](auto change) {
            auto copy = ShaderReflectionStateCopy::copy(vertex.reflection);
            change(copy.pushConstantMembers[0]);
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{quota}, [&]() noexcept -> const char* {
                    assert(ShaderPushConstantAbiIdentity::matchesMembers(
                        vertex.reflection.pushConstantMembers,
                        vertex.reflection.pushConstantMembers));
                    assert(!ShaderPushConstantAbiIdentity::matchesMembers(
                        vertex.reflection.pushConstantMembers, copy.pushConstantMembers));
                    return nullptr;
                }));
        };
        mismatch([](auto& m) {
            m.name += "changed";
        });
        mismatch([](auto& m) {
            m.scalarType += "changed";
        });
        mismatch([](auto& m) {
            ++m.scalarWidth;
        });
        mismatch([](auto& m) {
            ++m.vecSize;
        });
        mismatch([](auto& m) {
            ++m.matrixRows;
        });
        mismatch([](auto& m) {
            ++m.matrixCols;
        });
        mismatch([](auto& m) {
            ++m.matrixStride;
        });
        mismatch([](auto& m) {
            m.rowMajor = !m.rowMajor;
        });
        mismatch([](auto& m) {
            m.arrayDims.push_back(2u);
        });
        mismatch([](auto& m) {
            ++m.arrayStride;
        });
        mismatch([](auto& m) {
            ++m.offset;
        });
        mismatch([](auto& m) {
            ++m.size;
        });
        mismatch([](auto& m) {
            ++m.decorations;
        });
        std::printf(
            "BINDLESS_RHI_GRAPHICS_INTERFACE_MERGE_OK %llu full-abi binding-union no-partial-output hostile-pmr hot-allocation0\n",
            static_cast<unsigned long long>(peak));
    }
};
} // namespace arp::test
