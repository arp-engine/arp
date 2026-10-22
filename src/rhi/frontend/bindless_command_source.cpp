#include "rhi/frontend/bindless_initialized_graphics.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/command_sources.h"
#include "rhi/semantics/bindless_entry_access.h"
#include "rhi/semantics/bindless_graphics_member_memory_lowering.h"
#include "rhi/semantics/bindless_ordinary_buffer_range_lowering.h"
#include "rhi/semantics/bindless_ordinary_memory_lowering.h"
#include "rhi/semantics/bindless_ordinary_opaque_lowering.h"
#include "rhi/semantics/bindless_original_preparation_lowering.h"

namespace arp::rhi {
const char* OriginalBindlessRootNativeClosureContract::producedRootDiagnostic(
    const OriginalCommandBindlessRootSource& root, std::uint32_t version) noexcept
{
    if (version != producedGeometryVersion)
        return nullptr;
    const auto& plan = root.originalSnapshot()->plan();
    if (!plan.success || !plan.executable || plan.originalPreparationContractVersion
        || !((plan.originalExecutionContractVersion == 15u
                 && plan.execution == BindlessExecution::Native)
            || (plan.originalExecutionContractVersion == 16u
                && plan.execution == BindlessExecution::Emulated))
        || root.authorization()->package().gpuVisibilityConsumerContractVersion() != 2u)
        return "RHI-BINDLESS-PRODUCED-CLOSURE-PACKAGE-PROFILE";
    const bool graphics =
        std::holds_alternative<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
            root.originalPrepared());
    return root.originalShaderEntryAccessVersion() == (graphics ? 2u : 3u)
        ? nullptr
        : "RHI-BINDLESS-PRODUCED-CLOSURE-ENTRY-ACCESS";
}

namespace {
using Root = OriginalCommandBindlessRootSource;

const char* resolveOrdinaryBufferMemory(const Root& root,
    std::uint32_t set,
    std::uint32_t binding,
    DescriptorType type,
    ShaderStage selectedStage,
    std::uint64_t minimumBytes,
    std::uint64_t sourceBytes,
    std::uint64_t alignmentBytes,
    bool entrySpecific,
    BindlessResourceAccess& access,
    std::size_t& sourceIndex) noexcept
{
    for (const auto& domain : root.ordinaryDomains()) {
        if (domain.set != set)
            continue;
        for (const auto& element : domain.source->frozenElements()) {
            const auto& fields = element.declaration;
            if (fields.write.binding != binding || fields.write.arrayElement)
                continue;
            const auto* lease = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                &element.resourceOwner);
            if (!lease || !*lease || fields.write.type != type)
                return "RHI-BINDLESS-ORDINARY-MEMORY-SOURCE";
            DescriptorBufferBindingRangeContract::Resolved range;
            if (const auto* id = DescriptorBufferBindingRangeContract::resolve(
                    fields.write.bufferBindingRange, (*lease)->layout().sizeBytes, 0u, range))
                return id;
            if (range.sizeBytes < minimumBytes || range.sizeBytes < sourceBytes
                || !core::containsUnsignedRange(fields.bufferAccessOffset,
                    fields.bufferAccessBytes,
                    range.offsetBytes,
                    minimumBytes)
                || !core::containsUnsignedRange(fields.bufferAccessOffset,
                    fields.bufferAccessBytes,
                    range.offsetBytes,
                    sourceBytes))
                return "RHI-BINDLESS-ORDINARY-MEMORY-RANGE";
            if (alignmentBytes && range.offsetBytes % alignmentBytes)
                return "RHI-BINDLESS-ORDINARY-MEMORY-ALIGNMENT";
            const auto* layout =
                findDescriptorSetBinding(domain.source->frozenDefinition(), binding);
            if (!layout)
                return "RHI-BINDLESS-ORDINARY-MEMORY-BINDING";
            if ((layout->stages & selectedStage) != selectedStage)
                return "RHI-BINDLESS-ORDINARY-MEMORY-STAGE";
            access.resource = (*lease)->resource();
            access.kind = BindlessAccessKind::Buffer;
            access.stages = entrySpecific ? selectedStage : layout->stages;
            access.pipelineStages = fields.stages;
            access.state = fields.write.resourceState;
            access.access = fields.access;
            access.bufferOffset = range.offsetBytes;
            access.bufferSize = range.sizeBytes;
            sourceIndex = fields.sourceIndex;
            return nullptr;
        }
    }
    return "RHI-BINDLESS-ORDINARY-MEMORY-BINDING";
}

const char* validateOrdinaryDomains(const BindlessAuthorization& authorization,
    const OriginalCommandResourceSources& inventory,
    const Root::OrdinaryDomain* domains,
    std::size_t count,
    const OriginalCommandBindlessRootSourceBudget& budget) noexcept
{
    const auto& artifact = authorization.package().artifact();
    if (budget.contractVersion == 1u)
        return !artifact.additionalDomains.empty() || count || domains
            ? "RHI-ORIGINAL-BINDLESS-ROOT-DOMAINS-REQUIRED"
            : nullptr;
    if (count != artifact.additionalDomains.size() || !count || !domains)
        return "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-COVERAGE";
    if (count > budget.maxOrdinaryDomains)
        return "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-DOMAIN-CAPACITY";
    std::size_t elements = 0u;
    for (std::size_t i = 0u; i < count; ++i) {
        const auto& domain = domains[i];
        const BindlessPipelineDescriptorDomain* expected = nullptr;
        for (const auto& candidate : artifact.additionalDomains)
            if (candidate.set == domain.set)
                expected = &candidate;
        if (!expected || domain.set == artifact.set)
            return "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-SET";
        for (std::size_t j = 0u; j < i; ++j)
            if (domains[j].set == domain.set || domains[j].sourceIndex == domain.sourceIndex)
                return "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-DUPLICATE";
        if (const auto* id = OriginalCommandDescriptorSetSource::validateOriginal(domain.source))
            return id;
        if (!OriginalCommandDescriptorSetSource::canonicalVersion(
                domain.source->sourceContractVersion()))
            return "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-SOURCE-VERSION";
        if (domain.sourceIndex >= inventory.sources().size())
            return "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-POSITION";
        const auto* original =
            std::get_if<std::shared_ptr<const OriginalCommandDescriptorSetSource>>(
                &inventory.sources()[domain.sourceIndex]);
        if (!original || !core::SharedOwnerProjection::sameOwner(*original, domain.source)
            || !domain.source->matchesCanonicalSources(inventory))
            return "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-INVENTORY";
        if (!descriptorLayoutDefinitionMatches(expected->layout, domain.source->frozenDefinition()))
            return "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-LAYOUT";
        const auto& frozen = domain.source->frozenElements();
        if (frozen.size() != expected->layout.bindings.size())
            return "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-ELEMENTS";
        if (frozen.size() > budget.maxOrdinaryElements - elements)
            return "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-ELEMENT-CAPACITY";
        elements += frozen.size();
        for (const auto& binding : expected->layout.bindings) {
            if (binding.arrayCount != 1u || binding.dynamicOffset || binding.tableBacked)
                return "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-BINDING-SHAPE";
            const OriginalCommandDescriptorSetFrozenElement* matched = nullptr;
            for (const auto& element : frozen)
                if (element.declaration.write.binding == binding.binding
                    && !element.declaration.write.arrayElement)
                    matched = &element;
            if (!matched)
                return "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-ELEMENTS";
            if (const auto* id = OriginalCommandDescriptorDependencies::validateGeneralFootprint(
                    matched->declaration, binding, matched->resourceOwner))
                return id;
        }
    }
    return nullptr;
}
} // namespace

static_assert(OriginalCommandBindlessRootSource::cpuOwnerSlots
    <= OriginalCommandGraphicsPipelineSource::cpuOwnerSlots);

OriginalCommandBindlessRootSource::OriginalCommandBindlessRootSource(ConstructionKey,
    std::shared_ptr<const BindlessSnapshot> snapshot,
    Prepared prepared,
    std::shared_ptr<const OriginalCommandResourceSources> inventory,
    OriginalCommandBindlessRootSourceBudget budget)
    : snapshot_(std::move(snapshot)), prepared_(std::move(prepared)),
      authorization_(std::visit(
          [](const auto& p) {
              return p->authorization();
          },
          prepared_)),
      inventory_(std::move(inventory)), budget_(budget), generation_(snapshot_->generation()),
      contentRevision_(snapshot_->set_->contentRevision())
{}

const char* OriginalCommandBindlessRootSource::validateBudget(
    const OriginalCommandBindlessRootSourceBudget& budget) noexcept
{
    if (budget.contractVersion != 1u && budget.contractVersion != 2u)
        return "RHI-ORIGINAL-BINDLESS-ROOT-VERSION";
    if (!budget.prototypeOptIn)
        return "RHI-ORIGINAL-BINDLESS-ROOT-OPT-IN";
    if (!budget.maxPhysicalElements || !budget.sourceCpuBytes)
        return "RHI-ORIGINAL-BINDLESS-ROOT-BUDGET";
    if (budget.maxExclusiveRetainedOwners < retainedOwnerCount + cpuOwnerSlots)
        return "RHI-ORIGINAL-BINDLESS-ROOT-OWNER-CAPACITY";
    if (budget.contractVersion == 1u && (budget.maxOrdinaryDomains || budget.maxOrdinaryElements))
        return "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-SCHEMA";
    if (budget.contractVersion == 2u && (!budget.maxOrdinaryDomains || !budget.maxOrdinaryElements))
        return "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-BUDGET";
    return nullptr;
}

const DescriptorSet* OriginalCommandBindlessRootSource::originalObject() const noexcept
{
    return snapshot_ ? snapshot_->set_.get() : nullptr;
}

const DescriptorArena* OriginalCommandBindlessRootSource::originalArena() const noexcept
{
    return snapshot_ ? snapshot_->arena_.get() : nullptr;
}

std::size_t OriginalCommandBindlessRootSource::size() const noexcept
{
    std::size_t count = snapshot_->faultReadbackBuffer() ? 1u : 0u;
    for (const auto& element : elements_)
        if (element.declaration.write.type != DescriptorType::Sampler)
            ++count;
    for (const auto& domain : ordinaryDomains_)
        for (const auto& element : domain.source->frozenElements())
            if (element.declaration.write.type != DescriptorType::Sampler)
                ++count;
    return count;
}

bool OriginalCommandBindlessRootSource::read(std::size_t index, BindlessResourceAccess& out) const
{
    std::size_t declaration = SIZE_MAX;
    return readOriginalDeclaration(index, out, declaration);
}

bool OriginalCommandBindlessRootSource::readOriginalDeclaration(
    std::size_t index, BindlessResourceAccess& out, std::size_t& declaration) const
{
    out = {};
    declaration = SIZE_MAX;
    if (!valid())
        return false;
    return readValidatedDeclaration(index, out, declaration);
}

bool OriginalCommandBindlessRootSource::readValidatedDeclaration(
    std::size_t index, BindlessResourceAccess& out, std::size_t& declaration) const
{
    out = {};
    declaration = SIZE_MAX;
    for (const auto& element : elements_) {
        const auto& write = element.declaration.write;
        if (write.type == DescriptorType::Sampler)
            continue;
        if (index) {
            --index;
            continue;
        }
        const auto physical =
            descriptorElementIndex(snapshot_->set_->layout(), write.binding, write.arrayElement);
        out = snapshot_->accesses_[physical];
        declaration = element.declaration.sourceIndex;
        return out.resource.isValid();
    }
    for (const auto& domain : ordinaryDomains_)
        for (const auto& element : domain.source->frozenElements()) {
            const auto& fields = element.declaration;
            if (fields.write.type == DescriptorType::Sampler)
                continue;
            if (index) {
                --index;
                continue;
            }
            const auto* binding =
                findDescriptorSetBinding(domain.source->frozenDefinition(), fields.write.binding);
            if (!binding)
                return false;
            out.stages = binding->stages;
            out.pipelineStages = fields.stages;
            out.access = fields.access;
            out.state = fields.write.resourceState;
            declaration = fields.sourceIndex;
            if (const auto* buffer = std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(
                    &element.resourceOwner)) {
                out.resource = (*buffer)->resource();
                out.kind = BindlessAccessKind::Buffer;
                out.bufferOffset = fields.bufferAccessOffset;
                out.bufferSize = fields.bufferAccessBytes;
            } else if (const auto* view =
                           std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                               &element.resourceOwner)) {
                out.resource = (*view)->originalParentLease()->resource();
                out.kind = BindlessAccessKind::Texture;
                const auto& shape = (*view)->originalViewLease()->layout();
                out.textureRange = {shape.baseMipLevel,
                    shape.mipLevelCount,
                    shape.baseArrayLayer,
                    shape.arrayLayerCount,
                    shape.aspect};
            } else
                return false;
            return out.resource.isValid();
        }
    if (!index && snapshot_->faultReadbackBuffer()) {
        out = snapshot_->faultReadbackAccess();
        return out.resource.isValid();
    }
    return false;
}

const char* OriginalCommandBindlessRootSource::originalOrdinaryOpaqueBinding(
    const std::shared_ptr<const OriginalCommandBindlessRootSource>& root,
    core::StringView entry,
    std::uint32_t set,
    std::uint32_t binding,
    BindlessOrdinaryOpaqueBinding& output,
    std::uint32_t version) noexcept
{
    output = {};
    if (version != 1u)
        return "RHI-BINDLESS-ORDINARY-OPAQUE-VERSION";
    if (const auto* id = validateOriginal(root))
        return id;
    if (root->sourceContractVersion() != 2u)
        return "RHI-BINDLESS-ORDINARY-OPAQUE-ROOT-VERSION";
    const auto backend = std::visit(
        [](const auto& prepared) {
            return prepared->owners()->desc().pipeline->validationReport().backend;
        },
        root->prepared_);
    const auto decision =
        semantics::OriginalBindlessOrdinaryOpaqueLowering::lowering(backend, version);
    if (!decision.inspectionAvailable || decision.diagnostic)
        return decision.diagnostic;
    ShaderStage selectedStage = ShaderStage::Compute;
    const BindlessPackageModule* selected = nullptr;
    const char* diagnostic = nullptr;
    const auto& package = root->authorization_->package();
    if (const auto* compute =
            std::get_if<std::shared_ptr<const BindlessPreparedComputeBinding>>(&root->prepared_))
        selected = selectBindlessComputeEntryAccess(
            package, (*compute)->owners()->desc().pipeline->shaderReflection(), diagnostic);
    else {
        const auto& graphics =
            std::get<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(root->prepared_);
        selected = selectBindlessGraphicsEntryAccess(
            package, *graphics->owners()->desc().pipeline, entry, selectedStage, diagnostic);
    }
    if (!selected)
        return diagnostic;
    if (selected->entry != entry)
        return "RHI-BINDLESS-ORDINARY-OPAQUE-ENTRY";
    const auto* layout =
        semantics::BindlessPipelineInterfaceContract::descriptorLayout(package.artifact(), set);
    if (!layout || set == package.artifact().set)
        return "RHI-BINDLESS-ORDINARY-OPAQUE-SET";
    const auto* shape = findDescriptorSetBinding(*layout, binding);
    if (!shape
        || (shape->type != DescriptorType::SampledTexture && shape->type != DescriptorType::Sampler)
        || shape->arrayCount != 1u || shape->dynamicOffset || shape->tableBacked)
        return "RHI-BINDLESS-ORDINARY-OPAQUE-BINDING";
    bool active = false;
    if (const auto* id =
            semantics::OriginalBindlessEntryAccessContract::binding(selected->reflection.reflection,
                package.artifact().execution,
                set,
                binding,
                selectedStage,
                active))
        return id;
    if (!active)
        return "RHI-BINDLESS-ORDINARY-OPAQUE-INACTIVE";
    std::size_t accessOrdinal = 0u;
    for (const auto& element : root->elements_)
        if (element.declaration.write.type != DescriptorType::Sampler)
            ++accessOrdinal;
    for (const auto& domain : root->ordinaryDomains_) {
        for (const auto& element : domain.source->frozenElements()) {
            const auto& fields = element.declaration;
            const auto ordinal = accessOrdinal;
            if (fields.write.type != DescriptorType::Sampler)
                ++accessOrdinal;
            if (domain.set != set || fields.write.binding != binding || fields.write.arrayElement)
                continue;
            BindlessOrdinaryOpaqueBinding candidate;
            candidate.contractVersion = version;
            candidate.set = set;
            candidate.declaration = *shape;
            candidate.selectedStage = selectedStage;
            candidate.sourceIndex = fields.sourceIndex;
            candidate.resource = fields.write.resource;
            candidate.registrationGeneration = fields.registrationGeneration;
            if (const auto* source =
                    std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                        &element.resourceOwner)) {
                if (shape->type != DescriptorType::SampledTexture)
                    return "RHI-BINDLESS-ORDINARY-OPAQUE-SOURCE";
                const auto& parent = (*source)->originalParentLease();
                candidate.parentResource = parent->resource();
                candidate.parentRegistrationGeneration =
                    parent->originalObject()->originalResourceRegistration().generation();
                candidate.view = (*source)->originalViewLease()->layout();
                std::size_t projectedSource = SIZE_MAX;
                if (!root->readValidatedDeclaration(ordinal, candidate.access, projectedSource)
                    || projectedSource != fields.sourceIndex)
                    return "RHI-BINDLESS-ORDINARY-OPAQUE-PROJECTION";
            } else if (const auto* sampler =
                           std::get_if<std::shared_ptr<const OriginalCommandSamplerLease>>(
                               &element.resourceOwner)) {
                if (shape->type != DescriptorType::Sampler)
                    return "RHI-BINDLESS-ORDINARY-OPAQUE-SOURCE";
                candidate.sampler = (*sampler)->layout();
            } else
                return "RHI-BINDLESS-ORDINARY-OPAQUE-SOURCE";
            output = candidate;
            return nullptr;
        }
    }
    return "RHI-BINDLESS-ORDINARY-OPAQUE-BINDING";
}

const char* OriginalCommandBindlessRootSource::originalOrdinaryBufferMemory(
    const std::shared_ptr<const OriginalCommandBindlessRootSource>& root,
    core::StringView entry,
    std::uint32_t set,
    std::uint32_t binding,
    shader::TargetBufferMemoryLayout& output,
    BindlessResourceAccess& access,
    std::size_t& sourceIndex,
    std::uint32_t version) noexcept
{
    output = {};
    access = {};
    sourceIndex = SIZE_MAX;
    if (version != 1u && version != 2u)
        return "RHI-BINDLESS-ORDINARY-MEMORY-VERSION";
    if (const auto* id = validateOriginal(root))
        return id;
    ShaderStage selectedStage = ShaderStage::Compute;
    const auto* prepared =
        std::get_if<std::shared_ptr<const BindlessPreparedComputeBinding>>(&root->prepared_);
    const char* entryDiagnostic = nullptr;
    const BindlessPackageModule* selected = nullptr;
    if (prepared && *prepared) {
        selected = selectBindlessComputeEntryAccess(root->authorization_->package(),
            (*prepared)->owners()->desc().pipeline->shaderReflection(),
            entryDiagnostic);
    } else {
        if (version != 2u)
            return "RHI-BINDLESS-ORDINARY-MEMORY-PROFILE";
        const auto* graphics =
            std::get_if<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(&root->prepared_);
        if (!graphics || !*graphics)
            return "RHI-BINDLESS-ORDINARY-MEMORY-PROFILE";
        const auto& pipeline = *(*graphics)->owners()->desc().pipeline;
        const auto lowering = semantics::OriginalBindlessOrdinaryMemoryLowering::lowering(
            pipeline.validationReport().backend, version);
        if (!lowering.inspectionAvailable || lowering.diagnostic)
            return lowering.diagnostic;
        selected = selectBindlessGraphicsEntryAccess(
            root->authorization_->package(), pipeline, entry, selectedStage, entryDiagnostic);
    }
    if (!selected)
        return entryDiagnostic;
    if (selected->entry != entry)
        return "RHI-BINDLESS-ORDINARY-MEMORY-ENTRY";
    shader::TargetBufferMemoryLayout facts;
    if (const auto* id = BindlessShaderPackage::originalOrdinaryBufferMemory(
            root->authorization_->originalPackageOwner(), entry, set, binding, facts))
        return id;
    const auto type = descriptorTypeFromShaderBinding(facts.type);
    if (!type || !facts.target.minimumBindingBytes)
        return "RHI-BINDLESS-ORDINARY-MEMORY-TYPE";
    if (const auto* id = resolveOrdinaryBufferMemory(*root,
            set,
            binding,
            *type,
            selectedStage,
            facts.target.minimumBindingBytes,
            facts.source.staticOccupiedExtentBytes,
            0u,
            version == 2u,
            access,
            sourceIndex))
        return id;
    output = facts;
    return nullptr;
}

const char* OriginalCommandBindlessRootSource::originalOrdinaryBufferRequirement(
    const std::shared_ptr<const OriginalCommandBindlessRootSource>& root,
    core::StringView entry,
    std::uint32_t set,
    std::uint32_t binding,
    ShaderBufferMemoryRequirement& output,
    BindlessResourceAccess& access,
    std::size_t& sourceIndex,
    std::uint32_t version) noexcept
{
    output = {};
    access = {};
    sourceIndex = SIZE_MAX;
    if (version != 1u && version != 2u)
        return "RHI-BINDLESS-ORDINARY-RANGE-VERSION";
    if (const auto* id = validateOriginal(root))
        return id;
    return readValidatedOrdinaryBufferRequirement(
        root, entry, set, binding, output, access, sourceIndex, version);
}

const char* OriginalCommandBindlessRootSource::readValidatedOrdinaryBufferRequirement(
    const std::shared_ptr<const OriginalCommandBindlessRootSource>& root,
    core::StringView entry,
    std::uint32_t set,
    std::uint32_t binding,
    ShaderBufferMemoryRequirement& output,
    BindlessResourceAccess& access,
    std::size_t& sourceIndex,
    std::uint32_t version) noexcept
{
    output = {};
    access = {};
    sourceIndex = SIZE_MAX;
    if (version != 1u && version != 2u)
        return "RHI-BINDLESS-ORDINARY-RANGE-VERSION";
    const auto* prepared =
        std::get_if<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(&root->prepared_);
    if (!prepared || !*prepared)
        return "RHI-BINDLESS-ORDINARY-RANGE-PROFILE";
    const auto& pipeline = *(*prepared)->owners()->desc().pipeline;
    const auto lowering = semantics::OriginalBindlessOrdinaryBufferRangeLowering::lowering(
        pipeline.validationReport().backend, version);
    if (!lowering.available || lowering.diagnostic)
        return lowering.diagnostic;
    ShaderStage stage = ShaderStage::None;
    const char* diagnostic = nullptr;
    const auto* selected = selectBindlessGraphicsEntryAccess(
        root->authorization_->package(), pipeline, entry, stage, diagnostic);
    if (!selected)
        return diagnostic;
    ShaderBufferMemoryRequirement requirement;
    if (const auto* id =
            pipeline.bindlessOrdinaryBufferRequirement(stage, set, binding, requirement, version))
        return id;
    const shader::SpirvBufferDeclarationLayout* source = nullptr;
    for (const auto& facts : selected->ordinaryBinaryMemory)
        if (facts.set == set && facts.binding == binding)
            source = &facts;
    if (!source || (source->hasRuntimeArray && version == 1u) || source->descriptorCount != 1u
        || !requirement.minimumBytes || !requirement.sourceMinimumBytes
        || source->staticOccupiedExtentBytes != requirement.sourceOccupiedBytes
        || source->hasRuntimeArray != requirement.hasRuntimeArray
        || requirement.evidence == ShaderBufferMemoryRequirement::Evidence::None)
        return "RHI-BINDLESS-ORDINARY-RANGE-DECLARATION";
    if (source->hasRuntimeArray) {
        if (!source->runtimeArrayStrideBytes
            || source->runtimeArrayStrideBytes > UINT64_MAX - source->runtimeArrayOffsetBytes
            || requirement.sourceRuntimeArrayOffsetBytes != source->runtimeArrayOffsetBytes
            || requirement.sourceRuntimeArrayStrideBytes != source->runtimeArrayStrideBytes
            || requirement.sourceMinimumBytes
                != source->runtimeArrayOffsetBytes + source->runtimeArrayStrideBytes)
            return "RHI-BINDLESS-ORDINARY-RANGE-DECLARATION";
    } else if (requirement.sourceMinimumBytes != source->staticOccupiedExtentBytes)
        return "RHI-BINDLESS-ORDINARY-RANGE-DECLARATION";
    const shader::ShaderBinding* reflected = nullptr;
    for (const auto& candidate : selected->reflection.reflection.bindings)
        if (candidate.set == set && candidate.binding == binding)
            reflected = &candidate;
    const auto type = reflected ? descriptorTypeFromShaderBinding(reflected->type)
                                : std::optional<DescriptorType>{};
    if (!type || (source->storageClass != 2u && source->storageClass != 12u))
        return "RHI-BINDLESS-ORDINARY-RANGE-TYPE";
    BindlessResourceAccess resolved;
    std::size_t resolvedSource = SIZE_MAX;
    if (const auto* id = resolveOrdinaryBufferMemory(*root,
            set,
            binding,
            *type,
            stage,
            requirement.minimumBytes,
            requirement.sourceMinimumBytes,
            requirement.alignmentBytes,
            true,
            resolved,
            resolvedSource))
        return id;
    if (requirement.hasRuntimeArray) {
        requirement.sourceElementCapacity =
            (resolved.bufferSize - requirement.sourceRuntimeArrayOffsetBytes)
            / requirement.sourceRuntimeArrayStrideBytes;
        if (requirement.targetRuntimeArrayStrideBytes
            && (resolved.bufferSize < requirement.targetRuntimeArrayOffsetBytes
                || requirement.sourceElementCapacity
                    != (resolved.bufferSize - requirement.targetRuntimeArrayOffsetBytes)
                        / requirement.targetRuntimeArrayStrideBytes))
            return "RHI-BINDLESS-ORDINARY-RANGE-CAPACITY";
    }
    output = requirement;
    access = resolved;
    sourceIndex = resolvedSource;
    return nullptr;
}

const char* OriginalCommandBindlessRootSource::originalOrdinaryBufferMemberMemory(
    const std::shared_ptr<const OriginalCommandBindlessRootSource>& root,
    core::StringView entry,
    std::uint32_t set,
    std::uint32_t binding,
    ShaderBufferMemberMemory& output,
    BindlessResourceAccess& access,
    std::size_t& sourceIndex,
    std::uint32_t version) noexcept
{
    output = {};
    access = {};
    sourceIndex = SIZE_MAX;
    if (version != 1u && version != 2u)
        return "RHI-BINDLESS-MEMBER-MEMORY-VERSION";
    if (const auto* id = validateOriginal(root))
        return id;
    return readValidatedOrdinaryBufferMemberMemory(
        root, entry, set, binding, output, access, sourceIndex, version);
}

const char* OriginalCommandBindlessRootSource::readValidatedOrdinaryBufferMemberMemory(
    const std::shared_ptr<const OriginalCommandBindlessRootSource>& root,
    core::StringView entry,
    std::uint32_t set,
    std::uint32_t binding,
    ShaderBufferMemberMemory& output,
    BindlessResourceAccess& access,
    std::size_t& sourceIndex,
    std::uint32_t version) noexcept
{
    output = {};
    access = {};
    sourceIndex = SIZE_MAX;
    if (version != 1u && version != 2u)
        return "RHI-BINDLESS-MEMBER-MEMORY-VERSION";
    ShaderBufferMemoryRequirement range;
    BindlessResourceAccess resolved;
    std::size_t originalSource = SIZE_MAX;
    if (const auto* id = readValidatedOrdinaryBufferRequirement(
            root, entry, set, binding, range, resolved, originalSource, version))
        return id;
    const auto& prepared =
        std::get<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(root->prepared_);
    const auto& pipeline = *prepared->owners()->desc().pipeline;
    const auto decision = semantics::OriginalBindlessGraphicsMemberMemoryLowering::lowering(
        pipeline.validationReport().backend, version);
    if (!decision.available || decision.diagnostic)
        return decision.diagnostic;
    ShaderStage stage = ShaderStage::None;
    const char* diagnostic = nullptr;
    if (!selectBindlessGraphicsEntryAccess(
            root->authorization_->package(), pipeline, entry, stage, diagnostic))
        return diagnostic;
    ShaderBufferMemberMemory facts;
    if (const auto* id =
            pipeline.bindlessOrdinaryBufferMemberMemory(stage, set, binding, facts, version))
        return id;
    if ((!range.hasRuntimeArray && !facts.sourceOccupiedBytes) || !facts.targetMinimumBytes
        || facts.evidence != decision.evidence
        || facts.sourceOccupiedBytes != range.sourceOccupiedBytes
        || facts.targetMinimumBytes != range.minimumBytes
        || resolved.bufferSize < facts.sourceOccupiedBytes
        || resolved.bufferSize < facts.targetMinimumBytes)
        return "RHI-BINDLESS-MEMBER-MEMORY-DECLARATION";
    if (version == 2u) {
        if (!facts.sourceMinimumBytes || facts.sourceMinimumBytes != range.sourceMinimumBytes
            || facts.hasRuntimeArray != range.hasRuntimeArray
            || facts.sourceRuntimeArrayOffsetBytes != range.sourceRuntimeArrayOffsetBytes
            || facts.sourceRuntimeArrayStrideBytes != range.sourceRuntimeArrayStrideBytes
            || facts.targetRuntimeArrayOffsetBytes != range.targetRuntimeArrayOffsetBytes
            || facts.targetRuntimeArrayStrideBytes != range.targetRuntimeArrayStrideBytes
            || facts.sourceElementCapacity
            || (facts.hasRuntimeArray
                && (!facts.targetRuntimeArrayStrideBytes || !range.sourceElementCapacity)))
            return "RHI-BINDLESS-MEMBER-MEMORY-DECLARATION";
        facts.sourceElementCapacity = range.sourceElementCapacity;
    }
    output = facts;
    access = resolved;
    sourceIndex = originalSource;
    return nullptr;
}

const char* OriginalCommandBindlessRootSource::validateOriginalGraphicsBufferRanges(
    const std::shared_ptr<const OriginalCommandBindlessRootSource>& root,
    std::uint32_t version) noexcept
{
    if (version != 1u)
        return "RHI-BINDLESS-ORDINARY-RANGE-VERSION";
    if (const auto* id = validateOriginal(root))
        return id;
    return validateScopedGraphicsBufferRanges(root, version);
}

const char* OriginalCommandBindlessRootSource::validateScopedGraphicsBufferRanges(
    const std::shared_ptr<const OriginalCommandBindlessRootSource>& root,
    std::uint32_t version) noexcept
{
    if (version != 1u)
        return "RHI-BINDLESS-ORDINARY-RANGE-VERSION";
    const auto* prepared =
        std::get_if<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(&root->prepared_);
    if (!prepared || !*prepared)
        return "RHI-BINDLESS-ORDINARY-RANGE-PROFILE";
    if (root->ordinaryDomains_.empty())
        return nullptr;
    const auto& pipeline = *(*prepared)->owners()->desc().pipeline;
    const auto decision = semantics::OriginalBindlessOrdinaryBufferRangeLowering::lowering(
        pipeline.validationReport().backend, version);
    if (!decision.available || decision.diagnostic)
        return decision.diagnostic;
    const auto& package = root->authorization_->package();
    for (const auto stage : {ShaderStage::Vertex, ShaderStage::Fragment}) {
        if ((pipeline.bindlessShaderStages() & stage) == ShaderStage::None)
            continue;
        const auto* pointer = pipeline.bindlessShaderModule(stage, 1u);
        // Resolve membership by address before reading any candidate fields.
        const BindlessPackageModule* selected = nullptr;
        for (const auto& original : package.modules())
            if (&original == pointer)
                selected = &original;
        if (!selected)
            return "RHI-BINDLESS-GRAPHICS-ENTRY-OWNER";
        ShaderStage selectedStage = ShaderStage::None;
        const char* diagnostic = nullptr;
        if (!selectBindlessGraphicsEntryAccess(
                package, pipeline, selected->entry, selectedStage, diagnostic))
            return diagnostic;
        if (selectedStage != stage)
            return "RHI-BINDLESS-GRAPHICS-ENTRY-STAGE";
        // Walk authenticated reflection rather than just the available proofs:
        // an omitted proof must not make an active ordinary buffer disappear.
        for (const auto& binding : selected->reflection.reflection.bindings) {
            const auto type = descriptorTypeFromShaderBinding(binding.type);
            if (binding.set == package.artifact().set || !type
                || (*type != DescriptorType::UniformBuffer
                    && *type != DescriptorType::ReadOnlyStorageBuffer
                    && *type != DescriptorType::StorageBuffer))
                continue;
            ShaderBufferMemoryRequirement requirement;
            BindlessResourceAccess access;
            std::size_t source = SIZE_MAX;
            if (const auto* id = readValidatedOrdinaryBufferRequirement(root,
                    selected->entry,
                    binding.set,
                    binding.binding,
                    requirement,
                    access,
                    source,
                    2u))
                return id;
            if (selected->nativeMemory.version()) {
                ShaderBufferMemberMemory members;
                if (const auto* id = readValidatedOrdinaryBufferMemberMemory(root,
                        selected->entry,
                        binding.set,
                        binding.binding,
                        members,
                        access,
                        source,
                        requirement.hasRuntimeArray ? 2u : 1u))
                    return id;
            }
        }
    }
    return nullptr;
}

bool OriginalCommandBindlessRootSource::matchesCanonicalSources(
    const OriginalCommandResourceSources& sources) const noexcept
{
    const auto owner = inventory_.lock();
    if (!owner || owner.get() != &sources || !owner->hasQuotaBoundSharedOwner(owner))
        return false;
    for (const auto& element : elements_)
        if (!OriginalCommandDescriptorDependencies::matches(
                sources, element.declaration.sourceIndex, element.owner))
            return false;
    for (const auto& domain : ordinaryDomains_) {
        if (domain.sourceIndex >= sources.sources().size())
            return false;
        const auto* original =
            std::get_if<std::shared_ptr<const OriginalCommandDescriptorSetSource>>(
                &sources.sources()[domain.sourceIndex]);
        if (!original || !core::SharedOwnerProjection::sameOwner(*original, domain.source)
            || !domain.source->matchesCanonicalSources(sources))
            return false;
    }
    return true;
}

std::uint32_t OriginalCommandBindlessRootSource::originalShaderEntryAccessVersion() const noexcept
{
    if (authorization_ && authorization_->package().gpuVisibilityConsumerContractVersion() == 2u)
        return std::holds_alternative<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(
                   prepared_)
            ? 2u
            : 3u;
    if (authorization_) {
        const auto* prepared =
            std::get_if<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(&prepared_);
        if (prepared && *prepared && (*prepared)->owners()
            && (*prepared)->owners()->desc().pipeline) {
            const auto& pipeline = *(*prepared)->owners()->desc().pipeline;
            const auto* vp = pipeline.bindlessShaderModule(ShaderStage::Vertex, 1u);
            const auto* fp = pipeline.bindlessShaderModule(ShaderStage::Fragment, 1u);
            const BindlessPackageModule *vertex = nullptr, *fragment = nullptr;
            for (const auto& module : authorization_->package().modules()) {
                if (&module == vp)
                    vertex = &module;
                if (&module == fp)
                    fragment = &module;
            }
            if (vertex && fragment) {
                const auto selected = semantics::BindlessInitializedGraphicsContract::select(
                    vertex->initializedGraphics, fragment->initializedGraphics);
                if (selected.recognized && !selected.diagnostic)
                    return 4u;
            }
        }
    }
    return authorization_
            && semantics::OriginalBindlessPreparationLowering::hasImageCellBindingPlan(
                authorization_->plan())
        ? 1u
        : 0u;
}

const char* OriginalCommandBindlessRootSource::readOriginalShaderDeclaration(std::size_t index,
    ShaderStage stages,
    BindlessResourceAccess& out,
    std::size_t& declaration,
    bool& active,
    std::uint32_t version) const noexcept
{
    active = false;
    out = {};
    declaration = SIZE_MAX;
    if (version != 1u && version != 2u && version != 3u && version != 4u)
        return "RHI-BINDLESS-ENTRY-ACCESS-VERSION";
    if (const auto* id = validate())
        return id;
    return readValidatedShaderDeclaration(index, stages, out, declaration, active, version);
}

const char* OriginalCommandBindlessRootSource::readValidatedShaderDeclaration(std::size_t index,
    ShaderStage stages,
    BindlessResourceAccess& out,
    std::size_t& declaration,
    bool& active,
    std::uint32_t version) const noexcept
{
    active = false;
    out = {};
    declaration = SIZE_MAX;
    if (version != 1u && version != 2u && version != 3u && version != 4u)
        return "RHI-BINDLESS-ENTRY-ACCESS-VERSION";
    if (version == 2u || version == 3u || version == 4u) {
        if (originalShaderEntryAccessVersion() != version || stages == ShaderStage::None
            || (version == 3u ? stages != ShaderStage::Compute
                              : (stages & (ShaderStage::Vertex | ShaderStage::Fragment)) != stages))
            return "RHI-BINDLESS-ENTRY-ACCESS-PROFILE";
        const auto* graphics =
            std::get_if<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(&prepared_);
        const auto* compute =
            std::get_if<std::shared_ptr<const BindlessPreparedComputeBinding>>(&prepared_);
        if (version != 3u ? !graphics || !*graphics || !(*graphics)->owners()
                    || !(*graphics)->owners()->desc().pipeline
                          : !compute || !*compute || !(*compute)->owners()
                    || !(*compute)->owners()->desc().pipeline)
            return "RHI-BINDLESS-ENTRY-ACCESS-PIPELINE";
        std::uint32_t set = authorization_->package().artifact().set, binding = 0u;
        bool found = false;
        auto remaining = index;
        const auto select = [&](const auto& elements, std::uint32_t logicalSet) noexcept {
            for (const auto& element : elements) {
                if (element.declaration.write.type == DescriptorType::Sampler)
                    continue;
                if (remaining) {
                    --remaining;
                    continue;
                }
                set = logicalSet;
                binding = element.declaration.write.binding;
                found = true;
                return;
            }
        };
        select(elements_, set);
        for (const auto& domain : ordinaryDomains_)
            if (!found)
                select(domain.source->frozenElements(), domain.set);
        if (!found) {
            // readValidatedDeclaration places the explicit copy destination
            // after the ordinary domains; it is never a shader declaration.
            return !remaining && snapshot_->faultReadbackBuffer()
                ? nullptr
                : "RHI-BINDLESS-ENTRY-ACCESS-DECLARATION";
        }
        BindlessResourceAccess candidate;
        std::size_t source = SIZE_MAX;
        if (!readValidatedDeclaration(index, candidate, source))
            return "RHI-BINDLESS-ENTRY-ACCESS-DECLARATION";
        ShaderStage selectedStages = ShaderStage::None;
        for (const auto stage :
            {ShaderStage::Vertex, ShaderStage::Fragment, ShaderStage::Compute}) {
            if ((stages & stage) == ShaderStage::None)
                continue;
            const char* id = nullptr;
            const BindlessPackageModule* module = nullptr;
            if (version == 3u) {
                module = selectBindlessComputeEntryAccess(authorization_->package(),
                    (*compute)->owners()->desc().pipeline->shaderReflection(),
                    id);
                if (!module)
                    return id ? id : "RHI-BINDLESS-ENTRY-ACCESS-PIPELINE";
            } else {
                const auto& pipeline = *(*graphics)->owners()->desc().pipeline;
                module = pipeline.bindlessShaderModule(stage, 1u);
                if (!module)
                    return "RHI-BINDLESS-ENTRY-ACCESS-PIPELINE";
                ShaderStage actualStage = ShaderStage::None;
                const auto* original = selectBindlessGraphicsEntryAccess(
                    authorization_->package(), pipeline, module->entry, actualStage, id);
                if (!original || original != module || actualStage != stage)
                    return id ? id : "RHI-BINDLESS-ENTRY-ACCESS-PIPELINE";
            }
            bool consumed = false;
            if (const auto* failure = semantics::OriginalBindlessEntryAccessContract::binding(
                    module->reflection.reflection,
                    authorization_->package().artifact().execution,
                    set,
                    binding,
                    stage,
                    consumed))
                return failure;
            if (consumed)
                selectedStages = selectedStages | stage;
        }
        if (selectedStages != ShaderStage::None) {
            candidate.stages = selectedStages;
            candidate.pipelineStages = bindlessPipelineStagesForShader(selectedStages);
            out = candidate;
            declaration = source;
            active = true;
        }
        return nullptr;
    }
    if (originalShaderEntryAccessVersion() != version || stages != ShaderStage::Compute
        || !ordinaryDomains_.empty())
        return "RHI-BINDLESS-ENTRY-ACCESS-PROFILE";
    const auto* prepared =
        std::get_if<std::shared_ptr<const BindlessPreparedComputeBinding>>(&prepared_);
    if (!prepared || !*prepared)
        return "RHI-BINDLESS-ENTRY-ACCESS-PIPELINE";
    const char* id = nullptr;
    const auto* module = selectBindlessComputeEntryAccess(
        authorization_->package(), (*prepared)->owners()->desc().pipeline->shaderReflection(), id);
    if (!module)
        return id;
    if (!readValidatedDeclaration(index, out, declaration))
        return "RHI-BINDLESS-ENTRY-ACCESS-DECLARATION";
    for (const auto& element : elements_) {
        if (element.declaration.write.type == DescriptorType::Sampler)
            continue;
        if (index) {
            --index;
            continue;
        }
        id = semantics::OriginalBindlessEntryAccessContract::binding(module->reflection.reflection,
            authorization_->package().artifact().execution,
            authorization_->package().artifact().set,
            element.declaration.write.binding,
            stages,
            active);
        if (id)
            return id;
        if (active) {
            out.stages = stages;
            out.pipelineStages = bindlessPipelineStagesForShader(stages);
        }
        return nullptr;
    }
    // The retained fault readback destination is consumed by an explicit copy,
    // never by the shader entry which accesses the separate fault latch.
    return nullptr;
}

std::shared_ptr<const OriginalCompletedDepthTextureSource>
OriginalCommandBindlessRootSource::originalCompletedDepthAt(std::size_t element) const noexcept
{
    if (!snapshot_ || !snapshot_->set_)
        return {};
    if (element >= elements_.size()) {
        auto ordinal = element - elements_.size();
        if (snapshot_->faultReadbackBuffer()) {
            if (!ordinal)
                return {};
            --ordinal;
        }
        for (const auto& domain : ordinaryDomains_) {
            const auto& frozen = domain.source->frozenElements();
            if (ordinal < frozen.size())
                return frozen[ordinal].completedDepth;
            ordinal -= frozen.size();
        }
        return {};
    }
    const auto& write = elements_[element].declaration.write;
    const auto physical =
        descriptorElementIndex(snapshot_->set_->layout(), write.binding, write.arrayElement);
    return physical < snapshot_->completedTextures_.size()
        ? snapshot_->completedTextures_[physical].depth
        : nullptr;
}

const char* OriginalCommandBindlessRootSource::validate() const noexcept
{
    if (const auto* id = validateBudget(budget_))
        return id;
    if (!originalMetadataQuota() || originalMetadataQuota()->limitBytes() != budget_.sourceCpuBytes)
        return "RHI-ORIGINAL-BINDLESS-ROOT-QUOTA";
    if (!snapshot_ || !snapshot_->sharedOwnerMatches(snapshot_) || !snapshot_->sealed_
        || !snapshot_->set_ || !snapshot_->arena_
        || !snapshot_->arena_->sharedOwnerMatches(snapshot_->arena_))
        return "RHI-ORIGINAL-BINDLESS-ROOT-SNAPSHOT";
    // The original registered lifetime is a prerequisite for every retained
    // descriptor/content observation. Refuse a terminal lifetime through its
    // stable ID before a legacy ValidationResult can allocate diagnostic text.
    if (const auto* id = snapshot_->set_->originalResourceRegistration().diagnostic())
        return id;
    if (!snapshot_->validate().success || snapshot_->generation() != generation_)
        return "RHI-ORIGINAL-BINDLESS-ROOT-SNAPSHOT-STALE";
    if (!snapshot_->set_->isCurrentAllocation()
        || snapshot_->set_->contentRevision() != contentRevision_)
        return "RHI-ORIGINAL-BINDLESS-ROOT-DESCRIPTOR-STALE";
    if (!authorization_ || !authorization_->current(authorization_)
        || !bindlessPlansMatch(authorization_->plan(), snapshot_->plan()))
        return "RHI-ORIGINAL-BINDLESS-ROOT-AUTHORIZATION";
    const auto* bindingId = std::visit(
        [&](const auto& prepared) -> const char* {
            if (!prepared || !prepared->originalFactoryOwnerMatches(prepared)
                || !prepared->validateOwners().success || !prepared->validatePayload().success)
                return "RHI-ORIGINAL-BINDLESS-ROOT-PREPARED";
            const auto& owners = prepared->owners();
            if (!core::SharedOwnerProjection::sameOwner(owners->desc().snapshot, snapshot_)
                || !core::SharedOwnerProjection::sameOwner(
                    prepared->authorization(), authorization_)
                || !owners->originalCountLease() || !owners->originalByteLease()
                || !owners->originalCountLease()->charged()
                || !owners->originalByteLease()->charged())
                return "RHI-ORIGINAL-BINDLESS-ROOT-PREPARED";
            return nullptr;
        },
        prepared_);
    if (bindingId)
        return bindingId;
    const auto inventory = inventory_.lock();
    if (const auto* id = OriginalCommandDescriptorDependencies::validateInventoryOwner(
            inventory, originalMetadataQuota()))
        return id;
    if (!matchesCanonicalSources(*inventory))
        return "RHI-ORIGINAL-BINDLESS-ROOT-INVENTORY";
    if (const auto* id = validateOrdinaryDomains(*authorization_,
            *inventory,
            ordinaryDomains_.empty() ? nullptr : ordinaryDomains_.data(),
            ordinaryDomains_.size(),
            budget_))
        return id;
    for (const auto& element : elements_) {
        const char* observationDiagnostic = nullptr;
        const auto written =
            snapshot_->set_->observeSuccessfulBindingWrite(element.declaration.write.binding,
                element.declaration.write.arrayElement,
                observationDiagnostic);
        if (!written || !descriptorBindingWriteMatches(written->write(), element.declaration.write))
            return "RHI-ORIGINAL-BINDLESS-ROOT-WRITE";
        if (const auto* id = OriginalCommandDescriptorDependencies::validateOwner(
                element.owner, element.declaration.registrationGeneration, originalMetadataQuota()))
            return id;
    }
    return nullptr;
}

const char* OriginalCommandBindlessRootSource::validateOriginal(
    const std::shared_ptr<const OriginalCommandBindlessRootSource>& self) noexcept
{
    if (!self || !self->hasQuotaBoundSharedOwner(self))
        return "RHI-ORIGINAL-BINDLESS-ROOT-OWNER";
    return self->validate();
}

const char* OriginalCommandBindlessRootSource::validateOriginal(
    const std::shared_ptr<const OriginalCommandBindlessRootSource>& self,
    const GpuDevice& device) noexcept
{
    if (const auto* id = validateOriginal(self))
        return id;
    if (const auto* id = device.validateOriginalResourceRegistration(*self->originalArena()))
        return id;
    if (const auto* id = device.validateOriginalResourceRegistration(*self->originalObject()))
        return id;
    if (!self->originalObject()->originalResourceRegistration().matchesParent(
            self->originalArena()->originalResourceRegistration()))
        return "RHI-ORIGINAL-BINDLESS-ROOT-ARENA";
    if (!self->originalObject()->allocationEpochTicket().sameCurrentEpoch(
            self->originalArena()->allocationEpochTicket()))
        return "RHI-ORIGINAL-BINDLESS-ROOT-ARENA-EPOCH";
    for (const auto& element : self->elements_)
        if (const auto* id =
                OriginalCommandDescriptorDependencies::validateRegistered(element.owner, device))
            return id;
    for (const auto& domain : self->ordinaryDomains_)
        if (const auto* id =
                OriginalCommandDescriptorSetSource::validateOriginal(domain.source, device))
            return id;
    return nullptr;
}

std::shared_ptr<const OriginalCommandBindlessRootSource>
OriginalCommandBindlessRootSource::prepareOriginal(std::shared_ptr<const BindlessSnapshot> snapshot,
    Prepared prepared,
    const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
    const OriginalCommandDescriptorSetElement* elements,
    std::size_t count,
    const OriginalCommandBindlessRootSourceBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const GpuDevice& device,
    const char*& diagnostic)
{
    return prepareOriginal(std::move(snapshot),
        std::move(prepared),
        inventory,
        elements,
        count,
        nullptr,
        0u,
        budget,
        std::move(quota),
        device,
        diagnostic);
}

std::shared_ptr<const OriginalCommandBindlessRootSource>
OriginalCommandBindlessRootSource::prepareOriginal(std::shared_ptr<const BindlessSnapshot> snapshot,
    Prepared prepared,
    const std::shared_ptr<const OriginalCommandResourceSources>& inventory,
    const OriginalCommandDescriptorSetElement* elements,
    std::size_t count,
    const OrdinaryDomain* ordinary,
    std::size_t ordinaryCount,
    const OriginalCommandBindlessRootSourceBudget& budget,
    std::shared_ptr<core::CpuMemoryQuota> quota,
    const GpuDevice& device,
    const char*& diagnostic)
{
    diagnostic = validateBudget(budget);
    if (diagnostic)
        return {};
    if ((diagnostic = semantics::OriginalBindlessRootSourceLowering::lowering(
             device.backend(), budget.contractVersion)
                .diagnostic))
        return {};
    if (!quota || quota->limitBytes() != budget.sourceCpuBytes) {
        diagnostic = "RHI-ORIGINAL-BINDLESS-ROOT-QUOTA";
        return {};
    }
    if (!snapshot || !snapshot->sharedOwnerMatches(snapshot) || !snapshot->sealed_
        || !snapshot->set_ || !snapshot->arena_ || !snapshot->validate().success) {
        diagnostic = "RHI-ORIGINAL-BINDLESS-ROOT-SNAPSHOT";
        return {};
    }
    if (count > budget.maxPhysicalElements || (count && !elements)) {
        diagnostic = "RHI-ORIGINAL-BINDLESS-ROOT-ELEMENTS";
        return {};
    }
    if (std::visit(
            [&](const auto& binding) {
                return !binding || !binding->originalFactoryOwnerMatches(binding)
                    || !binding->validateOwners().success || !binding->validatePayload().success
                    || !core::SharedOwnerProjection::sameOwner(
                        binding->owners()->desc().snapshot, snapshot);
            },
            prepared)) {
        diagnostic = "RHI-ORIGINAL-BINDLESS-ROOT-PREPARED";
        return {};
    }
    if ((diagnostic =
                OriginalCommandDescriptorDependencies::validateInventory(inventory, quota, device)))
        return {};
    const auto authorization = std::visit(
        [](const auto& binding) {
            return binding->authorization();
        },
        prepared);
    if (!authorization || !authorization->current(authorization)
        || !bindlessPlansMatch(authorization->plan(), snapshot->plan())) {
        diagnostic = "RHI-ORIGINAL-BINDLESS-ROOT-AUTHORIZATION";
        return {};
    }
    if ((diagnostic = validateOrdinaryDomains(
             *authorization, *inventory, ordinary, ordinaryCount, budget)))
        return {};
    for (std::size_t i = 0u; i < ordinaryCount; ++i)
        if ((diagnostic = OriginalCommandDescriptorSetSource::validateOriginal(
                 ordinary[i].source, device)))
            return {};
    if ((diagnostic = device.validateOriginalResourceRegistration(*snapshot->arena_)))
        return {};
    if ((diagnostic = device.validateOriginalResourceRegistration(*snapshot->set_)))
        return {};
    if ((diagnostic = OriginalCommandDescriptorDependencies::validate(
             snapshot->set_->layout(), elements, count, inventory, device, 1u)))
        return {};
    std::uint64_t expected = 0u;
    for (const auto& binding : snapshot->set_->layout().bindings)
        expected += binding.arrayCount;
    if (expected != count) {
        diagnostic = "RHI-ORIGINAL-BINDLESS-ROOT-COMPLETE-CLOSURE";
        return {};
    }
    for (std::size_t i = 0u; i < count; ++i) {
        const auto& element = elements[i];
        const char* observationDiagnostic = nullptr;
        const auto written = snapshot->set_->observeSuccessfulBindingWrite(
            element.write.binding, element.write.arrayElement, observationDiagnostic);
        if (!written || !descriptorBindingWriteMatches(written->write(), element.write)) {
            diagnostic = "RHI-ORIGINAL-BINDLESS-ROOT-WRITE";
            return {};
        }
        const auto index = descriptorElementIndex(
            snapshot->set_->layout(), element.write.binding, element.write.arrayElement);
        const auto& access = snapshot->accesses_[index];
        if (const auto& source = snapshot->originalBufferRanges_[index].source) {
            const auto& original = inventory->sources()[element.sourceIndex];
            const auto* buffer =
                std::get_if<std::shared_ptr<const OriginalCommandBufferLease>>(&original);
            if (!buffer || !core::SharedOwnerProjection::sameOwner(source, *buffer)) {
                diagnostic = "RHI-ORIGINAL-BINDLESS-ROOT-BUFFER-RANGE-SOURCE";
                return {};
            }
        }
        const auto* binding =
            findDescriptorSetBinding(snapshot->set_->layout(), element.write.binding);
        const auto sampler = element.write.type == DescriptorType::Sampler;
        if (element.dynamicOffset
            || element.stages
                != (sampler ? bindlessPipelineStagesForShader(binding->stages)
                            : access.pipelineStages)
            || element.access != (sampler ? ResourceAccess::None : access.access)
            || (access.kind == BindlessAccessKind::Buffer && access.resource.isValid()
                && (element.bufferAccessOffset != access.bufferOffset
                    || element.bufferAccessBytes != access.bufferSize))) {
            diagnostic = "RHI-ORIGINAL-BINDLESS-ROOT-ACCESS";
            return {};
        }
    }
    for (std::size_t i = 0u; i < snapshot->originalBufferSources_.size(); ++i) {
        if (snapshot->internalBuffer(static_cast<BindlessSnapshot::OriginalBufferRole>(i))) {
            const auto& source = snapshot->originalBufferSources_[i];
            if (!source || inventory->originalLeaseIndex(source) == SIZE_MAX) {
                diagnostic = "RHI-ORIGINAL-BINDLESS-ROOT-INTERNAL-SOURCE";
                return {};
            }
        }
    }
    try {
        core::CpuMemoryQuotaScope scope(quota);
        auto result = core::makeUnique<OriginalCommandBindlessRootSource>(
            ConstructionKey{}, std::move(snapshot), std::move(prepared), inventory, budget);
        result->elements_.reserve(count);
        for (std::size_t i = 0u; i < count; ++i) {
            auto owner =
                OriginalCommandDescriptorDependencies::capture(*inventory, elements[i].sourceIndex);
            if (!owner) {
                diagnostic = "RHI-ORIGINAL-BINDLESS-ROOT-DEPENDENCY";
                return {};
            }
            result->elements_.push_back({elements[i], std::move(*owner)});
        }
        result->ordinaryDomains_.reserve(ordinaryCount);
        for (std::size_t i = 0u; i < ordinaryCount; ++i)
            result->ordinaryDomains_.push_back(ordinary[i]);
        auto shared = shareOriginal(std::move(result));
        diagnostic = validateOriginal(shared, device);
        return diagnostic ? nullptr : shared;
    } catch (const std::bad_alloc&) {
        diagnostic = "RHI-ORIGINAL-BINDLESS-ROOT-ALLOCATION";
        return {};
    } catch (const std::length_error&) {
        diagnostic = "RHI-ORIGINAL-BINDLESS-ROOT-ALLOCATION";
        return {};
    }
}
} // namespace arp::rhi
