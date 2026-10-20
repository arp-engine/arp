#pragma once
#include "cpu_quota_factory_fixture.h"
#include "original_shader_artifact_owner_fixture.h"
#include "shader/graphics_shader_module_artifact.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class GraphicsShaderModuleArtifactFixture final {
public:
    static void verify(const std::filesystem::path& publication)
    {
        verify(publication, [](const auto&) {});
    }

    template <class OnLoaded>
    static void verify(const std::filesystem::path& publication, OnLoaded&& onLoaded)
    {
        using namespace shader;
        verifyReflectionAllocator();
        GraphicsShaderModuleLoadBudget budget{
            1024u * 1024u, 16u * 1024u * 1024u, 4u * 1024u * 1024u, 4u, 4u, 256u, 2u};
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        diagnostic.reserve(256u);
        auto factory = [&] {
            return GraphicsShaderModuleArtifact::load(publication, budget, quota, diagnostic);
        };
        auto expect = [&](core::StringView id) {
            const auto before = quota->liveBytes();
            const auto value = factory();
            if (value || diagnostic.find(id) == diagnostic.npos)
                std::printf("MODULE_EXPECT %.*s GOT %s\n",
                    static_cast<int>(id.size()),
                    id.data(),
                    diagnostic.c_str());
            assert(
                !value && diagnostic.find(id) != diagnostic.npos && quota->liveBytes() == before);
        };
        auto value = factory();
        if (!value)
            std::printf("MODULE_LOAD_FAILURE %s\n", diagnostic.c_str());
        assert(value && value->identity().size() == core::Sha256::digestByteCount);
        const auto total = value->inputBytes();
        assert(value->quota() == quota && value->vertex()->quota() == quota
            && value->fragment()->quota() == quota);
        assert(value->vertex()->stage() == ShaderStage::Vertex
            && value->fragment()->stage() == ShaderStage::Fragment);
        assert(value->bytes(ShaderArtifactRole::Source)
            == value->vertex()->bytes(ShaderArtifactRole::Source));
        assert(value->bytes(ShaderArtifactRole::Source)
            == value->fragment()->bytes(ShaderArtifactRole::Source));
        assert(value->bytes(ShaderArtifactRole::Wgsl)
            != value->vertex()->bytes(ShaderArtifactRole::Wgsl));
        assert(value->bytes(ShaderArtifactRole::Wgsl)
            != value->fragment()->bytes(ShaderArtifactRole::Wgsl));
        value.reset();
        assert(!quota->liveBytes());
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        value = factory();
        std::pmr::set_default_resource(previous);
        if (!value)
            std::printf("MODULE_HOSTILE_DEFAULT_FAILURE %s\n", diagnostic.c_str());
        assert(value);
        value.reset();
        assert(!quota->liveBytes());
        budget.maxTotalInputBytes = total;
        value = factory();
        assert(value);
        value.reset();
        --budget.maxTotalInputBytes;
        expect("FILE-READ-FAILED");
        ++budget.maxTotalInputBytes;
        --budget.maxCpuMetadataBytes;
        expect("CPU-QUOTA");
        ++budget.maxCpuMetadataBytes;
        budget.maxModuleBindings = 0u;
        expect("BINDING-BUDGET");
        budget.maxModuleBindings = 4u;
        budget.maxEntryBindings = 0u;
        expect("BINDING-BUDGET");
        budget.maxEntryBindings = 4u;
        budget.maxReferenceDepth = 1u;
        expect("REFERENCE-PATH");
        budget.maxReferenceDepth = 2u;
        budget.maxReferencePathBytes = 1u;
        expect("REFERENCE-PATH");
        budget.maxReferencePathBytes = 256u;
        const auto originalFileCap = budget.maxFileBytes;
        budget.maxFileBytes = 1u;
        expect("FILE-READ-FAILED");
        budget.maxFileBytes = originalFileCap;
        // The nested graph uses one shared aggregate quota, including children.
        CpuQuotaFactoryFixture admission(quota);
        const auto peak = admission.measure(factory);
        admission.verify(peak, factory);
        {
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes());
            assert(pressure.charged());
            expect("CPU-ALLOCATION");
        }
        value = factory();
        assert(value);
        OriginalShaderArtifactOwnerFixture::module(value);
        onLoaded(value);
        auto child = value->vertex();
        std::weak_ptr<const GraphicsShaderModuleArtifact> observer = value;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease pressure(quota, quota->limitBytes() - quota->liveBytes());
            assert(pressure.charged() && value->identity().size() == 32u);
            assert(value->vertex()->entry() == "vertexMain"
                && value->fragment()->entry() == "fragmentMain");
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        }
        value.reset();
        assert(observer.expired() && quota->liveBytes() && child->stage() == ShaderStage::Vertex);
        assert(!SingleEntryShaderArtifact::validateOriginal(child));
        child.reset();
        observer.reset();
        assert(!quota->liveBytes());
        std::printf("BINDLESS_RHI_GRAPHICS_MODULE_ARTIFACT_OK %llu %llu\n",
            static_cast<unsigned long long>(total),
            static_cast<unsigned long long>(peak));
    }

private:
    static void verifyReflectionAllocator()
    {
        // Pure cache-parser allocation fixture; no executable shader authority.
        const core::pmr::String source(R"yaml(moduleName: AllocatorFixture
entryPoints:
  - name: allocatorOwnedLongComputeEntryNameThatExceedsNativeAndWasmSmallStringStorage
    stage: Compute
bindings:
  - name: allocatorOwnedLongResourceNameThatExceedsNativeAndWasmSmallStringStorage
    type: StorageBuffer
    set: 0
    binding: 3
    arrayCount: 1
    stageMask: 4
webGpuBindingSpace:
  - name: allocatorOwnedLongPhysicalNameThatExceedsNativeAndWasmSmallStringStorage
    set: 0
    binding: 3
    group: 0
    webGpuBinding: 0
    active: true
    physicalType: StorageBuffer
    arrayElement: 0
    capacity: 1
)yaml",
            &core::defaultCpuMemoryResource());
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        {
            core::CpuMemoryQuotaScope scope(quota);
            auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
            auto parsed = shader::ShaderReflectionCache::loadFromYaml(source);
            std::pmr::set_default_resource(previous);
            assert(parsed.success && parsed.module.reflection.entryPoints.size() == 1u);
            const auto& reflection = parsed.module.reflection;
            assert(reflection.entryPoints[0].name.get_allocator().resource()
                == &core::defaultCpuMemoryResource());
            assert(reflection.bindings[0].name.get_allocator().resource()
                == &core::defaultCpuMemoryResource());
            assert(reflection.webGpuBindingSpace[0].name.get_allocator().resource()
                == &core::defaultCpuMemoryResource());
            assert(quota->liveBytes());
        }
        assert(!quota->liveBytes());
    }
};
} // namespace arp::test
