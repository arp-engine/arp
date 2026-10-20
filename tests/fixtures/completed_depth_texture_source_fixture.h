#pragma once
#include "cpu_quota_factory_fixture.h"
#include "rhi/backends/webgpu/webgpu_canonical_native_closure.h"
#include "rhi/backends/webgpu/webgpu_completed_texture_publication.h"
#include "rhi/frontend/completed_depth_texture_source.h"
#include "rhi/frontend/transfer_upload_destination_validation.h"

namespace arp::test {
// Consumes the existing genuine clear/store journal. Never issues completion.
class CompletedDepthTextureSourceFixture final {
public:
    static void verify(
        const std::shared_ptr<const rhi::OriginalCommandTextureViewSource>& view, bool completed)
    {
        using Source = rhi::OriginalCompletedDepthTextureSource;
        using Contract = rhi::OriginalCompletedTextureDescriptorContract;
        const auto& quota = view->originalMetadataQuota();
        const auto baseline = quota->liveBytes();
        {
            const Source::Budget budget{1u,
                true,
                view->originalObject()->desc().mipLevelCount,
                quota->limitBytes(),
                Source::completeRetainedOwnerCount};
            const char* id = nullptr;
            const auto factory = [&] {
                return Source::prepareOriginal(view, budget, quota, id);
            };
            if (!completed) {
                assert(!factory() && id && quota->liveBytes() == baseline);
                return;
            }
            auto wrong = budget;
            wrong.maxCells = 0u;
            assert(!Source::prepareOriginal(view, wrong, quota, id));
            wrong = budget;
            --wrong.maxRetainedOwners;
            assert(!Source::prepareOriginal(view, wrong, quota, id));
            wrong = budget;
            wrong.prototypeOptIn = false;
            assert(!Source::prepareOriginal(view, wrong, quota, id));
            const auto foreign = core::makeShared<core::CpuMemoryQuota>(quota->limitBytes());
            assert(!Source::prepareOriginal(view, budget, foreign, id));
            const auto forgedView =
                std::shared_ptr<const rhi::OriginalCommandTextureViewSource>(foreign, view.get());
            assert(!Source::prepareOriginal(forgedView, budget, quota, id));
            assert(quota->liveBytes() == baseline && !foreign->liveBytes());
            CpuQuotaFactoryFixture pressure(quota);
            const auto bytes = pressure.measureSingleUse([] {}, factory);
            pressure.verifySingleUse(bytes, [] {}, factory);
            auto source = factory();
            if (!source)
                std::fprintf(stderr, "completed depth source: %s\n", id ? id : "missing");
            assert(source && !id && !Source::validateOriginal(source)
                && !source->nativeExecutionProven());
            assert(source->cells().size() == budget.maxCells);
            using Read = rhi::OriginalCompletedDepthReadInputContract;
            std::uint64_t texels = 0u;
            assert(!rhi::semantics::TextureUploadDestinationValidation::footprint(
                view->originalParentLease()->originalObject()->desc(), texels));
            const Read::Budget readBudget{
                1u, true, budget.maxCells, texels, Source::completeRetainedOwnerCount};
            const Read::Input read{source,
                rhi::ResourceState::ShaderRead,
                rhi::PipelineStage::ComputeShader,
                rhi::ResourceAccess::ShaderRead};
            const auto positive = Read::inspect(read, view, readBudget);
            assert(!positive.diagnostic && positive.cells == source->cells().size()
                && positive.textureTexelBytes == texels && !positive.nativeExecutionProven());
            const auto refused = [&](auto input, auto canonical, auto limits) {
                const auto result = Read::inspect(input, canonical, limits);
                assert(result.diagnostic && !result.cells && !result.textureTexelBytes);
            };
            auto shortRead = readBudget;
            --shortRead.maxCells;
            refused(read, view, shortRead);
            shortRead = readBudget;
            --shortRead.maxTextureTexelBytes;
            refused(read, view, shortRead);
            shortRead = readBudget;
            --shortRead.maxRetainedOwners;
            refused(read, view, shortRead);
            refused(read, forgedView, readBudget);
            auto wrongRead = read;
            wrongRead.source = {};
            refused(wrongRead, view, readBudget);
            wrongRead = read;
            wrongRead.source = std::shared_ptr<const Source>(foreign, source.get());
            refused(wrongRead, view, readBudget);
            wrongRead = read;
            wrongRead.stages = rhi::PipelineStage::VertexShader;
            refused(wrongRead, view, readBudget);
            auto otherView =
                rhi::OriginalCommandTextureViewSource::prepareOriginal(view->originalViewLease(),
                    view->originalParentLease(),
                    {1u,
                        true,
                        rhi::OriginalCommandTextureViewSource::completeRetainedOwnerCount,
                        quota->limitBytes()},
                    quota,
                    id);
            assert(otherView && !id && otherView->resource() == view->resource());
            refused(read, otherView, readBudget);
            otherView.reset();
            const auto limit = quota->limitBytes();
            quota->setLimitBytes(limit - 1u);
            assert(Source::validateOriginal(source));
            quota->setLimitBytes(limit);
            // A captured source must continue to cover the exact visible range.
            auto& descriptor = const_cast<rhi::TextureViewDesc&>(view->originalObject()->desc());
            const auto mip = descriptor.baseMipLevel;
            descriptor.baseMipLevel =
                (mip + 1u) % view->originalParentLease()->originalObject()->desc().mipLevelCount;
            assert(Source::validateOriginal(source));
            refused(read, view, readBudget);
            descriptor.baseMipLevel = mip;
            assert(!Source::validateOriginal(source));
            assert(!Contract::validateSource(source, 3u));
            assert(Contract::validateSource(source, 2u));
            const auto forged = std::shared_ptr<const Source>(foreign, source.get());
            assert(Source::validateOriginal(forged));
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
            using namespace rhi_webgpu;
            WebGpuNativeDescriptorBindingPayload binding;
            binding.binding = {
                0u, rhi::DescriptorType::SampledTexture, rhi::ShaderStage::Fragment, 1u};
            binding.binding.depthTexture = true;
            binding.binding.textureViewDimensionConstrained = true;
            binding.binding.textureViewDimension = rhi::TextureViewDimension::Texture2D;
            binding.textureView = webGpuNativePayloadFor(*view->originalObject());
            binding.write.binding = 0u;
            binding.write.type = rhi::DescriptorType::SampledTexture;
            binding.write.resource = view->resource();
            binding.write.resourceKind = rhi::DescriptorResourceKind::TextureView;
            binding.write.resourceState = rhi::ResourceState::ShaderRead;
            const WebGpuCompletedTexturePublication publication{binding.write, view, {}, source};
            assert(!validateWebGpuCompletedTexturePublication(publication, binding));
            assert(!WebGpuCanonicalNativeClosure::validateCompletedDepthTextureInput(
                read, view, readBudget));
            auto fragmentRead = read;
            fragmentRead.stages = rhi::PipelineStage::FragmentShader;
            assert(!WebGpuCanonicalNativeClosure::validateCompletedDepthTextureInput(
                fragmentRead, view, readBudget));
            assert(WebGpuCanonicalNativeClosure::validateCompletedDepthTextureInput(
                read, forgedView, readBudget));
            auto invalid = publication;
            invalid.depthContent = forged;
            assert(validateWebGpuCompletedTexturePublication(invalid, binding));
            invalid = publication;
            invalid.source = forgedView;
            assert(validateWebGpuCompletedTexturePublication(invalid, binding));
            auto color = binding;
            color.binding.depthTexture = false;
            assert(validateWebGpuCompletedTexturePublication(publication, color));
            const auto nativeParent = binding.textureView->parentTexture;
            binding.textureView->parentTexture =
                std::shared_ptr<WebGpuNativeTexturePayload>(foreign, nativeParent.get());
            assert(validateWebGpuCompletedTexturePublication(publication, binding));
            assert(WebGpuCanonicalNativeClosure::validateCompletedDepthTextureInput(
                read, view, readBudget));
            binding.textureView->parentTexture = nativeParent;
            auto admission = core::makeShared<core::CpuMemoryQuota>(quota->limitBytes());
            {
                core::CpuMemoryQuotaScope scope(admission);
                rhi::DescriptorLayoutDesc layout;
                layout.bindings.push_back(binding.binding);
                core::pmr::Vector<WebGpuNativeDescriptorBindingPayload> bindings;
                bindings.push_back(binding);
                core::pmr::Vector<WebGpuOriginalBufferRangePublication> ranges;
                core::pmr::Vector<WebGpuCompletedTexturePublication> publications;
                publications.push_back(publication);
                const auto metadata = [&] {
                    try {
                        return createWebGpuNativeOriginalCompletedTextureDescriptorPayload(
                            layout, bindings, ranges, publications);
                    } catch (const std::bad_alloc&) {
                        return std::shared_ptr<WebGpuNativeDescriptorGroupPayload>{};
                    }
                };
                CpuQuotaFactoryFixture allocation(admission);
                const auto minimum = allocation.measureSingleUse([] {}, metadata);
                allocation.verifySingleUse(minimum, [] {}, metadata);
                auto retained = metadata();
                assert(retained && !retained->group
                    && retained->completedTexturePublications.size() == 1u);
                assert(core::SharedOwnerProjection::sameOwner(
                    retained->completedTexturePublications.front().depthContent, source));
                assert(!createWebGpuNativeDescriptorGroupPayload(layout, bindings));
                auto duplicate = publications;
                duplicate.push_back(publication);
                assert(!createWebGpuNativeOriginalCompletedTextureDescriptorPayload(
                    layout, bindings, ranges, duplicate));
            }
            assert(!admission->liveBytes());
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{quota}, [&]() noexcept -> const char* {
                    if (const auto* diagnostic = Source::validateOriginal(source))
                        return diagnostic;
                    if (const auto* diagnostic =
                            WebGpuCanonicalNativeClosure::validateCompletedDepthTextureInput(
                                read, view, readBudget))
                        return diagnostic;
                    return validateWebGpuCompletedTexturePublication(publication, binding);
                }));
#endif
            std::printf(
                "COMPLETED_DEPTH_SOURCE1_OK mip%u layer%u cells%llu exact-less-exhausted alias-foreign-refused hot0 native-metadata-only execution-closed\n",
                view->originalObject()->desc().baseMipLevel,
                view->originalObject()->desc().baseArrayLayer,
                static_cast<unsigned long long>(source->cells().size()));
        }
        assert(quota->liveBytes() == baseline);
    }
};
} // namespace arp::test
