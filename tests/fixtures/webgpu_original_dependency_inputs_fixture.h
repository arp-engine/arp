#pragma once
#include "rhi/backends/webgpu/webgpu_original_resource_states.h"

#include <array>
#include <cassert>

namespace arp::test {
// Shared exact six-command original fixture builder. No native/proof authority.
class WebGpuOriginalDependencyInputsFixture final {
public:
    WebGpuOriginalDependencyInputsFixture(
        std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryTransferRetention> root,
        const rhi::TransferCopyArguments& copy,
        rhi::CommandUsageKind sourceKind,
        rhi::CommandUsageKind destinationKind,
        std::shared_ptr<core::CpuMemoryQuota> quota,
        std::string_view label = {})
        : copy_(copy), quota_(std::move(quota))
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using K = CommandUsageKind;
        using A = CommandScalarArguments;
        const auto* source = root->find(copy.source());
        const auto* destination = root->find(copy.destination());
        assert(source && destination);
        const auto sourceDomain =
            source->buffer ? CommandReferenceDomain::Buffer : CommandReferenceDomain::Texture;
        const auto destinationDomain =
            destination->buffer ? CommandReferenceDomain::Buffer : CommandReferenceDomain::Texture;
        const std::array<WebGpuCommandReference, 4> refs{
            {{sourceDomain, WebGpuCommandReferenceSource::Transfer, source->id},
                {destinationDomain, WebGpuCommandReferenceSource::Transfer, destination->id},
                {sourceDomain, WebGpuCommandReferenceSource::Transfer, source->id},
                {destinationDomain, WebGpuCommandReferenceSource::Transfer, destination->id}}};
        const auto count = static_cast<std::uint32_t>(root->resources().size());
        WebGpuGenericCommandProjectionBudget graphBudget{
            1u, true, 1u, 0u, 0u, 0u, 4u, count, {}, quota_->limitBytes(), root->retainedBytes()};
        for (const auto& ref : refs)
            ++graphBudget.maxDomainReferences[static_cast<std::size_t>(ref.domain)];
        WebGpuGenericCommandProjection::Sources sources;
        sources.transfer = std::move(root);
        const char* diagnostic = nullptr;
        auto graph =
            WebGpuGenericCommandProjection::shareOriginal(WebGpuGenericCommandProjection::prepare(
                std::move(sources), refs.data(), refs.size(), graphBudget, quota_, diagnostic));
        assert(graph);
        const std::array<CommandUsage, 6> commands{{{sourceKind, 0u, 1u},
            {destinationKind, 1u, 1u},
            {K::BeginTransferPass, 2u, 0u},
            {copy.kind(), 2u, 2u},
            {K::EndTransferPass, 4u, 0u},
            {K::Finish, 4u, 0u}}};
        auto usage = WebGpuGenericCommandUsage::prepareOriginal(graph,
            commands.data(),
            commands.size(),
            {1u, true, 6u, 4u, quota_->limitBytes()},
            quota_,
            diagnostic);
        assert(usage);
        const std::array<CommandVariableArguments::Input, 6> raw{
            {{*A::capture(sourceKind, A::None{})},
                {*A::capture(destinationKind, A::None{})},
                {*A::capture(K::BeginTransferPass, A::None{}), label},
                {*A::capture(copy.kind(), A::None{})},
                {*A::capture(K::EndTransferPass, A::None{})},
                {*A::capture(K::Finish, A::None{})}}};
        auto arguments = CommandVariableArguments::prepareOriginal(raw.data(),
            raw.size(),
            {1u, true, 6u, 0u, static_cast<std::uint32_t>(label.size()), 0u, quota_->limitBytes()},
            quota_,
            diagnostic);
        assert(arguments);
        scalars_ = WebGpuFrozenCommandScalars::prepareOriginal(
            usage, arguments, {1u, true, 6u, 0u, quota_->limitBytes()}, quota_, diagnostic);
        assert(scalars_);
    }

    std::shared_ptr<const rhi_webgpu::WebGpuOriginalResourceStates> prepare(
        const rhi::ResourceDependencyArguments& source,
        const rhi::ResourceDependencyArguments& destination,
        std::shared_ptr<core::CpuMemoryQuota> stateQuota) const
    {
        using namespace rhi_webgpu;
        const char* diagnostic = nullptr;
        const std::array<WebGpuOriginalCommandInputs::OriginalDescriptor, 3> records{
            {{0u, source}, {1u, destination}, {3u, copy_}}};
        auto inputs = WebGpuOriginalCommandInputs::prepareOriginal(scalars_,
            {},
            records.data(),
            records.size(),
            {1u, true, 3u, quota_->limitBytes()},
            quota_,
            diagnostic);
        assert(inputs);
        const auto count = static_cast<std::uint32_t>(
            scalars_->usage()->projection()->transfer()->resources().size());
        return WebGpuOriginalResourceStates::prepareOriginal(
            inputs, {1u, true, count, count, stateQuota->limitBytes()}, stateQuota, diagnostic);
    }

private:
    const rhi::TransferCopyArguments copy_;
    const std::shared_ptr<core::CpuMemoryQuota> quota_;
    std::shared_ptr<const rhi_webgpu::WebGpuFrozenCommandScalars> scalars_;
};
} // namespace arp::test
