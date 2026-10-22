#include "rhi/backends/webgpu/webgpu_bindless_preparation.h"

#include "rhi/backends/webgpu/webgpu_backend_objects.h"
#include "rhi/backends/webgpu/webgpu_completed_texture_publication.h"
#include "rhi/backends/webgpu/webgpu_native_ordinary_physical_groups.h"
#include "rhi/backends/webgpu/webgpu_native_payloads.h"
#include "rhi/frontend/bindless_initialized_graphics.h"
#include "rhi/frontend/bindless_root_callback.h"
#include "rhi/frontend/descriptor_buffer_write_validation.h"
#include "rhi/frontend/descriptor_set_validation.h"
#include "rhi/semantics/bindless_entry_access.h"
#include "rhi/semantics/bindless_ordinary_color_domain.h"
#include "rhi/semantics/bindless_ordinary_depth_pipeline.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"
#include "rhi/semantics/bindless_pipeline_interface.h"

#include <algorithm>

namespace arp::rhi_webgpu {
namespace {
template <class T> bool same(const std::shared_ptr<T>& a, const std::shared_ptr<T>& b) noexcept
{
    return (!a && !b)
        || (a && b && a.get() == b.get() && core::SharedOwnerProjection::sameOwner(a, b));
}

const rhi::BindlessPackageModule* selectIssuedModule(
    const rhi::BindlessShaderPackage& package, const WebGpuNativePipelineStageFact& stage) noexcept
{
    const auto expected = stage.stage == rhi::ShaderStage::Compute ? shader::ShaderStage::Compute
        : stage.stage == rhi::ShaderStage::Vertex                  ? shader::ShaderStage::Vertex
                                                                   : shader::ShaderStage::Fragment;
    const rhi::BindlessPackageModule* selected = nullptr;
    for (const auto& module : package.modules()) {
        if (module.reflection.reflection.entryPoints.size() != 1u
            || module.reflection.reflection.entryPoints[0u].stage != expected
            || module.wgslFile >= package.files().size())
            continue;
        const auto& file = package.files()[module.wgslFile];
        if (file.bytes.size() != stage.sourceBytes
            || !core::Sha256::matchesRawHexDigest(stage.sourceSha256, file.sha256))
            continue;
        if (selected)
            return nullptr;
        selected = &module;
    }
    return selected;
}

bool issuedInitializedGraphicsProfile(const WebGpuNativeIssuedPipelineContract& issued,
    const rhi::BindlessAuthorization& authorization) noexcept
{
    if (issued.stages().size() != 2u)
        return false;
    const rhi::BindlessPackageModule *vertex = nullptr, *fragment = nullptr;
    for (const auto& stage : issued.stages()) {
        const auto* module = selectIssuedModule(authorization.package(), stage);
        if (!module)
            return false;
        if (stage.stage == rhi::ShaderStage::Vertex && !vertex)
            vertex = module;
        else if (stage.stage == rhi::ShaderStage::Fragment && !fragment)
            fragment = module;
        else
            return false;
    }
    if (!vertex || !fragment)
        return false;
    const auto scene = rhi::semantics::BindlessInitializedGraphicsContract::select(
        vertex->initializedGraphics, fragment->initializedGraphics);
    return scene.recognized && !scene.diagnostic;
}

// The issued shader bytes, rather than a caller's layout or an absent fact,
// select authenticated per-entry inactivity for cold8 or public storage10.
const shader::ShaderReflection* coldStorageReflection(
    const WebGpuNativeIssuedPipelineContract& issued,
    const rhi::BindlessAuthorization& authorization) noexcept
{
    if (!rhi::semantics::OriginalBindlessPreparationLowering::hasImageCellBindingPlan(
            authorization.plan())
        || issued.stages().size() != 1u || issued.stages()[0].stage != rhi::ShaderStage::Compute)
        return nullptr;
    const auto& stage = issued.stages()[0];
    const auto& package = authorization.package();
    const shader::ShaderReflection* result = nullptr;
    for (const auto& module : package.modules()) {
        if (module.wgslFile >= package.files().size())
            continue;
        const auto& file = package.files()[module.wgslFile];
        if (file.bytes.size() != stage.sourceBytes
            || !core::Sha256::matchesRawHexDigest(stage.sourceSha256, file.sha256))
            continue;
        if (result)
            return nullptr;
        result = &module.reflection.reflection;
    }
    return result;
}
} // namespace

const char* inspectWebGpuOriginalBindlessBindingActivity(
    const WebGpuNativeIssuedPipelineContract& issued,
    const rhi::BindlessAuthorization& authorization,
    std::uint32_t set,
    std::uint32_t binding,
    bool& active) noexcept
{
    active = false;
    const bool produced = rhi::OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(
        authorization.plan(), 16u);
    const bool scene = issuedInitializedGraphicsProfile(issued, authorization);
    if (produced || scene) {
        const auto& package = authorization.package();
        if ((produced && package.gpuVisibilityConsumerContractVersion() != 2u)
            || package.artifact().execution != rhi::BindlessExecution::Emulated)
            return "RHI-WEBGPU-BINDLESS-PHYSICAL-PRODUCED-PACKAGE";
        return rhi::semantics::OriginalBindlessEntryAccessContract::selectedBindings(
            issued.stages(),
            rhi::BindlessExecution::Emulated,
            set,
            binding,
            [&](const auto& stage) noexcept -> const shader::ShaderReflection* {
                const auto* module = selectIssuedModule(package, stage);
                return module ? &module->reflection.reflection : nullptr;
            },
            active);
    }
    const auto* historical = coldStorageReflection(issued, authorization);
    active = !historical
        || !rhi::semantics::BindlessPipelineInterfaceContract::explicitWebGpuInactiveBinding(
            *historical, set, binding);
    return nullptr;
}

const char* validateWebGpuOriginalBindlessPhysicalAliases(
    const WebGpuNativeDescriptorGroupPayload& lhs,
    const WebGpuNativeDescriptorGroupPayload& rhs) noexcept
{
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    using Contract = rhi::semantics::BindlessPipelineInterfaceContract;
    const auto range = [](const WebGpuNativeTextureViewPayload& view) noexcept {
        const auto& key = view.creationKey;
        const auto aspect = key.nativeAspect == WGPUTextureAspect_StencilOnly
            ? rhi::TextureAspect::Stencil
            : rhi::isDepthTextureFormat(view.parentTexture->rhiFormat) ? rhi::TextureAspect::Depth
                                                                       : rhi::TextureAspect::Color;
        return rhi::TextureSubresourceRange{
            key.baseMipLevel, key.mipLevelCount, key.baseArrayLayer, key.arrayLayerCount, aspect};
    };
    for (std::size_t x = 0u; x < lhs.bindings.size(); ++x)
        for (std::size_t y = &lhs == &rhs ? x + 1u : 0u; y < rhs.bindings.size(); ++y) {
            const auto& a = lhs.bindings[x];
            const auto& b = rhs.bindings[y];
            if (a.buffer && b.buffer) {
                if (const auto* id = Contract::bufferAliasDiagnostic(rhi::Backend::WebGPU,
                        a.write,
                        a.buffer->sizeBytes,
                        b.write,
                        b.buffer->sizeBytes))
                    return id;
            }
            if (a.textureView && b.textureView) {
                const auto& av = *a.textureView;
                const auto& bv = *b.textureView;
                if (!av.parentTexture || !bv.parentTexture)
                    return "RHI-WEBGPU-BINDLESS-PHYSICAL-TEXTURE-PARENT";
                if (const auto* id = Contract::textureAliasDiagnostic(rhi::Backend::WebGPU,
                        av.parentTexture.get() == bv.parentTexture.get(),
                        a.binding,
                        range(av),
                        b.binding,
                        range(bv)))
                    return id;
            }
        }
#else
    (void)lhs;
    (void)rhs;
#endif
    return nullptr;
}

rhi::ValidationResult prepareWebGpuOriginalBindlessPhysicalGroup(
    const WebGpuNativeIssuedPipelineContract& issued,
    const rhi::DescriptorSet& set,
    const std::shared_ptr<WebGpuNativeDescriptorGroupPayload>& descriptors,
    const rhi::BindlessAuthorization& authorization,
    std::uint32_t logicalSet,
    std::shared_ptr<WebGpuNativeDescriptorGroupPayload>& physical,
    std::uint32_t& groupIndex,
    std::uint32_t ordinaryGraphicsClosureVersion)
{
    const bool canonicalColor =
        ordinaryGraphicsClosureVersion == 17u || ordinaryGraphicsClosureVersion == 18u;
    const bool canonicalDepth =
        ordinaryGraphicsClosureVersion == 19u || ordinaryGraphicsClosureVersion == 20u;
    const bool canonicalGraphics = canonicalColor || canonicalDepth;
    if (ordinaryGraphicsClosureVersion && !canonicalGraphics)
        return {false, "RHI-WEBGPU-BINDLESS-ORDINARY-COLOR-CLOSURE-VERSION"};
    if (canonicalGraphics
        && (logicalSet == authorization.package().artifact().set
            || (canonicalDepth
                    ? rhi::OriginalBindlessRootNativeClosureContract::
                          canonicalGraphicsDepthProfileDiagnostic(
                              authorization.plan(), 2u, true, true, ordinaryGraphicsClosureVersion)
                    : rhi::OriginalBindlessRootNativeClosureContract::
                          canonicalGraphicsColorProfileDiagnostic(authorization.plan(),
                              2u,
                              true,
                              true,
                              ordinaryGraphicsClosureVersion))))
        return {false, "RHI-WEBGPU-BINDLESS-ORDINARY-COLOR-CLOSURE-PROFILE"};
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    physical.reset();
    groupIndex = UINT32_MAX;
    if (!descriptors)
        return {false, "RHI-WEBGPU-BINDLESS-PHYSICAL-GROUP-MAPPING"};
    std::size_t active = 0u;
    for (const auto& binding : descriptors->bindings) {
        bool consumed = false;
        if (const auto* id = inspectWebGpuOriginalBindlessBindingActivity(
                issued, authorization, logicalSet, binding.binding.binding, consumed))
            return {false, id};
        active += consumed;
    }
    const auto mapping =
        std::find_if(issued.sets().begin(), issued.sets().end(), [&](const auto& m) {
            return m.logicalSet == logicalSet;
        });
    // An entirely inactive ordinary set has no native mapping or bind group.
    // Its original descriptors/resources remain retained by the Root closure.
    if (!active && logicalSet != authorization.package().artifact().set
        && (rhi::OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(
                authorization.plan(), 16u)
            || issuedInitializedGraphicsProfile(issued, authorization))) {
        if (mapping != issued.sets().end()
            || std::any_of(issued.facts().begin(), issued.facts().end(), [&](const auto& fact) {
                   return fact.mapping.logicalSet == logicalSet;
               }))
            return {false, "RHI-WEBGPU-BINDLESS-PHYSICAL-INACTIVE-SET"};
        return {true, {}};
    }
    if (mapping == issued.sets().end()
        || (issued.semantics().usesPush
            && (issued.semantics().pushGroup >= issued.semantics().groupCount
                || issued.semantics().pushBinding == UINT32_MAX
                || mapping->webGpuGroup == issued.semantics().pushGroup))
        || !issued.tables().empty() || mapping->webGpuGroup >= issued.semantics().groupCount)
        return {false, "RHI-WEBGPU-BINDLESS-PHYSICAL-GROUP-MAPPING"};
    groupIndex = mapping->webGpuGroup;
    const auto required =
        std::count_if(issued.facts().begin(), issued.facts().end(), [&](const auto& f) {
            return f.mapping.logicalSet == logicalSet;
        });
    if (std::size_t(required) != active)
        return {false, "RHI-WEBGPU-BINDLESS-PHYSICAL-GROUP-COVERAGE"};
    core::pmr::Vector<WGPUBindGroupEntry> entries;
    entries.reserve(required);
    physical = core::makeShared<WebGpuNativeDescriptorGroupPayload>();
    physical->bindings.reserve(required);
    for (const auto& fact : issued.facts()) {
        if (fact.mapping.logicalSet != logicalSet)
            continue;
        bool consumed = false;
        if (const auto* id = inspectWebGpuOriginalBindlessBindingActivity(
                issued, authorization, logicalSet, fact.mapping.logicalBinding, consumed))
            return {false, id};
        if (!consumed)
            return {false, "RHI-WEBGPU-BINDLESS-PHYSICAL-INACTIVE-BINDING"};
        const auto found = std::find_if(
            descriptors->bindings.begin(), descriptors->bindings.end(), [&](const auto& binding) {
                return binding.binding.binding == fact.mapping.logicalBinding
                    && binding.arrayElement == 0u;
            });
        if (fact.mapping.logicalSet != logicalSet || fact.mapping.webGpuGroup != groupIndex
            || found == descriptors->bindings.end()
            || !rhi::descriptorBindingLayoutMatches(fact.shape, found->binding)
            || fact.shape.dynamicOffset || fact.shape.arrayCount != 1u || fact.shape.tableBacked)
            return {false, "RHI-WEBGPU-BINDLESS-PHYSICAL-GROUP-BINDING"};
        // Match the immutable L2 publication before any physical emission.
        // Backend write metadata alone cannot grant finite-range authority.
        const char* publicationDiagnostic = nullptr;
        const auto original = set.observeSuccessfulBindingWrite(
            found->write.binding, found->write.arrayElement, publicationDiagnostic);
        if (!original || publicationDiagnostic
            || !rhi::descriptorBindingWriteMatches(original->write(), found->write))
            return {false, "RHI-WEBGPU-BINDLESS-PREPARED-PUBLICATION-MISMATCH"};
        auto entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = fact.mapping.webGpuBinding;
        const bool buffer = fact.shape.type == rhi::DescriptorType::UniformBuffer
            || fact.shape.type == rhi::DescriptorType::ReadOnlyStorageBuffer
            || fact.shape.type == rhi::DescriptorType::StorageBuffer;
        if (buffer) {
            if (!found->buffer || !found->buffer->buffer || found->textureView || found->sampler
                || found->buffer->ownerDevice != issued.nativeDevice()
                || found->buffer->sizeBytes < fact.nativeDeclaredMinimumBytes
                || wgpuBufferGetSize(found->buffer->buffer) != found->buffer->sizeBytes)
                return {false, "RHI-WEBGPU-BINDLESS-PREPARED-BUFFER-SHAPE"};
            rhi::DescriptorBufferBindingRangeContract::Resolved range;
            if (found->write.bufferBindingRange.contractVersion == 2u) {
                const auto limits = webGpuNativeDeviceLimits();
                // A native minBindingSize of zero means a deferred use check,
                // not a zero shader footprint. The authenticated generated
                // package supplies the table ABI minimum independently.
                auto minimum = fact.nativeDeclaredMinimumBytes;
                const auto& artifact = authorization.package().artifact();
                for (const auto& table : artifact.tables)
                    if (logicalSet == artifact.set && found->write.binding >= table.firstBinding
                        && found->write.binding - table.firstBinding < table.capacity)
                        minimum = std::max(minimum, table.bufferLayout.minBindingSizeBytes);
                if (const auto* id =
                        rhi::OriginalDescriptorBufferBindingContract::validateFiniteInterval(
                            found->write.type,
                            found->write.bufferBindingRange,
                            found->buffer->sizeBytes,
                            0u,
                            minimum,
                            {1u,
                                limits.uniformBufferOffsetAlignment,
                                limits.storageBufferOffsetAlignment,
                                limits.maxUniformBufferBindingSize,
                                limits.maxStorageBufferBindingSize},
                            range))
                    return {false, id};
            } else if (const auto* id = rhi::DescriptorBufferBindingRangeContract::resolve(
                           found->write.bufferBindingRange, found->buffer->sizeBytes, 0u, range))
                return {false, id};
            entry.buffer = found->buffer->buffer;
            entry.offset = range.offsetBytes;
            entry.size = range.sizeBytes;
        } else {
            const bool mixedImages =
                rhi::semantics::OriginalBindlessPreparationLowering::hasMixedImageResourcePlan(
                    authorization.plan());
            const bool producedImage =
                rhi::semantics::OriginalBindlessPreparationLowering::hasProducedImageReadPlan(
                    authorization.plan());
            const bool storagePreparation =
                rhi::semantics::OriginalBindlessPreparationLowering::hasStoragePlan(
                    authorization.plan())
                || (mixedImages && !fact.shape.depthTexture);
            const bool depthPreparation = (canonicalDepth && fact.shape.depthTexture)
                || (mixedImages && fact.shape.depthTexture)
                || rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                    authorization.plan(), 5u)
                || rhi::OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(
                    authorization.plan(), 5u);
            const bool depthDeclaration = fact.shape.depthTexture || fact.shape.comparisonSampler;
            if (canonicalGraphics
                && (canonicalDepth && depthDeclaration
                        ? rhi::semantics::OriginalBindlessOrdinaryDepthPipelineDeclaration::inspect(
                              rhi::Backend::WebGPU, fact.shape)
                            != nullptr
                        : !rhi::semantics::OriginalBindlessOrdinaryColorDomain::
                              acceptsGraphicsBinding(fact.shape)))
                return {false, "RHI-WEBGPU-BINDLESS-ORDINARY-COLOR-BINDING"};
            if (!canonicalGraphics
                && !rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(
                    authorization.plan(), 3u)
                && !depthPreparation && !storagePreparation
                && !rhi::OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(
                    authorization.plan(), 3u)
                && !rhi::OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(
                    authorization.plan(), 4u)
                && !rhi::OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(
                    authorization.plan(), 16u))
                return {false, "RHI-WEBGPU-BINDLESS-PREPARED-SAMPLED-PROFILE"};
            WebGpuOrdinaryResourceObservation registered;
            const auto completed = std::find_if(descriptors->completedTexturePublications.begin(),
                descriptors->completedTexturePublications.end(),
                [&](const auto& proof) {
                    return rhi::descriptorBindingWriteMatches(proof.write, found->write);
                });
            if (completed != descriptors->completedTexturePublications.end()) {
                const bool producedColor = completed->colorPublicationVersion == 6u
                    && rhi::OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(
                        authorization.plan(), 16u);
                if (((canonicalGraphics && !depthPreparation) || producedColor)
                    != (completed->colorPublicationVersion == 6u))
                    return {false, "RHI-WEBGPU-BINDLESS-ORDINARY-COLOR-PUBLICATION"};
                if (bool(completed->depthContent) != depthPreparation
                    || bool(completed->cellIdentity) != (storagePreparation || producedImage))
                    return {false, "RHI-WEBGPU-BINDLESS-PREPARED-DEPTH-PROOF"};
                if (const auto* id = (storagePreparation || producedImage)
                        ? validateWebGpuOriginalTexturePublication(*completed, *found)
                        : validateWebGpuCompletedTexturePublication(*completed, *found))
                    return {false, id};
                if (const auto* id = observeOriginalWebGpuResource(
                        *completed->source->originalObject(), registered))
                    return {false, id};
                physical->completedTexturePublications.push_back(*completed);
            } else {
                if (storagePreparation || producedImage
                    || (canonicalGraphics && found->textureView))
                    return {false, "RHI-WEBGPU-BINDLESS-PREPARED-CELL-PROOF"};
                if (const auto* id =
                        observeWebGpuDescriptorResource(set, found->write.resource, registered))
                    return {false, id};
            }
            if (!same(registered.view(), found->textureView)
                || !same(registered.sampler(), found->sampler)
                || (found->textureView
                    && !same(registered.backing().texture, found->textureView->parentTexture)))
                return {false, "RHI-WEBGPU-BINDLESS-PREPARED-RESOURCE-REGISTRY"};
            if (storagePreparation || producedImage) {
                const auto& view = found->textureView;
                const auto parent = view ? view->parentTexture : nullptr;
                const bool storage = fact.shape.type == rhi::DescriptorType::StorageTexture;
                if ((!storage && fact.shape.type != rhi::DescriptorType::SampledTexture)
                    || (producedImage
                        && (storage || completed == descriptors->completedTexturePublications.end()
                            || !rhi::OriginalTextureCellDescriptorContract::isProducedImageRead(
                                completed->cellRequest.contractVersion)))
                    || fact.shape.stages != rhi::ShaderStage::Compute || fact.shape.depthTexture
                    || fact.shape.textureViewDimension != rhi::TextureViewDimension::Texture2D
                    || (storage
                        && (fact.shape.storageTextureFormat != rhi::TextureFormat::Rgba16Float
                            || fact.shape.storageTextureAccess
                                != rhi::StorageTextureAccess::WriteOnly))
                    || !view || !view->view || !parent || !parent->texture || found->buffer
                    || found->sampler || parent->ownerDevice != issued.nativeDevice()
                    || parent->browserSurfaceTexture
                    || parent->rhiFormat != rhi::TextureFormat::Rgba16Float
                    || parent->format != WGPUTextureFormat_RGBA16Float
                    || parent->dimension != WGPUTextureDimension_2D || parent->sampleCount != 1u
                    || parent->extent.depth != 1u || parent->arrayLayerCount != 1u
                    || view->creationKey.usage
                        != (storage ? rhi::TextureUsage::Storage : rhi::TextureUsage::Sampled)
                    || view->creationKey.nativeDimension != WGPUTextureViewDimension_2D
                    || view->creationKey.nativeFormat != WGPUTextureFormat_RGBA16Float
                    || wgpuTextureGetWidth(parent->texture) != parent->extent.width
                    || wgpuTextureGetHeight(parent->texture) != parent->extent.height
                    || wgpuTextureGetDepthOrArrayLayers(parent->texture) != parent->arrayLayerCount
                    || wgpuTextureGetMipLevelCount(parent->texture) != parent->mipLevelCount
                    || (wgpuTextureGetUsage(parent->texture)
                           & (storage ? WGPUTextureUsage_StorageBinding
                                      : WGPUTextureUsage_TextureBinding))
                        == 0u)
                    return {false, "RHI-WEBGPU-BINDLESS-PREPARED-CELL-SHAPE"};
                entry.textureView = view->view;
            } else if (fact.shape.type == rhi::DescriptorType::SampledTexture) {
                if (depthPreparation
                    && (completed == descriptors->completedTexturePublications.end()
                        || !completed->depthContent
                        || fact.shape.textureViewDimension != rhi::TextureViewDimension::Texture2D))
                    return {false, "RHI-WEBGPU-BINDLESS-PREPARED-DEPTH-PROOF"};
                const auto& view = found->textureView;
                const auto parent = view ? view->parentTexture : nullptr;
                const bool hdr = canonicalGraphics && parent
                    && parent->rhiFormat == rhi::TextureFormat::Rgba16Float;
                if (!view || !view->view || !parent || !parent->texture || found->buffer
                    || found->sampler || parent->ownerDevice != issued.nativeDevice()
                    || parent->browserSurfaceTexture
                    || parent->rhiFormat
                        != (depthPreparation ? rhi::TextureFormat::D32Float
                                : hdr        ? rhi::TextureFormat::Rgba16Float
                                             : rhi::TextureFormat::Rgba8Unorm)
                    || parent->dimension != WGPUTextureDimension_2D || parent->sampleCount != 1u
                    || parent->extent.depth != 1u || fact.shape.depthTexture != depthPreparation
                    || view->creationKey.usage != rhi::TextureUsage::Sampled
                    || view->creationKey.nativeDimension
                        != (fact.shape.textureViewDimension == rhi::TextureViewDimension::Texture2D
                                ? WGPUTextureViewDimension_2D
                                : canonicalGraphics
                                    && fact.shape.textureViewDimension
                                        == rhi::TextureViewDimension::TextureCube
                                ? WGPUTextureViewDimension_Cube
                                : WGPUTextureViewDimension_2DArray)
                    || view->creationKey.nativeFormat
                        != (depthPreparation ? WGPUTextureFormat_Depth32Float
                                : hdr        ? WGPUTextureFormat_RGBA16Float
                                             : WGPUTextureFormat_RGBA8Unorm)
                    || wgpuTextureGetWidth(parent->texture) != parent->extent.width
                    || wgpuTextureGetHeight(parent->texture) != parent->extent.height
                    || wgpuTextureGetDepthOrArrayLayers(parent->texture) != parent->arrayLayerCount
                    || wgpuTextureGetMipLevelCount(parent->texture) != parent->mipLevelCount
                    || (wgpuTextureGetUsage(parent->texture) & WGPUTextureUsage_TextureBinding)
                        == 0u)
                    return {false, "RHI-WEBGPU-BINDLESS-PREPARED-SAMPLED-SHAPE"};
                entry.textureView = view->view;
            } else if (fact.shape.type == rhi::DescriptorType::Sampler) {
                if (!found->sampler || !found->sampler->sampler || found->buffer
                    || found->textureView || found->sampler->ownerDevice != issued.nativeDevice()
                    || (fact.shape.comparisonSampler && !canonicalDepth))
                    return {false, "RHI-WEBGPU-BINDLESS-PREPARED-SAMPLER-SHAPE"};
                entry.sampler = found->sampler->sampler;
            } else
                return {false, "RHI-WEBGPU-BINDLESS-PREPARED-SAMPLED-PROFILE"};
        }
        entries.push_back(entry);
        physical->bindings.push_back(*found);
    }
    if (const auto* id = validateWebGpuOriginalBindlessPhysicalAliases(*physical, *physical))
        return {false, id};
    core::pmr::String diagnostic;
    if (!initializeWebGpuNativePhysicalDescriptorGroup(
            *physical, issued, groupIndex, entries.data(), entries.size(), diagnostic))
        return {false, std::move(diagnostic)};
    return {true, {}};
#else
    (void)issued;
    (void)set;
    (void)descriptors;
    (void)authorization;
    (void)logicalSet;
    (void)physical;
    (void)groupIndex;
    return {false, "RHI-WEBGPU-BINDLESS-PHYSICAL-GROUP-NATIVE-UNAVAILABLE"};
#endif
}

namespace {
// Compute and Graphics share the frozen physical resource correspondence.
// Sampled Graphics/3 adds views/parents/samplers only to cold preparation.
// Only the typed pipeline and independently approved profile differ.
template <class Base, class Owners, class NativePipeline, std::uint32_t ColdVersion>
class WebGpuOriginalPreparedBinding final : public Base {
public:
    explicit WebGpuOriginalPreparedBinding(std::shared_ptr<const Owners> owners)
        : Base(std::move(owners)),
          pipeline_(webGpuNativePayloadFor(*this->owners()->desc().pipeline)),
          descriptors_(descriptorPayload(*this->owners()->desc().snapshot))
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (pipeline_) {
            pipelineHandle_ = pipeline_->pipeline;
            pipelineLayout_ = pipeline_->layout;
        }
        if (descriptors_) {
            resources_.reserve(descriptors_->bindings.size());
            for (const auto& binding : descriptors_->bindings)
                resources_.push_back(capture(binding));
        }
#endif
    }

    rhi::ValidationResult preparePhysical()
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (!pipeline_ || !pipeline_->issuedContract() || !descriptors_
            || !webGpuNativeDeviceAvailable() || webGpuNativeDeviceLost())
            return {false,
                diagnosticId("RHI-WEBGPU-BINDLESS-PREPARED-COMPUTE-NATIVE",
                    "RHI-WEBGPU-BINDLESS-PREPARED-GRAPHICS-NATIVE")};
        const auto& desc = this->owners()->desc();
        const auto valid = prepareWebGpuOriginalBindlessPhysicalGroup(*pipeline_->issuedContract(),
            *desc.snapshot->descriptorSet(),
            descriptors_,
            *this->authorization(),
            desc.snapshot->setIndex(),
            physical_,
            groupIndex_);
        if (!valid.success)
            return valid;
        group_ = physical_->group;
        groupLayout_ = physical_->layout;
        return {true, {}};
#else
        return {false,
            diagnosticId("RHI-WEBGPU-BINDLESS-PREPARED-COMPUTE-NATIVE-UNAVAILABLE",
                "RHI-WEBGPU-BINDLESS-PREPARED-GRAPHICS-NATIVE-UNAVAILABLE")};
#endif
    }

    static bool hasPlan(const rhi::BindlessLoweringPlan& plan) noexcept
    {
        if (rhi::OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(plan, 16u))
            return true;
        if (rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(plan, 5u)
            || rhi::OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(plan, 5u))
            return true;
        if constexpr (ColdVersion == 1u)
            return rhi::OriginalBindlessRootNativeExecutionContract::hasPreparedPlan(plan)
                || rhi::semantics::OriginalBindlessPreparationLowering::hasImageCellBindingPlan(
                    plan);
        return rhi::OriginalBindlessRootNativeExecutionContract::hasPreparedPlan(plan, ColdVersion)
            || rhi::OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(plan, 3u)
            || rhi::OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(plan, 4u)
            || rhi::OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(plan, 13u)
            || rhi::OriginalBindlessRootNativeExecutionContract::hasExecutionPlan(plan, 14u)
            || rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(plan, 3u)
            || rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(plan, 11u)
            || rhi::semantics::OriginalBindlessPreparationLowering::hasPlan(plan, 12u);
    }

    static constexpr const char* authorizationDiagnostic() noexcept
    {
        return diagnosticId("RHI-WEBGPU-BINDLESS-PREPARED-COMPUTE-AUTHORIZATION",
            "RHI-WEBGPU-BINDLESS-PREPARED-GRAPHICS-AUTHORIZATION");
    }

    static constexpr const char* profileDiagnostic() noexcept
    {
        return diagnosticId("RHI-WEBGPU-BINDLESS-PREPARED-COMPUTE-PAYLOAD",
            "RHI-WEBGPU-BINDLESS-PREPARED-GRAPHICS-PAYLOAD");
    }

    const auto& physicalGroup() const noexcept { return physical_; }

    rhi::ValidationResult validatePayload() const override
    {
        const auto ownersValid = this->validateOwners();
        if (!ownersValid.success)
            return ownersValid;
        const auto& desc = this->owners()->desc();
        const auto currentPipeline = webGpuNativePayloadFor(*desc.pipeline);
        const auto* set = desc.snapshot->descriptorSet();
        const auto currentSet = set ? descriptorPayload(*desc.snapshot) : nullptr;
        if (!hasPlan(desc.plan) || desc.pipeline->validationReport().backend != rhi::Backend::WebGPU
            || !desc.pipeline->validationReport().success || !pipeline_ || !descriptors_
            || !core::SharedOwnerProjection::sameOwner(currentPipeline, pipeline_)
            || !core::SharedOwnerProjection::sameOwner(currentSet, descriptors_)
            || currentPipeline.get() != pipeline_.get() || currentSet.get() != descriptors_.get())
            return {false,
                diagnosticId("RHI-WEBGPU-BINDLESS-PREPARED-COMPUTE-PAYLOAD",
                    "RHI-WEBGPU-BINDLESS-PREPARED-GRAPHICS-PAYLOAD")};
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        const auto& issued = pipeline_->issuedContract();
        if (issued) {
            const auto mapping =
                std::find_if(issued->sets().begin(), issued->sets().end(), [&](const auto& m) {
                    return m.logicalSet == desc.snapshot->setIndex();
                });
            if (mapping == issued->sets().end() || mapping->webGpuGroup != groupIndex_)
                return {false, "RHI-WEBGPU-BINDLESS-PREPARED-GROUP-MAPPING"};
        }
        if (!issued || webGpuNativeDeviceLost() || !pipelineHandle_
            || pipelineHandle_ != pipeline_->pipeline || !pipelineLayout_
            || pipelineLayout_ != pipeline_->layout || issued->nativeObject() != pipelineHandle_
            || issued->nativeLayout() != pipelineLayout_ || !issued->nativeDevice() || !physical_
            || !group_ || group_ != physical_->group || !groupLayout_
            || groupLayout_ != physical_->layout
            || resources_.size() != descriptors_->bindings.size())
            return {false,
                diagnosticId("RHI-WEBGPU-BINDLESS-PREPARED-COMPUTE-NATIVE",
                    "RHI-WEBGPU-BINDLESS-PREPARED-GRAPHICS-NATIVE")};
        std::size_t inactiveCount = 0u, inactiveTextures = 0u;
        for (const auto& current : descriptors_->bindings) {
            bool consumed = false;
            if (const auto* id = inspectWebGpuOriginalBindlessBindingActivity(*issued,
                    *this->authorization(),
                    desc.snapshot->setIndex(),
                    current.binding.binding,
                    consumed))
                return {false, id};
            if (!consumed) {
                ++inactiveCount;
                if (current.textureView)
                    ++inactiveTextures;
            }
        }
        if (physical_->bindings.size() + inactiveCount != descriptors_->bindings.size())
            return {false, "RHI-WEBGPU-BINDLESS-PHYSICAL-GROUP-COVERAGE"};
        if (physical_->completedTexturePublications.size() + inactiveTextures
            != descriptors_->completedTexturePublications.size())
            return {false, "RHI-WEBGPU-BINDLESS-PREPARED-COMPLETED-PROOF"};
        for (std::size_t i = 0u; i < resources_.size(); ++i) {
            const auto& frozen = resources_[i];
            const auto& current = descriptors_->bindings[i];
            const auto physical = std::find_if(
                physical_->bindings.begin(), physical_->bindings.end(), [&](const auto& binding) {
                    return rhi::descriptorBindingWriteMatches(binding.write, current.write);
                });
            bool consumed = false;
            if (const auto* id = inspectWebGpuOriginalBindlessBindingActivity(*issued,
                    *this->authorization(),
                    desc.snapshot->setIndex(),
                    current.binding.binding,
                    consumed))
                return {false, id};
            const bool omitted = !consumed;
            if (!matches(frozen, current)
                || (omitted ? physical != physical_->bindings.end()
                            : physical == physical_->bindings.end() || !matches(frozen, *physical)))
                return {false,
                    diagnosticId("RHI-WEBGPU-BINDLESS-PREPARED-COMPUTE-BUFFER",
                        "RHI-WEBGPU-BINDLESS-PREPARED-GRAPHICS-RESOURCE")};
            if (current.textureView && current.textureView->parentTexture
                && current.textureView->parentTexture->contentAccessPolicy()
                    .ordinaryAccessDiagnostic()) {
                const auto source = std::find_if(descriptors_->completedTexturePublications.begin(),
                    descriptors_->completedTexturePublications.end(),
                    [&](const auto& proof) {
                        return rhi::descriptorBindingWriteMatches(proof.write, current.write);
                    });
                const auto retained = std::find_if(physical_->completedTexturePublications.begin(),
                    physical_->completedTexturePublications.end(),
                    [&](const auto& proof) {
                        return rhi::descriptorBindingWriteMatches(proof.write, current.write);
                    });
                if (source == descriptors_->completedTexturePublications.end()
                    || (!omitted
                        && (retained == physical_->completedTexturePublications.end()
                            || !core::SharedOwnerProjection::sameOwner(
                                source->source, retained->source)
                            || !same(source->depthContent, retained->depthContent))))
                    return {false, "RHI-WEBGPU-BINDLESS-PREPARED-COMPLETED-PROOF"};
                const bool storage =
                    rhi::semantics::OriginalBindlessPreparationLowering::hasStoragePlan(desc.plan)
                    || rhi::semantics::OriginalBindlessPreparationLowering::
                        hasProducedImageReadPlan(desc.plan)
                    || (rhi::semantics::OriginalBindlessPreparationLowering::
                            hasMixedImageResourcePlan(desc.plan)
                        && !current.binding.depthTexture);
                if (bool(source->cellIdentity) != storage
                    || (!omitted && bool(retained->cellIdentity) != storage))
                    return {false, "RHI-WEBGPU-BINDLESS-PREPARED-CELL-PROOF"};
                if (const auto* id = storage
                        ? validateWebGpuOriginalTexturePublication(*source, current)
                        : validateWebGpuCompletedTexturePublication(*source, current))
                    return {false, id};
                if (!omitted) {
                    if (const auto* id = storage
                            ? validateWebGpuOriginalTexturePublication(*retained, *physical)
                            : validateWebGpuCompletedTexturePublication(*retained, *physical))
                        return {false, id};
                }
            }
        }
        return rhi::validateDescriptorSetBinding(
            desc.pipeline->layout(), desc.snapshot->setIndex(), *set);
#else
        return {false,
            diagnosticId("RHI-WEBGPU-BINDLESS-PREPARED-COMPUTE-NATIVE-UNAVAILABLE",
                "RHI-WEBGPU-BINDLESS-PREPARED-GRAPHICS-NATIVE-UNAVAILABLE")};
#endif
    }

    std::uint32_t argumentBufferCount() const noexcept override { return 0u; }

    std::uint64_t argumentBufferBytes() const noexcept override { return 0u; }

private:
    static std::shared_ptr<WebGpuNativeDescriptorGroupPayload> descriptorPayload(
        const rhi::BindlessSnapshot& snapshot)
    {
        if constexpr (ColdVersion == 1u) {
            if (rhi::semantics::OriginalBindlessPreparationLowering::hasImageCellBindingPlan(
                    snapshot.plan()))
                return prepareWebGpuOriginalImageCellDescriptors(snapshot);
        }
        return snapshot.descriptorSet() ? webGpuNativePayloadFor(*snapshot.descriptorSet())
                                        : nullptr;
    }

    static constexpr const char* diagnosticId(const char* compute, const char* graphics) noexcept
    {
        return ColdVersion == 1u ? compute : graphics;
    }

    const std::shared_ptr<NativePipeline> pipeline_;
    const std::shared_ptr<WebGpuNativeDescriptorGroupPayload> descriptors_;
    std::shared_ptr<WebGpuNativeDescriptorGroupPayload> physical_;
    std::uint32_t groupIndex_ = UINT32_MAX;
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    struct Resource {
        WebGpuNativeDescriptorBindingPayload binding;
        WGPUBuffer buffer = nullptr;
        WGPUTextureView view = nullptr;
        WGPUTexture texture = nullptr;
        WGPUSampler sampler = nullptr;
        WGPUDevice device = nullptr;
        WGPUTextureFormat nativeFormat = WGPUTextureFormat_Undefined;
        WGPUTextureDimension dimension = WGPUTextureDimension_Undefined;
        bool surface = false;
        std::uint64_t bytes = 0u;
        std::shared_ptr<WebGpuNativeTexturePayload> parent;
        WebGpuNativeTextureViewCacheKey viewKey;
        rhi::Extent3D extent;
        std::uint32_t mips = 0u, layers = 0u, samples = 0u;
        rhi::TextureFormat format = rhi::TextureFormat::Unknown;
    };

    static Resource capture(const WebGpuNativeDescriptorBindingPayload& binding) noexcept
    {
        Resource value;
        value.binding = binding;
        if (binding.buffer) {
            value.buffer = binding.buffer->buffer;
            value.device = binding.buffer->ownerDevice;
            value.bytes = binding.buffer->sizeBytes;
        } else if (binding.textureView) {
            value.view = binding.textureView->view;
            value.viewKey = binding.textureView->creationKey;
            value.parent = binding.textureView->parentTexture;
            if (value.parent) {
                value.texture = value.parent->texture;
                value.device = value.parent->ownerDevice;
                value.nativeFormat = value.parent->format;
                value.dimension = value.parent->dimension;
                value.surface = value.parent->browserSurfaceTexture;
                value.extent = value.parent->extent;
                value.mips = value.parent->mipLevelCount;
                value.layers = value.parent->arrayLayerCount;
                value.samples = value.parent->sampleCount;
                value.format = value.parent->rhiFormat;
            }
        } else if (binding.sampler) {
            value.sampler = binding.sampler->sampler;
            value.device = binding.sampler->ownerDevice;
        }
        return value;
    }

    bool matches(
        const Resource& frozen, const WebGpuNativeDescriptorBindingPayload& current) const noexcept
    {
        const auto now = capture(current);
        return rhi::descriptorBindingWriteMatches(frozen.binding.write, current.write)
            && rhi::descriptorBindingLayoutMatches(frozen.binding.binding, current.binding)
            && frozen.binding.arrayElement == current.arrayElement
            && same(frozen.binding.buffer, current.buffer)
            && same(frozen.binding.textureView, current.textureView)
            && same(frozen.binding.sampler, current.sampler) && same(frozen.parent, now.parent)
            && frozen.buffer == now.buffer && frozen.view == now.view
            && frozen.texture == now.texture && frozen.sampler == now.sampler
            && frozen.device == now.device
            && frozen.device == pipeline_->issuedContract()->nativeDevice()
            && frozen.bytes == now.bytes && frozen.viewKey == now.viewKey
            && frozen.extent.width == now.extent.width && frozen.extent.height == now.extent.height
            && frozen.extent.depth == now.extent.depth && frozen.mips == now.mips
            && frozen.layers == now.layers && frozen.samples == now.samples
            && frozen.format == now.format && frozen.nativeFormat == now.nativeFormat
            && frozen.dimension == now.dimension && frozen.surface == now.surface;
    }

    core::pmr::Vector<Resource> resources_{&core::defaultCpuMemoryResource()};
    decltype(NativePipeline::pipeline) pipelineHandle_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUBindGroup group_ = nullptr;
    WGPUBindGroupLayout groupLayout_ = nullptr;
#endif
};

using WebGpuOriginalPreparedCompute =
    WebGpuOriginalPreparedBinding<rhi::BindlessPreparedComputeBinding,
        rhi::BindlessComputeBindingOwners,
        WebGpuNativeComputePipelinePayload,
        1u>;
using WebGpuOriginalPreparedGraphics =
    WebGpuOriginalPreparedBinding<rhi::BindlessPreparedGraphicsBinding,
        rhi::BindlessGraphicsBindingOwners,
        WebGpuNativeRenderPipelinePayload,
        2u>;

template <class Prepared, class Owners>
std::shared_ptr<const Prepared> prepareBinding(const rhi::BindlessCrossExtension& issuer,
    std::shared_ptr<const Owners> owners,
    core::pmr::String& diagnostic)
{
    if (!owners || !owners->sharedOwnerMatches(owners) || !owners->authorization()
        || !owners->authorization()->issuedBy(&issuer, owners->authorization())) {
        diagnostic = Prepared::authorizationDiagnostic();
        return {};
    }
    const auto valid = owners->validate();
    if (!valid.success) {
        diagnostic = valid.diagnostics;
        return {};
    }
    if (!Prepared::hasPlan(owners->desc().plan)) {
        diagnostic = Prepared::profileDiagnostic();
        return {};
    }
    try {
        auto result = core::makeShared<Prepared>(std::move(owners));
        const auto physical = result->preparePhysical();
        if (!physical.success) {
            diagnostic = physical.diagnostics;
            return {};
        }
        const auto payload = result->validatePayload();
        if (!payload.success) {
            diagnostic = payload.diagnostics;
            return {};
        }
        diagnostic.clear();
        return result;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-WEBGPU-BINDLESS-PREPARED-ALLOCATION";
        return {};
    }
}

template <class Prepared, class Base>
std::shared_ptr<WebGpuNativeDescriptorGroupPayload> physicalGroup(
    const std::shared_ptr<const Base>& binding) noexcept
{
    const auto* original = dynamic_cast<const Prepared*>(binding.get());
    return original && binding->originalFactoryOwnerMatches(binding) ? original->physicalGroup()
                                                                     : nullptr;
}
} // namespace

std::shared_ptr<const rhi::BindlessPreparedComputeBinding> prepareWebGpuOriginalBindlessCompute(
    const rhi::BindlessCrossExtension& issuer,
    std::shared_ptr<const rhi::BindlessComputeBindingOwners> owners,
    core::pmr::String& diagnostic)
{
    return prepareBinding<WebGpuOriginalPreparedCompute>(issuer, std::move(owners), diagnostic);
}

std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding> prepareWebGpuOriginalBindlessGraphics(
    const rhi::BindlessCrossExtension& issuer,
    std::shared_ptr<const rhi::BindlessGraphicsBindingOwners> owners,
    core::pmr::String& diagnostic)
{
    return prepareBinding<WebGpuOriginalPreparedGraphics>(issuer, std::move(owners), diagnostic);
}

std::shared_ptr<WebGpuNativeDescriptorGroupPayload> webGpuOriginalBindlessPhysicalGroup(
    const std::shared_ptr<const rhi::BindlessPreparedComputeBinding>& binding) noexcept
{
    return physicalGroup<WebGpuOriginalPreparedCompute>(binding);
}

std::shared_ptr<WebGpuNativeDescriptorGroupPayload> webGpuOriginalBindlessPhysicalGroup(
    const std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding>& binding) noexcept
{
    return physicalGroup<WebGpuOriginalPreparedGraphics>(binding);
}
} // namespace arp::rhi_webgpu
