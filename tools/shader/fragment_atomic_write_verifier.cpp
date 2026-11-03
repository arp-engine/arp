#include "core/text/ascii_token_cursor.h"
#include "shader/fragment_atomic_write_proof.h"
#include "shader/spirv_buffer_declaration_layout.h"
#include "tools/shader/spirv_verifier_source.h"

#include <charconv>
#include <cstdio>
#include <exception>
#include <string_view>

namespace {
class VerifierApplication final {
public:
    int run(int argc, char** argv) const
    {
        std::string_view httpBase;
        if (argc >= 3 && std::string_view(argv[argc - 2]) == "--source-http-base") {
            httpBase = argv[argc - 1];
            if (httpBase.empty())
                return fail("SHADER-VERIFIER-SOURCE-BASE");
            argc -= 2;
        }
        if (argc != 4 && argc != 5)
            return fail("SHADER-GRAPHICS-WRITE-TOOL-ARGUMENTS");
        std::uint64_t limit = 0u;
        const std::string_view value(argv[2]);
        const auto parsed = std::from_chars(value.data(), value.data() + value.size(), limit);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || !limit)
            return fail("SHADER-GRAPHICS-WRITE-TOOL-BUDGET");
        const auto file =
            arp::tools::shader::SpirvVerifierSource::acquire({argv[1], httpBase, limit});
        if (file.status != arp::core::io::FileIoStatus::Success) {
            std::fprintf(stderr,
                "SHADER-GRAPHICS-WRITE-TOOL-READ: %s\n",
                file.diagnosticId ? file.diagnosticId : "SHADER-VERIFIER-SOURCE-FAILURE");
            return 1;
        }
        if (argc == 5) {
            if (std::string_view(argv[3]) != "--buffer-declarations")
                return fail("SHADER-BUFFER-LAYOUT-TOOL-MODE");
            std::uint32_t maxBindings = 0u;
            arp::core::AsciiTokenCursor cursor(argv[4]);
            if (!cursor.decimalUint32(maxBindings) || !cursor.atEnd())
                return fail("SHADER-BUFFER-LAYOUT-TOOL-BUDGET");
            arp::core::pmr::Vector<arp::shader::SpirvBufferDeclarationLayout> layouts{
                &arp::core::defaultCpuMemoryResource()};
            if (const auto* id = arp::shader::SpirvBufferDeclarationLayoutVerifier::verify(
                    file.bytes.data(), file.bytes.size(), maxBindings, layouts))
                return fail(id);
            std::printf(
                "type: SpirvBufferDeclarationLayouts\ncontractVersion: 1\nstatus: DeclarationOnly\nbindings:%s\n",
                layouts.empty() ? " []" : "");
            for (const auto& facts : layouts)
                std::printf(
                    "  - set: %u\n    binding: %u\n    spirvId: %u\n    blockTypeId: %u\n    storageClass: %u\n"
                    "    descriptorCount: %llu\n    staticOccupiedExtentBytes: %llu\n    hasRuntimeArray: %s\n"
                    "    runtimeArrayOffsetBytes: %llu\n    runtimeArrayStrideBytes: %llu\n    runtimeElementOccupiedExtentBytes: %llu\n",
                    facts.set,
                    facts.binding,
                    facts.spirvId,
                    facts.blockTypeId,
                    facts.storageClass,
                    static_cast<unsigned long long>(facts.descriptorCount),
                    static_cast<unsigned long long>(facts.staticOccupiedExtentBytes),
                    facts.hasRuntimeArray ? "true" : "false",
                    static_cast<unsigned long long>(facts.runtimeArrayOffsetBytes),
                    static_cast<unsigned long long>(facts.runtimeArrayStrideBytes),
                    static_cast<unsigned long long>(facts.runtimeElementOccupiedExtentBytes));
            return 0;
        }
        arp::shader::FragmentAtomicWriteProof::Facts facts;
        if (const auto* id = arp::shader::FragmentAtomicWriteProof::verify(
                file.bytes.data(), file.bytes.size(), argv[3], facts))
            return fail(id);
        std::printf(
            "type: FragmentAtomicWriteProof\ncontractVersion: %u\nset: %u\nbinding: %u\nreplacement: %u\nbyteOffset: %llu\nbyteCount: %llu\n",
            facts.contractVersion,
            facts.set,
            facts.binding,
            facts.replacement,
            static_cast<unsigned long long>(facts.byteOffset),
            static_cast<unsigned long long>(facts.byteCount));
        return 0;
    }

private:
    static int fail(const char* id)
    {
        std::fprintf(stderr, "%s\n", id);
        return 1;
    }
};
} // namespace

int main(int argc, char** argv)
{
    try {
        return VerifierApplication{}.run(argc, argv);
    } catch (const std::bad_alloc&) {
        std::fputs("SHADER-GRAPHICS-WRITE-TOOL-ALLOCATION\n", stderr);
        return 1;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "SHADER-GRAPHICS-WRITE-TOOL-READ: %s\n", error.what());
        return 1;
    }
}
