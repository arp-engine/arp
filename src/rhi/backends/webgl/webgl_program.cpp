#include "core/hash/sha256.h"
#include "core/text/number_format.h"
#include "rhi/backends/webgl/webgl_program_internal.h"

#include <algorithm>

#ifdef __EMSCRIPTEN__
#include <GLES2/gl2.h>
#endif

namespace arp::rhi::webgl {

const char* WebGlResourceDomain::shaderAdmission(
    const shader::WebGlShaderProgramArtifact& artifact) const noexcept
{
    if (const auto* id = artifact.assess(runtime->shaderLimits()))
        return id;
    for (const auto* stage : {&artifact.vertex(), &artifact.fragment()})
        for (const auto& extension : stage->extensions) {
            if (extension == "GL_OES_standard_derivatives" && !extensionDecision.native.derivatives)
                return "RHI-WEBGL1-EXTENSION-EXECUTION-DERIVATIVES-CLOSED";
            if (extension == "GL_EXT_frag_depth" && !extensionDecision.native.fragmentDepth)
                return "RHI-WEBGL1-EXTENSION-EXECUTION-FRAGMENT-DEPTH-CLOSED";
        }
    return nullptr;
}

WebGlProgram::WebGlProgram(ConstructionKey, core::UniquePtr<Impl> impl) : impl_(std::move(impl))
{}

WebGlProgram::~WebGlProgram() = default;

GpuResourceId WebGlProgram::id() const noexcept
{
    return impl_->object->id;
}

const std::shared_ptr<const shader::WebGlShaderProgramArtifact>&
WebGlProgram::artifact() const noexcept
{
    return impl_->artifact;
}

const char* WebGlProgram::currentDiagnostic() const noexcept
{
    if (const auto* id = impl_->object->domain->currentDiagnostic())
        return id;
    if (const auto* id = shader::WebGlShaderProgramArtifact::validateOriginal(impl_->artifact))
        return id;
    return impl_->object->current() ? nullptr : "RHI-WEBGL1-PROGRAM-OWNER-RETIRED";
}

#ifdef __EMSCRIPTEN__
namespace {
using Uniform = WebGlProgram::Impl::Uniform;

GLenum uniformType(const shader::WebGlUniformLeaf& leaf) noexcept
{
    if (leaf.columns == 4 && leaf.components == 4 && !leaf.integer)
        return GL_FLOAT_MAT4;
    if (leaf.columns != 1 || leaf.components < 1 || leaf.components > 4)
        return 0;
    constexpr std::array<GLenum, 4> floating{GL_FLOAT, GL_FLOAT_VEC2, GL_FLOAT_VEC3, GL_FLOAT_VEC4};
    constexpr std::array<GLenum, 4> integer{GL_INT, GL_INT_VEC2, GL_INT_VEC3, GL_INT_VEC4};
    return (leaf.integer ? integer : floating)[leaf.components - 1];
}

bool sameSource(const Uniform& old,
    const shader::WebGlUniformBlock& block,
    const shader::WebGlUniformLeaf& leaf) noexcept
{
    const auto& a = *old.leaf;
    const auto& b = *old.block;
    return b.push == block.push && b.set == block.set && b.binding == block.binding
        && a.offset == leaf.offset && a.size == leaf.size && a.components == leaf.components
        && a.columns == leaf.columns && a.matrixStride == leaf.matrixStride
        && a.integer == leaf.integer;
}

Uniform* findUniform(WebGlProgram::Impl& program, std::string_view name) noexcept
{
    for (auto& uniform : program.uniforms)
        if (uniform.name == name)
            return &uniform;
    return nullptr;
}

const char* declarations(WebGlProgram::Impl& p)
{
    for (const auto* stage : {&p.artifact->vertex(), &p.artifact->fragment()})
        for (const auto& block : stage->blocks)
            for (const auto& leaf : block.leaves) {
                if (auto* old = findUniform(p, leaf.name)) {
                    if (!old->leaf || !sameSource(*old, block, leaf))
                        return "SHADER-WEBGL1-CROSS-STAGE-UNIFORM";
                    old->required = old->required || leaf.required;
                    continue;
                }
                p.uniforms.emplace_back(leaf.name);
                auto& uniform = p.uniforms.back();
                uniform.block = &block;
                uniform.leaf = &leaf;
                uniform.required = leaf.required;
                uniform.type = uniformType(leaf);
                if (!uniform.type)
                    return "SHADER-WEBGL1-UNIFORM-TYPE";
            }
    const auto& units = p.artifact->textureUnits();
    for (std::size_t i = 0; i < units.size(); ++i) {
        if (findUniform(p, units[i]))
            return "SHADER-WEBGL1-UNIFORM-ALIAS";
        p.uniforms.emplace_back(units[i]);
        auto& uniform = p.uniforms.back();
        uniform.type = GL_SAMPLER_2D;
        uniform.textureUnit = static_cast<std::uint32_t>(i);
    }
    return nullptr;
}

const char* interfaces(WebGlProgram::Impl& p, const semantics::WebGlProgramCreationRequest& request)
{
    const auto native = p.object->native;
    GLint attributes = 0, uniforms = 0, attributeBytes = 0, uniformBytes = 0;
    glGetProgramiv(native, GL_ACTIVE_ATTRIBUTES, &attributes);
    glGetProgramiv(native, GL_ACTIVE_UNIFORMS, &uniforms);
    glGetProgramiv(native, GL_ACTIVE_ATTRIBUTE_MAX_LENGTH, &attributeBytes);
    glGetProgramiv(native, GL_ACTIVE_UNIFORM_MAX_LENGTH, &uniformBytes);
    if (attributes < 0 || uniforms < 0 || attributeBytes < 0 || uniformBytes < 0
        || std::uint32_t(attributeBytes) > request.maxInterfaceNameBytes
        || std::uint32_t(uniformBytes) > request.maxInterfaceNameBytes)
        return "SHADER-WEBGL1-INTERFACE-NAME-BUDGET";
    const auto& inputs = p.artifact->vertex().inputs;
    if (static_cast<std::size_t>(attributes) != inputs.size())
        return "SHADER-WEBGL1-REQUIRED-ATTRIBUTE";
    p.attributes.assign(inputs.size(), -1);
    core::pmr::Vector<char> name{&core::defaultCpuMemoryResource()};
    name.resize(request.maxInterfaceNameBytes);
    for (GLint i = 0; i < attributes; ++i) {
        GLsizei length = 0, size = 0;
        GLenum type = 0;
        glGetActiveAttrib(
            native, i, static_cast<GLsizei>(name.size()), &length, &size, &type, name.data());
        if (length <= 0 || std::size_t(length) >= name.size())
            return "SHADER-WEBGL1-ACTIVE-ATTRIBUTE";
        const std::string_view observed{name.data(), static_cast<std::size_t>(length)};
        const auto found = std::find_if(inputs.begin(), inputs.end(), [&](const auto& a) {
            return a.name == observed;
        });
        if (found == inputs.end() || size != 1)
            return "SHADER-WEBGL1-ACTIVE-ATTRIBUTE";
        const auto location = glGetAttribLocation(native, name.data());
        shader::WebGlUniformLeaf leaf;
        leaf.columns = 1;
        leaf.components = found->components;
        const auto index = static_cast<std::size_t>(found - inputs.begin());
        if (type != uniformType(leaf) || location < 0 || std::uint32_t(location) != found->location
            || p.attributes[index] != -1)
            return "SHADER-WEBGL1-ACTIVE-ATTRIBUTE";
        p.attributes[index] = location;
    }
    for (GLint i = 0; i < uniforms; ++i) {
        GLsizei length = 0, size = 0;
        GLenum type = 0;
        glGetActiveUniform(
            native, i, static_cast<GLsizei>(name.size()), &length, &size, &type, name.data());
        if (length <= 0 || std::size_t(length) >= name.size() || size <= 0
            || static_cast<std::size_t>(size) > p.uniforms.size())
            return "SHADER-WEBGL1-ACTIVE-UNIFORM";
        const std::string_view observed{name.data(), static_cast<std::size_t>(length)};
        if (size > 1 && (observed.size() < 3 || observed.substr(observed.size() - 3) != "[0]"))
            return "SHADER-WEBGL1-ACTIVE-ARRAY";
        for (GLsizei j = 0; j < size; ++j) {
            core::pmr::String expanded{observed, &core::defaultCpuMemoryResource()};
            if (size > 1) {
                expanded.resize(expanded.size() - 3);
                expanded += '[';
                core::appendUnsigned(expanded, static_cast<std::uint32_t>(j));
                expanded += ']';
            }
            auto* uniform = findUniform(p, expanded);
            if (!uniform || uniform->active || uniform->type != type)
                return "SHADER-WEBGL1-ACTIVE-UNIFORM";
            uniform->location = glGetUniformLocation(native, expanded.c_str());
            if (uniform->location < 0)
                return "SHADER-WEBGL1-ACTIVE-UNIFORM";
            uniform->active = true;
        }
    }
    for (const auto& uniform : p.uniforms)
        if (uniform.required && !uniform.active)
            return "SHADER-WEBGL1-REQUIRED-UNIFORM";
    return nullptr;
}

struct ColdShaders final {
    explicit ColdShaders(WebGlResourceDomain& domain) : domain(domain) {}

    ~ColdShaders()
    {
        for (const auto shader : handles)
            if (shader)
                domain.deleteNative(WebGlObjectKind::Shader, shader);
    }

    WebGlResourceDomain& domain;
    std::array<GLuint, 2> handles{};
};

const char* nativeLog(WebGlResourceDomain& domain,
    GLuint object,
    bool isShader,
    const semantics::WebGlProgramCreationRequest& request,
    const shader::WebGlShaderProgramArtifact& artifact,
    const char* stage)
{
    const auto digest = [](const core::ByteBuffer& bytes) {
        return core::Sha256::hexDigest({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
    };
    core::pmr::String header{&core::defaultCpuMemoryResource()};
    header = "programIdentitySha256=";
    header += digest(artifact.identity());
    header += "\nvertexSourceSha256=";
    header += digest(artifact.vertex().glsl);
    header += "\nfragmentSourceSha256=";
    header += digest(artifact.fragment().glsl);
    header += "\nstage=";
    header += stage;
    header += "\nlog=";
    if (header.size() > request.maxDiagnosticBytes)
        return "SHADER-WEBGL1-DIAGNOSTIC-BUDGET";
    domain.nativeDiagnostic = header;
    GLint bytes = 0;
    if (isShader)
        glGetShaderiv(object, GL_INFO_LOG_LENGTH, &bytes);
    else
        glGetProgramiv(object, GL_INFO_LOG_LENGTH, &bytes);
    if (bytes < 0 || static_cast<std::uint32_t>(bytes) > request.maxDiagnosticBytes - header.size())
        return "SHADER-WEBGL1-DIAGNOSTIC-BUDGET";
    domain.nativeDiagnostic.resize(header.size() + static_cast<std::size_t>(bytes));
    if (bytes) {
        GLsizei length = 0;
        if (isShader)
            glGetShaderInfoLog(
                object, bytes, &length, domain.nativeDiagnostic.data() + header.size());
        else
            glGetProgramInfoLog(
                object, bytes, &length, domain.nativeDiagnostic.data() + header.size());
        if (length < 0 || length > bytes)
            return "SHADER-WEBGL1-DIAGNOSTIC-LENGTH";
        domain.nativeDiagnostic.resize(header.size() + static_cast<std::size_t>(length));
    }
    return isShader ? "SHADER-WEBGL1-COMPILE" : "SHADER-WEBGL1-LINK";
}
} // namespace
#endif

core::UniquePtr<WebGlProgram> WebGlResources::createProgram(
    const std::shared_ptr<const shader::WebGlShaderProgramArtifact>& artifact,
    const semantics::WebGlProgramCreationRequest& request)
{
    auto& d = *domain_;
    if ((d.lastDiagnostic =
                semantics::WebGlProgramCreationContract::validate(Backend::WebGL, request))
        || (d.lastDiagnostic = shader::WebGlShaderProgramArtifact::validateOriginal(artifact))
        || d.preflight(0, 0))
        return {};
    if ((d.lastDiagnostic = d.shaderAdmission(*artifact)))
        return {};
    try {
        core::CpuMemoryQuotaScope scope(d.metadataQuota);
        d.nativeDiagnostic.clear();
        auto impl = core::makeUnique<WebGlProgram::Impl>();
        impl->object = core::makeShared<WebGlResourcePayload>(
            WebGlResourceFactoryKey{}, domain_, WebGlObjectKind::Program);
        impl->artifact = artifact;
        auto result =
            core::makeUnique<WebGlProgram>(WebGlProgram::ConstructionKey{}, std::move(impl));
        auto& p = *result->impl_;
#ifdef __EMSCRIPTEN__
        if ((d.lastDiagnostic = declarations(p)))
            return {};
        for (const auto& uniform : p.uniforms)
            if (uniform.name.size() >= request.maxInterfaceNameBytes) {
                d.lastDiagnostic = "SHADER-WEBGL1-INTERFACE-NAME-BUDGET";
                return {};
            }
        if (d.nativeResult())
            return {};
        ColdShaders shaders(d);
        const std::array<const shader::WebGlShaderTarget*, 2> targets{
            &artifact->vertex(), &artifact->fragment()};
        for (std::size_t i = 0; i < targets.size(); ++i) {
            shaders.handles[i] = glCreateShader(i == 0 ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
            if (!shaders.handles[i]) {
                d.lastDiagnostic = "SHADER-WEBGL1-NATIVE-SHADER";
                return {};
            }
            const auto& source = targets[i]->glsl;
            if (source.size() > std::size_t{INT32_MAX}) {
                d.lastDiagnostic = "SHADER-WEBGL1-SOURCE-BUDGET";
                return {};
            }
            const auto* bytes = reinterpret_cast<const GLchar*>(source.data());
            const GLint size = static_cast<GLint>(source.size());
            glShaderSource(shaders.handles[i], 1, &bytes, &size);
            glCompileShader(shaders.handles[i]);
            GLint success = GL_FALSE;
            glGetShaderiv(shaders.handles[i], GL_COMPILE_STATUS, &success);
            if (d.nativeResult())
                return {};
            if (!success) {
                d.lastDiagnostic = nativeLog(d,
                    shaders.handles[i],
                    true,
                    request,
                    *artifact,
                    i == 0 ? "vertex" : "fragment");
                return {};
            }
        }
        p.object->native = glCreateProgram();
        if (!p.object->native) {
            d.lastDiagnostic = "SHADER-WEBGL1-NATIVE-PROGRAM";
            return {};
        }
        for (const auto shader : shaders.handles)
            glAttachShader(p.object->native, shader);
        for (const auto& a : artifact->vertex().inputs)
            glBindAttribLocation(p.object->native, a.location, a.name.c_str());
        glLinkProgram(p.object->native);
        GLint linked = GL_FALSE;
        glGetProgramiv(p.object->native, GL_LINK_STATUS, &linked);
        if (d.nativeResult())
            return {};
        if (!linked) {
            d.lastDiagnostic = nativeLog(d, p.object->native, false, request, *artifact, "link");
            return {};
        }
        if ((d.lastDiagnostic = interfaces(p, request)))
            return {};
        for (const auto shader : shaders.handles)
            glDetachShader(p.object->native, shader);
        if (d.nativeResult())
            return {};
#else
        d.lastDiagnostic = "RHI-WEBGL1-CONTEXT-PLATFORM-UNAVAILABLE";
        return {};
#endif
        if ((d.lastDiagnostic = d.publish(p.object)))
            return {};
        return result;
    } catch (const std::bad_alloc&) {
        d.lastDiagnostic = "RHI-WEBGL1-RESOURCE-CPU-ALLOCATION";
    } catch (const std::length_error&) {
        d.lastDiagnostic = "RHI-WEBGL1-RESOURCE-REGISTRY-CAPACITY";
    }
    return {};
}
} // namespace arp::rhi::webgl
