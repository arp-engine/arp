#include "../fixtures/original_target_matrix_memory_fixture.h"
#include "../fixtures/original_target_memory_fixture.h"
#include "../fixtures/original_target_push_matrix_memory_fixture.h"
#ifdef NDEBUG
#error Original target memory tests require assertions
#endif
int main(int argc, char** argv)
{
    using namespace arp;
    assert(argc >= 3);
    if (core::StringView(argv[1]) == "--production-push-binary") {
        assert(argc == 4);
        test::OriginalTargetPushMatrixMemoryFixture::verifyProductionBinary(argv[2], argv[3]);
        return 0;
    }
    if (core::StringView(argv[1]) == "--matrix-push-module") {
        test::OriginalTargetPushMatrixMemoryFixture::verifyModule(argv[2]);
        return 0;
    }
    if (core::StringView(argv[1]) == "--matrix-push-entry") {
        test::OriginalTargetPushMatrixMemoryFixture::verify(argv[2]);
        return 0;
    }
    if (core::StringView(argv[1]) == "--matrix-entry") {
        test::OriginalTargetMatrixMemoryFixture::verify(argv[2]);
        return 0;
    }
    if (core::StringView(argv[1]).find("--") != 0u) {
        const std::filesystem::path path = argv[1];
        const bool module = path.extension() == ".yaml"
            && path.filename().string().find(".graphics.module.") != std::string::npos;
        const bool probe = path.filename() == "buffer_layout_probe.computeMain.entry.yaml";
        core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
        const core::StringView expected = argv[2];
        if (module) {
            auto budget = test::OriginalTargetMemoryFixture::moduleBudget();
            auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
            auto bare = shader::GraphicsShaderModuleArtifact::load(path, budget, quota, diagnostic);
            if (bare) {
                test::TargetMemoryRequestFixture vertex, fragment;
                vertex.prepare(bare->vertex());
                fragment.prepare(bare->fragment());
                shader::GraphicsTargetMemoryLoadRequest request{1u,
                    vertex.request(),
                    fragment.request(),
                    test::TargetMemoryRequestFixture::graphBudget,
                    test::TargetMemoryRequestFixture::graphBudget,
                    512u};
                bare.reset();
                assert(!quota->liveBytes());
                budget.targetMemory = &request;
                auto value =
                    shader::GraphicsShaderModuleArtifact::load(path, budget, quota, diagnostic);
                if (value && expected == "valid")
                    return 0;
                if (value && expected == "valid-padded-stage") {
                    shader::GraphicsTargetBufferMemoryLayout facts;
                    assert(!shader::GraphicsShaderModuleArtifact::originalTargetBufferMemory(
                        value, 0u, 3u, facts));
                    assert(facts.stageMask == 3u && facts.combinedTargetDeclarationBytes == 32u
                        && facts.target.minimumBindingBytes == 48u);
                    return 0;
                }
                if (value && expected.find("lookup:") == 0u) {
                    shader::GraphicsTargetBufferMemoryLayout facts;
                    facts.target.minimumBindingBytes = UINT64_MAX;
                    const auto* id =
                        shader::GraphicsShaderModuleArtifact::originalTargetBufferMemory(
                            value, 0u, 3u, facts);
                    assert(id && !facts.target.minimumBindingBytes
                        && core::StringView(id).find(expected.substr(7u))
                            != core::StringView::npos);
                    shader::TargetPushMemoryLayout push;
                    push.targetDeclarationBytes = UINT64_MAX;
                    assert(
                        shader::GraphicsShaderModuleArtifact::originalTargetPushMemory(value, push)
                        && !push.targetDeclarationBytes);
                    return 0;
                }
                assert(!value && !quota->liveBytes());
            }
        } else {
            auto budget = test::OriginalTargetMemoryFixture::entryBudget();
            auto quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
            auto bare = shader::SingleEntryShaderArtifact::load(path, budget, quota, diagnostic);
            if (bare) {
                test::TargetMemoryRequestFixture plans;
                plans.prepare(bare, probe);
                bare.reset();
                assert(!quota->liveBytes());
                budget.targetMemory = &plans.request();
                auto value =
                    shader::SingleEntryShaderArtifact::load(path, budget, quota, diagnostic);
                if (value && expected == "valid")
                    return 0;
                if (value && expected.find("lookup:") == 0u) {
                    shader::TargetBufferMemoryLayout facts;
                    facts.target.minimumBindingBytes = UINT64_MAX;
                    const auto* id = shader::SingleEntryShaderArtifact::originalTargetBufferMemory(
                        value, 0u, 0u, facts);
                    assert(id && !facts.target.minimumBindingBytes
                        && core::StringView(id).find(expected.substr(7u))
                            != core::StringView::npos);
                    return 0;
                }
                assert(!value && !quota->liveBytes());
            }
        }
        if (diagnostic.find(expected) == diagnostic.npos) {
            std::fprintf(stderr, "Expected %s, got %s\n", argv[2], diagnostic.c_str());
            return 1;
        }
        return 0;
    }
    if (core::StringView(argv[1]) == "--entry") {
        auto value = test::OriginalTargetMemoryFixture::entry(argv[2], argc == 4);
        return 0;
    }
    if (core::StringView(argv[1]) == "--module") {
        auto value = test::OriginalTargetMemoryFixture::module(argv[2]);
        return 0;
    }
    if (core::StringView(argv[1]) == "--entry-deleted") {
        auto value = test::OriginalTargetMemoryFixture::entry(argv[2], argc == 4, true);
        return 0;
    }
    if (core::StringView(argv[1]) == "--module-deleted") {
        auto value = test::OriginalTargetMemoryFixture::module(argv[2], true);
        return 0;
    }
    return 1;
}
