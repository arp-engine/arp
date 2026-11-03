#include "animation/skeleton.h"
#include "assets/mesh/glb_accessor_table.h"
#include "core/io/file_io.h"
#include "core/io/file_service.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "tests/fixtures/static_glb_geometry.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>

namespace {
using namespace arp;
using namespace assets::detail;
using namespace core::io;
constexpr GlbAccessorTable::Limits limits{152, 5, 5, 10 * sizeof(GlbAccessorSpan), 11, 38};

// The fixture contains animation times, a non-symmetric glTF IBM, strided u8
// joints, normalized u16 weights and u8 indices in one embedded binary buffer.
class Fixture final {
public:
    static core::pmr::String document()
    {
        // clang-format off
        return R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":152}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":12},
{"buffer":0,"byteOffset":16,"byteLength":64},
{"buffer":0,"byteOffset":80,"byteLength":16,"byteStride":8},
{"buffer":0,"byteOffset":96,"byteLength":16},
{"buffer":0,"byteOffset":112,"byteLength":3}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"SCALAR"},
{"bufferView":1,"componentType":5126,"count":1,"type":"MAT4"},
{"bufferView":2,"componentType":5121,"count":2,"type":"VEC4"},
{"bufferView":3,"componentType":5123,"count":2,"type":"VEC4","normalized":true},
{"bufferView":4,"componentType":5121,"count":3,"type":"SCALAR"}]})";
        // clang-format on
    }

    static void word(core::ByteBuffer& bytes, std::size_t offset, std::uint32_t value)
    {
        assert(core::UnsignedElementSpan::writeLittleEndian(value,
            core::UnsignedElementSpan::Width::Uint32,
            bytes.data() + offset,
            bytes.size() - offset));
    }

    static void real(core::ByteBuffer& bytes, std::size_t offset, float value)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        word(bytes, offset, bits);
    }

    static core::ByteBuffer bytes()
    {
        const auto json = document();
        auto raw = tests::StaticGlbGeometryFixture::bytes(std::string(json.data(), json.size()));
        const auto start = raw.size() - 152;
        std::fill(raw.begin() + start, raw.end(), 0);
        real(raw, start, 0);
        real(raw, start + 4, .5f);
        real(raw, start + 8, 1.5f);
        constexpr std::array<float, 16> matrix{2, 2, 3, 0, 4, 5, 6, 0, 7, 8, 10, 0, 10, 20, 30, 1};
        for (std::size_t i = 0; i < matrix.size(); ++i)
            real(raw, start + 16 + i * 4, matrix[i]);
        for (unsigned i = 0; i < 4; ++i) {
            raw[start + 80 + i] = i;
            raw[start + 88 + i] = i + 4;
        }
        constexpr std::array<std::uint16_t, 8> weights{65535, 0, 0, 0, 0, 32768, 32767, 0};
        for (std::size_t i = 0; i < weights.size(); ++i)
            assert(core::UnsignedElementSpan::writeLittleEndian(weights[i],
                core::UnsignedElementSpan::Width::Uint16,
                raw.data() + start + 96 + 2 * i,
                2));
        raw[start + 113] = 1;
        raw[start + 114] = 2;
        return raw;
    }

    static JsonValue root(const core::pmr::String& json = document())
    {
        JsonValue result;
        assert(parseJson(json, result, {16, 256, false}));
        return result;
    }

    static JsonValue& member(JsonValue& value, const char* name)
    {
        auto* result = value.find(name);
        assert(result);
        return *const_cast<JsonValue*>(result);
    }
};

class LayoutProbe final : private GlbGeometrySchema {
public:
    static const char* check(
        const JsonValue& root, const core::pmr::String& bin, GlbLayoutProfile profile)
    {
        try {
            validateLayout(root, bin, profile);
            return nullptr;
        } catch (const Failure& failure) {
            return failure.diagnostic;
        }
    }
};

void check(const GlbAccessorTable& table)
{
    assert(table.size() == 5 && !table.find(5));
    const auto before = core::defaultCpuAllocator().stats();
    float value = -9;
    assert(table.find(0)->count() == 3 && table.find(0)->components() == 1);
    assert(table.find(0)->readFloat(1, 0, value) && value == .5f);
    animation::Mat4 matrix;
    for (unsigned i = 0; i < 16; ++i)
        assert(table.find(1)->readFloat(0, i, matrix.m[i]));
    const auto point = animation::transformPoint(matrix, {2, 3, 4});
    assert(point.x == 54 && point.y == 71 && point.z == 94);
    assert(matrix.m[12] == 10 && matrix.m[13] == 20 && matrix.m[14] == 30);
    std::uint32_t joint = 99;
    for (unsigned row = 0; row < 2; ++row)
        for (unsigned col = 0; col < 4; ++col)
            assert(table.find(2)->readUnsigned(row, col, joint) && joint == row * 4 + col);
    assert(table.find(3)->normalized() && table.find(3)->readFloat(0, 0, value) && value == 1);
    assert(table.find(3)->readFloat(1, 1, value) && value == 32768.f / 65535.f);
    assert(table.find(4)->readUnsigned(2, 0, joint) && joint == 2);
    value = -9;
    joint = 99;
    assert(!table.find(0)->readUnsigned(0, 0, joint) && joint == 99);
    assert(!table.find(3)->readUnsigned(0, 0, joint) && joint == 99);
    assert(!table.find(2)->readFloat(0, 0, value) && value == -9);
    assert(!table.find(0)->readFloat(3, 0, value) && value == -9);
    assert(!table.find(1)->readFloat(0, 16, value) && value == -9);
    const GlbAccessorSpan empty;
    assert(!empty.readFloat(0, 0, value) && !empty.readUnsigned(0, 0, joint));
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.allocationCount == after.allocationCount);
}

void legacyRotationProbe()
{
    animation::AnimationClip clip;
    clip.durationSeconds = 1;
    animation::AnimationChannel channel;
    channel.jointIndex = 0;
    channel.targetPath = "rotation";
    animation::JointKeyframe first, last;
    first.timeSeconds = 0;
    first.transform.rotation = animation::Quat::identity();
    last.timeSeconds = 1;
    last.transform.rotation = {0, 1, 0, 0};
    channel.keyframes = {first, last};
    clip.channels.push_back(std::move(channel));
    core::pmr::Vector<animation::JointTransform> pose;
    pose.resize(1);
    assert(animation::sampleClip(clip, .25f, pose) && pose.size() == 1);
    const auto actual = pose.front().rotation.y;
    const auto legacy = .25f / std::sqrt(.25f * .25f + .75f * .75f);
    const auto gltfLinear = std::sin(std::acos(-1.f) / 8.f);
    // Default legacy channels keep their current ABI/behavior. A future glTF
    // source profile needs an explicit slerp payload; it cannot relabel nlerp.
    assert(std::abs(actual - legacy) < 1e-6f && std::abs(actual - gltfLinear) > .01f);
    std::printf(
        "Legacy rotation probe: y=%g; glTF LINEAR needs y=%g at t=0.25\n", actual, gltfLinear);
}

void contracts()
{
    legacyRotationProbe();
    const auto raw = Fixture::bytes();
    GlbChunks chunks;
    core::pmr::String message;
    assert(readGlbChunks({reinterpret_cast<const char*>(raw.data()), raw.size()}, chunks, message));
    auto root = Fixture::root();
    assert(!LayoutProbe::check(root, chunks.bin, GlbLayoutProfile::Animated));
    for (const auto profile : {GlbLayoutProfile::StrictStatic,
             GlbLayoutProfile::StaticScene,
             static_cast<GlbLayoutProfile>(99)})
        assert(LayoutProbe::check(root, chunks.bin, profile));
    GlbAccessorTable table;
    const char* diagnostic = nullptr;
    assert(table.prepare(root, chunks.bin, limits, diagnostic) && !diagnostic);
    check(table);
    {
        auto integerRoot = Fixture::root();
        auto integerBytes = chunks.bin;
        auto& accessor = Fixture::member(integerRoot, "accessors").arrayValue[4];
        auto& view = Fixture::member(integerRoot, "bufferViews").arrayValue[4];
        Fixture::member(accessor, "componentType").numberValue = 5125;
        Fixture::member(view, "byteLength").numberValue = 12;
        constexpr std::uint32_t exact = 0xabcdef01u;
        assert(core::UnsignedElementSpan::writeLittleEndian(
            exact, core::UnsignedElementSpan::Width::Uint32, integerBytes.data() + 112, 4));
        GlbAccessorTable integers;
        assert(integers.prepare(integerRoot, integerBytes, limits, diagnostic));
        std::uint32_t value = 0;
        assert(integers.find(4)->readUnsigned(0, 0, value) && value == exact);
        auto& weights = Fixture::member(integerRoot, "accessors").arrayValue[3];
        Fixture::member(weights, "normalized").boolValue = false;
        assert(integers.prepare(integerRoot, integerBytes, limits, diagnostic));
        assert(integers.find(3)->readUnsigned(0, 0, value) && value == UINT16_MAX);
    }
    const auto* original = table.find(0);
    for (unsigned test = 0; test < 27; ++test) {
        auto changed = Fixture::root();
        auto budget = limits;
        auto& accessors = Fixture::member(changed, "accessors").arrayValue;
        auto& views = Fixture::member(changed, "bufferViews").arrayValue;
        if (test == 0)
            budget.maxBinaryBytes = 0;
        if (test == 1)
            budget.maxBinaryBytes = 151;
        if (test == 2)
            budget.maxBufferViews = 4;
        if (test == 3)
            budget.maxAccessors = 4;
        if (test == 4)
            budget.maxDescriptorBytes = limits.maxDescriptorBytes - 1;
        if (test == 5)
            budget.maxElements = 10;
        if (test == 6)
            budget.maxScalars = 37;
        if (test == 7)
            Fixture::member(accessors[0], "count").numberValue = 4;
        if (test == 8)
            Fixture::member(accessors[1], "componentType").numberValue = 5123;
        if (test == 9)
            Fixture::member(views[2], "byteStride").numberValue = 2;
        if (test == 10)
            Fixture::member(views[2], "byteStride").numberValue = 7;
        if (test == 11)
            Fixture::member(accessors[0], "bufferView").numberValue = 5;
        if (test == 12)
            Fixture::member(accessors[0], "count").numberValue = -.5;
        if (test == 13)
            Fixture::member(accessors[0], "count").numberValue = .5;
        if (test == 14)
            Fixture::member(accessors[0], "count").numberValue = 4294967296.;
        if (test == 15)
            Fixture::member(views[0], "byteOffset").numberValue = 1;
        if (test == 16)
            Fixture::member(views[1], "byteLength").numberValue = 63;
        if (test == 17)
            Fixture::member(views[1], "byteLength").numberValue = 1000;
        if (test == 18)
            Fixture::member(views[1], "buffer").numberValue = 1;
        if (test == 19)
            Fixture::member(accessors[1], "type").stringValue = "MAT3";
        if (test == 20)
            Fixture::member(accessors[3], "componentType").numberValue = 5126;
        if (test == 21)
            Fixture::member(accessors[3], "componentType").numberValue = 5125;
        if (test == 22)
            Fixture::member(accessors[3], "normalized").type = JsonValue::Type::Number;
        if (test == 23)
            budget.maxElements = UINT64_MAX;
        if (test == 24)
            budget.maxDescriptorBytes = SIZE_MAX;
        if (test == 25)
            budget.maxScalars = UINT64_MAX;
        if (test == 26)
            budget.maxDescriptorBytes = 5 * sizeof(GlbAccessorSpan);
        assert(!table.prepare(changed, chunks.bin, budget, diagnostic) && diagnostic);
        assert(table.find(0) == original);
        check(table);
    }
    auto nonfinite = chunks.bin;
    constexpr std::uint32_t nanBits = 0x7fc00000;
    assert(core::UnsignedElementSpan::writeLittleEndian(
        nanBits, core::UnsignedElementSpan::Width::Uint32, nonfinite.data(), nonfinite.size()));
    GlbAccessorTable finite;
    assert(finite.prepare(root, nonfinite, limits, diagnostic));
    float value = 7;
    assert(!finite.find(0)->readFloat(0, 0, value) && value == 7);
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(!table.prepare(root, chunks.bin, limits, diagnostic)
            && core::StringView(diagnostic) == "ASSET-GLB-ACCESSOR-ALLOCATION");
    }
    assert(table.find(0) == original && quota->liveBytes() == 0);
    check(table);
}

void cycle()
{
    const auto bytes = Fixture::bytes();
    auto backend =
        core::makeShared<MemoryFileBackend>(MemoryFileBackendDesc{1, 4096, 1, 4096, 128});
    assert(
        backend->registerFile("animated.glb", bytes.data(), bytes.size()) == FileIoStatus::Success);
    MountedFileBackend mounts({1, 1, 4096, 32, 128});
    const auto mount = mounts.mount("animated", backend);
    core::runtime::CompletionQueue queue({2});
    FileService files(queue, mounts);
    FileReadRequest request;
    request.contractVersion = 1;
    request.pathScope = FilePathScope::MountedResource;
    request.path = "animated.glb";
    request.mount = mount;
    request.maxBytes = 4096;
    FileReadResult retained;
    bool done = false;

    struct Callback {
        FileReadResult& retained;
        bool& done;
    } callback{retained, done};

    assert(files.read(request,
               {[](void* data, FileReadResult value) {
                    auto& callback = *static_cast<Callback*>(data);
                    callback.retained = std::move(value);
                    callback.done = true;
                },
                   &callback})
        == FileIoStatus::Accepted);
    assert(!done);
    queue.dispatch(2);
    assert(done && retained.status == FileIoStatus::Success);
    assert(mounts.unmount(mount));
    backend->shutdown();
    GlbChunks chunks;
    core::pmr::String diagnostic;
    assert(
        readGlbChunks({reinterpret_cast<const char*>(retained.bytes.data()), retained.bytes.size()},
            chunks,
            diagnostic));
    auto root = Fixture::root(chunks.json);
    GlbAccessorTable table;
    const char* issue = nullptr;
    assert(table.prepare(root, chunks.bin, limits, issue));
    root = {};
    retained.bytes.clear();
    check(table); // Only BIN remains borrowed; JSON and location authority are gone.
    assert(files.telemetry().submitted == 1 && !queue.pending() && !mounts.telemetry().inFlight
        && !mounts.telemetry().reservedReadBytes);
}

void corpus(const std::filesystem::path& path)
{
    // An explicit test profile for the two existing Build-a-Queen characters:
    // each has fewer than 3000 views/accessors and 106000 emitted elements.
    auto quota = core::makeShared<core::CpuMemoryQuota>(64 * 1024 * 1024);
    const auto baseline = core::defaultCpuAllocator().stats();
    {
        core::CpuMemoryQuotaScope scope(quota);
        const auto source = core::readBinaryFile(path, 2 * 1024 * 1024);
        assert(source.success);
        GlbChunks chunks;
        core::pmr::String message;
        assert(
            readGlbChunks({reinterpret_cast<const char*>(source.bytes.data()), source.bytes.size()},
                chunks,
                message));
        JsonValue root;
        assert(parseJson(chunks.json, root, {32, 262144, false}));
        GlbAccessorTable table;
        const char* issue = nullptr;
        const GlbAccessorTable::Limits characterLimits{
            2 * 1024 * 1024, 3000, 3000, 3000 * sizeof(GlbAccessorSpan), 200000, 1000000};
        assert(table.prepare(root, chunks.bin, characterLimits, issue));
        std::uint64_t scalars = 0;
        std::size_t matrices = 0, floatTimes = 0;
        const auto before = core::defaultCpuAllocator().stats();
        for (std::size_t index = 0; index < table.size(); ++index) {
            const auto& accessor = *table.find(index);
            if (accessor.components() == 16)
                ++matrices;
            if (accessor.components() == 1 && accessor.componentType() == 5126)
                ++floatTimes;
            for (std::uint32_t element = 0; element < accessor.count(); ++element)
                for (std::uint32_t component = 0; component < accessor.components(); ++component) {
                    if (accessor.componentType() == 5126 || accessor.normalized()) {
                        float value;
                        assert(
                            accessor.readFloat(element, component, value) && std::isfinite(value));
                    } else {
                        std::uint32_t value;
                        assert(accessor.readUnsigned(element, component, value));
                    }
                    ++scalars;
                }
        }
        const auto after = core::defaultCpuAllocator().stats();
        assert(matrices && floatTimes && before.allocationCount == after.allocationCount);
        std::printf(
            "Actual character accessor corpus: %zu accessors, %zu matrices, %zu float-time accessors, %llu scalars, core peak %llu bytes\n",
            table.size(),
            matrices,
            floatTimes,
            (unsigned long long)scalars,
            (unsigned long long)quota->peakBytes());
    }
    const auto after = core::defaultCpuAllocator().stats();
    assert(quota->liveBytes() == 0 && after.liveBytes == baseline.liveBytes
        && after.allocationCount - after.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && core::StringView(argv[1]) == "--corpus")
        corpus(std::filesystem::u8path(argv[2]));
    else if (argc == 1) {
        contracts();
        cycle();
    } else {
        assert(argc == 2 && core::StringView(argv[1]) == "--smoke");
        for (unsigned i = 0; i < 20; ++i)
            cycle();
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            cycle();
        const auto after = core::defaultCpuAllocator().stats();
        std::printf("Animated accessor 20+20: core bytes %llu -> %llu; live %llu -> %llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)after.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)(after.allocationCount - after.deallocationCount));
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
    }
    std::puts(
        "PASS bounded glTF animated accessor layout, little-endian values, failure preservation and retained-source smoke");
}
