#pragma once
#include "original_accepted_compute_pipeline_factory_fixture.h"
#include "original_accepted_graphics_pipeline_factory_fixture.h"
#include "original_runtime_length_pipeline_input_fixture.h"
#include "webgpu_ordinary_pipeline_fixture.h"

namespace arp::test {
// Creation-only evidence; shares existing actors and never records GPU commands.
class OriginalRuntimeLengthNativePipelineFixture final {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
    template <class Context> static void verifyContext(Context& c)
    {
        const auto input = c.receipt->originalInput();
        using Input = std::remove_const_t<typename decltype(input)::element_type>;
        constexpr bool graphics = std::is_same_v<Input, rhi::OriginalGraphicsPipelineInput>;
        assert(!c.publish());
        EM_ASM({
            // clang-format off
                const d = Module.preinitializedWebGPUDevice;
                Module.arpRuntimeLengthShader = d.createShaderModule;
                Module.arpRuntimeLengthCompute = d.createComputePipeline;
                Module.arpRuntimeLengthGraphics = d.createRenderPipeline;
                Module.arpRuntimeLengthShaderCalls = 0;
                Module.arpRuntimeLengthPipelineCalls = 0;
                d.createShaderModule = function(s) {
                    Module.arpRuntimeLengthShaderCalls++;
                    return Module.arpRuntimeLengthShader.call(this, s);
                };
                d.createComputePipeline = function(s) {
                    Module.arpRuntimeLengthPipelineCalls++;
                    return Module.arpRuntimeLengthCompute.call(this, s);
                };
                d.createRenderPipeline = function(s) {
                    Module.arpRuntimeLengthPipelineCalls++;
                    return Module.arpRuntimeLengthGraphics.call(this, s);
                };
            // clang-format on
        });
        c.ready();
        assert(c.receipt->scopesObserved() && c.receipt->sourceDiagnostic().empty());
        assert(core::SharedOwnerProjection::sameOwner(c.receipt->originalInput(), input));
        assert(c.receipt->definition().shaderPackage == &input->package());
        auto pipeline = c.publish();
        if (!pipeline)
            std::fprintf(stderr,
                "runtime-length accepted wrapper: %s\n",
                c.device->lastOriginalResourceRegistrationDiagnostic().data());
        assert(pipeline);
        auto native = rhi_webgpu::webGpuNativePayloadFor(*pipeline);
        assert(native);
        const auto& issued = native->issuedContract();
        assert(issued);
        if constexpr (graphics)
            assert(core::SharedOwnerProjection::sameOwner(issued->graphicsInput(), input));
        else
            assert(core::SharedOwnerProjection::sameOwner(issued->artifactInput(), input));
        const auto target = Input::selectTarget(input, rhi::Backend::WebGPU);
        assert(!target.diagnostic);
        const auto bytes = input->artifact()->bytes(shader::ShaderArtifactRole::Wgsl);
        const auto digest = core::Sha256::digest(
            core::StringView(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        assert(issued->stages().size() == (graphics ? 2u : 1u));
        for (const auto& stage : issued->stages())
            assert(stage.sourceBytes == bytes.size()
                && std::equal(digest.begin(), digest.end(), stage.sourceSha256.begin()));
        auto changed = rhi::ShaderPackageStateCopy::copy(input->package());
        changed.label = "copied caller package";
        assert(Input::packageDiagnostic(input, changed)
            && !rhi::validateShaderPackage(changed).success);
        const char* diagnostic = nullptr;
        if constexpr (graphics) {
            auto definition = rhi::RenderPipelineStateCopy::copy(c.receipt->definition());
            definition.shaderPackage = &changed;
            assert(!rhi_webgpu::WebGpuAcceptedOriginalGraphicsPipelineFactory::create(
                definition, c.receipt, c.quotas[6], diagnostic));
        } else {
            auto definition = rhi::ComputePipelineStateCopy::copy(c.receipt->definition());
            definition.shaderPackage = &changed;
            assert(!rhi_webgpu::WebGpuAcceptedOriginalComputePipelineFactory::create(
                definition, c.receipt, c.quotas[6], diagnostic));
        }
        assert(diagnostic);
        pipeline.reset();
        auto second = c.publish();
        assert(second);
        assert(core::SharedOwnerProjection::sameOwner(
            native, rhi_webgpu::webGpuNativePayloadFor(*second)));
        second.reset();
        native.reset();
        for (const auto index : std::array{1u, 6u}) {
            CpuQuotaFactoryFixture pressure(c.quotas[index]);
            const auto factory = [&] {
                return c.publish();
            };
            const auto peak = pressure.measure(factory);
            pressure.verify(peak, factory);
            {
                core::CpuMemoryQuotaLease full(
                    c.quotas[index], c.quotas[index]->limitBytes() - c.quotas[index]->liveBytes());
                assert(full.charged() && !factory());
            }
            assert(!c.receipt->validate());
        }
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(c.quotas, [&]() noexcept {
            return c.receipt->validate();
        }));
        assert(EM_ASM_INT({
            // clang-format off
                return Module.arpRuntimeLengthShaderCalls === 1 && Module.arpRuntimeLengthPipelineCalls === 1;
            // clang-format on
        }));
        EM_ASM({
            // clang-format off
                const d = Module.preinitializedWebGPUDevice;
                d.createShaderModule = Module.arpRuntimeLengthShader;
                d.createComputePipeline = Module.arpRuntimeLengthCompute;
                d.createRenderPipeline = Module.arpRuntimeLengthGraphics;
            // clang-format on
        });
        assert(c.receipt->executionDiagnostic());
    }

    template <bool Graphics, class Factory>
    static void publication(Factory&& create, const char* path, const char* folder)
    {
        OriginalRuntimeLengthFixture authored;
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        const auto budget = [&] {
            if constexpr (Graphics)
                return authored.moduleBudget();
            else
                return authored.entryBudget();
        }();
        using Artifact = std::conditional_t<Graphics,
            shader::GraphicsShaderModuleArtifact,
            shader::SingleEntryShaderArtifact>;
        auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        auto artifact = Artifact::load(path, budget, quota, diagnostic);
        assert(artifact);
        auto input = OriginalRuntimeLengthPipelineInputFixture::verify(artifact);
        auto inputQuota = input->quota();
        auto layout = rhi::buildPipelineLayoutDescFromShaderReflection(input->package().reflection);
        EM_ASM(
            {
                // clang-format off
                    const p = UTF8ToString($0);
                    FS.rename(p, p + ".available-input-hidden");
                // clang-format on
            },
            folder);
        artifact.reset();
        auto context = [&] {
            if constexpr (Graphics) {
                auto state = WebGpuOrdinaryPipelineFixture::graphicsState();
                state.vertexShader = input->package().entrypoints[0];
                state.fragmentShader = input->package().entrypoints[1];
                return OriginalAcceptedGraphicsPipelineFactoryFixture::prepareContext(
                    create, input, layout, state);
            } else
                return OriginalAcceptedComputePipelineFactoryFixture::prepareContext(
                    create, input, layout);
        }();
        input.reset();
        verifyContext(*context);
        context->retire();
        context.reset();
        assert(!quota->liveBytes() && !inputQuota->liveBytes());
        EM_ASM(
            {
                // clang-format off
                    const p = UTF8ToString($0);
                    FS.rename(p + ".available-input-hidden", p);
                // clang-format on
            },
            folder);
        std::printf(
            "BINDLESS_RHI_RUNTIME_LENGTH_NATIVE_PIPELINE_OK %s original-input exact-target scopes-complete wrapper-reuse quotas-zero files-absent execution-closed\n",
            Graphics ? "graphics" : path);
    }
#endif
public:
    template <class Factory> static void verify(Factory&& create)
    {
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)                                             \
    && defined(WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT)
        for (const auto* variant : {"structured", "rwstructured", "byteaddress", "rwbyteaddress"}) {
            core::pmr::String folder(
                "/runtime-length-fixture/original/", &core::defaultCpuMemoryResource());
            folder += variant;
            core::pmr::String path(folder, &core::defaultCpuMemoryResource());
            path += "/runtime_length_";
            path += variant;
            path += ".computeMain.entry.yaml";
            publication<false>(create, path.c_str(), folder.c_str());
        }
        publication<true>(create,
            "/runtime-length-fixture/original/graphics/runtime_length_graphics.graphics.module.yaml",
            "/runtime-length-fixture/original/graphics");
#else
        (void)create;
#endif
    }
};
} // namespace arp::test
