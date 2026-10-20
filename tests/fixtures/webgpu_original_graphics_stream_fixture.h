#pragma once
#include "rhi/backends/webgpu/webgpu_original_graphics_stream_validation.h"
#include "webgpu_original_graphics_batch_fixture.h"

#include <cassert>
#include <cstdio>

namespace arp::test {
class WebGpuOriginalGraphicsStreamFixture final {
    using Validation = rhi_webgpu::WebGpuOriginalGraphicsStreamValidation;
    using States = rhi_webgpu::WebGpuOriginalResourceStates;

public:
    static void verifyContract()
    {
        assert(std::string_view(Validation::validate({}, {}))
            == "RHI-WEBGPU-ORIGINAL-ACCESS-STATE-OWNER");
    }
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
    static void verify(const std::shared_ptr<const rhi_webgpu::WebGpuOriginalCommandInputs>& inputs,
        const std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryGraphicsTriangleRange>& range)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        const char* diagnostic = nullptr;
        const auto& graph = *inputs->scalars()->usage()->projection();
        auto texture = graph.resources()->resources()[0].backing().texture;
        WebGpuOrdinaryPhysicalGroups::OriginalBufferBinding binding;
        assert(!WebGpuOrdinaryPhysicalGroups::originalBufferBinding(range->originalGroups(),
            range->facts().target.group,
            range->facts().target.binding,
            binding));
        auto buffer = binding.resource->backing().buffer;
        const auto originalTexture = texture->recordedState().state(),
                   originalBuffer = buffer->recordedState().state();
        assert(!texture->recordState(ResourceState::ColorAttachment)
            && !buffer->recordState(ResourceState::ShaderRead));
        auto quota = core::makeShared<core::CpuMemoryQuota>(16384u);
        const auto prepare = [&](const std::shared_ptr<const WebGpuOriginalCommandInputs>& source) {
            return States::prepareOriginal(
                source, {1u, true, 2u, 2u, quota->limitBytes()}, quota, diagnostic);
        };
        auto states = prepare(inputs);
        assert(states);
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        auto* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        assert(!Validation::validate(states, range));
        auto forged = std::shared_ptr<const States>(quota, states.get());
        assert(std::string_view(Validation::validate(forged, range))
            == "RHI-WEBGPU-ORIGINAL-ACCESS-STATE-OWNER");
        auto forgedRange =
            std::shared_ptr<const WebGpuOrdinaryGraphicsTriangleRange>(quota, range.get());
        assert(std::string_view(Validation::validate(states, forgedRange))
            == "RHI-WEBGPU-ORIGINAL-ACCESS-RANGE-OWNER");
        core::MutationJournal pending;
        assert(!texture->recordState(ResourceState::ColorAttachment, &pending));
        assert(std::string_view(Validation::validate(states, range))
            == "RHI-WEBGPU-ORIGINAL-STATES-PENDING");
        pending.rollback();
        assert(std::string_view(Validation::validate(states, range))
            == "RHI-WEBGPU-ORIGINAL-STATES-STALE");
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::pmr::set_default_resource(previous);
        forged.reset();
        states.reset();
        for (bool colorWrong : {true, false}) {
            assert(!texture->recordState(
                colorWrong ? ResourceState::CopyDst : ResourceState::ColorAttachment));
            assert(!buffer->recordState(
                colorWrong ? ResourceState::ShaderRead : ResourceState::CopyDst));
            states = prepare(inputs);
            assert(states);
            assert(std::string_view(Validation::validate(states, range))
                == "RHI-ACCESS-STATE-MISMATCH");
            WebGpuOriginalGraphicsBatchFixture::verifyRejected(
                states, range, "RHI-ACCESS-STATE-MISMATCH");
            states.reset();
        }
        assert(!texture->recordState(ResourceState::ColorAttachment)
            && !buffer->recordState(ResourceState::ShaderRead));
        verifyFrozenScalars(inputs, range, quota);
        assert(!texture->recordState(originalTexture) && !buffer->recordState(originalBuffer));
        assert(!retained_);
        retained_ = prepare(inputs);
        retainedRange_ = range;
        retainedQuota_ = quota;
        assert(retained_
            && std::string_view(retained_->executionDiagnostic())
                == webGpuGenericRecordingUsageDiagnostic);
        std::puts(
            "BINDLESS_RHI_ORIGINAL_GRAPHICS_STREAM_OK 8 3 2 original-color-buffer viewport scissor state rollback owner allocation0");
    }

    static void verifyActualDeviceLoss()
    {
        assert(retained_ && rhi_webgpu::webGpuNativeDeviceLost());
        assert(std::string_view(Validation::validate(retained_, retainedRange_))
            == "RHI-WEBGPU-COMMAND-PROJECTION-DEVICE-LOST");
        retained_.reset();
        retainedRange_.reset();
        assert(!retainedQuota_->liveBytes());
        retainedQuota_.reset();
        std::puts("BINDLESS_RHI_ORIGINAL_GRAPHICS_STREAM_LOSS_OK source-reject retire");
    }

private:
    static void verifyFrozenScalars(
        const std::shared_ptr<const rhi_webgpu::WebGpuOriginalCommandInputs>& inputs,
        const std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryGraphicsTriangleRange>& range,
        const std::shared_ptr<core::CpuMemoryQuota>& quota)
    {
        using namespace rhi;
        using namespace rhi_webgpu;
        using A = CommandScalarArguments;
        using Store = CommandVariableArguments;
        using K = CommandUsageKind;
        const char* diagnostic = nullptr;
        const auto& records = inputs->scalars()->arguments()->records();
        std::array<Store::Input, 8> raw{{{records[0].scalar},
            {records[1].scalar},
            {records[2].scalar},
            {records[3].scalar},
            {records[4].scalar},
            {records[5].scalar},
            {records[6].scalar},
            {records[7].scalar}}};
        const auto& offsets = range->originalGroups()->observedSets()[0].observation->offsets();
        raw[4].offsets = offsets.data();
        raw[4].offsetCount = static_cast<std::uint32_t>(offsets.size());
        const auto expect = [&](const char* expected) {
            auto arguments = Store::prepareOriginal(raw.data(),
                raw.size(),
                {1u, true, 8u, raw[4].offsetCount, 0u, 0u, quota->limitBytes()},
                quota,
                diagnostic);
            assert(arguments);
            auto scalars = WebGpuFrozenCommandScalars::prepareOriginal(inputs->scalars()->usage(),
                arguments,
                {1u, true, 8u, 0u, quota->limitBytes()},
                quota,
                diagnostic);
            assert(scalars);
            auto clone = WebGpuOriginalCommandInputs::prepareOriginal(scalars,
                inputs->passes(),
                nullptr,
                0u,
                {1u, true, 0u, quota->limitBytes()},
                quota,
                diagnostic);
            assert(clone);
            auto states = States::prepareOriginal(
                clone, {1u, true, 2u, 2u, quota->limitBytes()}, quota, diagnostic);
            assert(states);
            assert(std::string_view(Validation::validate(states, range)) == expected);
            WebGpuOriginalGraphicsBatchFixture::verifyRejected(states, range, expected);
        };
        auto viewport = *records[2].scalar.get<ViewportState>();
        viewport.width *= 0.5f;
        raw[2].scalar = *A::capture(K::SetViewport, viewport);
        expect("RHI-WEBGPU-ORIGINAL-GRAPHICS-VIEWPORT");
        raw[2].scalar = records[2].scalar;
        auto scissor = *records[3].scalar.get<ScissorState>();
        ++scissor.width;
        raw[3].scalar = *A::capture(K::SetScissor, scissor);
        expect("RHI-WEBGPU-ORIGINAL-GRAPHICS-SCISSOR");
        raw[3].scalar = records[3].scalar;
        raw[5].scalar = *A::capture(K::Draw, A::Draw{4u, 1u, 0u, 0u});
        expect("SHADER-GRAPHICS-TRIANGLE-DRAW-DOMAIN");
        raw[5].scalar = records[5].scalar;
        std::array<CommandUsage, 9> commands{};
        std::copy_n(inputs->scalars()->usage()->commands().begin(), 7u, commands.begin());
        commands[7] = {K::InsertDebugMarker, 3u, 0u};
        commands[8] = inputs->scalars()->usage()->commands()[7];
        auto usage =
            WebGpuGenericCommandUsage::prepareOriginal(inputs->scalars()->usage()->projection(),
                commands.data(),
                commands.size(),
                {1u, true, 9u, 3u, quota->limitBytes()},
                quota,
                diagnostic);
        assert(usage);
        const std::array<Store::Input, 9> extraRaw{{raw[0],
            raw[1],
            raw[2],
            raw[3],
            raw[4],
            raw[5],
            raw[6],
            {*A::capture(K::InsertDebugMarker, A::None{}), "extra"},
            raw[7]}};
        auto arguments = Store::prepareOriginal(extraRaw.data(),
            extraRaw.size(),
            {1u, true, 9u, raw[4].offsetCount, 5u, 0u, quota->limitBytes()},
            quota,
            diagnostic);
        assert(arguments);
        auto scalars = WebGpuFrozenCommandScalars::prepareOriginal(
            usage, arguments, {1u, true, 9u, 0u, quota->limitBytes()}, quota, diagnostic);
        assert(scalars);
        const auto& originalPass = inputs->passes()->passes()[0];
        auto passes = WebGpuFrozenRenderPassBinding::prepareOriginal(
            usage, &originalPass, 1u, {1u, true, 1u, quota->limitBytes()}, quota, diagnostic);
        assert(passes);
        auto clone = WebGpuOriginalCommandInputs::prepareOriginal(
            scalars, passes, nullptr, 0u, {1u, true, 0u, quota->limitBytes()}, quota, diagnostic);
        assert(clone);
        auto states = States::prepareOriginal(
            clone, {1u, true, 2u, 2u, quota->limitBytes()}, quota, diagnostic);
        assert(states);
        assert(std::string_view(Validation::validate(states, range))
            == "RHI-WEBGPU-ORIGINAL-GRAPHICS-LANGUAGE");
        WebGpuOriginalGraphicsBatchFixture::verifyRejected(
            states, range, "RHI-WEBGPU-ORIGINAL-GRAPHICS-LANGUAGE");
        states.reset();
        clone.reset();
        passes.reset();
        scalars.reset();
        arguments.reset();
        usage.reset();
        assert(!quota->liveBytes());
    }

    inline static std::shared_ptr<const States> retained_;
    inline static std::shared_ptr<const rhi_webgpu::WebGpuOrdinaryGraphicsTriangleRange>
        retainedRange_;
    inline static std::shared_ptr<core::CpuMemoryQuota> retainedQuota_;
#endif
};
} // namespace arp::test
