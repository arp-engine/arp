#include "../fixtures/cpp_source_evidence.h"
// WebGPU backend memory ownership contract.
//
// Native WebGPU objects are owned by the RHI/backend memory policy. Backend
// code must not introduce an untracked std::vector allocation path; bounded
// sequences use the project PMR containers instead.

#include "core/memory/cpu_allocator.h"
#include "rhi/frontend/transfer_validation.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

std::filesystem::path projectRoot()
{
#ifdef ARP_SOURCE_DIR
    return std::filesystem::path(ARP_SOURCE_DIR);
#else
    auto current = std::filesystem::current_path();
    for (;;) {
        if (std::filesystem::exists(current / "CMakeLists.txt")
            && std::filesystem::exists(current / "src" / "rhi" / "backends" / "webgpu")) {
            return current;
        }
        if (!current.has_parent_path() || current == current.parent_path()) {
            break;
        }
        current = current.parent_path();
    }
    return std::filesystem::current_path();
#endif
}

arp::tests::CppSourceEvidence readText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    std::ostringstream stream;
    stream << file.rdbuf();
    return arp::core::toPmrString(stream.str());
}

} // namespace

int main()
{
    const arp::tests::CppSourceEvidence evidence(
        arp::core::toPmrString("if (owner != nullptr)\n    return \"RHI-EXACT-ID\";"));
    assert(evidence.find("if(owner!=nullptr)return \"RHI-EXACT-ID\";") == 0u);
    assert(evidence.find("owner == nullptr") == evidence.npos);
    assert(evidence.find("\"RHI-EXACT- ID\"") == evidence.npos);
    assert(evidence.find("ow ner != nullptr") == evidence.npos);
    assert(evidence.substr(evidence.find("return")).find("\"RHI-EXACT-ID\"") != evidence.npos);
    const arp::tests::CppSourceEvidence script(
        arp::core::toPmrString("state === value; value => value;"));
    assert(script.find("state == = value") == script.npos);
    assert(script.find("value = > value") == script.npos);
    const arp::tests::CppSourceEvidence trivia(
        arp::core::toPmrString("call(/* formatting boundary */ value)"));
    assert(trivia.find("call(value)") == 0u);
    const arp::tests::CppSourceEvidence boundary(
        arp::core::toPmrString("{ inner(); }\n} // namespace"));
    assert(boundary.find("} // namespace") == boundary.rfind('}'));
#if defined(__EMSCRIPTEN__)
    // This contract scans backend source files through ARP_SOURCE_DIR. The
    // Emscripten test runs in a virtual filesystem and cannot see the host
    // checkout; the native target is the authoritative source scan.
    return 0;
#else
    const auto backendRoot = projectRoot() / "src" / "rhi" / "backends" / "webgpu";
    assert(std::filesystem::exists(backendRoot));

    std::uint32_t inspectedFiles = 0u;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(backendRoot)) {
        if (!entry.is_regular_file())
            continue;
        const auto extension = entry.path().extension().string();
        if (extension != ".cpp" && extension != ".h")
            continue;

        ++inspectedFiles;
        const auto source = readText(entry.path());
        assert(source.find("#include <vector>") == std::pmr::string::npos);
        assert(source.find("std::vector") == std::pmr::string::npos);
        assert(source.find("std::pmr::vector") == std::pmr::string::npos);
    }

    assert(inspectedFiles > 0u);

    // Compressed uploads use physical block rows for the source byte
    // footprint. rowsPerImage remains a logical texel-row pitch in the RHI
    // contract, so a BC7 upload must derive its final byte range through
    // copyRowCount() rather than multiplying by the texel height.
    const auto backendCommands = readText(backendRoot / "webgpu_backend_commands.cpp");
    const auto nativeTransfer = readText(backendRoot / "webgpu_native_transfer_commands.cpp");
    assert(backendCommands.find("copyRowCount(") != std::pmr::string::npos);
    assert(
        backendCommands.find(
            "const auto sourceRows = static_cast<std::uint64_t>(rhi::semantics::TextureTransferValidation::copyRowCount(")
        != std::pmr::string::npos);
    assert(backendCommands.find("const auto nativeRowCount = rowsPerImage * sliceCount64")
        == std::pmr::string::npos);
    assert(nativeTransfer.find("sourceRowsPerImage") != std::pmr::string::npos);
    assert(
        nativeTransfer.find("rhi::blocksForExtent(desc.extent.height") != std::pmr::string::npos);
    using Transfer = arp::rhi::semantics::TextureTransferValidation;
    assert(Transfer::copyRowCount(arp::rhi::TextureFormat::Bc7Unorm, 1024u) == 256u);
    assert(Transfer::copyRowBytes(arp::rhi::TextureFormat::Bc7Unorm, 1024u) == 4096u);

    return 0;
#endif
}
