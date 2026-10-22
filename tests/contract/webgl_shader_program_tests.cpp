#include "core/math/math.h"
#include "rhi/semantics/webgl_shader_coordinates.h"
#include "shader/webgl_uniform_snapshots.h"

#include <cassert>
#include <cstdio>
#include <fstream>

using namespace arp;
using Y = core::YamlContract;

static shader::WebGlShaderLoadBudget budget()
{
    return {{4u * 1024u * 1024u, 32u * 1024u * 1024u, 32u * 1024u * 1024u, 32u}, 256u, 32u, 1024u};
}

static auto load(const std::filesystem::path& path, core::pmr::String& diagnostic)
{
    return shader::WebGlShaderProgramArtifact::load(path,
        budget(),
        core::makeShared<core::CpuMemoryQuota>(budget().original.maxCpuMetadataBytes),
        diagnostic);
}

int main(int argc, char** argv)
{
    const std::filesystem::path directory = argc > 1 ? argv[1] : ARP_WEBGL_PROGRAM_DIR;
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    const auto p = load(directory / "webgl1_graphics.webgl-program.yaml", diagnostic);
    if (argc > 3) {
        if (p || (std::string(argv[3]) != "*" && diagnostic != argv[3])) {
            std::fprintf(stderr, "negative mismatch: %s\n", diagnostic.c_str());
            return 2;
        }
        std::printf("rejected: %s\n", diagnostic.c_str());
        return 0;
    }
    if (!p)
        std::fprintf(stderr, "load: %s\n", diagnostic.c_str());
    assert(p && !shader::WebGlShaderProgramArtifact::validateOriginal(p));
    assert(!p->nativeExecutionProven() && p->vertex().inputs.size() == 3
        && p->textureUnits().size() == 2);
    shader::WebGlShaderLimits facts{
        16, 16, 256, 224, 16, 16, 32, true, true, true, true, true, true};
    assert(!p->assess(facts));
    facts.fragmentHighpFloat = false;
    assert(p->assess(facts));
    facts.fragmentHighpFloat = true;
    facts.attributes = 2;
    assert(p->assess(facts));
    facts.attributes = 16;
    facts.fragmentUniformVectors = 1;
    assert(p->assess(facts));
    facts.fragmentUniformVectors = 224;
    facts.combinedTextures = 1;
    assert(p->assess(facts));
    facts.combinedTextures = 32;
    if (argc < 4) {
        const auto ext =
            load(directory / "extensions/webgl1_extensions.webgl-program.yaml", diagnostic);
        if (!ext)
            std::fprintf(stderr, "extensions: %s\n", diagnostic.c_str());
        assert(ext && ext->fragment().extensions.size() == 2 && !ext->assess(facts));
        facts.derivatives = false;
        assert(ext->assess(facts));
        facts.derivatives = true;
        facts.textureLod = false;
        assert(ext->assess(facts));
        facts.textureLod = true;
    }
    using Coordinates = rhi::semantics::WebGlShaderCoordinates;
    Coordinates::Rectangle lowered;
    assert(!Coordinates::lowerTopLeftRectangle({3, 7, 11, 13}, 64, 64, lowered));
    assert(lowered.y == 44 && Coordinates::clipDepth(.25f, 1) == -.5f
        && Coordinates::clipY(.25f) == -.25f);
    assert(Coordinates::lowerTopLeftRectangle({0, 63, 64, 2}, 64, 64, lowered));
    shader::WebGlUniformSnapshots snapshots;
    assert(snapshots.prepare(p, {0, 320}));
    assert(snapshots.prepare(p, {2, 319}));
    assert(snapshots.prepare(p, {2, 320}) == nullptr);
    assert(snapshots.prepare(p, {2, 320}));
    shader::WebGlCpuUniformData ubo(256), push(32, true);
    assert(!core::Sha256::isHexDigest("a"));
    assert(!ubo.update(0, "x", 1));
    assert(!ubo.initialized(0, 2));
    assert(ubo.update(256, "x", 1));
    core::Mat4 matrix{};
    matrix.m = {0.5f, 0.125f, 0, 0.125f, 0.0625f, -0.5f, 0, 0.0625f, 0, 0, 0.5f, 0.25f, 0, 0, 0, 1};
    std::array<float, 32> frame{};
    std::copy(matrix.m.begin(), matrix.m.end(), frame.begin());
    for (int i = 16; i < 24; ++i)
        frame[i] = 1;
    std::array<float, 8> draw{-.35f, 0, 0, 0, 1, 0, 0, 0};
    assert(!ubo.update(128, frame.data(), sizeof(frame)));
    assert(push.update(0, draw.data(), 16));
    assert(!push.update(0, draw.data(), sizeof(draw)));
    shader::WebGlUniformSnapshots::Binding bindings[]{
        {false, 0, 0, &ubo, 0, 128, 128, 16}, {true, 0, 0, &push, 0, 32, 0, 4}};
    std::uint32_t first = 0, second = 0;
    const auto allocationsBefore = core::defaultCpuAllocator().stats().allocationCount;
    assert(!snapshots.record(bindings, 2, first) && first == 0);
    assert(core::defaultCpuAllocator().stats().allocationCount == allocationsBefore);
    draw[0] = .35f;
    draw[4] = 0;
    draw[5] = 1;
    draw[7] = 1;
    assert(!push.update(0, draw.data(), sizeof(draw)));
    frame[24] = .125f;
    assert(!ubo.update(128 + 96, frame.data() + 24, 16));
    auto bad = bindings[0];
    bindings[0].dynamicOffset = 129;
    assert(snapshots.record(bindings, 2, second) && snapshots.size() == 1);
    bindings[0] = bad;
    shader::WebGlCpuUniformData untouched(128);
    bindings[0] = {false, 0, 0, &untouched, 0, 128, 0, 16};
    assert(snapshots.record(bindings, 2, second) && snapshots.size() == 1);
    bindings[0] = bad;
    const auto allocationsBeforeSecond = core::defaultCpuAllocator().stats().allocationCount;
    assert(!snapshots.record(bindings, 2, second) && second == 1);
    assert(core::defaultCpuAllocator().stats().allocationCount == allocationsBeforeSecond);
    assert(snapshots.record(bindings, 2, second) && snapshots.size() == 2);
    assert(snapshots.data(2, snapshots.slots()[0]) == nullptr);
    auto forged = snapshots.slots()[0];
    assert(!snapshots.data(0, forged));
    // The original resource CPU-byte adapter shares snapshot validation and
    // preserves immutability after external writes/invalidation.
    shader::WebGlUniformSnapshots resourceSnapshots;
    assert(!resourceSnapshots.prepare(p, {2, 320}));
    core::InitializedByteBuffer resourceBytes(128);
    assert(resourceBytes.write(0, frame.data(), 64));
    shader::WebGlUniformSnapshots::Binding resourceBindings[]{bindings[0], bindings[1]};
    resourceBindings[0] = {false, 0, 0, nullptr, 0, 128, 0, 4, &resourceBytes};
    assert(resourceSnapshots.validateRecord(resourceBindings, 2));
    assert(!resourceSnapshots.size());
    assert(resourceBytes.write(64, frame.data() + 16, 64));
    assert(!resourceSnapshots.validateRecord(resourceBindings, 2));
    assert(!resourceSnapshots.size());
    assert(!resourceSnapshots.record(resourceBindings, 2, first));
    resourceBytes.invalidate();
    assert(!resourceBytes.initialized(0, 128));
    assert(resourceSnapshots.validateRecord(resourceBindings, 2));
    assert(resourceSnapshots.size() == 1);
    resourceBindings[0].source = &ubo;
    assert(resourceSnapshots.validateRecord(resourceBindings, 2));
    Y::Node result = Y::Node::mapping_type{};
    result["type"] = "WebGlShaderSnapshotFixture";
    result["contractVersion"] = 1;
    std::string identity;
    for (auto byte : p->identity()) {
        identity += "0123456789abcdef"[byte >> 4];
        identity += "0123456789abcdef"[byte & 15];
    }
    result["programIdentity"] = identity;
    result["draws"] = Y::Node::sequence_type{};
    for (std::uint32_t d = 0; d < 2; ++d) {
        Y::Node values = Y::Node::sequence_type{};
        for (const auto& slot : snapshots.slots()) {
            Y::Node v = Y::Node::mapping_type{};
            v["push"] = slot.push;
            v["set"] = slot.set;
            v["binding"] = slot.binding;
            Y::Node bytes = Y::Node::sequence_type{};
            for (std::size_t b = 0; b < slot.size; ++b)
                bytes.get_value_ref<Y::Node::sequence_type&>().push_back(
                    int(snapshots.data(d, slot)[b]));
            v["bytes"] = bytes;
            values.get_value_ref<Y::Node::sequence_type&>().push_back(v);
        }
        result["draws"].get_value_ref<Y::Node::sequence_type&>().push_back(values);
    }
    // Snapshots outlive all later caller writes; immutable first draw stays red/left.
    const auto& slot = snapshots.slots()[1];
    float x = 0;
    std::memcpy(&x, snapshots.data(0, slot), 4);
    assert(x == -.35f);
    std::memcpy(&x, snapshots.data(1, slot), 4);
    assert(x == .35f);
    if (argc > 2) {
        std::ofstream out(argv[2]);
        out << Y::Node::serialize(result);
    }
    std::puts(
        "WebGlShaderProgram/1 original binary ABI, limits, immutable two-draw snapshots passed");
}
