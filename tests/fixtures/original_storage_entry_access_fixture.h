#pragma once
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/command_texture_ranges.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class OriginalStorageEntryAccessFixture final {
public:
    static void verify(const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
        const std::shared_ptr<rhi::OriginalCommandResourceSources>& inventory,
        bool store)
    {
        using namespace rhi;
        const auto& pipeline = std::get<std::shared_ptr<const BindlessPreparedComputeBinding>>(
            root->originalPrepared())
                                   ->owners()
                                   ->desc()
                                   .pipeline;
        const char* id = nullptr;
        const auto& package = root->authorization()->package();
        const auto* module =
            selectBindlessComputeEntryAccess(package, pipeline->shaderReflection(), id);
        assert(module && !id && module->entry == (store ? "storeMain" : "readMain"));
        for (unsigned field = 0u; field != 9u; ++field) {
            auto bad = pipeline->shaderReflection();
            if (field == 0u)
                bad.sourceSha256 = "incorrect";
            if (field == 1u)
                bad.descriptorBindings.front().stages = ShaderStage::Vertex;
            if (field == 2u)
                ++bad.descriptorBindings.front().arrayCount;
            if (field == 3u)
                ++bad.descriptorBindings.front().set;
            if (field == 4u)
                bad.descriptorBindings.front().binding = UINT32_MAX;
            if (field == 5u)
                bad.descriptorBindings.front().type = DescriptorType::Sampler;
            if (field == 6u)
                bad.descriptorBindings.push_back(bad.descriptorBindings.front());
            if (field == 7u)
                bad.descriptorBindings.pop_back();
            if (field == 8u)
                ++bad.computeWorkgroupSizeX;
            assert(!selectBindlessComputeEntryAccess(package, bad, id) && id);
        }
        assert(root->originalShaderEntryAccessVersion() == 1u);
        BindlessResourceAccess access;
        std::size_t declaration = SIZE_MAX;
        bool active = true;
        assert(root->readOriginalShaderDeclaration(
                   0u, ShaderStage::Compute, access, declaration, active, 2u)
            && !active);
        assert(root->readOriginalShaderDeclaration(
                   0u, ShaderStage::Vertex, access, declaration, active)
            && !active);
        assert(root->readOriginalShaderDeclaration(
                   root->size(), ShaderStage::Compute, access, declaration, active)
            && !active);
        const auto expected = store ? 4u : 6u; // Two physical cells/buffers + metadata/fault.
        std::size_t shaderCount = 0u;
        for (std::size_t i = 0u; i < root->size(); ++i) {
            assert(root->readOriginalDeclaration(i, access, declaration));
            assert(!root->readOriginalShaderDeclaration(
                i, ShaderStage::Compute, access, declaration, active));
            if (active)
                ++shaderCount;
        }
        assert(shaderCount == expected && root->size() == 9u && root->elements().size() == 8u);

        // Exercise the actual canonical dispatch projector. Retention keeps all
        // nine declarations; only the selected entry consumes shader resources.
        const auto bytes = root->originalMetadataQuota()->limitBytes();
        const auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        {
            core::CpuMemoryQuotaScope scope(quota);
            using K = CommandUsageKind;
            const std::array<CommandUsage, 6u> commands{{{K::RetainIndexedSnapshot, 0u, 1u},
                {K::BeginComputePass, 1u, 0u},
                {K::BindPreparedComputeBinding, 1u, 1u},
                {K::Dispatch, 2u, 0u},
                {K::EndComputePass, 2u, 0u},
                {K::Finish, 2u, 0u}}};
            const auto rootIndex = inventory->sources().size() - 1u;
            const std::array<OriginalCommandUsage::Reference, 2u> refs{
                {{static_cast<std::uint32_t>(rootIndex)}, {static_cast<std::uint32_t>(rootIndex)}}};
            auto usage = OriginalCommandUsage::prepareOriginal(
                inventory, commands.data(), commands.size(), refs.data(), refs.size(), quota, id);
            if (id)
                std::fprintf(stderr, "Storage entry usage: %s\n", id);
            assert(usage && !id);
            core::pmr::Vector<CommandVariableArguments::Input> fields;
            fields.reserve(commands.size());
            for (std::size_t i = 0u; i < commands.size(); ++i) {
                const auto scalar = commands[i].kind == K::Dispatch
                    ? CommandScalarArguments::capture(
                          K::Dispatch, CommandScalarArguments::Dispatch{1u, 1u, 1u})
                    : CommandScalarArguments::capture(
                          commands[i].kind, CommandScalarArguments::None{});
                assert(scalar);
                fields.push_back({*scalar});
            }
            auto arguments = CommandVariableArguments::prepareOriginal(
                fields.data(), fields.size(), {1u, true, 6u, 0u, 0u, 0u, bytes}, quota, id);
            assert(arguments && !id);
            auto scalars = OriginalCommandScalars::prepareOriginal(usage, arguments, quota, id);
            assert(scalars && !id);
            auto inputs = OriginalCommandInputs::prepareOriginal(scalars, nullptr, 0u, quota, id);
            if (id)
                std::fprintf(stderr, "Storage entry inputs: %s\n", id);
            assert(inputs && !id);
            OriginalBindlessRootStateBudget budget{2u, true, 1u, expected, UINT64_MAX};
            std::size_t textures = 0u;
            unsigned mipMask = 0u;
            auto emit = [&](const auto& point) noexcept -> const char* {
                assert(point.commandIndex == 3u && point.rootSourceIndex == rootIndex);
                if (point.declaration.kind == BindlessAccessKind::Texture) {
                    ++textures;
                    const auto mip = point.declaration.textureRange.baseMipLevel;
                    assert(mip == 1u || mip == 2u);
                    mipMask |= 1u << mip;
                    assert(point.declaration.access
                        == (store ? ResourceAccess::ShaderWrite : ResourceAccess::ShaderRead));
                }
                return nullptr;
            };
            const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
            const auto projected =
                common::OriginalCommandBindlessRootAccesses::visit(inputs, budget, emit);
            if (projected.diagnostic)
                std::fprintf(stderr, "Storage entry projection: %s\n", projected.diagnostic);
            assert(!projected.diagnostic && projected.roots == 1u && projected.accesses == expected
                && textures == 2u && mipMask == 6u);
            assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
            budget.maxSourceVisits = projected.sourceVisits;
            assert(!common::OriginalCommandBindlessRootAccesses::visit(inputs, budget, emit)
                    .diagnostic);
            --budget.maxSourceVisits;
            assert(common::OriginalCommandBindlessRootAccesses::visit(inputs, budget, emit)
                    .diagnostic);
            budget.maxSourceVisits = projected.sourceVisits;
            --budget.maxAccesses;
            assert(common::OriginalCommandBindlessRootAccesses::visit(inputs, budget, emit)
                    .diagnostic);
        }
        assert(!quota->liveBytes() && !root->nativeExecutionProven());
    }
};
} // namespace arp::test
