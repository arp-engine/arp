#pragma once

#include "core/hash/sha256.h"
#include "rhi/backends/webgl/webgl_execution.h"
#include "rhi/backends/webgl/webgl_pipeline_internal.h"

#define GL_GLEXT_PROTOTYPES
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <emscripten.h>

namespace arp::rhi::webgl::test {
EM_JS(int, extensionExecutionMode, (), {
    // clang-format off
    return new URLSearchParams(location.search).get('extensionsOnly') === '1';
    // clang-format on
});

// Synthetic absence only changes the test query. Native success always uses
// the unmodified browser and original context-owned extension enablement.
EM_JS(void, extensionQueryFilter, (int field), {
    // clang-format off
    const proto = WebGLRenderingContext.prototype;
    if (!Module.extensionOriginalQuery) Module.extensionOriginalQuery = proto.getSupportedExtensions;
    const original = Module.extensionOriginalQuery;
    const names = ['OES_element_index_uint','ANGLE_instanced_arrays','OES_vertex_array_object',
        'WEBGL_depth_texture','OES_standard_derivatives'];
    proto.getSupportedExtensions = field < 0 ? original : function() {
        return original.call(this).filter(name => name !== names[field]);
    };
    // clang-format on
});

EM_JS(void, reportExtensionExecution, (int success, const char* diagnostic, int negatives), {
    // clang-format off
    fetch('/result', {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({
        type:'WebGlExtensionExecutionSmoke', contractVersion:1, success:!!success,
        diagnostic:UTF8ToString(diagnostic),negatives,
        webGpuUnavailable:navigator.gpu === undefined,
        subscriptionsRetired:Module.webGlOwnedSubscriptions.size === 0,
        native:Module.webGlExtensionNative || null,
        gpuCompletionClaimed:false, productionReady:false})});
    Module.arpWorkloadFinished?.();
    // clang-format on
});

EM_JS(void, beginExtensionNativeAudit, (const char* identity), {
    // clang-format off
    const context = GL.currentContext.GLctx;
    const evidence = {programIdentity:UTF8ToString(identity), version:context.getParameter(context.VERSION), renderer:context.getParameter(context.RENDERER),
        browser:navigator.userAgent, extensions:context.getSupportedExtensions(),
        uint32Draws:0,instances:0,vaoBinds:0,
        divisorWrites:0,depthAttachments:0};
    (Module.webGlExtensionNative ||= []).push(evidence);

    const originalBind = context.bindVertexArray;
    const bind = originalBind.bind(context);
    context.bindVertexArray = value => {if(value) ++evidence.vaoBinds; return bind(value);};
    const originalDivisor = context.vertexAttribDivisor;
    const divisor = originalDivisor.bind(context);
    context.vertexAttribDivisor = (slot,value) => {++evidence.divisorWrites; return divisor(slot,value);};
    const originalDraw = context.drawElementsInstanced;
    const draw = originalDraw.bind(context);
    context.drawElementsInstanced = (mode,count,type,offset,instances) => {
        if(type === context.UNSIGNED_INT && count === 6) ++evidence.uint32Draws;
        evidence.instances += instances;
        return draw(mode,count,type,offset,instances);
    };
    const originalAttach = context.framebufferTexture2D;
    const attach = originalAttach.bind(context);
    context.framebufferTexture2D = (target,attachment,...rest) => {
        if(attachment === context.DEPTH_STENCIL_ATTACHMENT && rest[1]) ++evidence.depthAttachments;
        return attach(target,attachment,...rest);
    };
    Module.endExtensionAudit = () => {
        context.bindVertexArray = originalBind;
        context.vertexAttribDivisor = originalDivisor;
        context.drawElementsInstanced = originalDraw;
        context.framebufferTexture2D = originalAttach;
    };
    // clang-format on
});

EM_JS(void, endExtensionNativeAudit, (), {
    // clang-format off
    Module.endExtensionAudit?.(); Module.endExtensionAudit = null;
    // clang-format on
});

class ExtensionNativeAuditScope final {
public:
    explicit ExtensionNativeAuditScope(const char* identity)
    {
        beginExtensionNativeAudit(identity);
    }

    ~ExtensionNativeAuditScope() { endExtensionNativeAudit(); }
};

class ExtensionExecutionSmoke final {
public:
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    int negatives = 0;

    bool run(int (*lose)(), int (*restore)())
    {
        EM_ASM({
            // clang-format off
            Module.webGlExtensionNative = [];
            // clang-format on
        });
        using namespace semantics;
        constexpr std::array fields{&WebGlRuntimeExtensions::uint32Indices,
            &WebGlRuntimeExtensions::instancing,
            &WebGlRuntimeExtensions::vertexArrayObjects,
            &WebGlRuntimeExtensions::depthTextures,
            &WebGlRuntimeExtensions::derivatives};
        WebGlContextOwnershipRequest context;
        context.contractVersion = context.context.contractVersion = 1;
        context.canvasSelector = "#canvas";
        context.colorSpace = WebGlCanvasColorSpace::SRgb;
        context.maxCpuMetadataBytes = 256 * 1024;
        context.maxIdentityFieldBytes = 4096;
        context.extensionOptIn = {1, {true, true, true, true, true}};
        WebGlResourceOwnershipRequest ownership;
        ownership.contractVersion = ownership.budget.contractVersion = 1;
        ownership.budget.capacity.cpuMetadataBytes = 4 * 1024 * 1024;
        ownership.budget.capacity.cpuUploadBytes = 4 * 1024 * 1024;
        ownership.budget.capacity.logicalBufferBytes = 2 * 1024 * 1024;
        ownership.budget.capacity.logicalTextureBytes = 128 * 1024;
        ownership.budget.capacity.objectCount = 64;
        ownership.extensions = {1, true, context.extensionOptIn.extensions};
        for (std::size_t i = 0; i < fields.size(); ++i) {
            auto quota = core::makeShared<core::CpuMemoryQuota>(context.maxCpuMetadataBytes);
            const char* id = nullptr;
            extensionQueryFilter(static_cast<int>(i));
            auto missing = WebGlRuntime::create(context, quota, id);
            extensionQueryFilter(-1);
            if (!check(!missing && id && std::string_view(id) == "RHI-WEBGL1-EXTENSION-UNAVAILABLE"
                        && !quota->liveBytes(),
                    "EXTENSION-NATIVE-ABSENCE-REJECTED"))
                return false;
            ++negatives;
            auto consent = context;
            consent.extensionOptIn.extensions.*fields[i] = false;
            auto runtime = WebGlRuntime::create(consent, quota, id);
            if (!check(bool(runtime), id))
                return false;
            auto metadata =
                core::makeShared<core::CpuMemoryQuota>(ownership.budget.capacity.cpuMetadataBytes);
            auto upload =
                core::makeShared<core::CpuMemoryQuota>(ownership.budget.capacity.cpuUploadBytes);
            auto rejected =
                WebGlResources::create(std::move(runtime), ownership, metadata, upload, id);
            if (!check(!rejected && id && !quota->liveBytes() && !metadata->liveBytes()
                        && !upload->liveBytes(),
                    "EXTENSION-ORIGINAL-CONSENT-REJECTED"))
                return false;
            ++negatives;
        }
        auto contextQuota = core::makeShared<core::CpuMemoryQuota>(context.maxCpuMetadataBytes);
        const char* id = nullptr;
        auto runtime = WebGlRuntime::create(context, contextQuota, id);
        if (!check(bool(runtime), id))
            return false;
        auto metadata =
            core::makeShared<core::CpuMemoryQuota>(ownership.budget.capacity.cpuMetadataBytes);
        auto upload =
            core::makeShared<core::CpuMemoryQuota>(ownership.budget.capacity.cpuUploadBytes);
        auto resources =
            WebGlResources::create(std::move(runtime), ownership, metadata, upload, id);
        if (!check(bool(resources), id))
            return false;
        if (!check(!resources->enableRecovery({1, true, 1}), "EXTENSION-RECOVERY-ENABLE"))
            return false;
        if (!execute(*resources))
            return false;
        auto oldPipeline = retainedPipeline;
        if (!check(lose() && resources->contextPhase() == WebGlContextPhase::Lost
                    && WebGlRenderPipeline::current(*oldPipeline->impl)
                    && !WebGlExecution::submit(*resources, *retainedCommand).success,
                "EXTENSION-LOSS-CLOSES-ORIGINAL"))
            return false;
        if (!check(restore() && resources->contextPhase() == WebGlContextPhase::RestorePending,
                "EXTENSION-NATIVE-RESTORE-PENDING"))
            return false;
        auto nextMetadata =
            core::makeShared<core::CpuMemoryQuota>(ownership.budget.capacity.cpuMetadataBytes);
        auto nextUpload =
            core::makeShared<core::CpuMemoryQuota>(ownership.budget.capacity.cpuUploadBytes);
        auto changed = ownership;
        changed.extensions.required.derivatives = false;
        if (!check(!resources->restore(changed, nextMetadata, nextUpload, id) && id
                    && std::string_view(id) == "RHI-WEBGL1-RECOVERY-EXTENSION-WORKLOAD-MISMATCH",
                "EXTENSION-RESTORE-INTENT-IMMUTABLE"))
            return false;
        auto rebuilt = resources->restore(ownership, nextMetadata, nextUpload, id);
        if (!check(bool(rebuilt), id))
            return false;
        if (!check(WebGlRenderPipeline::current(*oldPipeline->impl)
                    && !WebGlExecution::submit(*rebuilt, *retainedCommand).success,
                "EXTENSION-RESTORE-OLD-OWNERS-STAY-CLOSED"))
            return false;
        retainedCommand.reset();
        retainedPipeline.reset();
        const bool success = execute(*rebuilt);
        retainedCommand.reset();
        retainedPipeline.reset();
        rebuilt.reset();
        resources.reset();
        oldPipeline.reset();
        return success
            && check(!nextMetadata->liveBytes() && !nextUpload->liveBytes()
                    && !metadata->liveBytes() && !upload->liveBytes() && !contextQuota->liveBytes(),
                "EXTENSION-RESTORED-QUOTA-RETIREMENT");
    }

private:
    std::shared_ptr<WebGlRenderPipeline> retainedPipeline;
    core::UniquePtr<CommandBuffer> retainedCommand;

    bool check(bool value, const char* id)
    {
        if (!value)
            diagnostic = id ? id : "EXTENSION-EXECUTION-UNKNOWN";
        return value;
    }

    bool execute(WebGlResources& resources)
    {
        using namespace semantics;
        shader::WebGlShaderLoadBudget budget{
            {4 * 1024 * 1024, 32 * 1024 * 1024, 32 * 1024 * 1024, 32}, 256, 32, 1024};
        auto artifactQuota =
            core::makeShared<core::CpuMemoryQuota>(budget.original.maxCpuMetadataBytes);
        auto artifact = shader::WebGlShaderProgramArtifact::load(
            "/artifacts/extension-execution/webgl1_extension_execution.webgl-program.yaml",
            budget,
            artifactQuota,
            diagnostic);
        if (!artifact)
            return false;
        ShaderPackageDesc package;
        const auto reflected = populateSelectedGraphicsShaderInterface(
            *artifact->vertex().original, artifact->fragment().original.get(), package);
        if (!check(reflected.success, reflected.diagnostics.c_str()))
            return false;
        auto definition = buildPipelineLayoutDescFromShaderReflection(package.reflection);
        auto layout = core::shareUnique(resources.createPipelineLayout(definition));
        if (!check(bool(layout), resources.diagnostic()))
            return false;
        RenderPipelineDesc state;
        state.layout = layout.get();
        state.vertexShader = package.entrypoints[0];
        state.fragmentShader = package.entrypoints[1];
        state.colorFormat = TextureFormat::Rgba8Unorm;
        state.colorTargets.push_back({TextureFormat::Rgba8Unorm});
        state.depthFormat = state.depthStencil.format = TextureFormat::Depth24Stencil8;
        state.dynamicViewportScissor = true;
        state.cullMode = CullMode::None;
        // VertexInputInterface/1 assigns semantic order deterministically.
        if (!check(artifact->vertex().inputs.size() == 4, "EXTENSION-INPUT-COUNT"))
            return false;
        VertexBufferLayoutDesc vertex, instance;
        vertex.slot = 0;
        vertex.strideBytes = 4 * sizeof(float);
        instance.slot = 1;
        instance.strideBytes = 6 * sizeof(float);
        instance.inputRate = VertexInputRate::Instance;
        const std::array<std::uint32_t, 4> components{2, 2, 2, 4};
        for (std::size_t i = 0; i < components.size(); ++i) {
            const auto& input = artifact->vertex().inputs[i];
            if (!check(input.components == components[i], "EXTENSION-INPUT-SHAPE"))
                return false;
            (i < 2 ? vertex : instance)
                .attributes.push_back({input.location,
                    static_cast<std::uint32_t>((i % 2) * 2 * sizeof(float)),
                    components[i] == 4 ? VertexFormat::Float32x4 : VertexFormat::Float32x2});
        }
        state.vertexBuffers.push_back(std::move(vertex));
        state.vertexBuffers.push_back(std::move(instance));
        auto pipeline = core::shareUnique(
            resources.createRenderPipeline(state, layout, artifact, {1, {1, 1024, 4096}, {}}));
        if (!check(bool(pipeline), resources.diagnostic()))
            return false;
        const auto identity =
            core::Sha256::hexDigest({reinterpret_cast<const char*>(artifact->identity().data()),
                artifact->identity().size()});
        ExtensionNativeAuditScope audit(identity.c_str());
        TextureDesc texture;
        texture.extent = {64, 64, 1};
        texture.format = TextureFormat::Rgba8Unorm;
        texture.usage = TextureUsage::RenderAttachment;
        texture.initialState = ResourceState::ColorAttachment;
        auto firstColor = resources.createTexture(texture),
             secondColor = resources.createTexture(texture);
        texture.format = TextureFormat::Depth24Stencil8;
        texture.initialState = ResourceState::DepthAttachment;
        auto secondDepth = resources.createTexture(texture);
        texture.usage = TextureUsage::RenderAttachment | TextureUsage::Sampled;
        auto depth = resources.createTexture(texture);
        if (!check(firstColor && secondColor && secondDepth && depth, resources.diagnostic()))
            return false;
        std::array<std::shared_ptr<WebGlFramebuffer>, 2> targets{
            core::shareUnique(resources.createFramebuffer(*firstColor, depth.get())),
            core::shareUnique(resources.createFramebuffer(*secondColor, secondDepth.get()))};
        if (!check(targets[0] && targets[1], resources.diagnostic()))
            return false;
        TextureViewDesc vd;
        vd.format = TextureFormat::Depth24Stencil8;
        vd.aspect = TextureAspect::Depth;
        vd.usage = TextureUsage::Sampled;
        auto view = resources.createTextureView(*depth, vd);
        SamplerDesc sd;
        sd.minFilter = sd.magFilter = SamplerFilter::Nearest;
        sd.mipFilter = SamplerMipFilter::None;
        auto sampler = resources.createSampler(sd);
        if (!check(view && sampler, resources.diagnostic()))
            return false;
        const std::array<float, 16> vertices{
            -.5f, -1, 0, 0, .5f, -1, 1, 0, .5f, 1, 1, 1, -.5f, 1, 0, 1};
        const std::array<float, 12> instances{-.5f, 0, 1, 0, 0, 1, .5f, 0, 0, 1, 0, 1};
        const std::array<std::uint32_t, 6> indices{65536, 65537, 65538, 65536, 65538, 65539};
        BufferDesc bd;
        bd.memory = MemoryUsage::CpuToGpu;
        bd.initialState = ResourceState::ShaderRead;
        bd.usage = ResourceUsage::VertexBuffer | ResourceUsage::CopyDst;
        bd.sizeBytes = (65536 + 4) * 4 * sizeof(float);
        auto vb = resources.createBuffer(bd);
        bd.sizeBytes = sizeof(instances);
        auto ib = resources.createBuffer(bd);
        bd.usage = ResourceUsage::IndexBuffer | ResourceUsage::CopyDst;
        bd.sizeBytes = sizeof(indices);
        auto index = resources.createBuffer(bd);
        if (!check(vb && ib && index
                    && resources
                        .writeBuffer(
                            *vb, vertices.data(), sizeof(vertices), 65536 * 4 * sizeof(float))
                        .success
                    && resources.writeBuffer(*ib, instances.data(), sizeof(instances), 0).success
                    && resources.writeBuffer(*index, indices.data(), sizeof(indices), 0).success,
                resources.diagnostic()))
            return false;
        const std::array<float, 4> constants{4, 32, .5f, 0};
        bd.usage = ResourceUsage::UniformBuffer | ResourceUsage::CopyDst;
        bd.sizeBytes = sizeof(constants);
        auto uniform = resources.createBuffer(bd);
        if (!check(uniform
                    && resources.writeBuffer(*uniform, constants.data(), sizeof(constants), 0)
                        .success,
                resources.diagnostic()))
            return false;
        auto arena = resources.createDescriptorArena({{}, 1, 3});
        DescriptorSetDesc ds;
        ds.layout = layout->desc().descriptorLayouts[0];
        auto set = arena->allocateDescriptorSet(ds);
        DescriptorBindingWrite write;
        write.type = DescriptorType::SampledTexture;
        write.resourceKind = DescriptorResourceKind::TextureView;
        write.resource = view->id();
        if (!check(set && set->write(write).success, "EXTENSION-DEPTH-DESCRIPTOR"))
            return false;
        write.binding = 1;
        write.type = DescriptorType::Sampler;
        write.resourceKind = DescriptorResourceKind::Sampler;
        write.resource = sampler->id();
        if (!check(set->write(write).success, "EXTENSION-SAMPLER-DESCRIPTOR"))
            return false;
        write.binding = 2;
        write.type = DescriptorType::UniformBuffer;
        write.resourceKind = DescriptorResourceKind::Buffer;
        write.resource = uniform->id();
        if (!check(set->write(write).success, "EXTENSION-UNIFORM-DESCRIPTOR"))
            return false;
        WebGlPreparedCommandsRequest request;
        request.contractVersion = 3;
        request.prototypeOptIn = true;
        request.maxDraws = 1;
        request.maxUniformBytes = 16;
        request.maxAnalysisIndices = 6;
        request.maxPipelines = 1;
        request.maxTargets = request.maxPasses = 2;
        request.maxTransitions = 1;
        request.issue = {1, true, 5, 16, 24, 256 * 1024, 20};
        auto quota = core::makeShared<core::CpuMemoryQuota>(request.issue.maxCpuMetadataBytes);
        const std::array<std::shared_ptr<RenderPipeline>, 1> catalog{pipeline};
        const char* id = nullptr;
        auto encoder = WebGlExecution::prepare(resources, catalog, targets, request, quota, id);
        if (!check(bool(encoder), id))
            return false;
        RenderPassDesc pass;
        pass.colorAttachments.push_back({{},
            firstColor->id(),
            TextureFormat::Rgba8Unorm,
            LoadOp::Clear,
            StoreOp::Store,
            false,
            {},
            {0, 0, 1, 1}});
        pass.hasDepthStencilAttachment = true;
        pass.depthStencilAttachment.target = depth->id();
        pass.depthStencilAttachment.format = TextureFormat::Depth24Stencil8;
        pass.depthStencilAttachment.depthLoadOp = pass.depthStencilAttachment.stencilLoadOp =
            LoadOp::Clear;
        pass.depthStencilAttachment.depthClearValue = .25f;
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        auto first = encoder->beginRenderPass(pass);
        if (!check(first && first->end().success, "EXTENSION-DEPTH-GENERATE"))
            return false;
        ResourceBarrierDesc barrier;
        barrier.resource = depth->id();
        barrier.textureSubresources = defaultTextureSubresourceRange(depth->desc());
        barrier.before = ResourceState::DepthAttachment;
        barrier.after = ResourceState::ShaderRead;
        barrier.sourceStages = PipelineStage::DepthStencil;
        barrier.sourceAccess = ResourceAccess::DepthStencilWrite;
        barrier.destinationStages = PipelineStage::FragmentShader;
        barrier.destinationAccess = ResourceAccess::ShaderRead;
        if (!check(encoder->transition(*depth, barrier).success, "EXTENSION-DEPTH-TRANSITION"))
            return false;
        pass.colorAttachments[0].target = secondColor->id();
        pass.depthStencilAttachment.target = secondDepth->id();
        auto draw = encoder->beginRenderPass(pass);
        if (!check(draw && draw->setPipeline(*pipeline).success
                    && draw->setViewport({0, 0, 64, 64, 0, 1}).success
                    && draw->setScissor({0, 0, 64, 64}).success
                    && draw->bindVertexBuffer(0, *vb).success
                    && draw->bindVertexBuffer(1, *ib).success
                    && draw->bindIndexBuffer(*index, IndexFormat::Uint32).success
                    && draw->bindDescriptorSet(0, *set).success
                    && draw->drawIndexed(6, 2, 0, 0, 0).success && draw->end().success,
                "EXTENSION-RECORD-INSTANCED-UINT32"))
            return false;
        auto command = encoder->finish();
        if (!check(command && core::defaultCpuAllocator().stats().allocationCount == allocations,
                "EXTENSION-RECORD-NO-ALLOCATION"))
            return false;
        auto nativePipeline = std::dynamic_pointer_cast<WebGlRenderPipeline>(pipeline);
        glBindVertexArrayOES(nativePipeline->impl->vertexArray->native);
        for (std::uint32_t slot = 0; slot < resources.profile().limits().maxVertexAttributes;
            ++slot)
            glVertexAttribDivisorANGLE(slot, 7);
        glBindVertexArrayOES(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        const auto result = WebGlExecution::submit(resources, *command);
        if (!check(result.success && result.acceptedOperations == 5 && !result.gpuCompleted,
                result.diagnostic))
            return false;
        if (!check(core::defaultCpuAllocator().stats().allocationCount == allocations,
                "EXTENSION-SUBMIT-NO-ALLOCATION"))
            return false;
        const auto original = dynamic_cast<WebGlTexture*>(secondColor.get())->payload;
        glBindFramebuffer(GL_FRAMEBUFFER, original->domain->find(targets[1]->id())->native);
        std::array<std::uint8_t, 64 * 64 * 4> rgba{};
        glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        if (!check(glGetError() == GL_NO_ERROR, "EXTENSION-NATIVE-READBACK"))
            return false;
        for (std::size_t i = 0; i < 64 * 64; ++i) {
            const bool left = i % 64 < 32;
            if (!check(rgba[4 * i] == (left ? 255 : 0) && rgba[4 * i + 1] == (left ? 0 : 255)
                        && rgba[4 * i + 2] == 0 && rgba[4 * i + 3] == 255,
                    "EXTENSION-DEPTH-DERIVATIVE-INSTANCE-PIXELS"))
                return false;
        }
        retainedPipeline = std::move(nativePipeline);
        retainedCommand = std::move(command);
        return true;
    }
};
} // namespace arp::rhi::webgl::test
