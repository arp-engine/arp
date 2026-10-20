#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/frontend/resource_texture_validation.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalStorageContentSequenceFixture final {
public:
    static void verify()
    {
        using C = rhi::OriginalTextureContentCellSequence;
        using A = C::Action;
        rhi::TextureDesc desc;
        desc.extent = {4u, 4u, 1u};
        desc.mipLevelCount = 3u;
        desc.format = rhi::TextureFormat::Rgba16Float;
        const char* id = nullptr;
        const auto geometry = rhi::semantics::OriginalTextureStateRange::capture(desc, 3u, id);
        assert(geometry && !id);
        const rhi::GpuResourceId resource{71u};
        const rhi::TextureSubresourceRange mip0{0u, 1u, 0u, 1u, rhi::TextureAspect::Color};
        auto mip1 = mip0;
        mip1.baseMipLevel = 1u;
        // A real full attachment clear/store establishes initialization. A
        // partial shader write preserves it, then a later command may sample.
        std::array<C::Step, 4> steps{{{resource, 17u, mip0, A::Clear, 0u},
            {resource, 17u, mip0, A::Store, 0u},
            {resource, 17u, mip0, A::StorageWrite, SIZE_MAX},
            {resource, 17u, mip0, A::SampledRead, SIZE_MAX}}};
        C::Budget budget{4u, true, 3u, steps.size(), steps.size()};
        const auto inspect = [&](bool initialized = false, std::uint64_t cell = 0u) noexcept {
            return C::inspect(*geometry,
                resource,
                17u,
                cell,
                initialized,
                steps.size(),
                steps.size(),
                budget,
                [&](std::size_t command) noexcept -> std::optional<C::Step> {
                    return steps[command];
                });
        };
        const auto reject = [&](const char* expected) {
            const auto result = inspect();
            assert(result.diagnostic && std::string_view(result.diagnostic) == expected
                && !result.stored && !result.writes && !result.steps && !result.commandVisits);
        };
        auto result = inspect();
        assert(!result.diagnostic && result.stored && result.writes == 2u
            && result.steps == steps.size() && !result.nativeExecutionProven());
        result = inspect(false, 1u);
        assert(!result.diagnostic && !result.stored && !result.writes);
        for (auto version : {2u, 3u}) {
            budget.contractVersion = version;
            reject("RHI-ORIGINAL-CONTENT-SEQUENCE-STORAGE-PROFILE");
        }
        budget.contractVersion = 4u;
        steps[0].action = A::DontCare;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-WRITE-UNINITIALIZED");
        steps[0].action = A::Clear;
        steps[1].action = A::Discard;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-WRITE-UNINITIALIZED");
        steps[1].action = A::Store;
        steps[2].passBegin = 2u;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-STORAGE-PASS");
        steps[2].passBegin = SIZE_MAX;
        steps[2].resourceGeneration = 18u;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-GENERATION");
        steps[2].resourceGeneration = 17u;
        --budget.maxSteps;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-STEP-CAPACITY");
        ++budget.maxSteps;
        --budget.maxCommandVisits;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-VISIT-CAPACITY");
        ++budget.maxCommandVisits;
        budget.prototypeOptIn = false;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-OPT-IN");
        budget.prototypeOptIn = true;
        // Initialization of one mip cannot establish its neighbor's contents.
        steps[2].range = mip1;
        const auto neighbor = inspect(false, 1u);
        assert(neighbor.diagnostic
            && std::string_view(neighbor.diagnostic)
                == "RHI-ORIGINAL-CONTENT-SEQUENCE-WRITE-UNINITIALIZED"
            && !neighbor.writes);
        steps[2].range = mip0;
        // Existing committed contents need no gratuitous re-clear. Repeated
        // writes are real mutations; read-only cells stay unmodified.
        steps[0].action = steps[1].action = A::SampledRead;
        steps[0].passBegin = steps[1].passBegin = SIZE_MAX;
        result = inspect(true);
        assert(!result.diagnostic && result.stored && result.writes == 1u);
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-READ-UNINITIALIZED");
        steps[0].action = A::StorageWrite;
        result = inspect();
        assert(result.diagnostic
            && std::string_view(result.diagnostic)
                == "RHI-ORIGINAL-CONTENT-SEQUENCE-WRITE-UNINITIALIZED");
        steps[0].action = A::Clear;
        steps[0].passBegin = 0u;
        steps[1].action = A::StorageWrite;
        reject("RHI-ORIGINAL-CONTENT-SEQUENCE-WRITE-ACTIVE-ATTACHMENT");
        steps[1].action = A::Store;
        steps[1].passBegin = 0u;
        // Projection order cannot hide same-dispatch read/write feedback on
        // an overlapping cell. Different mips remain independent.
        for (bool writeFirst : {false, true}) {
            result = C::inspectProjected(*geometry,
                resource,
                17u,
                0u,
                true,
                1u,
                1u,
                budget,
                [&](std::size_t, auto&& emit) noexcept -> const char* {
                    auto read = steps[3], write = steps[2];
                    if (const auto* diagnostic = emit(writeFirst ? write : read))
                        return diagnostic;
                    return emit(writeFirst ? read : write);
                });
            assert(result.diagnostic
                && std::string_view(result.diagnostic)
                    == "RHI-ORIGINAL-CONTENT-SEQUENCE-STORAGE-FEEDBACK"
                && !result.writes && !result.steps);
            result = C::inspectProjected(*geometry,
                resource,
                17u,
                0u,
                true,
                1u,
                1u,
                budget,
                [&](std::size_t, auto&& emit) noexcept -> const char* {
                    auto write = steps[2];
                    write.range = mip1;
                    if (const auto* diagnostic = emit(write))
                        return diagnostic;
                    return emit(steps[3]);
                });
            assert(!result.diagnostic && result.stored && !result.writes && result.steps == 1u);
        }
        steps[3].action = A::ReadbackRead;
        assert(!inspect().diagnostic && inspect().writes == 2u);
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array<std::shared_ptr<core::CpuMemoryQuota>, 0u>{}, [&]() noexcept -> const char* {
                return inspect().diagnostic;
            }));
        // Color-only storage writes cannot authorize depth or stencil stores.
        desc.format = rhi::TextureFormat::D32Float;
        const auto depth = rhi::semantics::OriginalTextureStateRange::capture(desc, 3u, id);
        assert(depth);
        auto invalid = steps[2];
        invalid.range.aspect = rhi::TextureAspect::Depth;
        result = C::inspect(*depth,
            resource,
            17u,
            0u,
            true,
            1u,
            1u,
            budget,
            [&](std::size_t) noexcept -> std::optional<C::Step> {
                return invalid;
            });
        assert(result.diagnostic
            && std::string_view(result.diagnostic)
                == "RHI-ORIGINAL-CONTENT-SEQUENCE-STORAGE-ASPECT");
        for (auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto lowering = C::lowering(backend, 4u);
            assert(lowering.contractVersion == 4u && !lowering.executionAvailable
                && lowering.diagnostic && lowering.validatorOnly == (backend == rhi::Backend::Null)
                && lowering.validationAvailable == (backend == rhi::Backend::Null));
        }
        assert(!C::lowering(static_cast<rhi::Backend>(99u), 4u).validationAvailable);
        std::puts(
            "BINDLESS_RHI_STORAGE_CONTENT_SEQUENCE4_OK initialized-only partial-store-preserves mip-isolation no-feedback same-timeline bounded hot0 native-closed");
    }
};
} // namespace arp::test
