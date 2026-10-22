#include "shader/webgl_shader_program_artifact.h"

#include "core/text/number_format.h"

#include <algorithm>

namespace arp::shader {
namespace {
using Y = core::YamlContract;
using G = BufferDeclarationTypeLayouts;
using I = SpirvInstructionView::Instruction;

void need(bool value, const char* id)
{
    requireShaderArtifact(value, id);
}

void schema(const Y::Node& n, std::size_t count)
{
    need(n.is_mapping() && n.size() == count, "SHADER-WEBGL1-SCHEMA");
}

Y::Node parse(const core::ByteBuffer& bytes)
{
    auto s = shaderArtifactText(bytes);
    return Y::Node::deserialize(s.begin(), s.end());
}

core::pmr::String indexed(core::StringView prefix, std::uint32_t index)
{
    core::pmr::String s(prefix, &core::defaultCpuMemoryResource());
    core::appendUnsigned(s, index);
    return s;
}

core::pmr::String rootName(bool push, std::uint32_t set, std::uint32_t binding)
{
    if (push)
        return core::pmr::String("arpPush", &core::defaultCpuMemoryResource());
    auto name = indexed("arpUbo_", set);
    name += '_';
    core::appendUnsigned(name, binding);
    return name;
}

bool sameInterface(const WebGlShaderInterface& a, const WebGlShaderInterface& b)
{
    return a.name == b.name && a.location == b.location && a.components == b.components;
}

bool samePair(const WebGlSamplerPair& a, const WebGlSamplerPair& b)
{
    return a.name == b.name && a.textureSet == b.textureSet && a.textureBinding == b.textureBinding
        && a.samplerSet == b.samplerSet && a.samplerBinding == b.samplerBinding;
}

// Binary grammar for the deliberately conservative ES100/1 subset. Shared
// instruction framing and declaration DAG own parsing; this owns admission.
class BinaryAbi final {
public:
    BinaryAbi(const core::ByteBuffer& bytes) : view_(bytes.data(), bytes.size())
    {
        need(!view_.diagnostic(), "SHADER-WEBGL1-SPIRV-BINARY");
    }

    static int result(const I& i)
    {
        if ((i.opcode() >= 19 && i.opcode() <= 33) || i.opcode() == 7 || i.opcode() == 11)
            return 0;
        switch (i.opcode()) {
        case 41:
        case 42:
        case 43:
        case 44:
        case 54:
        case 55:
        case 57:
        case 59:
        case 61:
        case 65:
        case 66:
        case 86:
            return 1;
        default:
            return -1;
        }
    }

    I definition(std::uint32_t id) const
    {
        I i;
        need(view_.definition(id, result, i), "SHADER-WEBGL1-BINARY-DEFINITION");
        return i;
    }

    std::uint32_t decoration(std::uint32_t id, std::uint32_t kind) const
    {
        std::uint32_t v = 0;
        need(view_.decoration(id, kind, v), "SHADER-WEBGL1-BINARY-DECORATION");
        return v;
    }

    template <class F> void each(F f) const
    {
        for (auto at = view_.headerWords; at < view_.sizeWords();) {
            I i;
            need(view_.read(at, i), "SHADER-WEBGL1-BINARY-FRAMING");
            at = i.next();
            f(i);
        }
    }

    void validateSubset() const
    {
        each([&](const I& i) {
            switch (i.opcode()) {
            case 0:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 7:
            case 8:
            case 10:
            case 11:
            case 12:
            case 14:
            case 15:
            case 16:
            case 17:
            case 19:
            case 20:
            case 21:
            case 22:
            case 23:
            case 24:
            case 25:
            case 26:
            case 27:
            case 28:
            case 30:
            case 32:
            case 33:
            case 41:
            case 42:
            case 43:
            case 44:
            case 54:
            case 55:
            case 56:
            case 57:
            case 59:
            case 61:
            case 62:
            case 65:
            case 66:
            case 71:
            case 72:
            case 79:
            case 80:
            case 81:
            case 82:
            case 83:
            case 84:
            case 86:
            case 87:
            case 88:
            case 109:
            case 110:
            case 111:
            case 124:
            case 126:
            case 127:
            case 128:
            case 129:
            case 130:
            case 131:
            case 132:
            case 133:
            case 135:
            case 136:
            case 138:
            case 140:
            case 141:
            case 142:
            case 143:
            case 144:
            case 145:
            case 146:
            case 147:
            case 148:
            case 154:
            case 155:
            case 156:
            case 157:
            case 164:
            case 165:
            case 166:
            case 167:
            case 168:
            case 169:
            case 170:
            case 171:
            case 173:
            case 175:
            case 177:
            case 179:
            case 180:
            case 182:
            case 184:
            case 186:
            case 188:
            case 190:
            case 207:
            case 208:
            case 209:
            case 245:
            case 247:
            case 248:
            case 249:
            case 250:
            case 253:
            case 254:
            case 317:
            case 330:
                break;
            default:
                need(false, "SHADER-WEBGL1-OPCODE-UNSUPPORTED");
            }
            if (i.opcode() == 22)
                need(i.operand(1) == 32, "SHADER-WEBGL1-FLOAT-WIDTH");
            if (i.opcode() == 21)
                need(i.operand(1) == 32, "SHADER-WEBGL1-INT-WIDTH");
            if (i.opcode() == 65 || i.opcode() == 66)
                for (std::uint32_t k = 3; k < i.operands(); ++k)
                    need(definition(i.operand(k)).opcode() == 43,
                        "SHADER-WEBGL1-DYNAMIC-INDEX-UNPROVEN");
            if (i.opcode() == 71) {
                const auto d = i.operand(1), v = i.operand(2);
                need(d != 13 && d != 14 && d != 16, "SHADER-WEBGL1-INTERPOLATION-UNSUPPORTED");
                need(d != 11 || v == 0 || v == 17, "SHADER-WEBGL1-BUILTIN-UNSUPPORTED");
            }
            if (i.opcode() == 59) {
                auto s = i.operand(2);
                need(s == 0 || s == 1 || s == 2 || s == 3 || s == 6 || s == 7 || s == 9,
                    "SHADER-WEBGL1-STORAGE-UNSUPPORTED");
            }
            if (i.opcode() == 88)
                need(i.operands() == 6 && i.operand(4) == 2,
                    "SHADER-WEBGL1-EXPLICIT-SAMPLE-OPERANDS");
            if (i.opcode() == 208)
                each([&](const I& consumer) {
                    const auto op = consumer.opcode();
                    if ((op < 54 && op != 12) || op == 54 || op == 55 || op == 56 || op == 71
                        || op == 72 || op == 317 || op == 330)
                        return;
                    // Untyped side effects/control flow have no type/result
                    // prefix: Store uses operand 1, ReturnValue operand 0.
                    const auto first = op == 12                ? 4u
                        : op == 62 || (op >= 247 && op <= 254) ? 0u
                                                               : 2u;
                    for (std::uint32_t at = first; at < consumer.operands(); ++at)
                        if (consumer.operand(at) == i.operand(1)) {
                            I imported;
                            need(op == 12 && consumer.operands() == 5 && consumer.operand(3) == 4
                                    && consumer.operand(4) == i.operand(1)
                                    && view_.definition(consumer.operand(2), result, imported)
                                    && imported.opcode() == 11
                                    && imported.stringEquals(1, "GLSL.std.450"),
                                "SHADER-WEBGL1-SIGNED-DDY-UNPROVEN");
                        }
                });
            // Unsigned constants are allowed only as declaration/index literals.
            if ((i.opcode() == 12 || i.opcode() >= 41) && i.opcode() != 43 && i.opcode() != 56
                && i.opcode() != 62 && i.opcode() != 71 && i.opcode() != 72 && i.opcode() != 247
                && i.opcode() != 248 && i.opcode() != 249 && i.opcode() != 250 && i.opcode() != 253
                && i.opcode() != 254 && i.opcode() != 317 && i.opcode() != 330) {
                I t;
                if (view_.definition(i.operand(0), result, t)) {
                    if (t.opcode() == 23)
                        t = definition(t.operand(1));
                    if (t.opcode() == 21)
                        need(t.operand(2) == 1, "SHADER-WEBGL1-UINT-UNSUPPORTED");
                }
            }
        });
    }

    void interfaces(WebGlShaderTarget& target, bool vertex, std::uint32_t max) const
    {
        each([&](const I& i) {
            if (i.opcode() != 59 || (i.operand(2) != 1 && i.operand(2) != 3))
                return;
            std::uint32_t unused = 0;
            if (view_.decoration(i.operand(1), 11, unused))
                return;
            const auto location = decoration(i.operand(1), 30);
            const auto ptr = definition(i.operand(0));
            need(ptr.opcode() == 32, "SHADER-WEBGL1-INTERFACE-POINTER");
            auto t = definition(ptr.operand(2));
            std::uint32_t width = 1;
            if (t.opcode() == 23) {
                width = t.operand(2);
                t = definition(t.operand(1));
            }
            need(t.opcode() == 22 && t.operand(1) == 32 && width >= 1 && width <= 4,
                "SHADER-WEBGL1-INTERFACE-TYPE");
            const bool input = i.operand(2) == 1;
            need(vertex || input || (location == 0 && width == 4), "SHADER-WEBGL1-FRAGMENT-OUTPUT");
            auto& list = input ? target.inputs : target.outputs;
            need(list.size() < max, "SHADER-WEBGL1-INTERFACE-BUDGET");
            for (const auto& old : list)
                need(old.location != location, "SHADER-WEBGL1-INTERFACE-DUPLICATE");
            WebGlShaderInterface v;
            v.location = location;
            v.components = width;
            v.name = indexed(vertex && input ? "arpAttribute"
                    : !vertex && !input      ? "arpOutput"
                                             : "arpVarying",
                location);
            list.push_back(std::move(v));
        });
        for (auto* list : {&target.inputs, &target.outputs})
            std::sort(list->begin(), list->end(), [](const auto& a, const auto& b) {
                return a.location < b.location;
            });
    }

    void pairs(WebGlShaderTarget& target, std::uint32_t max) const
    {
        each([&](const I& i) {
            if (i.opcode() != 86)
                return;
            auto image = definition(i.operand(2)), sampler = definition(i.operand(3));
            need(image.opcode() == 61 && sampler.opcode() == 61, "SHADER-WEBGL1-SAMPLER-DATAFLOW");
            image = definition(image.operand(2));
            sampler = definition(sampler.operand(2));
            need(image.opcode() == 59 && sampler.opcode() == 59 && image.operand(2) == 0
                    && sampler.operand(2) == 0,
                "SHADER-WEBGL1-SAMPLER-DATAFLOW");
            const auto ip = definition(image.operand(0)), sp = definition(sampler.operand(0));
            need(ip.opcode() == 32 && sp.opcode() == 32, "SHADER-WEBGL1-SAMPLER-POINTER");
            const auto it = definition(ip.operand(2));
            const auto st = definition(sp.operand(2));
            need(it.opcode() == 25 && st.opcode() == 26 && it.operand(2) == 1
                    && (it.operand(3) == 0 || it.operand(3) == 2) && it.operand(4) == 0
                    && it.operand(5) == 0 && it.operand(6) == 1,
                "SHADER-WEBGL1-TEXTURE-TYPE");
            const auto scalar = definition(it.operand(1));
            need(scalar.opcode() == 22 && scalar.operand(1) == 32, "SHADER-WEBGL1-TEXTURE-TYPE");
            WebGlSamplerPair p;
            p.textureSet = decoration(image.operand(1), 34);
            p.textureBinding = decoration(image.operand(1), 33);
            p.samplerSet = decoration(sampler.operand(1), 34);
            p.samplerBinding = decoration(sampler.operand(1), 33);
            p.name = indexed("arpTexture_", p.textureSet);
            for (auto v : {p.textureBinding, p.samplerSet, p.samplerBinding}) {
                p.name += '_';
                core::appendUnsigned(p.name, v);
            }
            for (const auto& old : target.pairs)
                if (samePair(old, p))
                    return;
            need(target.pairs.size() < max, "SHADER-WEBGL1-SAMPLER-BUDGET");
            target.pairs.push_back(std::move(p));
        });
        std::sort(target.pairs.begin(), target.pairs.end(), [](const auto& a, const auto& b) {
            return a.name < b.name;
        });
    }

private:
    SpirvInstructionView view_;
};

void leaves(const G& graph,
    std::size_t nodeIndex,
    core::pmr::String name,
    std::uint64_t offset,
    WebGlUniformBlock& block,
    std::uint32_t max,
    unsigned depth = 0)
{
    need(depth <= 32 && nodeIndex < graph.nodes().size(), "SHADER-WEBGL1-TYPE-DEPTH");
    const auto& n = graph.nodes()[nodeIndex];
    if (n.kind == G::Kind::Struct) {
        for (std::size_t j = 0; j < n.memberCount; ++j) {
            const auto& m = graph.members()[n.memberBegin + j];
            auto next = name;
            next += indexed(".m", j);
            leaves(graph, m.type, std::move(next), offset + m.offsetBytes, block, max, depth + 1);
        }
    } else if (n.kind == G::Kind::FixedArray) {
        need(n.count <= max && n.strideBytes, "SHADER-WEBGL1-ARRAY-LAYOUT");
        for (std::uint32_t j = 0; j < n.count; ++j) {
            auto next = name;
            next += indexed("[", j);
            next += ']';
            leaves(
                graph, n.child, std::move(next), offset + j * n.strideBytes, block, max, depth + 1);
        }
    } else {
        WebGlUniformLeaf leaf;
        leaf.name = std::move(name);
        leaf.offset = offset;
        auto scalar = &n;
        leaf.components = 1;
        leaf.columns = 1;
        if (n.kind == G::Kind::Vector) {
            leaf.components = n.count;
            scalar = &graph.nodes()[n.child];
        } else if (n.kind == G::Kind::Matrix) {
            need(n.rows == 4 && n.columns == 4 && n.strideBytes == 16
                    && n.matrixEncoding == G::MatrixEncoding::SpirvColMajorDecoration,
                "SHADER-WEBGL1-MATRIX-LAYOUT");
            leaf.components = n.rows;
            leaf.columns = n.columns;
            leaf.matrixStride = n.strideBytes;
            scalar = &graph.nodes()[graph.nodes()[n.child].child];
        } else
            need(n.kind == G::Kind::Scalar, "SHADER-WEBGL1-UNIFORM-TYPE");
        need(scalar->kind == G::Kind::Scalar && scalar->scalarBits == 32
                && (scalar->scalar == G::Scalar::FloatingPoint
                    || scalar->scalar == G::Scalar::SignedInteger),
            "SHADER-WEBGL1-UNIFORM-TYPE");
        leaf.integer = scalar->scalar == G::Scalar::SignedInteger;
        need(!leaf.integer || leaf.columns == 1, "SHADER-WEBGL1-MATRIX-TYPE");
        leaf.size = leaf.components * leaf.columns * 4;
        need(offset <= block.size && leaf.size <= block.size - offset && block.leaves.size() < max,
            "SHADER-WEBGL1-LEAF-BUDGET-OR-RANGE");
        block.leaves.push_back(std::move(leaf));
    }
}

void validateBlocks(
    WebGlShaderTarget& target, const Y::Node& root, const WebGlShaderLoadBudget& budget)
{
    const auto& binary = target.original->bytes(ShaderArtifactRole::Spirv);
    G graph;
    core::pmr::Vector<SpirvBufferDeclarationLayout> decl{&core::defaultCpuMemoryResource()};
    SpirvPushConstantDeclarationLayout push;
    if (auto id = SpirvBufferDeclarationLayoutVerifier::verifyWithPushConstants(binary.data(),
            binary.size(),
            budget.original.maxBindings,
            {budget.maxTypeNodes, budget.maxTypeNodes, budget.original.maxBindings + 1u},
            decl,
            graph,
            push))
        throw ShaderArtifactLoadError{id};
    const auto& blocks = Y::sequence(root, "blocks");
    need(blocks.size() == graph.roots().size(), "SHADER-WEBGL1-BLOCK-COVERAGE");
    std::size_t total = 0;
    for (const auto& r : graph.roots()) {
        need(r.storage == G::Storage::Uniform || r.storage == G::Storage::PushConstant,
            "SHADER-WEBGL1-STORAGE-UNSUPPORTED");
        WebGlUniformBlock block;
        block.push = r.storage == G::Storage::PushConstant;
        block.set = r.set;
        block.binding = r.binding;
        need(block.push || r.descriptorCount == 1, "SHADER-WEBGL1-DESCRIPTOR-ARRAY");
        const auto size = graph.nodes()[r.type].occupiedExtentBytes;
        need(size && size <= UINT32_MAX, "SHADER-WEBGL1-BLOCK-SIZE");
        block.size = size;
        leaves(graph,
            r.type,
            rootName(block.push, block.set, block.binding),
            0,
            block,
            budget.maxLeaves);
        const Y::Node* record = nullptr;
        for (const auto& b : blocks) {
            schema(b, 5);
            const auto source = Y::text(b, "source");
            need(source == "Uniform" || source == "Push", "SHADER-WEBGL1-BLOCK-SOURCE");
            if ((source == "Push") == block.push && Y::uint32(b, "set") == block.set
                && Y::uint32(b, "binding") == block.binding) {
                need(!record, "SHADER-WEBGL1-BLOCK-DUPLICATE");
                record = &b;
            }
        }
        need(record && Y::uint32(*record, "size") == block.size,
            "SHADER-WEBGL1-BLOCK-BINARY-MISMATCH");
        const auto& list = Y::sequence(*record, "leaves");
        need(list.size() == block.leaves.size(), "SHADER-WEBGL1-LEAF-COVERAGE");
        for (std::size_t j = 0; j < list.size(); ++j) {
            const auto& l = list[j];
            auto& expected = block.leaves[j];
            schema(l, 11);
            need(Y::text(l, "name") == expected.name
                    && Y::text(l, "source") == (block.push ? "Push" : "Uniform")
                    && Y::uint32(l, "set") == block.set && Y::uint32(l, "binding") == block.binding
                    && Y::uint32(l, "offset") == expected.offset
                    && Y::uint32(l, "size") == expected.size
                    && Y::text(l, "scalar") == (expected.integer ? "Int32" : "Float32")
                    && Y::uint32(l, "components") == expected.components
                    && Y::uint32(l, "columns") == expected.columns
                    && Y::uint32(l, "matrixStride") == expected.matrixStride,
                "SHADER-WEBGL1-LEAF-BINARY-MISMATCH");
            expected.required = Y::boolean(l, "required");
            target.uniformVectors += expected.columns;
        }
        total += list.size();
        need(total <= budget.maxLeaves, "SHADER-WEBGL1-LEAF-BUDGET");
        target.blocks.push_back(std::move(block));
    }
}

void validateInterface(const Y::Node& root, const WebGlShaderTarget& target)
{
    const auto& iface = Y::required(root, "interface");
    schema(iface, 2);
    for (bool input : {true, false}) {
        const auto& list = Y::sequence(iface, input ? "inputs" : "outputs");
        const auto& expected = input ? target.inputs : target.outputs;
        need(list.size() == expected.size(), "SHADER-WEBGL1-INTERFACE-COVERAGE");
        for (std::size_t i = 0; i < list.size(); ++i) {
            schema(list[i], 3);
            need(Y::text(list[i], "name") == expected[i].name
                    && Y::uint32(list[i], "location") == expected[i].location
                    && Y::uint32(list[i], "components") == expected[i].components,
                "SHADER-WEBGL1-INTERFACE-BINARY-MISMATCH");
        }
    }
}

WebGlShaderTarget loadTarget(const ShaderArtifactFiles::File& publication,
    const std::filesystem::path& parent,
    bool vertex,
    const WebGlShaderLoadBudget& budget,
    ShaderArtifactInput& input)
{
    const auto root = parse(publication.bytes);
    schema(root, 21);
    need(Y::text(root, "type") == "WebGlShaderTargetArtifact"
            && Y::uint32(root, "contractVersion") == 1 && Y::text(root, "target") == "glsl-es-100"
            && Y::text(root, "status") == "Available"
            && Y::text(root, "stage") == (vertex ? "Vertex" : "Fragment")
            && Y::text(root, "matrixLayout") == "SlangRowMajorTransposedLogicalMatrix/1"
            && Y::text(root, "coordinates") == "WebGlShaderCoordinates/1"
            && Y::text(root, "precision") == "HighpFloatAndInt/1",
        "SHADER-WEBGL1-TARGET-CONTRACT");
    const auto original = ShaderArtifactFiles::loadAvailableFile(
        Y::required(root, "original"), parent, input, ".entry.yaml");
    auto b = budget.original;
    b.maxTotalBytes = input.remaining();
    core::pmr::String diagnostic{&core::defaultCpuMemoryResource()};
    WebGlShaderTarget target;
    target.original = SingleEntryShaderArtifact::load(
        parent / original.name.c_str(), b, core::currentCpuMemoryQuota(), diagnostic);
    need(bool(target.original), "SHADER-WEBGL1-ORIGINAL-LOAD");
    need(target.original->publicationBytes() == original.bytes, "SHADER-WEBGL1-ORIGINAL-CHANGED");
    input.debit(target.original->inputBytes());
    need(target.original->stage() == (vertex ? ShaderStage::Vertex : ShaderStage::Fragment)
            && target.original->entry() == Y::text(root, "entry"),
        "SHADER-WEBGL1-ORIGINAL-STAGE");
    target.glsl = ShaderArtifactFiles::loadAvailableFile(
        Y::required(root, "glsl"), parent, input, ".glsl-es-100")
                      .bytes;
    const auto text = shaderArtifactText(target.glsl);
    need(text.substr(0, 12) == "#version 100" && text.find('\0') == core::StringView::npos,
        "SHADER-WEBGL1-GLSL-VERSION");
    need(vertex
            || (text.find("precision highp float;") != core::StringView::npos
                && text.find("precision highp int;") != core::StringView::npos),
        "SHADER-WEBGL1-GLSL-PRECISION");
    const auto& options = Y::required(root, "options");
    schema(options, 7);
    need(Y::uint32(options, "version") == 100, "SHADER-WEBGL1-OPTIONS");
    for (auto key : {"es", "floatHighp", "intHighp", "plainUniforms", "fixupDepth", "flipVertexY"})
        need(Y::boolean(options, key), "SHADER-WEBGL1-OPTIONS");
    for (auto key : {"generatorSha256", "adapterSha256"})
        need(core::Sha256::isHexDigest(Y::text(root, key)), "SHADER-WEBGL1-TOOL-IDENTITY");
    const auto& converter = Y::required(root, "converter");
    schema(converter, 2);
    const auto& version = Y::sequence(converter, "apiVersion");
    need(version.size() == 3 && Y::uint64(version[0]) == 0 && Y::uint64(version[1]) == 68
            && Y::uint64(version[2]) == 0
            && core::Sha256::isHexDigest(Y::text(converter, "sha256")),
        "SHADER-WEBGL1-CONVERTER-IDENTITY");
    const auto& slang = Y::required(root, "slang");
    schema(slang, 3);
    need(Y::text(slang, "matrixLayout") == "row-major" && !Y::text(slang, "version").empty()
            && core::Sha256::isHexDigest(Y::text(slang, "sha256")),
        "SHADER-WEBGL1-SLANG-IDENTITY");
    const auto& binary = target.original->bytes(ShaderArtifactRole::Spirv);
    BinaryAbi abi(binary);
    abi.validateSubset();
    abi.interfaces(target, vertex, budget.maxInterfaces);
    abi.pairs(target, budget.original.maxBindings);
    validateInterface(root, target);
    validateBlocks(target, root, budget);
    const auto& pairs = Y::sequence(root, "samplerPairs");
    need(pairs.size() == target.pairs.size(), "SHADER-WEBGL1-PAIR-COVERAGE");
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        const auto& p = pairs[i];
        const auto& e = target.pairs[i];
        schema(p, 5);
        need(Y::text(p, "name") == e.name && Y::uint32(p, "textureSet") == e.textureSet
                && Y::uint32(p, "textureBinding") == e.textureBinding
                && Y::uint32(p, "samplerSet") == e.samplerSet
                && Y::uint32(p, "samplerBinding") == e.samplerBinding,
            "SHADER-WEBGL1-PAIR-BINARY-MISMATCH");
    }
    target.uniformVectors += target.pairs.size();
    need(Y::uint32(root, "uniformVectors") == target.uniformVectors,
        "SHADER-WEBGL1-UNIFORM-VECTOR-COUNT");
    for (const auto& e : Y::sequence(root, "requiredExtensions")) {
        auto name = Y::text(e);
        need(name == "GL_OES_standard_derivatives" || name == "GL_EXT_shader_texture_lod",
            "SHADER-WEBGL1-EXTENSION-UNSUPPORTED");
        need(std::find(target.extensions.begin(), target.extensions.end(), name)
                == target.extensions.end(),
            "SHADER-WEBGL1-EXTENSION-DUPLICATE");
        target.extensions.push_back(std::move(name));
    }
    bool derivatives = false, lod = false;
    abi.each([&](const I& instruction) {
        const auto op = instruction.opcode();
        derivatives = derivatives || (op >= 207 && op <= 209);
        lod = lod || (!vertex && op == 88);
    });
    const auto declares = [&](core::StringView name) {
        return std::any_of(
            target.extensions.begin(), target.extensions.end(), [&](const auto& value) {
                return value == name;
            });
    };
    need(declares("GL_OES_standard_derivatives") == derivatives
            && declares("GL_EXT_shader_texture_lod") == lod,
        "SHADER-WEBGL1-EXTENSION-BINARY-MISMATCH");
    return target;
}
} // namespace

std::shared_ptr<const WebGlShaderProgramArtifact> WebGlShaderProgramArtifact::load(
    const std::filesystem::path& path,
    const WebGlShaderLoadBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    try {
        need(budget.maxLeaves && budget.maxInterfaces && budget.maxTypeNodes
                && budget.original.maxBindings && budget.original.maxFileBytes
                && budget.original.maxTotalBytes && budget.original.maxCpuMetadataBytes,
            "SHADER-WEBGL1-LOAD-BUDGET");
        need(quota && quota->limitBytes() == budget.original.maxCpuMetadataBytes,
            "SHADER-WEBGL1-CPU-QUOTA");
        core::CpuMemoryQuotaScope scope(quota);
        auto program = core::makeUnique<WebGlShaderProgramArtifact>(ConstructionKey{});
        ShaderArtifactInput input(budget.original.maxFileBytes, budget.original.maxTotalBytes);
        auto bytes = input.read(path);
        const auto root = parse(bytes);
        schema(root, 6);
        need(Y::text(root, "type") == "WebGlShaderProgramArtifact"
                && Y::uint32(root, "contractVersion") == 1
                && Y::text(root, "profile") == "WebGl1Graphics/1"
                && !Y::boolean(root, "nativeExecutionProven"),
            "SHADER-WEBGL1-PROGRAM-CONTRACT");
        program->identity_ = core::Sha256::digest(shaderArtifactText(bytes));
        const auto& targets = Y::sequence(root, "targets");
        need(targets.size() == 2, "SHADER-WEBGL1-PROGRAM-STAGES");
        for (std::size_t i = 0; i < 2; ++i) {
            schema(targets[i], 2);
            const auto name = Y::text(targets[i], "path");
            need(core::isRelativeArtifactPath(name, 2), "SHADER-WEBGL1-TARGET-PATH");
            const auto relative = std::filesystem::path(name.c_str());
            auto ref = targets[i];
            ref["path"] = relative.filename().string();
            const auto parent = path.parent_path() / relative.parent_path();
            const auto file =
                ShaderArtifactFiles::loadAvailableFile(ref, parent, input, ".glsl-es-100.yaml");
            program->stages_[i] = loadTarget(file, parent, i == 0, budget, input);
        }
        const auto& a = program->vertex().outputs;
        const auto& b = program->fragment().inputs;
        need(a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), sameInterface),
            "SHADER-WEBGL1-VARYING-INTERFACE");
        for (const auto& left : program->vertex().blocks)
            for (const auto& right : program->fragment().blocks) {
                if (left.push != right.push || left.set != right.set
                    || left.binding != right.binding)
                    continue;
                need(left.size == right.size && left.leaves.size() == right.leaves.size(),
                    "SHADER-WEBGL1-CROSS-STAGE-BLOCK-LAYOUT");
                for (std::size_t i = 0; i < left.leaves.size(); ++i) {
                    const auto& l = left.leaves[i];
                    const auto& r = right.leaves[i];
                    need(l.name == r.name && l.offset == r.offset && l.size == r.size
                            && l.components == r.components && l.columns == r.columns
                            && l.matrixStride == r.matrixStride && l.integer == r.integer,
                        "SHADER-WEBGL1-CROSS-STAGE-BLOCK-LAYOUT");
                }
            }
        for (const auto& s : program->stages_)
            for (const auto& p : s.pairs)
                if (std::find(program->textureUnits_.begin(), program->textureUnits_.end(), p.name)
                    == program->textureUnits_.end())
                    program->textureUnits_.push_back(p.name);
        std::sort(program->textureUnits_.begin(), program->textureUnits_.end());
        const auto& units = Y::sequence(root, "textureUnits");
        need(units.size() == program->textureUnits_.size(), "SHADER-WEBGL1-TEXTURE-UNIT-COVERAGE");
        for (std::size_t i = 0; i < units.size(); ++i) {
            schema(units[i], 2);
            need(Y::text(units[i], "name") == program->textureUnits_[i]
                    && Y::uint32(units[i], "unit") == i,
                "SHADER-WEBGL1-TEXTURE-UNIT-MAPPING");
        }
        return shareOriginal(std::move(program));
    } catch (const ShaderArtifactLoadError& e) {
        diagnostic = e.diagnostic;
    } catch (const core::YamlContractError& e) {
        diagnostic = e.diagnostic;
    } catch (const std::bad_alloc&) {
        diagnostic = "SHADER-WEBGL1-CPU-BUDGET";
    } catch (const std::exception&) {
        diagnostic = "SHADER-WEBGL1-YAML-INVALID";
    }
    return {};
}

const char* WebGlShaderProgramArtifact::validateOriginal(
    const std::shared_ptr<const WebGlShaderProgramArtifact>& p) noexcept
{
    return p && p->hasQuotaBoundSharedOwner(p) ? nullptr : "SHADER-WEBGL1-ORIGINAL-OWNER";
}

const char* WebGlShaderProgramArtifact::assess(const WebGlShaderLimits& f) const noexcept
{
    if (!f.vertexHighpFloat || !f.fragmentHighpFloat || !f.vertexHighpInt || !f.fragmentHighpInt)
        return "SHADER-WEBGL1-HIGHP-UNAVAILABLE";
    for (const auto& a : vertex().inputs)
        if (a.location >= f.attributes)
            return "SHADER-WEBGL1-ATTRIBUTE-LIMIT";
    if (vertex().outputs.size() > f.varyings)
        return "SHADER-WEBGL1-VARYING-LIMIT";
    if (vertex().uniformVectors > f.vertexUniformVectors
        || fragment().uniformVectors > f.fragmentUniformVectors)
        return "SHADER-WEBGL1-UNIFORM-LIMIT";
    if (vertex().pairs.size() > f.vertexTextures || fragment().pairs.size() > f.fragmentTextures
        || textureUnits_.size() > f.combinedTextures)
        return "SHADER-WEBGL1-TEXTURE-LIMIT";
    for (const auto& stage : stages_)
        for (const auto& ext : stage.extensions)
            if ((ext == "GL_OES_standard_derivatives" && !f.derivatives)
                || (ext == "GL_EXT_shader_texture_lod" && !f.textureLod))
                return "SHADER-WEBGL1-REQUIRED-EXTENSION";
    return nullptr;
}
} // namespace arp::shader
