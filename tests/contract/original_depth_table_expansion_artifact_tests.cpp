#include "../fixtures/cpu_quota_factory_fixture.h"
#include "../fixtures/original_command_descriptor_consumption_fixture.h"
#include "../fixtures/original_command_readback_result_contract_fixture.h"
#include "../fixtures/original_command_state_admission_contract_fixture.h"
#include "../fixtures/original_depth_table_expansion_input_fixture.h"
#include "../fixtures/original_depth_table_geometry_composition_contract_fixture.h"
#include "../fixtures/original_shader_artifact_owner_fixture.h"
#include "rhi/frontend/command_usage.h"
#include "rhi/semantics/command_lowering.h"
#include "shader/original_depth_table_expansion_artifact.h"

#include <cstdio>
#include <cstdlib>
#include <type_traits>
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
#include "../fixtures/webgpu_original_storage_execution_fixture.h"
#endif

#ifdef NDEBUG
#error Original table artifact tests require assertions
#endif
int main(int argc, char** argv)
{
    using namespace arp;
    using Artifact = shader::OriginalDepthTableExpansionArtifact;
    static_assert(
        !std::is_default_constructible_v<Artifact> && !std::is_copy_constructible_v<Artifact>);
    assert(argc == 4 || argc == 5 || argc == 6 || argc == 7);
    if (argc >= 5
        && (core::StringView(argv[4]) == "completed-depth-publication-only"
            || core::StringView(argv[4]) == "mixed-depth-storage-plan"
            || core::StringView(argv[4]) == "mixed-depth-storage-execution")) {
        const bool mixedExecution = core::StringView(argv[4]) == "mixed-depth-storage-execution";
#if defined(ARP_RHI_WEBGPU_NATIVE_PAYLOADS_HAS_NATIVE)
        if (mixedExecution) {
            test::OriginalMixedDepthStoragePlanFixture::executionCheck =
                &test::WebGpuOriginalStorageExecutionFixture::executeMixed;
            test::WebGpuOriginalStorageExecutionFixture::mixedDepthValues.fill(.25f);
            test::WebGpuOriginalStorageExecutionFixture::fault =
                argc == 6 && core::StringView(argv[5]) == "fault";
            if (argc == 7) {
                const std::array<core::StringView, 11u> terminals{
                    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"};
                const auto selected =
                    std::find(terminals.begin(), terminals.end(), core::StringView(argv[6]));
                assert(selected != terminals.end());
                test::WebGpuOriginalStorageExecutionFixture::terminal =
                    static_cast<unsigned>(selected - terminals.begin());
                assert(
                    core::StringView(argv[5]) == "clean" || core::StringView(argv[5]) == "fault");
                test::WebGpuOriginalStorageExecutionFixture::fault =
                    core::StringView(argv[5]) == "fault";
            }
        }
#endif
        assert(rhi_webgpu::webGpuNativeDeviceAvailable());
        core::pmr::String diagnostic;
        auto package = rhi::BindlessShaderPackage::load(
            "/bindless-sampled-depth-fixture/SampledDepth.emulated.bindless.yaml",
            {2u * 1024u * 1024u, 16u * 1024u * 1024u, 64u, 16u, 256u, 64u},
            diagnostic);
        if (!package)
            std::fprintf(stderr, "depth package: %s\n", diagnostic.c_str());
        assert(package);
        const auto terminal = argc == 6 && !mixedExecution
            ? static_cast<std::uint32_t>(std::strtoul(argv[5], nullptr, 10))
            : UINT32_MAX;
        test::OriginalCompletedDepthPublicationFixture::verify(package,
            terminal,
            mixedExecution || core::StringView(argv[4]) == "mixed-depth-storage-plan");
        test::OriginalMixedDepthStoragePlanFixture::executionCheck = nullptr;
        if (mixedExecution && argc == 7)
            std::printf("COMPLETED_DEPTH_MIXED_TERMINAL_OK terminal=%s retirement0\n", argv[6]);
        else if (mixedExecution)
            std::puts("COMPLETED_DEPTH_MIXED_EXECUTION_OK genuine-copy-reduce-read retirement0");
        else
            std::puts(
                "COMPLETED_DEPTH_ONLY_OK genuine-publication-update-retirement public-depth-compute-execution");
        return 0;
    }
    test::OriginalDepthTableRegistrationFixture::verifyContract();
    test::OriginalDepthTablePublicationFixture::verifyContract();
    test::OriginalDepthTableGeometryCompositionContractFixture::verify();
    test::OriginalCommandDescriptorConsumptionFixture::verifyContract();
    test::OriginalCommandStateAdmissionContractFixture::verify();
    test::OriginalCommandReadbackResultContractFixture::verifyTypedMapDrainContract();
    test::OriginalDepthTableExpansionPipelineFixture::nativeQueryRequested =
        argc == 5 && core::StringView(argv[4]) == "native-query";
    test::OriginalDepthTableExpansionPipelineFixture::nativeEmissionRequested =
        argc == 5 && core::StringView(argv[4]) == "native-emission";
    test::OriginalDepthTableExpansionPipelineFixture::nativeCreationRequested =
        argc == 5 && core::StringView(argv[4]) == "native-creation";
    test::OriginalDepthTableExpansionPipelineFixture::nativeRegistrationRequested =
        argc == 5 && core::StringView(argv[4]) == "native-registration";
    test::OriginalDepthTableExpansionPipelineFixture::nativePublicationRequested =
        argc == 5 && core::StringView(argv[4]) == "native-publication";
    test::OriginalDepthTableExpansionPipelineFixture::nativeExecutionRequested =
        argc == 5 && core::StringView(argv[4]) == "native-execution";
    test::OriginalDepthTableContentPlanFixture::mixedRecordingRequested =
        argc == 5 && core::StringView(argv[4]) == "native-recording";
    test::OriginalDepthTableContentPlanFixture::mixedReadbackMapLossRequested =
        argc == 5 && core::StringView(argv[4]) == "native-map-loss";
    using GeometryCase = test::OriginalVertexGeometryExecutionFixture::LifecycleCase;
    const auto invocationCase = argc != 5 ? GeometryCase::Complete
        : core::StringView(argv[4]) == "native-recording-uncertain"
        ? GeometryCase::RecordingUncertain
        : core::StringView(argv[4]) == "native-submission-uncertain"
        ? GeometryCase::SubmissionUncertain
        : core::StringView(argv[4]) == "native-map-uncertain" ? GeometryCase::MapUncertain
        : core::StringView(argv[4]) == "native-recording-scope-uncertain"
        ? GeometryCase::RecordingScopeUncertain
        : core::StringView(argv[4]) == "native-submission-scope-uncertain"
        ? GeometryCase::SubmissionScopeUncertain
        : core::StringView(argv[4]) == "native-queue-registration-uncertain"
        ? GeometryCase::QueueRegistrationUncertain
        : core::StringView(argv[4]) == "native-scope-registration-uncertain"
        ? GeometryCase::ScopeRegistrationUncertain
        : GeometryCase::Complete;
    test::OriginalDepthTableContentPlanFixture::geometryInvocationCase = invocationCase;
    assert(invocationCase == GeometryCase::Complete
        || core::StringView(argv[2]) == "GraphicsGeometry");
    test::OriginalDepthTableContentPlanFixture::mixedReadbackLossRequested =
        (argc == 5 && core::StringView(argv[4]) == "native-submitted-loss")
        || test::OriginalDepthTableContentPlanFixture::mixedReadbackMapLossRequested
        || invocationCase != GeometryCase::Complete;
    test::OriginalDepthTableContentPlanFixture::geometryRecordingCancelRequested =
        argc == 5 && core::StringView(argv[4]) == "native-recording-cancel";
    assert(!test::OriginalDepthTableContentPlanFixture::geometryRecordingCancelRequested
        || core::StringView(argv[2]) == "GraphicsGeometry");
    test::OriginalDepthTableContentPlanFixture::mixedCompletionRequested =
        (argc == 5 && core::StringView(argv[4]) == "native-completion")
        || test::OriginalDepthTableContentPlanFixture::mixedReadbackLossRequested
        || test::OriginalDepthTableContentPlanFixture::geometryRecordingCancelRequested;
    test::OriginalDepthTableContentPlanFixture::geometryCompositionRequested =
        core::StringView(argv[2]) == "GraphicsGeometry"
        && test::OriginalDepthTableContentPlanFixture::mixedCompletionRequested;
    test::OriginalDepthTableContentPlanFixture::mixedResultsRequested =
        (argc == 5 && core::StringView(argv[4]) == "native-results")
        || test::OriginalDepthTableContentPlanFixture::mixedCompletionRequested;
    test::OriginalDepthTableContentPlanFixture::mixedIssuersRequested =
        (argc == 5 && core::StringView(argv[4]) == "native-issuers")
        || test::OriginalDepthTableContentPlanFixture::mixedRecordingRequested
        || test::OriginalDepthTableContentPlanFixture::mixedResultsRequested;
    test::OriginalDepthTableExpansionPipelineFixture::nativeExecutionRequested |=
        test::OriginalDepthTableContentPlanFixture::mixedIssuersRequested;
    test::OriginalDepthTableExpansionPipelineFixture::nativePublicationRequested |=
        test::OriginalDepthTableExpansionPipelineFixture::nativeExecutionRequested;
    test::OriginalDepthTableExpansionPipelineFixture::nativeRegistrationRequested |=
        test::OriginalDepthTableExpansionPipelineFixture::nativePublicationRequested;
    test::OriginalDepthTableExpansionPipelineFixture::nativeCreationRequested |=
        test::OriginalDepthTableExpansionPipelineFixture::nativeRegistrationRequested;
    test::OriginalDepthTableExpansionPipelineFixture::nativeQueryRequested |=
        test::OriginalDepthTableExpansionPipelineFixture::nativeEmissionRequested
        || test::OriginalDepthTableExpansionPipelineFixture::nativeCreationRequested;
    const bool compute = core::StringView(argv[2]) == "Compute";
    const core::StringView expected = argv[3];
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    diagnostic.reserve(256u);
    Artifact::Budget budget{
        1u, true, 1024u * 1024u, 4u * 1024u * 1024u, 4u * 1024u * 1024u, 2u, 4u, 3u, 2u};
    auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
    const auto factory = [&] {
        return Artifact::load(argv[1], budget, quota, diagnostic);
    };
    auto object = factory();
    if (expected != "valid") {
        if (object || diagnostic.find(expected) == diagnostic.npos)
            std::fprintf(stderr, "expected %s got %s\n", argv[3], diagnostic.c_str());
        assert(!object && diagnostic.find(expected) != diagnostic.npos && !quota->liveBytes());
        return 0;
    }
    if (!object)
        std::fprintf(stderr, "%s\n", diagnostic.c_str());
    assert(object);
    assert(
        object->profile() == (compute ? Artifact::Profile::Compute : Artifact::Profile::Graphics));
    assert(object->vertexInputContractVersion()
        == (core::StringView(argv[2]) == "GraphicsGeometry" ? 1u : 0u));
    assert(!Artifact::validateOriginal(object) && !object->nativeExecutionProven());
    test::OriginalShaderArtifactOwnerFixture::verify(object,
        "SHADER-ORIGINAL-TABLE-EXPANSION-OWNER",
        "SHADER-ORIGINAL-TABLE-EXPANSION-QUOTA-LIMIT");
    assert(object->logicalBindings().size() == (compute ? 2u : 1u));
    const auto& table = object->logicalBindings()[0];
    assert(table.name == "Inputs" && table.type == shader::BindingType::SampledTexture
        && table.depthTexture);
    assert(!table.set && !table.binding && table.arrayCount == 3u
        && table.stageMask == (compute ? 4u : 2u));
    assert(object->entryCount() == (compute ? 1u : 2u)
        && object->identity().size() == core::Sha256::digestByteCount);
    for (std::size_t i = 0u; i < 3u; ++i) {
        assert(object->elements()[i].arrayElement == i && object->elements()[i].binding == i
            && !object->elements()[i].set);
        assert(object->elements()[i].name.get_allocator().resource()
            == &core::defaultCpuMemoryResource());
    }
    for (std::size_t i = 0u; i < object->entryCount(); ++i) {
        const auto& entry = object->entry(i);
        for (const auto role : {shader::ShaderArtifactRole::Spirv,
                 shader::ShaderArtifactRole::Metal,
                 shader::ShaderArtifactRole::Wgsl,
                 shader::ShaderArtifactRole::Reflection})
            assert(!entry.files.at(static_cast<std::size_t>(role)).bytes.empty());
        assert(entry.files.at(0u)
                .bytes.empty()); // shared source is original custody, not a fabricated entry.
        assert(entry.physical.reflection.bindings.size() == (compute ? 4u : i ? 3u : 0u));
    }
    const auto total = object->inputBytes();
    test::OriginalDepthTableExpansionInputFixture::verify(object);
    std::uint64_t maxFile = std::max({object->publicationBytes().size(),
        object->originalSource().size(),
        object->generatedSource().size()});
    for (std::size_t i = 0u; i < object->entryCount(); ++i)
        for (std::size_t role = 1u; role < 5u; ++role)
            maxFile = std::max(
                maxFile, static_cast<std::uint64_t>(object->entry(i).files[role].bytes.size()));
    auto retained = object;
    object.reset();
    assert(!Artifact::validateOriginal(retained));
    const auto* id =
        test::CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{quota}, [&]() noexcept {
            if (const auto* value = Artifact::validateOriginal(retained))
                return value;
            for (const auto backend : {rhi::Backend::Null,
                     rhi::Backend::Vulkan,
                     rhi::Backend::Metal,
                     rhi::Backend::WebGPU}) {
                const auto decision =
                    arp::rhi::semantics::OriginalCommandDescriptorBindingLowering::tableLowering(
                        backend);
                assert(!decision.executionAvailable && !decision.positionalAvailable);
            }
            return static_cast<const char*>(nullptr);
        });
    assert(!id);
    retained.reset();
    assert(!quota->liveBytes());
    for (const auto mutate : {0u, 1u, 2u, 3u, 4u, 5u}) {
        const auto saved = budget;
        if (mutate == 0u)
            budget.contractVersion = 2u;
        if (mutate == 1u)
            budget.explicitOptIn = false;
        if (mutate == 2u)
            budget.maxLogicalBindings = (compute ? 2u : 1u) - 1u;
        if (mutate == 3u)
            budget.maxPhysicalBindings = (compute ? 4u : 3u) - 1u;
        if (mutate == 4u)
            budget.maxElements = 2u;
        if (mutate == 5u)
            budget.maxEntries = (compute ? 1u : 2u) - 1u;
        assert(!factory() && !quota->liveBytes());
        budget = saved;
    }
    const auto saved = budget;
    budget.maxTotalBytes = total;
    assert(factory());
    budget.maxTotalBytes = total - 1u;
    assert(!factory() && !quota->liveBytes());
    budget = saved;
    budget.maxFileBytes = maxFile;
    assert(factory());
    budget.maxFileBytes = maxFile - 1u;
    assert(!factory() && !quota->liveBytes());
    budget = saved;
    budget.maxLogicalBindings = compute ? 2u : 1u;
    budget.maxPhysicalBindings = compute ? 4u : 3u;
    budget.maxEntries = compute ? 1u : 2u;
    test::CpuQuotaFactoryFixture fixture(quota);
    const auto peak = fixture.measureSingleUse([] {}, factory);
    fixture.verifySingleUse(peak, [] {}, factory);
    assert(!quota->liveBytes());
    std::printf(
        "ORIGINAL_TABLE_EXPANSION_ARTIFACT_OK %s logical3 physical1 ordered3 trusted-program-targets original-owner caller-drop quotas-exact-less-exhausted hot0 native-closed\n",
        argv[2]);
}
