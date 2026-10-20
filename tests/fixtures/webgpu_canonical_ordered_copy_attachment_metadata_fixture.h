#pragma once
#include "original_command_ordered_copy_attachment_fixture.h"
#include "webgpu_canonical_cold_admission_fixture.h"

namespace arp::test {
class WebGpuCanonicalOrderedCopyAttachmentMetadataFixture {
protected:
    using Admission = rhi_webgpu::WebGpuCanonicalColdAdmission;
    using Root = rhi::OriginalCommandStateTransaction;
    using K = rhi::CommandUsageKind;
    static constexpr std::uint64_t bytes = 65536u;

    static void is(const char* actual, const char* expected)
    {
        assert(actual && std::string_view(actual) == expected);
    }

    static void configure(rhi::OriginalCommandAdmissionBudget& b)
    {
        WebGpuCanonicalColdAdmissionFixture::configure(b);
        b.maxCommands = 18u;
        b.maxReferences = 12u;
        b.maxInputDescriptors = 9u;
        b.maxPassColorAttachments = b.maxPassAttachmentReferences = 2u;
        b.maxStateObservations = 10u;
        b.maxTextureRanges = 8u; // two buffers + eight declared texture uses.
    }

    template <class Context> static auto makeGraph(Context& c, const char*& diagnostic)
    {
        c.graphBudget.maxStateObservations = 10u;
        c.graphBudget.maxTextureRanges = 8u;
        rhi::TextureToBufferCopyDesc read;
        read.sourceTexture =
            std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(c.leases[3])
                ->resource();
        read.destinationBuffer =
            std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(c.leases[1])
                ->resource();
        read.extent = {4u, 4u, 1u};
        read.textureSubresources = {0u, 1u, 0u, 1u, rhi::TextureAspect::Color};
        read.bytesPerRow = 256u;
        read.rowsPerImage = 4u;
        auto first = c.freeze(read);
        read.bufferOffsetBytes = 1024u;
        auto second = c.freeze(read);
        c.pass.colorAttachments[0].clearColor = {1.0f, 0.0f, 0.0f, 1.0f};
        auto red = c.freezePass();
        c.pass.colorAttachments[0].clearColor = {0.0f, 1.0f, 0.0f, 1.0f};
        auto green = c.freezePass();
        rhi::ResourceBarrierDesc toRead;
        toRead.resource = read.sourceTexture;
        toRead.before = rhi::ResourceState::ColorAttachment;
        toRead.after = rhi::ResourceState::CopySrc;
        toRead.textureSubresources = {0u, 2u, 0u, 1u, rhi::TextureAspect::Color};
        toRead.sourceStages = rhi::PipelineStage::ColorAttachmentOutput;
        toRead.sourceAccess =
            rhi::ResourceAccess::ColorAttachmentRead | rhi::ResourceAccess::ColorAttachmentWrite;
        toRead.destinationStages = rhi::PipelineStage::Transfer;
        toRead.destinationAccess = rhi::ResourceAccess::TransferRead;
        auto toColor = toRead;
        toColor.before = rhi::ResourceState::CopySrc;
        toColor.after = rhi::ResourceState::ColorAttachment;
        toColor.sourceStages = rhi::PipelineStage::Transfer;
        toColor.sourceAccess = rhi::ResourceAccess::TransferRead;
        toColor.destinationStages = rhi::PipelineStage::ColorAttachmentOutput;
        toColor.destinationAccess =
            rhi::ResourceAccess::ColorAttachmentRead | rhi::ResourceAccess::ColorAttachmentWrite;
        const std::array<rhi::CommandUsage, 18> commands{{{K::BeginTransferPass, 0u, 0u},
            {K::BufferCopy, 0u, 2u},
            {K::EndTransferPass, 2u, 0u},
            {K::TextureBarrier, 2u, 1u},
            {K::BeginRenderPass, 3u, 1u},
            {K::EndRenderPass, 4u, 0u},
            {K::TextureBarrier, 4u, 1u},
            {K::BeginTransferPass, 5u, 0u},
            {K::TextureToBufferCopy, 5u, 2u},
            {K::EndTransferPass, 7u, 0u},
            {K::TextureBarrier, 7u, 1u},
            {K::BeginRenderPass, 8u, 1u},
            {K::EndRenderPass, 9u, 0u},
            {K::TextureBarrier, 9u, 1u},
            {K::BeginTransferPass, 10u, 0u},
            {K::TextureToBufferCopy, 10u, 2u},
            {K::EndTransferPass, 12u, 0u},
            {K::Finish, 12u, 0u}}};
        // Every reference names an existing original lease/view; no replacement
        // resource/control or inferred copy descriptor is introduced.
        const std::array<rhi::OriginalCommandUsage::Reference, 12> refs{
            {{0u}, {1u}, {3u}, {4u}, {3u}, {3u}, {1u}, {3u}, {4u}, {3u}, {3u}, {1u}}};
        auto initial = c.descriptors[4];
        initial.commandIndex = 3u;
        const std::array<rhi::OriginalCommandInputs::OriginalDescriptor, 8> descriptors{
            {c.descriptors[0],
                initial,
                {4u, red},
                {6u, *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, toRead)},
                {8u, first},
                {10u, *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, toColor)},
                {11u, green},
                {13u, *rhi::ResourceDependencyArguments::capture(K::TextureBarrier, toRead)}}};
        // The final copy is a ninth positional descriptor, independently charged.
        std::array<rhi::OriginalCommandInputs::OriginalDescriptor, 9> complete{{descriptors[0],
            descriptors[1],
            descriptors[2],
            descriptors[3],
            descriptors[4],
            descriptors[5],
            descriptors[6],
            descriptors[7],
            {15u, second}}};
        return c.tryMake(commands, refs, complete, diagnostic);
    }

    static Admission::Budget budget()
    {
        return {2u, true, 18u, 1u, 1u, 2u, 3u, 3u, 1u, 1u, 1u, 67u, bytes, 3u, 2u};
    }

public:
    template <class Factory> static void verifyMetadata(Factory&& create)
    {
        OriginalCommandOrderedCopyAttachmentFixture::verifyContract();
        OriginalCommandAccessFixture::inspectConfiguredOriginalMipGraph(
            create,
            configure,
            [&](auto& context, auto& initial, const char*) {
                initial.reset();
                const char* diagnostic = nullptr;
                auto graph = makeGraph(context, diagnostic);
                if (!graph)
                    std::fprintf(stderr, "ordered graph diagnostic %s\n", diagnostic);
                assert(graph && !diagnostic);
                auto root = OriginalCommandAccessFixture::prepareOriginalPlan(graph,
                    *context.device,
                    context.quotas[5],
                    diagnostic,
                    {1u, true, 18u, 0u, 32u, 128u},
                    {1u, true, 1u, 5u, 10u, 8u, 30u, bytes});
                if (!root)
                    std::fprintf(stderr, "ordered root diagnostic %s\n", diagnostic);
                assert(root);
                Admission::Requirements required;
                assert(!Admission::deriveRequirements(root, budget(), required));
                assert(required.contractVersion == 2u && required.originalCommands == 18u
                    && required.renderPasses == 2u && required.capacity.callbackTickets == 8u);
                is(Admission::inspectOriginalPlan(root), "RHI-WEBGPU-CANONICAL-COLD-LANGUAGE");
                auto bad = budget();
                --bad.maxCopyElements;
                assert(Admission::deriveRequirements(root, bad, required));
                bad = budget();
                --bad.maxRenderPasses;
                assert(Admission::deriveRequirements(root, bad, required));
                bad = budget();
                --bad.maxAttachmentReferences;
                assert(Admission::deriveRequirements(root, bad, required));
                assert(root->journalEmpty() && !root->hasOriginalRecordingSeal());
                std::puts(
                    "BINDLESS_RHI_ORDERED_COPY_ATTACHMENT_METADATA_OK original18 copy3 pass2 references12 capacity1-8-8 old-language-refusal independent-count-refusal same-root-journal-empty");
            },
            false);
    }
};
} // namespace arp::test
