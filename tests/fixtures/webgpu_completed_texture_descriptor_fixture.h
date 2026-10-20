#pragma once
#include "rhi/backends/webgpu/webgpu_completed_texture_publication.h"
#include "webgpu_canonical_texture_upload_source_fixture.h"
#include "webgpu_queue_readback_wait_fixture.h"

namespace arp::test {
class WebGpuCompletedTextureDescriptorFixture final {
public:
    template <class Factory> static void verify(Factory&& create)
    {
        std::shared_ptr<core::CpuMemoryQuota> publicationQuota;
        WebGpuCanonicalTextureUploadSourceFixture::verifyCompletedDescriptorPublication(
            create, [&](auto& root, auto& context, auto& proof, auto& original) {
                using namespace rhi;
                using namespace rhi_webgpu;
                const auto fresh = original->originalDestinationLease()
                                       ->originalObject()
                                       ->observeOriginalContent();
                assert(fresh && !fresh->contentGeneration());
                WebGpuCanonicalUploadProjectionFixture::verifyExecution(root,
                    *context.device,
                    proof,
                    *context.loss,
                    original,
                    WebGpuQueueReadbackWaitFixture::queue,
                    WebGpuQueueReadbackWaitFixture::map,
                    0u,
                    &context.device);
                const auto& parent = original->originalDestinationLease();
                const auto content = parent->originalObject()->observeOriginalContent();
                assert(content && content->controlVersion() == 1u
                    && content->contentGeneration() == 2u);
                auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
                publicationQuota = quota;
                auto admission = core::makeShared<core::CpuMemoryQuota>(65536u);
                std::weak_ptr<const OriginalCommandTextureViewSource> retired;
                std::shared_ptr<WebGpuNativeDescriptorGroupPayload> retained;
                {
                    std::puts("COMPLETED_TEXTURE_PUBLICATION2_PHASE create-view-arena-set");
                    core::CpuMemoryQuotaScope scope(quota);
                    TextureViewDesc desc;
                    desc.format = TextureFormat::Rgba8Unorm;
                    desc.usage = TextureUsage::Sampled;
                    desc.mipLevelCount = 2u;
                    auto view = context.device->createTextureView(*parent->originalObject(), desc);
                    assert(view);
                    const char* id = nullptr;
                    auto lease = OriginalCommandTextureViewLease::prepareOriginal(
                        view, {1u, true, 2u, 65536u}, context.quotas[5], id);
                    assert(lease && !id);
                    auto source = OriginalCommandTextureViewSource::prepareOriginal(
                        lease, parent, {1u, true, 5u, 65536u}, context.quotas[5], id);
                    assert(source && !id);
                    retired = source;
                    DescriptorLayoutDesc layout;
                    layout.name = "completed WebGPU upload publication2";
                    DescriptorBindingDesc binding{
                        0u, DescriptorType::SampledTexture, ShaderStage::Fragment, 1u};
                    binding.textureViewDimensionConstrained = true;
                    binding.textureViewDimension = TextureViewDimension::Texture2D;
                    layout.bindings.push_back(binding);
                    auto arena = core::shareUnique(
                        context.device->createOriginalDescriptorArena({layout.name, 1u, 1u}));
                    assert(arena);
                    auto set =
                        context.device->allocateOriginalDescriptorSet(arena, {layout.name, layout});
                    assert(set);
                    DescriptorBindingWrite write;
                    write.binding = 0u;
                    write.type = DescriptorType::SampledTexture;
                    write.resource = source->resource();
                    write.resourceKind = DescriptorResourceKind::TextureView;
                    write.resourceState = ResourceState::ShaderRead;
                    OriginalCompletedTextureDescriptorRequest request{2u, true, 1u, 1u, 5u};
                    const auto revision = set->contentRevision();
                    std::puts("COMPLETED_TEXTURE_PUBLICATION2_PHASE negative-publication");
                    assert(!set->write(write).success);
                    assert(
                        !set->writeOriginalCompletedTexture(
                                *context.device, write, source, *fresh, request)
                            .success);
                    auto legacy = request;
                    legacy.contractVersion = 1u;
                    assert(
                        !set->writeOriginalCompletedTexture(
                                *context.device, write, source, *content, legacy)
                            .success);
                    auto forged = std::shared_ptr<const OriginalCommandTextureViewSource>(
                        quota, source.get());
                    assert(
                        !set->writeOriginalCompletedTexture(
                                *context.device, write, forged, *content, request)
                            .success);
                    forged.reset();
                    {
                        std::puts("COMPLETED_TEXTURE_PUBLICATION2_PHASE exhausted-publication");
                        core::CpuMemoryQuotaScope pressureScope(admission);
                        core::CpuMemoryQuotaLease pressure(admission, admission->limitBytes());
                        assert(pressure.charged());
                        const auto failed = set->writeOriginalCompletedTexture(
                            *context.device, write, source, *content, request);
                        assert(!failed.success && failed.diagnostics == "RHI-OOM");
                        const auto repeated = set->writeOriginalCompletedTexture(
                            *context.device, write, source, *content, request);
                        assert(!repeated.success && repeated.diagnostics == "RHI-OOM");
                    }
                    assert(set->contentRevision() == revision && !set->hasWrittenBinding(0u)
                        && !webGpuNativePayloadFor(*set));
                    std::puts("COMPLETED_TEXTURE_PUBLICATION2_PHASE real-publication");
                    const auto published = set->writeOriginalCompletedTexture(
                        *context.device, write, source, *content, request);
                    if (!published.success)
                        std::fprintf(
                            stderr, "completed publication2: %s\n", published.diagnostics.c_str());
                    assert(published.success && set->contentRevision() == revision + 1u
                        && set->validateBindingElement(0u, 0u).success);
                    retained = webGpuNativePayloadFor(*set);
                    assert(retained && !retained->group
                        && retained->completedTexturePublications.size() == 1u);
                    assert(retained->ordinaryTextureAccessDiagnostic());
                    const auto& publication = retained->completedTexturePublications.front();
                    auto invalid = retained->bindings.front();
                    const auto originalBinding = invalid;
                    for (unsigned field = 0u; field != 7u; ++field) {
                        invalid = originalBinding;
                        if (field == 0u)
                            invalid.binding.type = DescriptorType::StorageTexture;
                        if (field == 1u)
                            ++invalid.binding.binding;
                        if (field == 2u)
                            ++invalid.arrayElement;
                        if (field == 3u)
                            invalid.binding.dynamicOffset = true;
                        if (field == 4u)
                            invalid.binding.depthTexture = true;
                        if (field == 5u)
                            invalid.write.bufferBindingRange = {2u, 0u, 4u};
                        if (field == 6u)
                            invalid.binding.textureViewDimension =
                                TextureViewDimension::Texture2DArray;
                        assert(validateWebGpuCompletedTexturePublication(publication, invalid));
                    }
                    core::pmr::Vector<WebGpuOriginalBufferRangePublication> ranges;
                    auto invalidLayout = layout;
                    invalidLayout.bindings.front().sampledTextureFilterable = false;
                    assert(
                        !createWebGpuNativeOriginalCompletedTextureDescriptorPayload(invalidLayout,
                            retained->bindings,
                            ranges,
                            retained->completedTexturePublications));
                    auto duplicates = retained->completedTexturePublications;
                    duplicates.push_back(publication);
                    assert(!createWebGpuNativeOriginalCompletedTextureDescriptorPayload(
                        layout, retained->bindings, ranges, duplicates));
                    const auto native = retained->bindings[0u].textureView;
                    const auto nativeParent = native->parentTexture;
                    ++native->creationKey.baseMipLevel;
                    assert(!set->validateBindingElement(0u, 0u).success);
                    --native->creationKey.baseMipLevel;
                    native->parentTexture =
                        std::shared_ptr<WebGpuNativeTexturePayload>(quota, nativeParent.get());
                    assert(!set->validateBindingElement(0u, 0u).success);
                    native->parentTexture = nativeParent;
                    assert(set->validateBindingElement(0u, 0u).success);
                    const auto factory = [&] {
                        core::CpuMemoryQuotaScope admissionScope(admission);
                        return createWebGpuNativeOriginalCompletedTextureDescriptorPayload(layout,
                            retained->bindings,
                            ranges,
                            retained->completedTexturePublications);
                    };
                    CpuQuotaFactoryFixture allocation(admission);
                    std::puts("COMPLETED_TEXTURE_PUBLICATION2_PHASE exact-less-factory");
                    // The metadata factory is allowed to throw allocation failure;
                    // the public descriptor boundary converts it before publishing.
                    const auto guarded = [&] {
                        try {
                            return factory();
                        } catch (const std::bad_alloc&) {
                            return std::shared_ptr<WebGpuNativeDescriptorGroupPayload>{};
                        }
                    };
                    // Earlier exhaustion raised historical peak to the limit.
                    // Reuse the history-independent exact threshold search.
                    const auto measured = allocation.measureSingleUse([] {}, guarded);
                    allocation.verifySingleUse(measured, [] {}, guarded);
                    assert(!admission->liveBytes());
                    assert(!createWebGpuNativeDescriptorGroupPayload(layout, retained->bindings));
                    assert(!set->write(write).success && set->contentRevision() == revision + 1u);
                    std::puts("COMPLETED_TEXTURE_PUBLICATION2_PHASE hot-validation");
                    assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        std::array{quota, admission}, [&]() noexcept -> const char* {
                            return set->validateBindingElement(0u, 0u).success
                                ? nullptr
                                : "completed publication invalid";
                        }));
                    std::puts(
                        "BINDLESS_RHI_WEBGPU_COMPLETED_TEXTURE_DESCRIPTOR_OK publication2 control1 generation2 pixels20 mips2 same-parent-registry stale-forged-mutated-refused atomic-oom exact-less-quota hot0 ordinary-closed no-sample no-execution");
                    // Publication6 retains its own profile in the original set.
                    // The old payload remains immutable; raw export of the new
                    // profile is closed until its typed Graphics Root exists.
                    auto canonicalRequest = request;
                    canonicalRequest.contractVersion = 6u;
                    {
                        core::CpuMemoryQuotaScope pressureScope(admission);
                        core::CpuMemoryQuotaLease pressure(admission, admission->limitBytes());
                        assert(pressure.charged());
                        const auto failed = set->writeOriginalCompletedTexture(
                            *context.device, write, source, *content, canonicalRequest);
                        assert(!failed.success && failed.diagnostics == "RHI-OOM");
                    }
                    assert(set->contentRevision() == revision + 1u
                        && core::SharedOwnerProjection::sameOwner(
                            retained, webGpuNativePayloadFor(*set)));
                    assert(
                        set->writeOriginalCompletedTexture(
                               *context.device, write, source, *content, canonicalRequest)
                            .success);
                    assert(set->contentRevision() == revision + 2u
                        && set->validateBindingElement(0u, 0u).success
                        && !webGpuNativePayloadFor(*set));
                    assert(publication.colorPublicationVersion == 2u);
                    auto canonical = publication;
                    canonical.colorPublicationVersion = 6u;
                    assert(!validateWebGpuCompletedTexturePublication(canonical, originalBinding));
                    auto copied = publication;
                    copied = canonical;
                    assert(copied.colorPublicationVersion == 6u);
                    copied.colorPublicationVersion = 5u;
                    assert(validateWebGpuCompletedTexturePublication(copied, originalBinding));
                    core::pmr::Vector<WebGpuCompletedTexturePublication> canonicalPublications;
                    canonicalPublications.push_back(canonical);
                    const auto canonicalPayload =
                        createWebGpuNativeOriginalCompletedTextureDescriptorPayload(
                            layout, retained->bindings, ranges, canonicalPublications);
                    assert(canonicalPayload && !canonicalPayload->group
                        && canonicalPayload->ordinaryTextureAccessDiagnostic()
                        && canonicalPayload->completedTexturePublications.front()
                                .colorPublicationVersion
                            == 6u);
                    assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        std::array{quota, admission}, [&]() noexcept -> const char* {
                            return set->validateBindingElement(0u, 0u).success
                                ? nullptr
                                : "canonical color publication invalid";
                        }));
                    std::puts(
                        "BINDLESS_RHI_WEBGPU_CANONICAL_COLOR_PUBLICATION6_OK control1 version-retained-copy atomic-oom hot0 raw-export-closed no-sample no-execution");
                }
                assert(!retired.expired());
                retained.reset();
                assert(retired.expired());
                retired.reset();
                assert(!admission->liveBytes());
            });
        assert(publicationQuota && !publicationQuota->liveBytes());
        std::puts(
            "BINDLESS_RHI_WEBGPU_COMPLETED_TEXTURE_DESCRIPTOR_RETIRE_OK real-upload-completion original-view-expired all-quotas-zero no-sample no-execution");
    }
};
} // namespace arp::test
