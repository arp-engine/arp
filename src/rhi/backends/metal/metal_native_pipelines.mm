#include "rhi/backends/metal/metal_native_pipelines.h"

#include "core/algorithm/strided_index_range.h"
#include "core/memory/cpu_allocator.h"
#include "rhi/backends/metal/metal_binding_validation.h"
#include "rhi/backends/metal/metal_buffer_type_layout.h"
#include "rhi/backends/metal/metal_compute_workgroup_limits.h"
#include "rhi/backends/metal/metal_native_format.h"
#include "rhi/backends/metal/metal_native_runtime.h"
#include "rhi/semantics/native_pipeline_state_lowering.h"
#include "shader/graphics_shader_module_artifact.h"

#include <Metal/Metal.h>
#include <sstream>

namespace arp::rhi_metal {

MTLVertexFormat metalVertexFormat(rhi::VertexFormat format)
{
    switch (format) {
    case rhi::VertexFormat::Float32x1:
        return MTLVertexFormatFloat;
    case rhi::VertexFormat::Float32x2:
        return MTLVertexFormatFloat2;
    case rhi::VertexFormat::Float32x3:
        return MTLVertexFormatFloat3;
    case rhi::VertexFormat::Float32x4:
        return MTLVertexFormatFloat4;
    }
    return MTLVertexFormatInvalid;
}

namespace {

// Shared exact member enumeration for texture and pointer facts. This is a cold
// native-reflection boundary, never a source-name or pointer-table size parser.
class GraphicsArgumentMemberEnumerator final {
public:
    template <class Visitor>
    static bool collect(MTLArgument* argument, MTLDataType kind, Visitor&& visitor)
    {
        if (argument.type != MTLArgumentTypeBuffer || !argument.bufferStructType)
            return true;
        for (MTLStructMember* member in argument.bufferStructType.members) {
            // metal::array is reflected as a single-member struct containing
            // the real array. Require the complete native shape and equal
            // argument indices; names/offset guesses cannot authorize it.
            if (member.dataType == MTLDataTypeStruct && member.structType.members.count == 1u) {
                auto child = member.structType.members[0];
                if (child.dataType == MTLDataTypeArray && child.arrayType.elementType == kind) {
                    if (child.argumentIndex != member.argumentIndex || child.offset != 0u)
                        return false;
                    member = child;
                }
            }
            if (member.dataType == kind) {
                if (argument.index > UINT32_MAX || member.argumentIndex > UINT32_MAX
                    || !visitor(member, nil, member.argumentIndex, 1u, 1u, 0u))
                    return false;
            } else if (member.dataType == MTLDataTypeArray
                && member.arrayType.elementType == kind) {
                auto array = member.arrayType;
                core::StridedIndexRange indices(
                    member.argumentIndex, array.arrayLength, array.argumentIndexStride, UINT32_MAX);
                if (argument.index > UINT32_MAX || array.arrayLength > UINT32_MAX
                    || array.argumentIndexStride > UINT32_MAX || !indices.valid())
                    return false;
                for (std::uint64_t element = 0u; element < indices.size(); ++element) {
                    std::uint64_t index = 0u;
                    if (!indices.index(element, index)
                        || !visitor(member,
                            array,
                            index,
                            array.arrayLength,
                            array.argumentIndexStride,
                            element))
                        return false;
                }
            }
        }
        return true;
    }
};

// Storage is reserved from logical cardinality before native objects exist;
// neither collector can grow allocations during native reflection.
class TextureBindingFactCollector final {
public:
    explicit TextureBindingFactCollector(core::pmr::Vector<MetalRhiTextureBindingFact>& facts)
        : facts_(facts)
    {}

    static bool reserve(const rhi::ShaderReflectionDesc& reflection,
        std::size_t stages,
        core::pmr::Vector<MetalRhiTextureBindingFact>& facts)
    {
        if (!stages)
            return false;
        std::size_t capacity = 0u;
        for (const auto& binding : reflection.descriptorBindings) {
            if (binding.type != rhi::DescriptorType::SampledTexture
                && binding.type != rhi::DescriptorType::StorageTexture)
                continue;
            if (!binding.arrayCount || binding.arrayCount > (facts.max_size() - capacity) / stages)
                return false;
            capacity += binding.arrayCount * stages;
        }
        facts.reserve(capacity);
        return true;
    }

    bool collect(NSArray<MTLArgument*>* arguments, rhi::ShaderStage stage)
    {
        for (MTLArgument* argument in arguments) {
            if (argument.type == MTLArgumentTypeTexture) {
                if (argument.arrayLength != 1u
                    || !append(stage,
                        argument.index,
                        UINT32_MAX,
                        argument.textureType,
                        argument.textureDataType,
                        argument.isDepthTexture,
                        argument.access,
                        argument.isActive != NO))
                    return false;
            } else if (!GraphicsArgumentMemberEnumerator::collect(argument,
                           MTLDataTypeTexture,
                           [&](MTLStructMember* member,
                               MTLArrayType* array,
                               std::uint64_t index,
                               std::uint64_t,
                               std::uint64_t,
                               std::uint64_t) {
                               auto type = array ? array.elementTextureReferenceType
                                                 : member.textureReferenceType;
                               return type
                                   && append(stage,
                                       argument.index,
                                       index,
                                       type.textureType,
                                       type.textureDataType,
                                       type.isDepthTexture,
                                       type.access,
                                       argument.isActive != NO);
                           }))
                return false;
        }
        return true;
    }

private:
    bool append(rhi::ShaderStage stage,
        NSUInteger slot,
        NSUInteger index,
        MTLTextureType type,
        MTLDataType scalar,
        bool depth,
        MTLArgumentAccess access,
        bool active)
    {
        if (slot > UINT32_MAX || index > UINT32_MAX || facts_.size() == facts_.capacity())
            return false;
        for (const auto& fact : facts_)
            if (fact.stage == stage && fact.slot == slot && fact.argumentIndex == index)
                return false;
        facts_.push_back({stage,
            static_cast<std::uint32_t>(slot),
            static_cast<std::uint32_t>(index),
            static_cast<std::uint32_t>(type),
            static_cast<std::uint32_t>(scalar),
            depth,
            static_cast<std::uint32_t>(access),
            active ? 1u : 0u});
        return true;
    }

    core::pmr::Vector<MetalRhiTextureBindingFact>& facts_;
};

class GraphicsBufferPointeeFactCollector final {
public:
    explicit GraphicsBufferPointeeFactCollector(MetalRhiStageBufferPointeeFacts& facts)
        : facts_(facts)
    {}

    void collect(NSArray<MTLArgument*>* arguments, rhi::ShaderStage stage)
    {
        facts_.stage = stage;
        if (@available(macOS 10.13, iOS 11.0, *)) {
            for (MTLArgument* argument in arguments) {
                if (!GraphicsArgumentMemberEnumerator::collect(argument,
                        MTLDataTypePointer,
                        [&](MTLStructMember* member,
                            MTLArrayType* array,
                            std::uint64_t index,
                            std::uint64_t count,
                            std::uint64_t stride,
                            std::uint64_t element) {
                            auto pointer = array ? array.elementPointerType : member.pointerType;
                            return append(argument.index, index, count, stride, element, pointer);
                        })) {
                    if (!diagnostic_)
                        diagnostic_ = "RHI-METAL-ORDINARY-POINTEE-INDEX-INVALID";
                    facts_.entries.clear();
                    facts_.queryDiagnostic = diagnostic_;
                    return;
                }
            }
            facts_.version = 1u;
            facts_.queryDiagnostic = nullptr;
        } else
            facts_.queryDiagnostic = "RHI-METAL-ORDINARY-POINTEE-QUERY-UNAVAILABLE";
    }

private:
    static bool scalarOrVector(MTLDataType type)
    {
        switch (type) {
        case MTLDataTypeFloat:
        case MTLDataTypeFloat2:
        case MTLDataTypeFloat3:
        case MTLDataTypeFloat4:
        case MTLDataTypeHalf:
        case MTLDataTypeHalf2:
        case MTLDataTypeHalf3:
        case MTLDataTypeHalf4:
        case MTLDataTypeInt:
        case MTLDataTypeInt2:
        case MTLDataTypeInt3:
        case MTLDataTypeInt4:
        case MTLDataTypeUInt:
        case MTLDataTypeUInt2:
        case MTLDataTypeUInt3:
        case MTLDataTypeUInt4:
            return true;
        default:
            return false;
        }
    }

    bool append(std::uint64_t slot,
        std::uint64_t index,
        std::uint64_t count,
        std::uint64_t stride,
        std::uint64_t element,
        MTLPointerType* pointer)
    {
        if (!pointer || !pointer.alignment || !pointer.dataSize) {
            diagnostic_ = "RHI-METAL-ORDINARY-POINTEE-FACTS-MISSING";
            return false;
        }
        bool supported = scalarOrVector(pointer.elementType);
        if (pointer.elementType == MTLDataTypeStruct && pointer.elementStructType
            && pointer.elementStructType.members.count) {
            supported = true;
            for (MTLStructMember* member in pointer.elementStructType.members)
                if (!scalarOrVector(member.dataType))
                    supported = false;
        }
        if (pointer.access != MTLArgumentAccessReadOnly || pointer.elementIsArgumentBuffer
            || !supported) {
            diagnostic_ = "RHI-METAL-ORDINARY-POINTEE-DOMAIN-UNSUPPORTED";
            return false;
        }
        if (facts_.entries.size() == facts_.entries.capacity()) {
            diagnostic_ = "RHI-METAL-ORDINARY-POINTEE-FACT-CAPACITY";
            return false;
        }
        for (const auto& fact : facts_.entries)
            if (fact.slot == slot && fact.argumentIndex == index) {
                diagnostic_ = "RHI-METAL-ORDINARY-POINTEE-FACTS-DUPLICATE";
                return false;
            }
        facts_.entries.push_back({static_cast<std::uint32_t>(slot),
            static_cast<std::uint32_t>(index),
            static_cast<std::uint32_t>(count),
            static_cast<std::uint32_t>(stride),
            static_cast<std::uint32_t>(element),
            static_cast<std::uint32_t>(pointer.access),
            static_cast<std::uint32_t>(pointer.elementType),
            pointer.alignment,
            pointer.dataSize,
            true,
            false});
        return true;
    }

    MetalRhiStageBufferPointeeFacts& facts_;
    const char* diagnostic_ = nullptr;
};

bool collectPhysicalBindingStages(NSArray<MTLArgument*>* arguments,
    rhi::ShaderStage stage,
    rhi::semantics::MetalPhysicalBindingStages& footprint,
    std::pmr::string& diagnostics,
    core::pmr::Vector<MetalRhiBufferBindingFact>* bufferFacts = nullptr,
    bool bufferFactsPreallocated = false)
{
    if (bufferFacts && !bufferFactsPreallocated)
        bufferFacts->reserve(arguments.count);
    for (MTLArgument* argument in arguments) {
        // Reflect the complete formal signature, including parameters marked
        // inactive. Semantic visibility is still validated separately at L2.
        rhi::DescriptorType type;
        switch (argument.type) {
        case MTLArgumentTypeBuffer:
            type = rhi::DescriptorType::UniformBuffer;
            break;
        case MTLArgumentTypeTexture:
            type = rhi::DescriptorType::SampledTexture;
            break;
        case MTLArgumentTypeSampler:
            type = rhi::DescriptorType::Sampler;
            break;
        case MTLArgumentTypeThreadgroupMemory:
            continue;
        default:
            diagnostics = "RHI-METAL-REFLECTED-ARGUMENT-KIND-UNSUPPORTED";
            return false;
        }
        if (argument.index > UINT32_MAX
            || !footprint.record(type, static_cast<std::uint32_t>(argument.index), stage)) {
            diagnostics = "RHI-METAL-REFLECTED-ARGUMENT-SLOT-INVALID";
            return false;
        }
        if (bufferFacts && argument.type == MTLArgumentTypeBuffer) {
            if (bufferFacts->size() == bufferFacts->capacity()) {
                diagnostics = "RHI-METAL-REFLECTED-BUFFER-FACT-CAPACITY";
                return false;
            }
            for (const auto& fact : *bufferFacts)
                if (fact.slot == argument.index) {
                    diagnostics = "RHI-METAL-REFLECTED-BUFFER-FACT-DUPLICATE";
                    return false;
                }
            bufferFacts->push_back({static_cast<std::uint32_t>(argument.index),
                static_cast<std::uint64_t>(argument.bufferAlignment),
                static_cast<std::uint64_t>(argument.bufferDataSize),
                static_cast<std::uint32_t>(argument.access),
                static_cast<std::uint32_t>(argument.bufferDataType),
                argument.isActive != NO ? 1u : 0u});
        }
    }
    return true;
}

id<MTLDevice> defaultMetalDevice()
{
    return reinterpret_cast<id<MTLDevice>>(metalRhiNativeDeviceHandle());
}

std::shared_ptr<void> retainNativeObject(id object)
{
    if (object == nil) {
        return {};
    }
    return std::shared_ptr<void>(
        reinterpret_cast<void*>(object),
        [](void* nativeObject) {
            if (nativeObject) {
                id object = reinterpret_cast<id>(nativeObject);
                [object release];
            }
        },
        std::pmr::polymorphic_allocator<std::byte>(&core::defaultCpuMemoryResource()));
}

std::shared_ptr<void> retainBorrowedNativeObject(id object)
{
    if (object == nil) {
        return {};
    }
    [object retain];
    return retainNativeObject(object);
}

MTLBlendFactor metalBlendFactor(rhi::BlendFactor factor)
{
    switch (factor) {
    case rhi::BlendFactor::Zero:
        return MTLBlendFactorZero;
    case rhi::BlendFactor::One:
        return MTLBlendFactorOne;
    case rhi::BlendFactor::SrcAlpha:
        return MTLBlendFactorSourceAlpha;
    case rhi::BlendFactor::OneMinusSrcAlpha:
        return MTLBlendFactorOneMinusSourceAlpha;
    case rhi::BlendFactor::DstAlpha:
        return MTLBlendFactorDestinationAlpha;
    case rhi::BlendFactor::OneMinusDstAlpha:
        return MTLBlendFactorOneMinusDestinationAlpha;
    }
    return MTLBlendFactorOne;
}

MTLBlendOperation metalBlendOperation(rhi::BlendOp operation)
{
    switch (operation) {
    case rhi::BlendOp::Add:
        return MTLBlendOperationAdd;
    case rhi::BlendOp::Subtract:
        return MTLBlendOperationSubtract;
    case rhi::BlendOp::ReverseSubtract:
        return MTLBlendOperationReverseSubtract;
    case rhi::BlendOp::Min:
        return MTLBlendOperationMin;
    case rhi::BlendOp::Max:
        return MTLBlendOperationMax;
    }
    return MTLBlendOperationAdd;
}

MTLColorWriteMask metalColorWriteMask(rhi::ColorWriteMask mask)
{
    MTLColorWriteMask result = MTLColorWriteMaskNone;
    if ((mask & rhi::ColorWriteMask::Red) != rhi::ColorWriteMask::None)
        result |= MTLColorWriteMaskRed;
    if ((mask & rhi::ColorWriteMask::Green) != rhi::ColorWriteMask::None)
        result |= MTLColorWriteMaskGreen;
    if ((mask & rhi::ColorWriteMask::Blue) != rhi::ColorWriteMask::None)
        result |= MTLColorWriteMaskBlue;
    if ((mask & rhi::ColorWriteMask::Alpha) != rhi::ColorWriteMask::None)
        result |= MTLColorWriteMaskAlpha;
    return result;
}

bool tryMetalPrimitiveType(
    rhi::PrimitiveTopology topology, bool tessellationAbi, MTLPrimitiveType& result)
{
    switch (topology) {
    case rhi::PrimitiveTopology::TriangleList:
        result = MTLPrimitiveTypeTriangle;
        return true;
    case rhi::PrimitiveTopology::TriangleStrip:
        result = MTLPrimitiveTypeTriangleStrip;
        return true;
    case rhi::PrimitiveTopology::LineList:
        result = MTLPrimitiveTypeLine;
        return true;
    case rhi::PrimitiveTopology::PointList:
        result = MTLPrimitiveTypePoint;
        return true;
    case rhi::PrimitiveTopology::PatchList:
        if (!tessellationAbi)
            return false;
        // Metal encodes patches through drawPatches* rather than an
        // MTLPrimitiveType value. Keep a valid non-patch value here; command
        // recording selects the patch entry points from primitiveTopology.
        result = MTLPrimitiveTypeTriangle;
        return true;
    }
    return false;
}

MTLCompareFunction metalCompareFunction(rhi::CompareOp compare)
{
    switch (compare) {
    case rhi::CompareOp::Never:
        return MTLCompareFunctionNever;
    case rhi::CompareOp::Less:
        return MTLCompareFunctionLess;
    case rhi::CompareOp::LessEqual:
        return MTLCompareFunctionLessEqual;
    case rhi::CompareOp::Equal:
        return MTLCompareFunctionEqual;
    case rhi::CompareOp::Greater:
        return MTLCompareFunctionGreater;
    case rhi::CompareOp::GreaterEqual:
        return MTLCompareFunctionGreaterEqual;
    case rhi::CompareOp::NotEqual:
        return MTLCompareFunctionNotEqual;
    case rhi::CompareOp::Always:
        return MTLCompareFunctionAlways;
    }
    return MTLCompareFunctionNever;
}

MTLStencilOperation metalStencilOperation(rhi::StencilOp operation)
{
    switch (operation) {
    case rhi::StencilOp::Keep:
        return MTLStencilOperationKeep;
    case rhi::StencilOp::Zero:
        return MTLStencilOperationZero;
    case rhi::StencilOp::Replace:
        return MTLStencilOperationReplace;
    case rhi::StencilOp::IncrementClamp:
        return MTLStencilOperationIncrementClamp;
    case rhi::StencilOp::DecrementClamp:
        return MTLStencilOperationDecrementClamp;
    case rhi::StencilOp::Invert:
        return MTLStencilOperationInvert;
    }
    return MTLStencilOperationKeep;
}

id<MTLLibrary> createLibrary(std::string_view mslSource, std::pmr::string& diagnostics);

id<MTLLibrary> createLibrary(const rhi::ShaderPackageDesc& package, std::pmr::string& diagnostics)
{
    rhi::GeneratedShaderTargetInput input;
    const auto selected =
        rhi::selectGeneratedShaderPackageTarget(package, shader::TargetBackend::Msl, input);
    if (!selected.success) {
        diagnostics = selected.diagnostics;
        return nil;
    }
    return createLibrary(input.text, diagnostics);
}

id<MTLLibrary> createLibrary(std::string_view mslSource, std::pmr::string& diagnostics)
{
    id<MTLDevice> device = defaultMetalDevice();
    if (device == nil || mslSource.empty() || mslSource.size() > NSUIntegerMax) {
        diagnostics = "Metal RHI native pipeline requires verified MSL bytes";
        return nil;
    }
    NSString* source = [[NSString alloc] initWithBytes:mslSource.data()
                                                length:static_cast<NSUInteger>(mslSource.size())
                                              encoding:NSUTF8StringEncoding];
    if (source == nil) {
        diagnostics = "Metal RHI verified MSL bytes are not valid UTF-8";
        return nil;
    }
    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    [source release];
    if (library == nil) {
        diagnostics = "Metal RHI failed to compile verified MSL bytes";
    }
    return library;
}

std::shared_ptr<MetalRhiNativePipelinePayload> makeBasePayload(
    const rhi::PipelineValidationReport& report, bool renderPipeline)
{
    auto payload = arp::core::makeShared<MetalRhiNativePipelinePayload>();
    payload->nativeDevice = metalRhiNativeDeviceIdentity();
    payload->renderPipelinePayloadPresent = renderPipeline;
    payload->computePipelinePayloadPresent = !renderPipeline;
    payload->pipelineCacheKeyStable = report.pipelineCacheKeyStable;
    payload->pipelineCacheKey = report.pipelineCacheKey;
    return payload;
}

} // namespace

class MetalRhiVertexInputFactCollector final {
public:
    explicit MetalRhiVertexInputFactCollector(MetalRhiVertexInputFacts& output) : output_(output) {}

    bool collect(MTLVertexDescriptor* descriptor,
        id<MTLFunction> function,
        const rhi::RenderPipelineDesc& desc,
        const rhi::semantics::MetalVertexStreamMapping& mapping)
    {
        output_ = MetalRhiVertexInputFacts{};
        if (!function || (desc.vertexBuffers.empty() ? descriptor != nil : descriptor == nil))
            return false;
        MetalRhiVertexInputFacts facts;
        facts.streams.reserve(desc.vertexBuffers.size());
        std::size_t count = 0u;
        for (const auto& stream : desc.vertexBuffers)
            count += stream.attributes.size();
        if (count > MetalRhiVertexInputFacts::attributeCapacity)
            return false;
        facts.attributes.reserve(count);
        for (const auto& stream : desc.vertexBuffers) {
            if (stream.slot >= mapping.capacity || mapping.slots[stream.slot] >= mapping.capacity)
                return false;
            const auto physical = mapping.slots[stream.slot];
            const auto native = descriptor.layouts[physical];
            if (native.stepFunction > UINT32_MAX)
                return false;
            facts.streams.push_back({stream.slot,
                physical,
                static_cast<std::uint32_t>(native.stepFunction),
                native.stride,
                native.stepRate});
            for (const auto& attribute : stream.attributes) {
                if (attribute.shaderLocation >= MetalRhiVertexInputFacts::attributeCapacity)
                    return false;
                const auto nativeAttribute = descriptor.attributes[attribute.shaderLocation];
                if (nativeAttribute.bufferIndex > UINT32_MAX || nativeAttribute.format > UINT32_MAX)
                    return false;
                facts.attributes.push_back({attribute.shaderLocation,
                    static_cast<std::uint32_t>(nativeAttribute.bufferIndex),
                    static_cast<std::uint32_t>(nativeAttribute.format),
                    nativeAttribute.offset});
            }
        }
        if (@available(macOS 10.12, iOS 10.0, *)) {
            const auto attributes = function.stageInputAttributes;
            if (attributes.count != count)
                return false;
            facts.shaderInputs.reserve(count);
            for (MTLAttribute* attribute in attributes) {
                if (attribute.attributeIndex > UINT32_MAX || attribute.attributeType > UINT32_MAX)
                    return false;
                facts.shaderInputs.push_back({static_cast<std::uint32_t>(attribute.attributeIndex),
                    static_cast<std::uint32_t>(attribute.attributeType),
                    attribute.isActive != NO});
            }
        } else
            return false;
        facts.contractVersion = MetalRhiVertexInputFacts::version;
        output_ = std::move(facts);
        return true;
    }

private:
    MetalRhiVertexInputFacts& output_;
};

static const char* collectOriginalMemberMemory(NSArray<MTLArgument*>* arguments,
    const shader::NativeBufferMemoryDeclarations& memory,
    const shader::ShaderReflection& reflection,
    const core::pmr::Vector<shader::SpirvBufferDeclarationLayout>& declarations,
    rhi::ShaderStage stage,
    MetalRhiNativeStageBufferMemberMemory& output)
{
    output = {};
    try {
        MetalRhiNativeStageBufferMemberMemory candidate;
        for (const auto& binary : declarations) {
            const auto* source = memory.lookup(binary.set, binary.binding);
            if (!source)
                return "RHI-METAL-MEMBER-MEMORY-SOURCE";
            const shader::ShaderMetalBindingSpaceBinding* physical = nullptr;
            for (const auto& candidate : reflection.metalBindingSpace)
                if (candidate.set == binary.set && candidate.binding == binary.binding) {
                    if (physical)
                        return "RHI-METAL-MEMBER-MEMORY-MAPPING-DUPLICATE";
                    physical = &candidate;
                }
            if (!physical || !physical->active || physical->metalSlot == UINT32_MAX)
                return "RHI-METAL-MEMBER-MEMORY-MAPPING";
            MTLArgument* selected = nil;
            for (MTLArgument* argument in arguments)
                if (argument.type == MTLArgumentTypeBuffer
                    && argument.index == physical->metalSlot) {
                    if (selected != nil)
                        return "RHI-METAL-MEMBER-MEMORY-ARGUMENT-DUPLICATE";
                    selected = argument;
                }
            if (selected == nil || !selected.bufferAlignment)
                return "RHI-METAL-MEMBER-MEMORY-ARGUMENT";
            MetalRhiNativeBufferMemberMemory record;
            record.set = binary.set;
            record.binding = binary.binding;
            record.slot = physical->metalSlot;
            if (source->rootForm == shader::SpirvMslBufferMemoryLowering::RootForm::Block) {
                if (selected.bufferDataType != MTLDataTypeStruct || !selected.bufferStructType)
                    return "RHI-METAL-MEMBER-MEMORY-ARGUMENT";
                if (const auto* id =
                        MetalRhiBufferTypeLayoutCollector::collect(selected.bufferStructType,
                            source->targetTypes,
                            record.target,
                            record.targetType))
                    return id;
            } else {
                if (const auto* id =
                        MetalRhiBufferTypeLayoutCollector::collect(selected.bufferPointerType,
                            source->targetTypes,
                            record.target,
                            record.targetType,
                            record.element))
                    return id;
                if (record.element.alignmentBytes != selected.bufferAlignment
                    || record.element.strideBytes != selected.bufferDataSize)
                    return "RHI-METAL-MEMBER-MEMORY-POINTER-RANGE";
            }
            core::pmr::Vector<shader::SpirvMslBufferMemoryLowering::MatrixColumns> matrices;
            shader::SpirvMslBufferMemoryLowering::Plan relation;
            if (const auto* id =
                    source->resolveTargetPlan(record.target, record.targetType, matrices, relation))
                return id;
            if (const auto* id = shader::SpirvMslBufferMemoryLowering::compare(memory.sourceTypes(),
                    source->sourceType,
                    record.target,
                    record.targetType,
                    relation,
                    record.relation,
                    record.element))
                return id;
            if (record.relation.occupiedBytes != binary.staticOccupiedExtentBytes
                || record.relation.hasRuntimeArray != binary.hasRuntimeArray
                || selected.bufferDataSize < record.relation.occupiedBytes)
                return "RHI-METAL-MEMBER-MEMORY-EXTENT";
            if (binary.hasRuntimeArray) {
                if (source->relationVersion != 2u || binary.runtimeArrayOffsetBytes
                    || record.relation.sourceRuntimeStrideBytes != binary.runtimeArrayStrideBytes
                    || record.relation.targetRuntimeStrideBytes != record.element.strideBytes
                    || !record.relation.targetRuntimeStrideBytes)
                    return "RHI-METAL-MEMBER-MEMORY-RUNTIME";
            } else if (!record.relation.occupiedBytes)
                return "RHI-METAL-MEMBER-MEMORY-EXTENT";
            candidate.entries.push_back(std::move(record));
        }
        candidate.stage = stage;
        candidate.version = 1u;
        output = std::move(candidate);
        return nullptr;
    } catch (const std::bad_alloc&) {
        output = {};
        return "RHI-METAL-MEMBER-MEMORY-ALLOCATION";
    }
}

static const char* collectOriginalGraphicsMemberMemory(NSArray<MTLArgument*>* arguments,
    const rhi::BindlessPackageModule* module,
    rhi::ShaderStage stage,
    MetalRhiNativeStageBufferMemberMemory& output)
{
    output = {};
    if (!module || !module->nativeMemory.version())
        return nullptr;
    return collectOriginalMemberMemory(arguments,
        module->nativeMemory,
        module->reflection.reflection,
        module->ordinaryBinaryMemory,
        stage,
        output);
}

static const char* collectOrdinaryMemberMemory(NSArray<MTLArgument*>* arguments,
    const std::shared_ptr<const shader::SingleEntryShaderArtifact>& artifact,
    const shader::ShaderReflection& physicalReflection,
    rhi::ShaderStage stage,
    MetalRhiNativeStageBufferMemberMemory& output)
{
    output = {};
    if (const auto* id = shader::SingleEntryShaderArtifact::validateOriginal(artifact))
        return id;
    if (!artifact->nativeBufferMemoryRequested())
        return nullptr;
    const shader::NativeBufferMemoryDeclarations* memory = nullptr;
    if (const auto* id =
            shader::SingleEntryShaderArtifact::originalNativeBufferMemory(artifact, memory))
        return id;
    core::pmr::Vector<shader::SpirvBufferDeclarationLayout> declarations{
        &core::defaultCpuMemoryResource()};
    for (const auto& binding : artifact->module().reflection.bindings) {
        if (binding.type != shader::BindingType::UniformBuffer
            && binding.type != shader::BindingType::ReadOnlyStorageBuffer
            && binding.type != shader::BindingType::StorageBuffer)
            continue;
        shader::SpirvBufferDeclarationLayout declaration;
        if (const auto* id = shader::SingleEntryShaderArtifact::originalBufferDeclarationLayout(
                artifact, binding.set, binding.binding, declaration))
            return id;
        declarations.push_back(declaration);
    }
    return collectOriginalMemberMemory(
        arguments, *memory, physicalReflection, declarations, stage, output);
}

static std::shared_ptr<MetalRhiNativePipelinePayload> createMetalRhiNativeRenderPipelinePayloadImpl(
    const rhi::RenderPipelineDesc& desc,
    const rhi::PipelineValidationReport& report,
    const rhi::semantics::MetalVertexStreamMapping& vertexStreams,
    bool supportsIndirectCommands,
    const std::string_view* verifiedVertexMsl,
    const std::string_view* verifiedFragmentMsl,
    const std::array<const rhi::BindlessPackageModule*, 2u>* originalModules = nullptr,
    const shader::GraphicsShaderModuleArtifact* ordinary = nullptr)
{
    auto payload = makeBasePayload(report, true);
    if (!vertexStreams.success) {
        payload->diagnostics = vertexStreams.diagnostics;
        return payload;
    }
    payload->vertexStreams = vertexStreams;
    for (const auto& stream : desc.vertexBuffers) {
        if (stream.slot >= vertexStreams.capacity
            || vertexStreams.slots[stream.slot] >= vertexStreams.capacity) {
            payload->diagnostics = "RHI-METAL-VERTEX-DESCRIPTOR-STREAM-DOMAIN";
            return payload;
        }
        for (const auto& attribute : stream.attributes)
            if (attribute.shaderLocation >= MetalRhiVertexInputFacts::attributeCapacity
                || !MetalRhiVertexInputFacts::nativeFormat(attribute.format)) {
                payload->diagnostics = "RHI-METAL-VERTEX-DESCRIPTOR-ATTRIBUTE-DOMAIN";
                return payload;
            }
    }
    if (!rhi::semantics::sampleMaskHasFullCoverage(
            desc.multisample.sampleCount, desc.multisample.sampleMask)) {
        payload->diagnostics = "RHI-METAL-SAMPLE-MASK-SHADER-LOWERING-REQUIRED";
        return payload;
    }
    payload->sampleCount = desc.multisample.sampleCount;
    payload->rasterStateRecorded = report.rasterStateValidated;
    payload->depthStencilStateRecorded = report.stencilStateValidated;
    payload->depthTestEnabled = desc.depthStencil.depthTestEnabled;
    payload->depthWriteEnabled = desc.depthStencil.depthWriteEnabled;
    payload->depthOnlyRenderPipeline = report.colorTargetCount == 0u;
    payload->colorFormats = report.colorFormats;
    payload->depthStencilFormat = report.depthStencilFormat;
    payload->renderPassCompatibilityEnabled = desc.renderPassCompatibility.enabled();
    payload->writesRenderTargetArrayIndex = report.selectedFragmentWritesRenderTargetArrayIndex;
    payload->usesDepthStencilAttachment = rhi::renderPipelineHasDepthStencilState(desc);
    payload->depthCompare = desc.depthStencil.depthCompare;
    payload->cullMode = desc.cullMode;
    payload->frontFace = desc.frontFace;
    payload->sampleMask = desc.multisample.sampleMask;
    payload->alphaToCoverageEnabled = desc.multisample.alphaToCoverageEnabled;
    payload->subpassIndex = desc.subpassIndex;
    payload->primitiveTopology = desc.topology;
    if (desc.topology == rhi::PrimitiveTopology::PatchList) {
        if (@available(macOS 10.13, iOS 11.0, *)) {
        } else {
            payload->diagnostics = "RHI-METAL-PATCH-LIST-API-UNAVAILABLE";
            return payload;
        }
    }
    payload->pushConstantMetalBufferIndex = desc.shaderPackage
        ? desc.shaderPackage->reflection.pushConstantMetalBufferIndex
        : UINT32_MAX;
    payload->computeWorkgroupSizeX =
        desc.shaderPackage ? desc.shaderPackage->reflection.computeWorkgroupSizeX : 0;
    payload->computeWorkgroupSizeY =
        desc.shaderPackage ? desc.shaderPackage->reflection.computeWorkgroupSizeY : 0;
    payload->computeWorkgroupSizeZ =
        desc.shaderPackage ? desc.shaderPackage->reflection.computeWorkgroupSizeZ : 0;
    if (desc.shaderPackage) {
        const auto& reflection = desc.shaderPackage->reflection;
        payload->metalGpuCountedIndirectContractVersion =
            reflection.metalGpuCountedIndirectContractVersion;
        payload->metalGpuCountedIndirectStatus = reflection.metalGpuCountedIndirectStatus;
        payload->metalGpuCountedIndirectCountBufferIndex =
            reflection.metalGpuCountedIndirectCountBufferIndex;
        payload->metalGpuCountedIndirectUsesDrawId = reflection.metalGpuCountedIndirectUsesDrawId;
        payload->metalGpuCountedIndirectMasksOutOfRange =
            reflection.metalGpuCountedIndirectMasksOutOfRange;
        payload->metalTessellationContractVersion = reflection.metalTessellationContractVersion;
        payload->metalTessellationStatus = reflection.metalTessellationStatus;
        payload->metalTessellationFactorBufferIndex = reflection.metalTessellationFactorBufferIndex;
        payload->metalTessellationControlPointCount = reflection.metalTessellationControlPointCount;
        payload->metalTessellationVertexEntryPoint = reflection.metalTessellationVertexEntryPoint;
        payload->metalTessellationPatchType = reflection.metalTessellationPatchType;
        payload->metalTessellationFactorFormat = reflection.metalTessellationFactorFormat;
        payload->metalTessellationFactorBytesPerPatch =
            reflection.metalTessellationFactorBytesPerPatch;
        payload->metalTessellationFactorInstanceStrideBytes =
            reflection.metalTessellationFactorInstanceStrideBytes;
        payload->metalTessellationAbiRecorded =
            rhi::hasMetalTessellationAbi(reflection, desc.patchControlPointCount);
    }
    if (desc.topology == rhi::PrimitiveTopology::PatchList
        && payload->metalTessellationFactorFormat != 0u) {
        payload->diagnostics = "RHI-METAL-TESSELLATION-FACTOR-FORMAT-UNSUPPORTED";
        return payload;
    }
    MTLPrimitiveType primitiveType = MTLPrimitiveTypeTriangle;
    if (!tryMetalPrimitiveType(
            desc.topology, payload->metalTessellationAbiRecorded, primitiveType)) {
        payload->diagnostics = "RHI-METAL-PATCH-LIST-NO-EXECUTABLE-TESSELLATION-LOWERING";
        return payload;
    }
    payload->primitiveType = static_cast<std::uint32_t>(primitiveType);
    payload->patchControlPointCount = desc.topology == rhi::PrimitiveTopology::PatchList
        ? payload->metalTessellationControlPointCount
        : desc.patchControlPointCount;
    // Record the L2 depthBias + stencil state so the command encoder can
    // lower them faithfully. Without these fields the Metal payload silently
    // dropped depthBias and stencil front/back, violating the RHI contract's
    // "no silent native semantics drop" rule. The encoder-side
    // setDepthBias:slopeScale:clamp: call is gated on depthBiasEnabled below.
    payload->depthBiasEnabled = desc.depthStencil.depthBiasEnabled;
    payload->hasViewport = desc.hasViewport;
    payload->viewport = desc.viewport;
    payload->hasScissor = desc.hasScissor;
    payload->scissor = desc.scissor;
    payload->depthBiasConstantFactor = desc.depthStencil.depthBiasConstantFactor;
    payload->depthBiasClamp = desc.depthStencil.depthBiasClamp;
    payload->depthBiasSlopeFactor = desc.depthStencil.depthBiasSlopeFactor;
    payload->stencilEnabled = desc.depthStencil.stencilEnabled;
    payload->stencilFront = desc.depthStencil.front;
    payload->stencilBack = desc.depthStencil.back;
    payload->stencilReadMask = desc.depthStencil.stencilReadMask;
    payload->stencilWriteMask = desc.depthStencil.stencilWriteMask;
    if (!desc.shaderPackage) {
        payload->diagnostics = "Metal RHI render pipeline requires shader package";
        return payload;
    }
    const auto selected =
        validateMetalRhiGraphicsStageSelection(desc, report, payload->preparedGraphicsStages);
    if (!selected.success) {
        payload->diagnostics = selected.diagnostics;
        return payload;
    }
    const bool hasFragment =
        (payload->preparedGraphicsStages & rhi::ShaderStage::Fragment) != rhi::ShaderStage::None;
    if (!hasFragment && verifiedFragmentMsl && !verifiedFragmentMsl->empty()) {
        payload->diagnostics = "RHI-METAL-GRAPHICS-UNEXPECTED-FRAGMENT-SOURCE";
        return payload;
    }

    std::size_t bufferPointeeFactCapacity = 0u;
    for (const auto& binding : desc.shaderPackage->reflection.descriptorBindings) {
        if (binding.type == rhi::DescriptorType::UniformBuffer
            || binding.type == rhi::DescriptorType::ReadOnlyStorageBuffer
            || binding.type == rhi::DescriptorType::StorageBuffer) {
            if (binding.arrayCount > payload->graphicsBufferPointeeFacts[0].entries.max_size()
                    - bufferPointeeFactCapacity) {
                payload->diagnostics = "RHI-METAL-GRAPHICS-POINTEE-FACT-CAPACITY-OVERFLOW";
                return payload;
            }
            bufferPointeeFactCapacity += binding.arrayCount;
        }
    }
    // Both selected formal signatures may include an inactive declaration.
    if (!TextureBindingFactCollector::reserve(
            desc.shaderPackage->reflection, hasFragment ? 2u : 1u, payload->graphicsTextureFacts)) {
        payload->diagnostics = "RHI-METAL-GRAPHICS-TEXTURE-FACT-CAPACITY-OVERFLOW";
        return payload;
    }
    payload->graphicsBufferPointeeFacts[0].entries.reserve(bufferPointeeFactCapacity);
    if (hasFragment)
        payload->graphicsBufferPointeeFacts[1].entries.reserve(bufferPointeeFactCapacity);
    // Formal signatures can include vertex streams, push constants and array
    // buffers. Reserve the specified physical namespace, not a guessed count
    // from semantic descriptor visibility. No storage grows while querying.
    payload->graphicsBufferFacts[0].entries.reserve(
        rhi::semantics::MetalVertexStreamMapping::capacity);
    if (hasFragment)
        payload->graphicsBufferFacts[1].entries.reserve(
            rhi::semantics::MetalVertexStreamMapping::capacity);

    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    id<MTLLibrary> library = verifiedVertexMsl ? createLibrary(*verifiedVertexMsl, diagnostics)
                                               : createLibrary(*desc.shaderPackage, diagnostics);
    payload->shaderLibrary = retainNativeObject(library);
    id<MTLLibrary> fragmentLibrary = hasFragment ? library : nil;
    if (library != nil && hasFragment && verifiedFragmentMsl) {
        fragmentLibrary = createLibrary(*verifiedFragmentMsl, diagnostics);
        payload->fragmentShaderLibrary = retainNativeObject(fragmentLibrary);
    }
    payload->shaderModulePayloadPresent = static_cast<bool>(payload->shaderLibrary);
    id<MTLRenderPipelineState> pipeline = nil;
    if (library != nil && (!hasFragment || fragmentLibrary != nil)) {
        const auto& vertexEntryPoint = desc.topology == rhi::PrimitiveTopology::PatchList
            ? payload->metalTessellationVertexEntryPoint
            : desc.vertexShader.entryPoint;
        id<MTLFunction> vertexFunction =
            [library newFunctionWithName:[NSString stringWithUTF8String:vertexEntryPoint.c_str()]];
        id<MTLFunction> fragmentFunction = hasFragment
            ? [fragmentLibrary
                  newFunctionWithName:[NSString stringWithUTF8String:desc.fragmentShader.entryPoint
                                                                         .c_str()]]
            : nil;
        payload->vertexFunction = retainBorrowedNativeObject(vertexFunction);
        payload->fragmentFunction = retainBorrowedNativeObject(fragmentFunction);
        const auto colorTargetCount = rhi::effectiveColorTargetCount(desc);
        const bool depthOnlyRenderPipeline = colorTargetCount == 0u;
        if (vertexFunction != nil && (!hasFragment || fragmentFunction != nil)
            && (depthOnlyRenderPipeline
                || nativePixelFormat(rhi::effectiveColorTargetAt(desc, 0).format)
                    != MTLPixelFormatInvalid)) {
            MTLRenderPipelineDescriptor* pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
            pipelineDesc.label = [NSString stringWithUTF8String:desc.label.c_str()];
            pipelineDesc.vertexFunction = vertexFunction;
            pipelineDesc.fragmentFunction = fragmentFunction;
            for (std::uint32_t index = 0; index < colorTargetCount; ++index) {
                const auto target = rhi::effectiveColorTargetAt(desc, index);
                auto* attachment = pipelineDesc.colorAttachments[index];
                attachment.pixelFormat = nativePixelFormat(target.format);
                attachment.blendingEnabled = target.blendEnabled;
                attachment.sourceRGBBlendFactor = metalBlendFactor(target.colorBlend.sourceFactor);
                attachment.destinationRGBBlendFactor =
                    metalBlendFactor(target.colorBlend.destinationFactor);
                attachment.rgbBlendOperation = metalBlendOperation(target.colorBlend.operation);
                attachment.sourceAlphaBlendFactor =
                    metalBlendFactor(target.alphaBlend.sourceFactor);
                attachment.destinationAlphaBlendFactor =
                    metalBlendFactor(target.alphaBlend.destinationFactor);
                attachment.alphaBlendOperation = metalBlendOperation(target.alphaBlend.operation);
                attachment.writeMask = metalColorWriteMask(target.writeMask);
            }
            const auto depthStencilFormat = rhi::effectiveDepthStencilFormat(desc);
            pipelineDesc.depthAttachmentPixelFormat = nativePixelFormat(depthStencilFormat);
            pipelineDesc.stencilAttachmentPixelFormat =
                depthStencilFormat == rhi::TextureFormat::Depth24Stencil8
                ? nativePixelFormat(depthStencilFormat)
                : MTLPixelFormatInvalid;
            pipelineDesc.sampleCount = desc.multisample.sampleCount;
            pipelineDesc.supportIndirectCommandBuffers = supportsIndirectCommands;
            payload->supportsIndirectCommands = supportsIndirectCommands;
            pipelineDesc.alphaToCoverageEnabled = desc.multisample.alphaToCoverageEnabled;
            if (desc.topology == rhi::PrimitiveTopology::PatchList) {
                pipelineDesc.tessellationFactorFormat = MTLTessellationFactorFormatHalf;
                pipelineDesc.tessellationPartitionMode = MTLTessellationPartitionModeInteger;
                pipelineDesc.tessellationFactorStepFunction =
                    MTLTessellationFactorStepFunctionPerPatch;
                pipelineDesc.tessellationOutputWindingOrder = MTLWindingCounterClockwise;
                pipelineDesc.maxTessellationFactor = 64.0f;
            }
            // Vertex descriptor: wire L2 VertexBufferLayoutDesc (slot/stride/
            // inputRate/attributes) into Metal's MTLVertexDescriptor. An
            // Instance-rate buffer advances once per draw instance, enabling
            // hardware instancing. Previously desc.vertexBuffers was ignored,
            // which meant Metal relied on shader auto-attribute binding and
            // could not express instanced vertex input.
            if (!desc.vertexBuffers.empty()) {
                MTLVertexDescriptor* vertexDescriptor = [MTLVertexDescriptor vertexDescriptor];
                for (const auto& vertexBuffer : desc.vertexBuffers) {
                    const auto physicalSlot = vertexStreams.slots[vertexBuffer.slot];
                    vertexDescriptor.layouts[physicalSlot].stride = vertexBuffer.strideBytes;
                    vertexDescriptor.layouts[physicalSlot].stepFunction =
                        (vertexBuffer.inputRate == rhi::VertexInputRate::Instance)
                        ? MTLVertexStepFunctionPerInstance
                        : MTLVertexStepFunctionPerVertex;
                    vertexDescriptor.layouts[physicalSlot].stepRate = 1;
                    for (const auto& attribute : vertexBuffer.attributes) {
                        vertexDescriptor.attributes[attribute.shaderLocation].format =
                            metalVertexFormat(attribute.format);
                        vertexDescriptor.attributes[attribute.shaderLocation].bufferIndex =
                            physicalSlot;
                        vertexDescriptor.attributes[attribute.shaderLocation].offset =
                            attribute.offsetBytes;
                    }
                }
                pipelineDesc.vertexDescriptor = vertexDescriptor;
            }
            // Query the descriptor copy actually supplied to native creation,
            // plus the actual selected function's stage inputs. Version0 closes
            // the new private instance ABI gate; it does not fake queried facts.
            MetalRhiVertexInputFactCollector(payload->vertexInputFacts)
                .collect(pipelineDesc.vertexDescriptor, vertexFunction, desc, vertexStreams);
            NSError* error = nil;
            MTLRenderPipelineReflection* reflection = nil;
            pipeline = [defaultMetalDevice()
                newRenderPipelineStateWithDescriptor:pipelineDesc
                                             options:MTLPipelineOptionArgumentInfo
                | MTLPipelineOptionBufferTypeInfo
                                          reflection:&reflection
                                               error:&error];
            if (pipeline == nil && error != nil) {
                diagnostics = "RHI-METAL-RENDER-PIPELINE-CREATE-FAILED: ";
                diagnostics += error.localizedDescription.UTF8String;
            }
            if (pipeline != nil) {
                if (reflection == nil)
                    diagnostics = "RHI-METAL-PIPELINE-REFLECTION-MISSING";
                if (reflection == nil
                    || !collectPhysicalBindingStages(reflection.vertexArguments,
                        rhi::ShaderStage::Vertex,
                        payload->nativeBindingStages,
                        diagnostics,
                        &payload->graphicsBufferFacts[0].entries,
                        true)
                    || (hasFragment
                        && !collectPhysicalBindingStages(reflection.fragmentArguments,
                            rhi::ShaderStage::Fragment,
                            payload->nativeBindingStages,
                            diagnostics,
                            &payload->graphicsBufferFacts[1].entries,
                            true))) {
                    [pipeline release];
                    pipeline = nil;
                }
                if (pipeline != nil) {
                    payload->graphicsBufferFacts[0].stage = rhi::ShaderStage::Vertex;
                    payload->graphicsBufferFacts[0].version = 1u;
                    if (hasFragment) {
                        payload->graphicsBufferFacts[1].stage = rhi::ShaderStage::Fragment;
                        payload->graphicsBufferFacts[1].version = 1u;
                    }
                    TextureBindingFactCollector facts(payload->graphicsTextureFacts);
                    if (facts.collect(reflection.vertexArguments, rhi::ShaderStage::Vertex)
                        && (!hasFragment
                            || facts.collect(
                                reflection.fragmentArguments, rhi::ShaderStage::Fragment)))
                        payload->graphicsTextureFactsVersion = 1u;
                    // Legacy recording does not consume these facts. The bounded
                    // private gate rejects version0 rather than guessing an ABI.
                    else
                        payload->graphicsTextureFacts.clear();
                    GraphicsBufferPointeeFactCollector(payload->graphicsBufferPointeeFacts[0])
                        .collect(reflection.vertexArguments, rhi::ShaderStage::Vertex);
                    if (hasFragment)
                        GraphicsBufferPointeeFactCollector(payload->graphicsBufferPointeeFacts[1])
                            .collect(reflection.fragmentArguments, rhi::ShaderStage::Fragment);
                    if (originalModules || ordinary) {
                        const char* memberDiagnostic = nullptr;
                        for (unsigned i = 0u; i < (hasFragment ? 2u : 1u); ++i) {
                            const auto arguments =
                                i ? reflection.fragmentArguments : reflection.vertexArguments;
                            const auto stage =
                                i ? rhi::ShaderStage::Fragment : rhi::ShaderStage::Vertex;
                            memberDiagnostic = ordinary
                                ? collectOrdinaryMemberMemory(arguments,
                                      i ? ordinary->fragment() : ordinary->vertex(),
                                      ordinary->module().reflection,
                                      stage,
                                      payload->graphicsBufferMemberMemory[i])
                                : collectOriginalGraphicsMemberMemory(arguments,
                                      (*originalModules)[i],
                                      stage,
                                      payload->graphicsBufferMemberMemory[i]);
                            if (memberDiagnostic)
                                break;
                        }
                        if (memberDiagnostic) {
                            diagnostics = memberDiagnostic;
                            for (auto& memory : payload->graphicsBufferMemberMemory)
                                memory = {};
                            [pipeline release];
                            pipeline = nil;
                        }
                    }
                }
            }
            [pipelineDesc release];
        }
        [vertexFunction release];
        [fragmentFunction release];
    }
    payload->pipelineState = retainNativeObject(pipeline);
    if (pipeline != nil && defaultMetalDevice() != nil) {
        MTLDepthStencilDescriptor* depthDesc = [[MTLDepthStencilDescriptor alloc] init];
        depthDesc.depthCompareFunction = desc.depthStencil.depthTestEnabled
            ? metalCompareFunction(desc.depthStencil.depthCompare)
            : MTLCompareFunctionAlways;
        depthDesc.depthWriteEnabled = desc.depthStencil.depthWriteEnabled;
        if (desc.depthStencil.stencilEnabled) {
            auto configureStencil = [](MTLStencilDescriptor* target,
                                        const rhi::StencilFaceState& source,
                                        std::uint32_t readMask,
                                        std::uint32_t writeMask) {
                target.stencilCompareFunction = metalCompareFunction(source.compare);
                target.stencilFailureOperation = metalStencilOperation(source.failOp);
                target.depthFailureOperation = metalStencilOperation(source.depthFailOp);
                target.depthStencilPassOperation = metalStencilOperation(source.passOp);
                target.readMask = readMask;
                target.writeMask = writeMask;
            };
            configureStencil(depthDesc.frontFaceStencil,
                desc.depthStencil.front,
                desc.depthStencil.stencilReadMask,
                desc.depthStencil.stencilWriteMask);
            configureStencil(depthDesc.backFaceStencil,
                desc.depthStencil.back,
                desc.depthStencil.stencilReadMask,
                desc.depthStencil.stencilWriteMask);
        }
        id<MTLDepthStencilState> depthState =
            [defaultMetalDevice() newDepthStencilStateWithDescriptor:depthDesc];
        [depthDesc release];
        payload->depthStencilState = retainNativeObject(depthState);
    }
    payload->renderPipelinePayloadPresent = static_cast<bool>(payload->pipelineState);
    const bool depthStencilStateRequired = desc.depthStencil.depthTestEnabled
        || desc.depthStencil.depthWriteEnabled || desc.depthStencil.stencilEnabled;
    payload->nativeRuntimeBacked = payload->shaderModulePayloadPresent
        && payload->renderPipelinePayloadPresent
        && (!depthStencilStateRequired || payload->depthStencilState != nullptr);
    payload->nativeRuntimeDiagnosticOnly = !payload->nativeRuntimeBacked;
    if (!payload->nativeRuntimeBacked) {
        if (diagnostics.empty())
            payload->diagnostics = "Metal RHI failed to create native render pipeline";
        else
            payload->diagnostics = diagnostics;
    }
    return payload;
}

std::shared_ptr<MetalRhiNativePipelinePayload> createMetalRhiNativeRenderPipelinePayload(
    const rhi::RenderPipelineDesc& desc,
    const rhi::PipelineValidationReport& report,
    const rhi::semantics::MetalVertexStreamMapping& streams,
    bool supportsIndirectCommands)
{
    return createMetalRhiNativeRenderPipelinePayloadImpl(
        desc, report, streams, supportsIndirectCommands, nullptr, nullptr);
}

std::shared_ptr<MetalRhiNativePipelinePayload> createMetalRhiNativeRenderPipelinePayload(
    const rhi::RenderPipelineDesc& desc,
    const rhi::PipelineValidationReport& report,
    const rhi::semantics::MetalVertexStreamMapping& streams,
    std::string_view verifiedVertexMsl,
    std::string_view verifiedFragmentMsl)
{
    auto payload = createMetalRhiNativeRenderPipelinePayloadImpl(
        desc, report, streams, false, &verifiedVertexMsl, &verifiedFragmentMsl);
    if (payload && !payload->nativeRuntimeBacked) {
        auto detail = std::move(payload->diagnostics);
        payload->diagnostics = "RHI-METAL-PREPARED-GRAPHICS-PIPELINE-CREATION-FAILED";
        if (!detail.empty()) {
            payload->diagnostics += ": ";
            payload->diagnostics += detail;
        }
    }
    return payload;
}

std::shared_ptr<MetalRhiNativePipelinePayload> createMetalRhiNativeRenderPipelinePayload(
    const rhi::RenderPipelineDesc& desc,
    const rhi::PipelineValidationReport& report,
    const rhi::semantics::MetalVertexStreamMapping& streams,
    const std::shared_ptr<const rhi::BindlessShaderPackage>& package)
{
    auto refuse = [&](const char* id) {
        auto payload = makeBasePayload(report, true);
        payload->diagnostics = id;
        return payload;
    };
    if (!package || !package->sharedOwnerMatches(package)
        || package->artifact().execution != rhi::BindlessExecution::Native)
        return refuse("RHI-METAL-MEMBER-MEMORY-ORIGINAL-PACKAGE");
    rhi::BindlessGraphicsPipelineEntries entries;
    if (const auto* id = rhi::BindlessGraphicsPipelineEntries::capture(*package, desc, entries))
        return refuse(id);
    const std::array<const rhi::BindlessPackageModule*, 2u> modules{
        entries.module(rhi::ShaderStage::Vertex, 1u),
        entries.module(rhi::ShaderStage::Fragment, 1u)};
    std::array<std::string_view, 2u> sources;
    for (unsigned i = 0u; i < modules.size(); ++i)
        if (modules[i]) {
            const auto& bytes = package->files()[modules[i]->metalFile].bytes;
            sources[i] = {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
        }
    return createMetalRhiNativeRenderPipelinePayloadImpl(
        desc, report, streams, false, &sources[0], &sources[1], &modules);
}

static std::shared_ptr<MetalRhiNativePipelinePayload>
createMetalRhiNativeComputePipelinePayloadImpl(const rhi::ComputePipelineDesc& desc,
    const rhi::PipelineValidationReport& report,
    const std::string_view* verifiedMslSource,
    const std::shared_ptr<const shader::SingleEntryShaderArtifact>& ordinary = {})
{
    auto payload = makeBasePayload(report, false);
    payload->pushConstantMetalBufferIndex = desc.shaderPackage
        ? desc.shaderPackage->reflection.pushConstantMetalBufferIndex
        : UINT32_MAX;
    payload->computeWorkgroupSizeX = report.selectedComputeWorkgroupSizeX;
    payload->computeWorkgroupSizeY = report.selectedComputeWorkgroupSizeY;
    payload->computeWorkgroupSizeZ = report.selectedComputeWorkgroupSizeZ;
    if (!desc.shaderPackage) {
        payload->diagnostics = "Metal RHI compute pipeline requires shader package";
        return payload;
    }

    if (!TextureBindingFactCollector::reserve(
            desc.shaderPackage->reflection, 1u, payload->computeTextureFacts)) {
        payload->diagnostics = "RHI-METAL-COMPUTE-TEXTURE-FACT-CAPACITY-OVERFLOW";
        return payload;
    }

    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    id<MTLLibrary> library = verifiedMslSource ? createLibrary(*verifiedMslSource, diagnostics)
                                               : createLibrary(*desc.shaderPackage, diagnostics);
    payload->shaderLibrary = retainNativeObject(library);
    payload->shaderModulePayloadPresent = static_cast<bool>(payload->shaderLibrary);
    id<MTLComputePipelineState> pipeline = nil;
    if (library != nil) {
        id<MTLFunction> function = [library
            newFunctionWithName:[NSString
                                    stringWithUTF8String:desc.computeShader.entryPoint.c_str()]];
        payload->computeFunction = retainBorrowedNativeObject(function);
        if (function != nil) {
            NSError* error = nil;
            MTLComputePipelineReflection* reflection = nil;
            pipeline = [defaultMetalDevice()
                newComputePipelineStateWithFunction:function
                                            options:MTLPipelineOptionArgumentInfo
                | (ordinary && ordinary->nativeBufferMemoryRequested()
                        ? MTLPipelineOptionBufferTypeInfo
                        : 0u)
                                         reflection:&reflection
                                              error:&error];
            if (pipeline != nil) {
                const auto axes = pipeline.device.maxThreadsPerThreadgroup;
                const MetalRhiComputeWorkgroupLimits limits{
                    axes.width, axes.height, axes.depth, pipeline.maxTotalThreadsPerThreadgroup};
                const auto* workgroupDiagnostic = limits.validate(payload->computeWorkgroupSizeX,
                    payload->computeWorkgroupSizeY,
                    payload->computeWorkgroupSizeZ);
                if (workgroupDiagnostic)
                    diagnostics = workgroupDiagnostic;
                else if (reflection == nil)
                    diagnostics = "RHI-METAL-PIPELINE-REFLECTION-MISSING";
                if (workgroupDiagnostic || reflection == nil
                    || !collectPhysicalBindingStages(reflection.arguments,
                        rhi::ShaderStage::Compute,
                        payload->nativeBindingStages,
                        diagnostics,
                        &payload->computeBufferFacts)) {
                    [pipeline release];
                    pipeline = nil;
                } else if (ordinary) {
                    payload->computeBufferFactsVersion = 1u;
                    if (const auto* id = collectOrdinaryMemberMemory(reflection.arguments,
                            ordinary,
                            ordinary->module().reflection,
                            rhi::ShaderStage::Compute,
                            payload->computeBufferMemberMemory)) {
                        diagnostics = id;
                        payload->computeBufferMemberMemory = {};
                        [pipeline release];
                        pipeline = nil;
                    }
                } else
                    payload->computeBufferFactsVersion = 1u;
                if (pipeline != nil) {
                    TextureBindingFactCollector facts(payload->computeTextureFacts);
                    if (facts.collect(reflection.arguments, rhi::ShaderStage::Compute))
                        payload->computeTextureFactsVersion = 1u;
                    else
                        payload->computeTextureFacts.clear();
                }
            }
        }
        [function release];
    }
    payload->pipelineState = retainNativeObject(pipeline);
    payload->computePipelinePayloadPresent = static_cast<bool>(payload->pipelineState);
    payload->nativeRuntimeBacked =
        payload->shaderModulePayloadPresent && payload->computePipelinePayloadPresent;
    payload->nativeRuntimeDiagnosticOnly = !payload->nativeRuntimeBacked;
    if (!payload->nativeRuntimeBacked) {
        if (diagnostics.empty())
            payload->diagnostics = "Metal RHI failed to create native compute pipeline";
        else
            payload->diagnostics = diagnostics;
    }
    return payload;
}

std::shared_ptr<MetalRhiNativePipelinePayload> createMetalRhiNativeComputePipelinePayload(
    const rhi::ComputePipelineDesc& desc, const rhi::PipelineValidationReport& report)
{
    return createMetalRhiNativeComputePipelinePayloadImpl(desc, report, nullptr);
}

std::shared_ptr<MetalRhiNativePipelinePayload> createMetalRhiNativeComputePipelinePayload(
    const rhi::ComputePipelineDesc& desc,
    const rhi::PipelineValidationReport& report,
    std::string_view verifiedMslSource)
{
    return createMetalRhiNativeComputePipelinePayloadImpl(desc, report, &verifiedMslSource);
}

std::shared_ptr<MetalRhiNativePipelinePayload> createMetalRhiNativeComputePipelinePayload(
    const rhi::ComputePipelineDesc& desc,
    const rhi::PipelineValidationReport& report,
    const std::shared_ptr<const shader::SingleEntryShaderArtifact>& artifact)
{
    if (const auto* id = shader::SingleEntryShaderArtifact::validateOriginal(artifact)) {
        auto payload = makeBasePayload(report, false);
        payload->diagnostics = id;
        return payload;
    }
    const auto& bytes = artifact->bytes(shader::ShaderArtifactRole::Metal);
    const std::string_view source(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return createMetalRhiNativeComputePipelinePayloadImpl(desc, report, &source, artifact);
}

std::shared_ptr<MetalRhiNativePipelinePayload> createMetalRhiNativeRenderPipelinePayload(
    const rhi::RenderPipelineDesc& desc,
    const rhi::PipelineValidationReport& report,
    const rhi::semantics::MetalVertexStreamMapping& streams,
    const std::shared_ptr<const shader::GraphicsShaderModuleArtifact>& artifact)
{
    if (const auto* id = shader::GraphicsShaderModuleArtifact::validateOriginal(artifact)) {
        auto payload = makeBasePayload(report, true);
        payload->diagnostics = id;
        return payload;
    }
    const auto& bytes = artifact->bytes(shader::ShaderArtifactRole::Metal);
    const std::string_view source(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return createMetalRhiNativeRenderPipelinePayloadImpl(
        desc, report, streams, false, &source, &source, nullptr, artifact.get());
}

} // namespace arp::rhi_metal
