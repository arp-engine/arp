#include "tests/fixtures/wocc_ibl_frozen_inputs_fixture.h"

#ifdef NDEBUG
#error Frozen IBL input tests require assertions
#endif

int main(int argc, char** argv)
{
    assert(argc == 2);
    using namespace arp;
    using Frozen = test::WoccIblFrozenInputsFixture;
    const std::filesystem::path manifest(argv[1]);
    const auto text = core::readTextFile(manifest, Frozen::manifestBytes);
    assert(text.success);
    const auto inputs = Frozen::load(manifest, Frozen::payloadBytes);
    assert(inputs && inputs->cubes[0].storage.size() * 2u == 262128u
        && inputs->cubes[1].storage.size() * 2u == 262128u && inputs->lut.size() * 2u == 131072u);
    const auto samples = test::OriginalGraphicsWoccIblDataFixture::analyticalSamples(*inputs);
    assert(std::all_of(samples.begin(), samples.end(), [](float value) {
        return std::isfinite(value);
    }));
    const auto reject = [&](const auto& candidate, std::uint64_t bytes, const char* expected) {
        bool refused = false;
        try {
            (void)Frozen::fromText(candidate, manifest.parent_path(), bytes);
        } catch (const core::YamlContractError& error) {
            refused = true;
            assert(std::strcmp(error.diagnostic, expected) == 0);
        }
        assert(refused);
    };
    reject(text.text, Frozen::payloadBytes - 1u, "BINDLESS-IBL-INPUTS-BUDGET");
    const auto changed = [&](core::StringView before, core::StringView after, const char* id) {
        auto copy = text.text;
        const auto at = copy.find(before);
        assert(at != copy.npos);
        copy.replace(at, before.size(), after);
        reject(copy, Frozen::payloadBytes, id);
    };
    changed("contractVersion: 1", "contractVersion: 2", "BINDLESS-IBL-INPUTS-SCHEMA");
    changed("Rgba16FloatLittleEndian", "Rgba16FloatBigEndian", "BINDLESS-IBL-INPUTS-ENCODING");
    changed("+X,-X,+Y,-Y,+Z,-Z", "-X,+X,+Y,-Y,+Z,-Z", "BINDLESS-IBL-INPUTS-ENCODING");
    changed("role: zenith", "role: horizon", "BINDLESS-IBL-INPUTS-RESOURCE-METADATA");
    changed("width: 64", "width: 32", "BINDLESS-IBL-INPUTS-RESOURCE-METADATA");
    changed("mipLevels: 7", "mipLevels: 6", "BINDLESS-IBL-INPUTS-RESOURCE-METADATA");
    changed("byteCount: 262128", "byteCount: 262127", "BINDLESS-IBL-INPUTS-RESOURCE-METADATA");
    changed("offsetBytes: 196608", "offsetBytes: 196610", "BINDLESS-IBL-INPUTS-MIP-LAYOUT");
    changed("file: horizon.rgba16f", "file: ../horizon.rgba16f", "BINDLESS-IBL-INPUTS-PATH");
    changed("file: horizon.rgba16f", "file: missing.rgba16f", "BINDLESS-IBL-INPUTS-READ");
    const auto hashAt = text.text.find("sha256: ");
    assert(hashAt != text.text.npos);
    auto hash = text.text;
    hash[hashAt + 8u] = hash[hashAt + 8u] == '0' ? '1' : '0';
    reject(hash, Frozen::payloadBytes, "BINDLESS-IBL-INPUTS-HASH");
    core::pmr::String oversized(Frozen::manifestBytes + 1u, ' ');
    reject(oversized, Frozen::payloadBytes, "BINDLESS-IBL-INPUTS-MANIFEST-BUDGET");
    std::puts(
        "BINDLESS_WOCC_IBL_FROZEN_INPUTS1_OK sha256 shapes3 mip15 bytes655328 hostile-schema-layout-hash-budget no-rebake");
}
