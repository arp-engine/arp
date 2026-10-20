#include "core/ascii_token_cursor.h"
#include "core/file_io.h"
#include "rhi/backends/metal/metal_buffer_type_layout.h"
#include "shader/spirv_buffer_declaration_layout.h"
#include "shader/spirv_msl_buffer_memory_lowering.h"

#include <cassert>
#include <cstdio>
#include <string_view>

#ifdef NDEBUG
#error Native declaration tests require assertions
#endif

namespace {
using Graph = arp::shader::BufferDeclarationTypeLayouts;

std::uint32_t number(const char* value)
{
    arp::core::AsciiTokenCursor cursor(value);
    std::uint32_t result = 0u;
    assert(cursor.decimalUint32(result) && cursor.atEnd());
    return result;
}

MTLVertexFormat vertexFormat(MTLDataType type)
{
    switch (type) {
    case MTLDataTypeFloat:
        return MTLVertexFormatFloat;
    case MTLDataTypeFloat2:
        return MTLVertexFormatFloat2;
    case MTLDataTypeFloat3:
        return MTLVertexFormatFloat3;
    case MTLDataTypeFloat4:
        return MTLVertexFormatFloat4;
    case MTLDataTypeUInt:
        return MTLVertexFormatUInt;
    case MTLDataTypeUInt2:
        return MTLVertexFormatUInt2;
    case MTLDataTypeUInt3:
        return MTLVertexFormatUInt3;
    case MTLDataTypeUInt4:
        return MTLVertexFormatUInt4;
    default:
        assert(false);
        return MTLVertexFormatInvalid;
    }
}

void dump(const Graph& graph, std::size_t root)
{
    std::printf("MSL_MEMBER_GRAPH root=%zu nodes=%zu members=%zu\n",
        root,
        graph.nodes().size(),
        graph.members().size());
    for (std::size_t i = 0u; i < graph.nodes().size(); ++i) {
        const auto& node = graph.nodes()[i];
        std::printf(
            "node=%zu kind=%u scalar=%u bits=%u rows=%u columns=%u child=%zu count=%llu stride=%llu extent=%llu\n",
            i,
            static_cast<unsigned>(node.kind),
            static_cast<unsigned>(node.scalar),
            node.scalarBits,
            node.rows,
            node.columns,
            node.child,
            static_cast<unsigned long long>(node.count),
            static_cast<unsigned long long>(node.strideBytes),
            static_cast<unsigned long long>(node.occupiedExtentBytes));
        for (std::size_t j = 0u; j < node.memberCount; ++j) {
            const auto& member = graph.members()[node.memberBegin + j];
            std::printf("member=%zu type=%zu offset=%llu\n",
                j,
                member.type,
                static_cast<unsigned long long>(member.offsetBytes));
        }
    }
}
} // namespace

int main(int argc, char** argv)
{
    using namespace arp;
    using Lowering = shader::SpirvMslBufferMemoryLowering;
    assert(argc >= 7);
    @autoreleasepool {
        const auto msl = core::readBinaryFile(argv[1], 16u * 1024u * 1024u);
        const auto binary = core::readBinaryFile(argv[2], 16u * 1024u * 1024u);
        assert(msl.success && binary.success);
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        assert(device);
        NSError* error = nil;
        NSString* text = [[NSString alloc] initWithBytes:msl.bytes.data()
                                                  length:msl.bytes.size()
                                                encoding:NSUTF8StringEncoding];
        MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
        options.fastMathEnabled = NO;
        id<MTLLibrary> library = [device newLibraryWithSource:text options:options error:&error];
        if (!library)
            std::fprintf(stderr, "MSL library: %s\n", error.localizedDescription.UTF8String);
        assert(library);
        id<MTLFunction> function =
            [library newFunctionWithName:[NSString stringWithUTF8String:argv[3]]];
        assert(function && function.functionType == MTLFunctionTypeVertex);
        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = function;
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        desc.vertexDescriptor = [MTLVertexDescriptor vertexDescriptor];
        // Reflection-only formal vertex input ABI. No vertex data is executed.
        for (MTLVertexAttribute* attribute in function.stageInputAttributes) {
            auto target = desc.vertexDescriptor.attributes[attribute.attributeIndex];
            target.format = vertexFormat(attribute.attributeType);
            target.offset = 0u;
            target.bufferIndex = 30u;
        }
        desc.vertexDescriptor.layouts[30u].stride = 16u;
        desc.vertexDescriptor.layouts[30u].stepFunction = MTLVertexStepFunctionPerVertex;
        desc.vertexDescriptor.layouts[30u].stepRate = 1u;
        MTLRenderPipelineReflection* reflection = nil;
        id<MTLRenderPipelineState> pipeline =
            [device newRenderPipelineStateWithDescriptor:desc
                                                 options:MTLPipelineOptionArgumentInfo
                | MTLPipelineOptionBufferTypeInfo
                                              reflection:&reflection
                                                   error:&error];
        if (!pipeline)
            std::fprintf(stderr, "MSL pipeline: %s\n", error.localizedDescription.UTF8String);
        assert(pipeline && reflection);
        MTLArgument* selected = nil;
        for (MTLArgument* argument in reflection.vertexArguments)
            if (argument.type == MTLArgumentTypeBuffer && argument.index == number(argv[4])) {
                assert(!selected);
                selected = argument;
            }
        assert(selected && selected.bufferStructType && selected.bufferAlignment
            && selected.bufferDataSize);
        Graph target;
        std::size_t targetType;
        const auto* id = rhi_metal::MetalRhiBufferTypeLayoutCollector::collect(
            selected.bufferStructType, {4096u, 4096u, 1u}, target, targetType);
        if (id)
            std::fprintf(stderr, "MSL graph: %s\n", id);
        assert(!id);
        dump(target, targetType);
        Graph exact;
        std::size_t exactType;
        assert(!rhi_metal::MetalRhiBufferTypeLayoutCollector::collect(selected.bufferStructType,
            {target.nodes().size(), target.members().size(), 1u},
            exact,
            exactType));
        bool equal = false;
        assert(!Graph::compare(target, targetType, exact, exactType, target.nodes().size(), equal)
            && equal);
        // Actual driver objects also exercise bounded failure and no partial
        // graph publication; a synthetic target cannot supply this evidence.
        for (const auto budget : {Graph::Budget{0u, 4096u, 1u},
                 Graph::Budget{4096u, 0u, 1u},
                 Graph::Budget{target.nodes().size() - 1u, target.members().size(), 1u},
                 Graph::Budget{target.nodes().size(), target.members().size() - 1u, 1u}}) {
            Graph rejected;
            std::size_t absent = 0u;
            assert(rhi_metal::MetalRhiBufferTypeLayoutCollector::collect(
                selected.bufferStructType, budget, rejected, absent));
            assert(
                absent == Graph::absent && rejected.nodes().empty() && rejected.members().empty());
        }
        auto quota = core::makeShared<core::CpuMemoryQuota>(1u);
        {
            core::CpuMemoryQuotaScope scope(quota);
            core::CpuMemoryQuotaLease exhausted(quota, 1u);
            Graph rejected;
            std::size_t absent = 0u;
            id = rhi_metal::MetalRhiBufferTypeLayoutCollector::collect(
                selected.bufferStructType, {4096u, 4096u, 1u}, rejected, absent);
            assert(id && std::string_view(id) == "RHI-METAL-BUFFER-TYPE-ALLOCATION"
                && absent == Graph::absent && rejected.nodes().empty());
        }
        assert(!quota->liveBytes());
        Graph source;
        core::pmr::Vector<shader::SpirvBufferDeclarationLayout> sourceFacts;
        assert(!shader::SpirvBufferDeclarationLayoutVerifier::verifyWithTypes(binary.bytes.data(),
            binary.bytes.size(),
            64u,
            {4096u, 4096u, 64u},
            sourceFacts,
            source));
        const auto sourceRoot = number(argv[5]);
        assert(sourceRoot < source.roots().size());
        if (std::string_view(argv[6]) != "inspect") {
            core::pmr::Vector<Lowering::ArrayWrapper> wrappers;
            core::pmr::Vector<Lowering::MatrixColumns> matrices;
            for (int i = 7; i < argc;) {
                const std::string_view tag(argv[i++]);
                if (tag == "matrix") {
                    assert(i + 1 < argc);
                    const auto a = number(argv[i++]), b = number(argv[i++]);
                    matrices.push_back({a, b});
                } else {
                    assert((tag == "source-array" || tag == "target-array") && i < argc);
                    wrappers.push_back(
                        {tag == "source-array" ? Lowering::Side::Source : Lowering::Side::Target,
                            number(argv[i++])});
                }
            }
            Lowering::Facts facts;
            const Lowering::Plan plan{1u,
                4096u,
                64u,
                64u,
                wrappers.data(),
                wrappers.size(),
                matrices.data(),
                matrices.size()};
            id = Lowering::compare(
                source, source.roots()[sourceRoot].type, target, targetType, plan, facts);
            if (std::string_view(argv[6]) == "valid") {
                if (id)
                    std::fprintf(stderr, "MSL relation: %s\n", id);
                assert(!id && facts.comparedPairs);
                assert(facts.occupiedBytes <= selected.bufferDataSize);
                std::printf(
                    "MSL_MEMBER_MEMORY1_OK occupied=%llu driverMinimum=%llu alignment=%llu pairs=%zu wrappers=%zu matrices=%zu declaration-only\n",
                    static_cast<unsigned long long>(facts.occupiedBytes),
                    static_cast<unsigned long long>(selected.bufferDataSize),
                    static_cast<unsigned long long>(selected.bufferAlignment),
                    facts.comparedPairs,
                    facts.arrayWrappers,
                    facts.matrixColumns);
            } else {
                assert(id && std::string_view(id) == argv[6] && !facts.occupiedBytes
                    && !facts.comparedPairs && !facts.arrayWrappers && !facts.matrixColumns);
                std::printf("MSL_MEMBER_MEMORY1_REFUSED %s output-cleared\n", id);
            }
        }
        [pipeline release];
        [desc release];
        [function release];
        [library release];
        [options release];
        [text release];
        [device release];
    }
}
