#include "../fixtures/original_runtime_length_fixture.h"
#ifdef NDEBUG
#error Original runtime length custody requires assertions
#endif
int main(int argc, char** argv)
{
    using namespace arp;
    assert(argc >= 3);
    const std::filesystem::path path = argv[1];
    const bool graphics = path.filename().string().find(".graphics.module.") != std::string::npos;
    const core::StringView expected = argv[2];
    if (expected == "verify" || expected == "deleted") {
        test::OriginalRuntimeLengthFixture::verify(path, graphics, expected == "deleted");
        return 0;
    }
    test::OriginalRuntimeLengthFixture fixture;
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    bool valid = false;
    std::shared_ptr<core::CpuMemoryQuota> quota;
    if (graphics) {
        auto budget = fixture.moduleBudget();
        quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        valid = bool(shader::GraphicsShaderModuleArtifact::load(path, budget, quota, diagnostic));
    } else {
        auto budget = fixture.entryBudget();
        quota = core::makeShared<core::CpuMemoryQuota>(budget.maxCpuMetadataBytes);
        valid = bool(shader::SingleEntryShaderArtifact::load(path, budget, quota, diagnostic));
    }
    assert(!quota->liveBytes());
    if (expected == "valid" && valid)
        return 0;
    if (valid || diagnostic.find(expected) == diagnostic.npos) {
        std::fprintf(
            stderr, "Expected %s got %s\n", argv[2], valid ? "success" : diagnostic.c_str());
        return 1;
    }
}
