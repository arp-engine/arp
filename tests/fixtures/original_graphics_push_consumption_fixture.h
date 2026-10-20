#pragma once
#include "original_bindless_root_source_fixture.h"
#include "original_command_upload_stream_builder_fixture.h"

namespace arp::test {
struct OriginalGraphicsPushPipelineFixture {
    static constexpr bool enabled = false, graphicsPush = true;

    void configureAdmission(rhi::OriginalCommandAdmissionBudget& budget) const
    {
        budget.maxPushBytes = 448u;
    }
};

class OriginalGraphicsPushConsumptionFixture final {
public:
    void operator()(const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& sources,
        rhi::GpuDevice* device) const
    {
        if (!device)
            return;
        using namespace rhi;
        using K = CommandUsageKind;
        constexpr auto stages = static_cast<ShaderStage>(
            std::uint32_t(ShaderStage::Vertex) | std::uint32_t(ShaderStage::Fragment));
        const auto rootIndex = static_cast<std::uint32_t>(sources->originalLeaseIndex(root));
        std::shared_ptr<const OriginalCommandTextureViewSource> view;
        for (const auto& source : sources->sources())
            if (const auto* candidate =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(&source))
                if ((*candidate)->originalObject()->desc().usage == TextureUsage::RenderAttachment)
                    view = *candidate;
        assert(view);
        const auto viewIndex = static_cast<std::uint32_t>(sources->originalLeaseIndex(view));
        auto quota = core::makeShared<core::CpuMemoryQuota>(65536u);
        const char* id = nullptr;
        RenderPassDesc pass;
        RenderPassColorAttachmentDesc color;
        color.target = view->resource();
        color.format = view->originalParentLease()->originalObject()->desc().format;
        color.loadOp = LoadOp::Clear;
        color.storeOp = StoreOp::Store;
        pass.colorAttachments.push_back(color);
        auto retainedPass = RenderPassArguments::prepareOriginal(
            pass, {1u, true, 1u, 0u, 0u, 0u, 0u, quota->limitBytes()}, quota, id);
        assert(retainedPass && !id);
        const std::array commands{CommandUsage{K::RetainIndexedSnapshot, 0u, 1u},
            CommandUsage{K::BeginRenderPass, 1u, 1u},
            CommandUsage{K::BindPreparedGraphicsBinding, 2u, 1u},
            CommandUsage{K::PushConstants, 3u, 1u},
            CommandUsage{K::Draw, 4u, 0u},
            CommandUsage{K::PushConstants, 4u, 1u},
            CommandUsage{K::Draw, 5u, 0u},
            CommandUsage{K::EndRenderPass, 5u, 0u},
            CommandUsage{K::Finish, 5u, 0u}};
        const std::array refs{OriginalCommandUsage::Reference{rootIndex},
            OriginalCommandUsage::Reference{viewIndex},
            OriginalCommandUsage::Reference{rootIndex},
            OriginalCommandUsage::Reference{rootIndex},
            OriginalCommandUsage::Reference{rootIndex}};
        std::array descriptors{OriginalCommandInputs::OriginalDescriptor{1u, retainedPass}};
        std::array<unsigned char, 224u> bytes{};
        std::array<unsigned char, 224u> second{};
        second.fill(0xA5u);
        core::pmr::Vector<CommandVariableArguments::Input> fields;
        fields.reserve(commands.size());
        for (std::size_t i = 0u; i < commands.size(); ++i) {
            const auto kind = commands[i].kind;
            const auto scalar = kind == K::PushConstants
                ? *CommandScalarArguments::capture(
                      kind, CommandScalarArguments::Push{stages, 0u, 224u})
                : kind == K::Draw
                ? *CommandScalarArguments::capture(
                      kind, CommandScalarArguments::Draw{3u, 1u, 0u, 0u})
                : *CommandScalarArguments::capture(kind, CommandScalarArguments::None{});
            fields.push_back({scalar});
        }
        fields[3].bytes = bytes.data();
        fields[3].byteCount = bytes.size();
        fields[5].bytes = second.data();
        fields[5].byteCount = second.size();
        auto inputs = OriginalCommandUploadStreamBuilderFixture::prepareRetained(sources,
            commands.data(),
            commands.size(),
            refs.data(),
            refs.size(),
            fields.data(),
            descriptors.data(),
            descriptors.size(),
            {1u, false},
            quota,
            id);
        assert(inputs && !id);
        bytes.fill(255u);
        second.fill(255u);
        OriginalBindlessRootStateBudget budget{2u, true, 2u, 2u * root->size(), 8192u};
        budget.pushConsumption = {1u, true, 2u, 448u};
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array{quota}, [&]() noexcept -> const char* {
                const auto result = common::OriginalCommandBindlessRootAccesses::visit(
                    inputs, budget, [](const auto&) noexcept -> const char* {
                        return nullptr;
                    });
                if (result.diagnostic)
                    std::fprintf(stderr, "Push Root consumption: %s\n", result.diagnostic);
                assert(!result.diagnostic && result.roots == 2u);
                for (const bool updates : {false, true}) {
                    auto less = budget;
                    if (updates)
                        --less.pushConsumption.maxUpdates;
                    else
                        --less.pushConsumption.maxBytes;
                    assert(common::OriginalCommandBindlessRootAccesses::visit(
                        inputs, less, [](const auto&) noexcept -> const char* {
                            return nullptr;
                        }).diagnostic);
                }
                auto absent = budget;
                absent.pushConsumption = {};
                assert(common::OriginalCommandBindlessRootAccesses::visit(
                    inputs, absent, [](const auto&) noexcept -> const char* {
                        return nullptr;
                    }).diagnostic);
                return nullptr;
            }));
        inputs.reset();
        std::get<std::shared_ptr<const RenderPassArguments>>(descriptors[0].arguments).reset();
        retainedPass.reset();
        assert(!quota->liveBytes());
        std::puts(
            "BINDLESS_ORIGINAL_PUSH_ROOT_CPU_OK authentic-pipeline updates2 full448 frozen-bytes hot0 retirement0 no-gpu");
    }
};
} // namespace arp::test
