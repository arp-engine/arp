#include "core/file_io.h"
#include "core/sha256.h"
#include "rhi/conformance/storage_mrt_execution.h"
#include "rhi/conformance/storage_mrt_workload.h"

#include <cassert>
#include <cstring>

int main()
{
    using namespace arp;
    rhi::ConformanceStorageMrtExecutionRequest request{
        {{1, 3, true, 196608, 4194304}, 1048576, 5000000000}, 1048576, 1048576};
    assert(!rhi::ConformanceStorageMrtExecutionContract::validate(request));
    auto invalidRequest = request;
    invalidRequest.maxNativeTextureBytes = 0;
    assert(rhi::ConformanceStorageMrtExecutionContract::validate(invalidRequest));
    invalidRequest = request;
    invalidRequest.maxNativeInputBufferBytes = 0;
    assert(rhi::ConformanceStorageMrtExecutionContract::validate(invalidRequest));
    invalidRequest = request;
    invalidRequest.execution.observation.prototypeOptIn = false;
    assert(rhi::ConformanceStorageMrtExecutionContract::validate(invalidRequest));
    assert(!rhi::ConformanceStorageMrtExecutionContract::loweringDiagnostic(rhi::Backend::Vulkan));
    assert(!rhi::ConformanceStorageMrtExecutionContract::loweringDiagnostic(rhi::Backend::WebGPU));
    assert(rhi::ConformanceStorageMrtExecutionContract::validateWebGpu(request));
    request.artifacts = {1, 65536, 65536, 65536, 65536, 65536, 65536, 512, 5000000000};
    assert(!rhi::ConformanceStorageMrtExecutionContract::validateWebGpu(request));
    for (int field = 0; field < 10; ++field) {
        auto invalid = request;
        if (field == 0)
            invalid.artifacts.contractVersion = 2;
        if (field == 1)
            invalid.artifacts.inputMetadataBytes = 0;
        if (field == 2)
            invalid.artifacts.creationOwnerBytes = 0;
        if (field == 3)
            invalid.artifacts.nativeMetadataBytes = 0;
        if (field == 4)
            invalid.artifacts.callbackBytes = 0;
        if (field == 5)
            invalid.artifacts.wrapperBytes = 0;
        if (field == 6)
            invalid.artifacts.targetBytes = 0;
        if (field == 7)
            invalid.artifacts.diagnosticBytes = 0;
        if (field == 8)
            invalid.artifacts.pipelineCreationTimeoutNanoseconds = 0;
        if (field == 9)
            invalid.artifacts.pipelineCreationTimeoutNanoseconds = UINT64_MAX;
        assert(rhi::ConformanceStorageMrtExecutionContract::validateWebGpu(invalid));
    }
    for (const auto backend : {rhi::Backend::Null,
             rhi::Backend::Metal,
             rhi::Backend::WebGL,
             static_cast<rhi::Backend>(255)})
        assert(rhi::ConformanceStorageMrtExecutionContract::loweringDiagnostic(backend));
    constexpr rhi::ConformanceWorkloadBudget budget{16384, 4096, 3};
    const auto source = core::readTextFile(
        ARP_SOURCE_DIR "/tests/fixtures/rhi_conformance_storage_mrt.yaml", budget.maxYamlBytes);
    assert(source.success);
    const char* id = nullptr;
    const auto workload = rhi::ConformanceStorageMrtWorkload::parse(source.text, budget, id);
    assert(workload && !id && workload->frames().size() == 3);
    assert(workload->sourceSha256() == core::Sha256::hexDigest(source.text));
    const auto shader = core::readTextFile(
        ARP_SOURCE_DIR "/tests/fixtures/conformance_storage_mrt.slang", budget.maxYamlBytes);
    assert(shader.success
        && workload->shaderSourceSha256() == core::Sha256::hexDigest(shader.text).substr(7));
    const auto fill = core::readTextFile(
        ARP_SOURCE_DIR "/tests/fixtures/conformance_storage_fill.slang", budget.maxYamlBytes);
    assert(fill.success
        && workload->computeSourceSha256() == core::Sha256::hexDigest(fill.text).substr(7));
    const auto mutate = [&](core::StringView before, core::StringView after) {
        auto text = source.text;
        const auto offset = text.find(before);
        assert(offset != core::pmr::String::npos);
        text.replace(offset, before.size(), after);
        return text;
    };
    const auto rejected = [&](core::StringView before, core::StringView after) {
        assert(!rhi::ConformanceStorageMrtWorkload::parse(mutate(before, after), budget, id) && id);
    };
    rejected("contractVersion: 1", "contractVersion: 2");
    rejected("requireTransientAliasing: false", "requireTransientAliasing: 0");
    rejected("width: 64", "width: 0");
    rejected("height: 64", "height: 4294967296");
    rejected("values: [0.125", "values: [.inf");
    rejected("values: [0.125", "values: [true");
    rejected("values: [0.125", "values: [-0.125");
    rejected("values: [0.125", "values: [1.125");
    rejected("values: [0.125", "values: [1.0e-300");
    rejected("expectedColor0: [32, 64, 96, 255]", "expectedColor0: [256, 64, 96, 255]");
    rejected("expectedColor0: [32, 64, 96, 255]", "expectedColor0: [32, 64, 96, 0]");
    rejected(
        "expectedStorage: [0.375, 0.125, 0.625, 1]", "expectedStorage: [0.375, 0.125, 0.625, 0]");
    rejected("id: storage-mrt-three-outputs", "id: storage-mrt-three-outputs\nextra: true");
    rejected("    values:", "    extra: 1\n    values:");
    rejected("extent: {width: 64, height: 64}", "extent: {width: 64, height: 64, depth: 1}");
    assert(!rhi::ConformanceStorageMrtWorkload::parse(
        source.text + "\n---\nignored: true\n", budget, id));
    for (unsigned i = 0; i != 4; ++i) {
        auto limited = budget;
        if (i == 0)
            limited.maxYamlBytes = source.text.size() - 1;
        if (i == 1)
            limited.maxPixelsPerFrame = 4095;
        if (i == 2)
            limited.maxFrames = 2;
        if (i == 3)
            limited.maxFrames = 0;
        assert(!rhi::ConformanceStorageMrtWorkload::parse(source.text, limited, id));
        assert(std::strcmp(id, "RHI-CONFORMANCE-WORKLOAD-BUDGET") == 0);
    }
    const auto alias = rhi::ConformanceStorageMrtWorkload::parse(
        mutate("requireTransientAliasing: false", "requireTransientAliasing: true"), budget, id);
    assert(alias);
    for (const auto backend : {rhi::Backend::Null,
             rhi::Backend::Vulkan,
             rhi::Backend::Metal,
             rhi::Backend::WebGPU,
             rhi::Backend::WebGL}) {
        rhi::DeviceCapabilities caps;
        caps.backend = backend;
        assert(workload->executionDiagnostic(caps));
        caps.core.graphics = caps.core.compute = caps.features.computeDispatch = true;
        caps.features.storageBufferBinding = true;
        caps.features.storageTextureBinding = true;
        caps.features.rgba16FloatStorageTexture = true;
        assert(
            !workload->executionDiagnostic(caps)); // Identity cannot authorize or veto a payload.
        for (unsigned missing = 0; missing != 6; ++missing) {
            auto denied = caps;
            if (missing == 0)
                denied.core.graphics = false;
            if (missing == 1)
                denied.features.storageBufferBinding = false;
            if (missing == 2)
                denied.features.storageTextureBinding = false;
            if (missing == 4)
                denied.core.compute = false;
            if (missing == 5)
                denied.features.computeDispatch = false;
            if (missing == 3)
                denied.features.rgba16FloatStorageTexture = false;
            assert(workload->executionDiagnostic(denied));
        }
        assert(std::strcmp(
                   alias->executionDiagnostic(caps), "RHI-CONFORMANCE-STORAGE-MRT-ALIAS-CAPABILITY")
            == 0);
        caps.features.transientAliasing = caps.memory.transientAliasing = true;
        assert(std::strcmp(alias->executionDiagnostic(caps),
                   "RHI-CONFORMANCE-STORAGE-MRT-ALIAS-EXECUTION-UNAVAILABLE")
            == 0);
    }
    const auto altered = rhi::ConformanceStorageMrtWorkload::parse(
        mutate("expectedColor0: [32", "expectedColor0: [33"), budget, id);
    assert(altered && altered->sourceSha256() != workload->sourceSha256());
    for (std::size_t i = 0; i != 3; ++i) {
        assert(altered->frames()[i].values == workload->frames()[i].values);
        assert(altered->frames()[i].expectedStorage == workload->frames()[i].expectedStorage);
    }
}
