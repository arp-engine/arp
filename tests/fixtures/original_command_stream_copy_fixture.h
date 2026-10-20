#pragma once
#include "original_command_access_fixture.h"
#include "original_command_stream_program_fixture.h"

namespace arp::test {
class OriginalCommandStreamCopyFixture final {
public:
    template <class Factory> static void verify(Factory&& create)
    {
        using K = rhi::CommandUsageKind;
        using Program = rhi::OriginalCommandStreamProgram;
        OriginalCommandStreamProgramFixture::verifyContract();
        OriginalCommandAccessFixture::withOriginalPlan(create, [&](auto& context, auto& root) {
            auto program = OriginalCommandStreamProgramFixture::verifyInputs(
                root->originalGraph()->originalInputs(),
                {10u, 6u, 10u, 4u, 0u, 0u, 0u, 1u, 1u},
                "copy-single");
            assert(root->journalEmpty() && !root->hasOriginalRecordingSeal());
            assert(program->operations()[1].firstElement == 0u
                && program->operations()[4].firstElement == 3u);
            context.device.reset();
            // CPU field ownership remains valid after caller/device release;
            // it grants no registered lifetime or native execution authority.
            assert(!program->validate() && program->executionDiagnostic(rhi::Backend::Null));
        });
        OriginalCommandAccessFixture::inspectConfiguredOriginalMipGraph(
            create,
            [](auto& b) {
                b.maxPassColorAttachments = b.maxPassAttachmentReferences = 2u;
            },
            [&](auto& context, auto& graph, const char* diagnostic) {
                assert(graph && !diagnostic);
                auto program = OriginalCommandStreamProgramFixture::verifyInputs(
                    graph->originalInputs(), {10u, 6u, 10u, 7u, 0u, 0u, 0u, 1u, 1u}, "copy-multi");
                const auto& ops = program->operations();
                assert(ops[1].firstElement == 0u && ops[1].elementCount == 1u
                    && ops[1].descriptorIndex == 0u);
                assert(ops[2].firstElement == 1u && ops[2].elementCount == 2u
                    && ops[2].descriptorIndex == 1u);
                assert(ops[3].firstElement == 3u && ops[3].elementCount == 2u
                    && ops[4].firstElement == 5u && ops[4].elementCount == 2u);
                assert(ops[6].descriptorIndex == 4u && !ops[6].elementCount
                    && ops[7].descriptorIndex == 5u && ops[7].elementCount == 1u);
                // A different legal ordered profile: transfer then dependency
                // then two original passes. No exact-ten admission is reused.
                const std::array<rhi::CommandUsage, 9> commands{{{K::BeginTransferPass, 0u, 0u},
                    {K::BufferCopy, 0u, 2u},
                    {K::EndTransferPass, 2u, 0u},
                    {K::TextureBarrier, 2u, 1u},
                    {K::BeginRenderPass, 3u, 1u},
                    {K::EndRenderPass, 4u, 0u},
                    {K::BeginRenderPass, 4u, 1u},
                    {K::EndRenderPass, 5u, 0u},
                    {K::Finish, 5u, 0u}}};
                const std::array<rhi::OriginalCommandUsage::Reference, 5> refs{
                    {context.references[0],
                        context.references[1],
                        context.references[8],
                        context.references[9],
                        context.references[9]}};
                auto dependency = context.descriptors[4];
                dependency.commandIndex = 3u;
                auto firstPass = context.descriptors[5];
                firstPass.commandIndex = 4u;
                auto secondPass = firstPass;
                secondPass.commandIndex = 6u;
                const std::array<rhi::OriginalCommandInputs::OriginalDescriptor, 4> descriptors{
                    {context.descriptors[0], dependency, firstPass, secondPass}};
                auto repeated = context.tryMake(commands, refs, descriptors, diagnostic);
                assert(repeated && !diagnostic);
                assert(!rhi::common::OriginalCommandAccessValidation::inspect(
                    repeated, {1u, true, 9u, 0u, 16u, 32u})
                        .diagnostic);
                auto ordered =
                    OriginalCommandStreamProgramFixture::verifyInputs(repeated->originalInputs(),
                        {9u, 4u, 5u, 1u, 0u, 0u, 0u, 2u, 2u},
                        "ordered-two-pass");
                assert(ordered->operations()[4].firstElement == 0u
                    && ordered->operations()[6].firstElement == 1u);
                assert(ordered->operations()[4].descriptorIndex == 2u
                    && ordered->operations()[6].descriptorIndex == 3u);
                isInputMismatch(program, ordered->originalInputs());
                // Complete CPU fields deliberately do not prove portable access
                // legality: the separate four-byte copy contract rejects six.
                auto unaligned = descriptors;
                rhi::BufferCopyDesc copy;
                assert(std::get<rhi::TransferCopyArguments>(unaligned[0].arguments).write(copy));
                copy.sizeBytes = 6u;
                unaligned[0].arguments = *rhi::TransferCopyArguments::capture(copy);
                auto illegal = context.tryMake(commands, refs, unaligned, diagnostic);
                assert(illegal && !diagnostic);
                const auto access = rhi::common::OriginalCommandAccessValidation::inspect(
                    illegal, {1u, true, 9u, 0u, 16u, 32u});
                assert(access.diagnostic
                    && std::string_view(access.diagnostic)
                        == "RHI-ORIGINAL-ACCESS-BUFFER-COPY-RANGE");
                auto fieldsOnly =
                    OriginalCommandStreamProgramFixture::verifyInputs(illegal->originalInputs(),
                        {9u, 4u, 5u, 1u, 0u, 0u, 0u, 2u, 2u},
                        "fields-access-unproven");
                assert(!fieldsOnly->nativeExecutionProven()
                    && fieldsOnly->executionDiagnostic(rhi::Backend::WebGPU));
                const auto source =
                    graph->originalInputs()->originalScalars()->originalUsage()->originalSources();
                auto invalid = commands;
                invalid[2].kind = K::EndRenderPass;
                assert(!rhi::OriginalCommandUsage::prepareOriginal(source,
                           invalid.data(),
                           invalid.size(),
                           refs.data(),
                           refs.size(),
                           context.quotas[3],
                           diagnostic)
                    && diagnostic);
                std::weak_ptr<const rhi::OriginalCommandInputs> retained = graph->originalInputs();
                graph.reset();
                assert(!retained.expired() && !program->validate());
                program.reset();
                assert(retained.expired());
                retained.reset();
            },
            false);
        std::puts(
            "BINDLESS_RHI_ORIGINAL_STREAM_COPY_OK original-single-multi-mip variable-ordered-two-pass sparse-descriptors invalid-order access-legality-separate caller-drop shutdown no-root-mutation");
    }

private:
    static void isInputMismatch(
        const std::shared_ptr<const rhi::OriginalCommandStreamProgram>& program,
        const std::shared_ptr<const rhi::OriginalCommandInputs>& inputs)
    {
        assert(std::string_view(program->compareOriginalInputs(inputs))
            == "RHI-ORIGINAL-STREAM-INPUTS");
    }
};
} // namespace arp::test
