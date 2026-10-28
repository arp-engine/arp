#include "scriptvm/plugin/cpp_plugin_importer.h"

#include "core/hash/sha256.h"
#include "core/io/file_io.h"
#include "core/system/process_identity_lock.h"
#include "core/system/process_runner.h"
#include "core/text/number_format.h"
#include "core/text/string_utils.h"
#include "reflection/registration_contract_format.h"
#include "reflection/registration_transaction.h"
#include "scriptvm/plugin/compiler_toolchain_layout.h"
#include "scriptvm/plugin/cpp_plugin_api.h"
#include "scriptvm/plugin/immutable_build_input_snapshot.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <exception>
#include <fkYAML/node.hpp>
#include <fstream>
#include <functional>
#include <limits>
#include <mutex>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#if defined(_WIN32) || defined(__CYGWIN__)
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace arp::scriptvm {

namespace {

using YamlNode = fkyaml::node;

const YamlNode* findYamlValue(const YamlNode& mapping, const char* key)
{
    if (!mapping.is_mapping()) {
        return nullptr;
    }
    const auto& values = mapping.get_value_ref<const YamlNode::mapping_type&>();
    const auto found = values.find(YamlNode(key));
    return found == values.end() ? nullptr : &found->second;
}

core::pmr::String yamlString(const YamlNode& node)
{
    const auto& value = node.get_value_ref<const YamlNode::string_type&>();
    return core::pmr::String(value.data(), value.size(), &core::defaultCpuMemoryResource());
}

core::pmr::String pathDiagnostic(const char* prefix, const std::filesystem::path& path)
{
    core::pmr::String result(prefix, &core::defaultCpuMemoryResource());
    const auto text = path.generic_string();
    result.append(text.data(), text.size());
    return result;
}

void appendBoundedProcessLog(core::pmr::String& diagnostics, const std::filesystem::path& logPath)
{
    const auto log = core::readTextFile(logPath);
    if (!log.success || log.text.empty())
        return;
    constexpr std::size_t kMaxProcessLogBytes = 8192u;
    diagnostics += "\n";
    diagnostics.append(log.text.data(), (std::min)(kMaxProcessLogBytes, log.text.size()));
    if (log.text.size() > kMaxProcessLogBytes)
        diagnostics += "\n[process log truncated]";
}

struct ReflectionMetadataClassContract final {
    core::pmr::String name{&core::defaultCpuMemoryResource()};
    core::pmr::String cppName{&core::defaultCpuMemoryResource()};
    core::pmr::String moduleExport{&core::defaultCpuMemoryResource()};
    arp::reflection::RegistrationOwnership ownership =
        arp::reflection::RegistrationOwnership::Unspecified;
    arp::reflection::RegistrationLifecycle lifecycle =
        arp::reflection::RegistrationLifecycle::Unspecified;
    core::pmr::Set<core::pmr::String> fields{&core::defaultCpuMemoryResource()};
    core::pmr::Set<core::pmr::String> bases{&core::defaultCpuMemoryResource()};
    core::pmr::Map<core::pmr::String, core::pmr::String> propertyTypes{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::pmr::String> constructorParameterTypes{
        &core::defaultCpuMemoryResource()};
    bool hasConstructor = false;
};

struct ReflectionMetadataCallableContract final {
    core::pmr::String bindingId{&core::defaultCpuMemoryResource()};
    core::pmr::String targetId{&core::defaultCpuMemoryResource()};
    core::pmr::String signatureHash{&core::defaultCpuMemoryResource()};
    core::pmr::String ownerType{&core::defaultCpuMemoryResource()};
    core::pmr::String memberName{&core::defaultCpuMemoryResource()};
    core::pmr::String returnType{&core::defaultCpuMemoryResource()};
    core::pmr::String parameterList{&core::defaultCpuMemoryResource()};
    int fixedArity = -1;
    bool maySuspend = false;
    bool isStatic = false;
};

core::pmr::String sha256Contract(std::string_view value)
{
    const auto digest = core::Sha256::digest(core::StringView(value.data(), value.size()));
    static constexpr char digits[] = "0123456789abcdef";
    core::pmr::String result("sha256:", &core::defaultCpuMemoryResource());
    result.reserve(7u + digest.size() * 2u);
    for (const auto byte : digest) {
        result.push_back(digits[(byte >> 4u) & 0x0fu]);
        result.push_back(digits[byte & 0x0fu]);
    }
    return result;
}

bool yamlBoolOr(const YamlNode& node, const char* key, bool fallback = false)
{
    const auto* value = findYamlValue(node, key);
    return value != nullptr && value->is_boolean() ? value->get_value<bool>() : fallback;
}

core::pmr::String yamlTextOrEmpty(const YamlNode& node, const char* key)
{
    const auto* value = findYamlValue(node, key);
    return value != nullptr && value->is_string()
        ? yamlString(*value)
        : core::pmr::String(&core::defaultCpuMemoryResource());
}

bool appendParameterContract(
    const YamlNode& callable, core::pmr::String& seed, int& arity, core::pmr::String& diagnostics)
{
    const auto* parameters = findYamlValue(callable, "parameters");
    if (parameters == nullptr || !parameters->is_sequence()) {
        diagnostics = "generated reflection callable has no parameter contract";
        return false;
    }
    arity = static_cast<int>(parameters->get_value_ref<const YamlNode::sequence_type&>().size());
    for (const auto& parameter : parameters->get_value_ref<const YamlNode::sequence_type&>()) {
        const auto cppType = yamlTextOrEmpty(parameter, "cppType");
        const auto referenceKind = yamlTextOrEmpty(parameter, "referenceKind");
        if (cppType.empty() || referenceKind.empty()) {
            diagnostics = "generated reflection parameter contract is incomplete";
            return false;
        }
        seed += "|param:";
        seed += cppType;
        seed += ":";
        seed += referenceKind;
        seed += yamlBoolOr(parameter, "nullable") ? ":1" : ":0";
    }
    return true;
}

bool appendDescriptorParameter(
    const YamlNode& parameter, core::pmr::String& output, core::pmr::String& diagnostics)
{
    const auto name = yamlTextOrEmpty(parameter, "name");
    const auto cppType = yamlTextOrEmpty(parameter, "cppType");
    const auto referenceKind = yamlTextOrEmpty(parameter, "referenceKind");
    if (name.empty() || cppType.empty() || referenceKind.empty()) {
        diagnostics = "generated reflection descriptor parameter is incomplete";
        return false;
    }
    arp::reflection::appendRegistrationContractField(output, "name", name);
    arp::reflection::appendRegistrationContractField(output, "cpp-type", cppType);
    arp::reflection::appendRegistrationContractField(output, "reference-kind", referenceKind);
    arp::reflection::appendRegistrationContractField(output,
        "nullable",
        yamlBoolOr(parameter, "nullable") ? std::string_view("1") : std::string_view("0"));
    return true;
}

bool appendSortedYamlStrings(const YamlNode& node,
    const char* yamlKey,
    core::pmr::String& output,
    std::string_view contractKey,
    core::pmr::String& diagnostics)
{
    const auto* values = findYamlValue(node, yamlKey);
    if (values == nullptr || !values->is_sequence()) {
        diagnostics = "generated reflection descriptor is missing sequence: ";
        diagnostics += yamlKey;
        return false;
    }
    core::pmr::Vector<core::pmr::String> ordered{&core::defaultCpuMemoryResource()};
    for (const auto& value : values->get_value_ref<const YamlNode::sequence_type&>()) {
        if (!value.is_string()) {
            diagnostics = "generated reflection descriptor sequence contains a non-string value";
            return false;
        }
        ordered.push_back(yamlString(value));
    }
    std::sort(ordered.begin(), ordered.end());
    for (const auto& value : ordered)
        arp::reflection::appendRegistrationContractField(output, contractKey, value);
    return true;
}

void appendDescriptorBool(
    core::pmr::String& output, std::string_view key, const YamlNode& node, const char* yamlKey)
{
    arp::reflection::appendRegistrationContractField(
        output, key, yamlBoolOr(node, yamlKey) ? std::string_view("1") : std::string_view("0"));
}

bool buildMethodDescriptorContract(
    const YamlNode& method, core::pmr::String& output, core::pmr::String& diagnostics)
{
    const auto name = yamlTextOrEmpty(method, "name");
    const auto reflectName = yamlTextOrEmpty(method, "reflectName");
    const auto returnType = yamlTextOrEmpty(method, "returnType");
    auto access = yamlTextOrEmpty(method, "access");
    if (access.empty())
        access = "public";
    const auto referenceKind = yamlTextOrEmpty(method, "returnReferenceKind");
    if (name.empty() || reflectName.empty() || returnType.empty() || referenceKind.empty()) {
        diagnostics = "generated reflection method descriptor is incomplete";
        return false;
    }
    arp::reflection::appendRegistrationContractField(output, "name", name);
    arp::reflection::appendRegistrationContractField(output, "reflect-name", reflectName);
    arp::reflection::appendRegistrationContractField(output, "return-type", returnType);
    arp::reflection::appendRegistrationContractField(
        output, "args", yamlTextOrEmpty(method, "args"));
    arp::reflection::appendRegistrationContractField(output, "access", access);
    appendDescriptorBool(output, "static", method, "static");
    appendDescriptorBool(output, "may-suspend", method, "maySuspend");
    appendDescriptorBool(output, "const", method, "const");
    appendDescriptorBool(output, "volatile", method, "volatile");
    appendDescriptorBool(output, "noexcept", method, "noexcept");
    arp::reflection::appendRegistrationContractField(
        output, "ref-qualifier", yamlTextOrEmpty(method, "refQualifier"));
    appendDescriptorBool(output, "nullable-return", method, "nullableReturn");
    arp::reflection::appendRegistrationContractField(
        output, "return-reference-kind", referenceKind);
    const auto* parameters = findYamlValue(method, "parameters");
    if (parameters == nullptr || !parameters->is_sequence()) {
        diagnostics = "generated reflection method descriptor has no parameters";
        return false;
    }
    for (const auto& parameter : parameters->get_value_ref<const YamlNode::sequence_type&>()) {
        core::pmr::String parameterValue(&core::defaultCpuMemoryResource());
        if (!appendDescriptorParameter(parameter, parameterValue, diagnostics))
            return false;
        arp::reflection::appendRegistrationContractField(output, "parameter", parameterValue);
    }
    return appendSortedYamlStrings(method, "flags", output, "flag", diagnostics);
}

bool buildClassDescriptorContract(
    const YamlNode& cls, core::pmr::String& output, core::pmr::String& diagnostics)
{
    for (const auto* key : {"kind", "cppName", "reflectName"}) {
        const auto value = yamlTextOrEmpty(cls, key);
        if (value.empty()) {
            diagnostics = "generated reflection class descriptor is incomplete: ";
            diagnostics += key;
            return false;
        }
        const auto contractKey = std::string_view(key) == "cppName" ? "cpp-name"
            : std::string_view(key) == "reflectName"                ? "reflect-name"
                                                                    : "kind";
        arp::reflection::appendRegistrationContractField(output, contractKey, value);
    }
    appendDescriptorBool(output, "default-construct", cls, "defaultConstruct");
    const auto ownership = yamlTextOrEmpty(cls, "ownership");
    const auto lifecycle = yamlTextOrEmpty(cls, "lifecycle");
    if (ownership.empty() || lifecycle.empty()) {
        diagnostics = "generated reflection class descriptor has no ownership contract";
        return false;
    }
    arp::reflection::appendRegistrationContractField(output, "ownership", ownership);
    arp::reflection::appendRegistrationContractField(output, "lifecycle", lifecycle);
    arp::reflection::appendRegistrationContractField(
        output, "module-export", yamlTextOrEmpty(cls, "moduleExport"));
    if (!appendSortedYamlStrings(cls, "bases", output, "base", diagnostics))
        return false;
    const auto* constructors = findYamlValue(cls, "constructors");
    if (constructors == nullptr || !constructors->is_sequence()) {
        diagnostics = "generated reflection class descriptor has no constructors contract";
        return false;
    }
    core::pmr::Vector<core::pmr::String> constructorValues{&core::defaultCpuMemoryResource()};
    for (const auto& constructor : constructors->get_value_ref<const YamlNode::sequence_type&>()) {
        const auto* parameters = findYamlValue(constructor, "parameters");
        if (parameters == nullptr || !parameters->is_sequence()) {
            diagnostics = "generated reflection constructor descriptor is incomplete";
            return false;
        }
        core::pmr::String value(&core::defaultCpuMemoryResource());
        for (const auto& parameter : parameters->get_value_ref<const YamlNode::sequence_type&>()) {
            core::pmr::String parameterValue(&core::defaultCpuMemoryResource());
            if (!appendDescriptorParameter(parameter, parameterValue, diagnostics))
                return false;
            arp::reflection::appendRegistrationContractField(value, "parameter", parameterValue);
        }
        constructorValues.push_back(std::move(value));
    }
    std::sort(constructorValues.begin(), constructorValues.end());
    for (const auto& value : constructorValues)
        arp::reflection::appendRegistrationContractField(output, "constructor", value);

    const auto* properties = findYamlValue(cls, "properties");
    if (properties == nullptr || !properties->is_sequence()) {
        diagnostics = "generated reflection class descriptor has no properties contract";
        return false;
    }
    core::pmr::Vector<core::pmr::String> propertyValues{&core::defaultCpuMemoryResource()};
    for (const auto& property : properties->get_value_ref<const YamlNode::sequence_type&>()) {
        const auto name = yamlTextOrEmpty(property, "name");
        const auto cppType = yamlTextOrEmpty(property, "cppType");
        const auto access = yamlTextOrEmpty(property, "access");
        const auto source = yamlTextOrEmpty(property, "source");
        if (name.empty() || cppType.empty() || access.empty()
            || (source != "field" && source != "accessor")) {
            diagnostics = "generated reflection property descriptor is incomplete";
            return false;
        }
        core::pmr::String value(&core::defaultCpuMemoryResource());
        arp::reflection::appendRegistrationContractField(value, "name", name);
        arp::reflection::appendRegistrationContractField(value, "cpp-type", cppType);
        arp::reflection::appendRegistrationContractField(value, "access", access);
        const auto doc = yamlTextOrEmpty(property, "doc");
        if (!doc.empty())
            arp::reflection::appendRegistrationContractField(value, "doc", doc);
        arp::reflection::appendRegistrationContractField(value, "source", source);
        if (source == "accessor") {
            const auto getter = yamlTextOrEmpty(property, "getter");
            const auto setter = yamlTextOrEmpty(property, "setter");
            if (getter.empty() || setter.empty()) {
                diagnostics =
                    "generated reflection accessor property has no getter/setter contract";
                return false;
            }
            arp::reflection::appendRegistrationContractField(value, "getter", getter);
            arp::reflection::appendRegistrationContractField(value, "setter", setter);
        }
        if (!appendSortedYamlStrings(property, "flags", value, "flag", diagnostics))
            return false;
        const auto referenceTarget = yamlTextOrEmpty(property, "referenceTarget");
        if (!referenceTarget.empty())
            arp::reflection::appendRegistrationContractField(
                value, "reference-target", referenceTarget);
        propertyValues.push_back(std::move(value));
    }
    std::sort(propertyValues.begin(), propertyValues.end());
    for (const auto& value : propertyValues)
        arp::reflection::appendRegistrationContractField(output, "property", value);

    const auto* methods = findYamlValue(cls, "methods");
    if (methods == nullptr || !methods->is_sequence()) {
        diagnostics = "generated reflection class descriptor has no methods contract";
        return false;
    }
    core::pmr::Vector<core::pmr::String> methodValues{&core::defaultCpuMemoryResource()};
    for (const auto& method : methods->get_value_ref<const YamlNode::sequence_type&>()) {
        core::pmr::String value(&core::defaultCpuMemoryResource());
        if (!buildMethodDescriptorContract(method, value, diagnostics))
            return false;
        methodValues.push_back(std::move(value));
    }
    std::sort(methodValues.begin(), methodValues.end());
    for (const auto& value : methodValues)
        arp::reflection::appendRegistrationContractField(output, "method", value);
    return true;
}

bool buildFunctionDescriptorContract(
    const YamlNode& function, core::pmr::String& output, core::pmr::String& diagnostics)
{
    for (const auto& field : {std::pair<const char*, const char*>{"cppName", "cpp-name"},
             {"reflectName", "reflect-name"},
             {"returnType", "return-type"},
             {"args", "args"},
             {"defaultArguments", "default-arguments"}}) {
        const auto value = yamlTextOrEmpty(function, field.first);
        if (field.first != std::string_view("args")
            && field.first != std::string_view("defaultArguments") && value.empty()) {
            diagnostics = "generated reflection function descriptor is incomplete";
            return false;
        }
        arp::reflection::appendRegistrationContractField(output, field.second, value);
    }
    appendDescriptorBool(output, "may-suspend", function, "maySuspend");
    appendDescriptorBool(output, "nullable-return", function, "nullableReturn");
    const auto referenceKind = yamlTextOrEmpty(function, "returnReferenceKind");
    if (referenceKind.empty()) {
        diagnostics = "generated reflection function has no return reference contract";
        return false;
    }
    arp::reflection::appendRegistrationContractField(
        output, "return-reference-kind", referenceKind);
    const auto* parameters = findYamlValue(function, "parameters");
    if (parameters == nullptr || !parameters->is_sequence()) {
        diagnostics = "generated reflection function descriptor has no parameters";
        return false;
    }
    for (const auto& parameter : parameters->get_value_ref<const YamlNode::sequence_type&>()) {
        core::pmr::String value(&core::defaultCpuMemoryResource());
        if (!appendDescriptorParameter(parameter, value, diagnostics))
            return false;
        arp::reflection::appendRegistrationContractField(output, "parameter", value);
    }
    return appendSortedYamlStrings(function, "flags", output, "flag", diagnostics);
}

bool buildEnumDescriptorContract(
    const YamlNode& enumeration, core::pmr::String& output, core::pmr::String& diagnostics)
{
    const auto cppName = yamlTextOrEmpty(enumeration, "cppName");
    const auto reflectName = yamlTextOrEmpty(enumeration, "reflectName");
    if (cppName.empty() || reflectName.empty()) {
        diagnostics = "generated reflection enum descriptor is incomplete";
        return false;
    }
    arp::reflection::appendRegistrationContractField(output, "cpp-name", cppName);
    arp::reflection::appendRegistrationContractField(output, "reflect-name", reflectName);
    return appendSortedYamlStrings(enumeration, "values", output, "value", diagnostics);
}

core::pmr::String receiptMemberName(const core::pmr::String& contract)
{
    const auto separator = contract.find(':');
    const auto end = contract.find('|', separator == core::pmr::String::npos ? 0u : separator + 1u);
    if (separator == core::pmr::String::npos)
        return {};
    const auto begin = separator + 1u;
    return contract.substr(
        begin, end == core::pmr::String::npos ? core::pmr::String::npos : end - begin);
}

core::pmr::String receiptMemberField(const core::pmr::String& contract, std::string_view field)
{
    core::pmr::String marker("|", &core::defaultCpuMemoryResource());
    marker.append(field.data(), field.size());
    marker.push_back(':');
    const auto begin = contract.find(marker);
    if (begin == core::pmr::String::npos)
        return {};
    const auto valueBegin = begin + marker.size();
    const auto end = contract.find('|', valueBegin);
    return contract.substr(
        valueBegin, end == core::pmr::String::npos ? core::pmr::String::npos : end - valueBegin);
}

core::pmr::String metadataRuntimeTypeContract(const core::pmr::String& cppType,
    const core::pmr::Map<core::pmr::String, core::pmr::String>& reflectedNames)
{
    return arp::reflection::canonicalRegistrationTypeContract(cppType, reflectedNames);
}

core::pmr::String metadataParameterList(const YamlNode& callable,
    const core::pmr::Map<core::pmr::String, core::pmr::String>& reflectedNames,
    core::pmr::String& diagnostics)
{
    const auto* parameters = findYamlValue(callable, "parameters");
    if (parameters == nullptr || !parameters->is_sequence()) {
        diagnostics = "generated reflection callable has no parameters";
        return {};
    }
    core::pmr::String result(&core::defaultCpuMemoryResource());
    for (const auto& parameter : parameters->get_value_ref<const YamlNode::sequence_type&>()) {
        const auto cppType = yamlTextOrEmpty(parameter, "cppType");
        if (cppType.empty()) {
            diagnostics = "generated reflection parameter has no C++ type";
            return {};
        }
        if (!result.empty())
            result.push_back(',');
        result += metadataRuntimeTypeContract(cppType, reflectedNames);
    }
    return result;
}

bool readReflectionNameSet(const YamlNode& root,
    const char* key,
    core::pmr::Set<core::pmr::String>& names,
    core::pmr::String& diagnostics)
{
    const auto* values = findYamlValue(root, key);
    if (values == nullptr || !values->is_sequence()) {
        diagnostics = "generated reflection metadata is missing sequence: ";
        diagnostics += key;
        return false;
    }
    for (const auto& value : values->get_value_ref<const YamlNode::sequence_type&>()) {
        const auto* reflectedName = findYamlValue(value, "reflectName");
        if (reflectedName == nullptr || !reflectedName->is_string()) {
            diagnostics = "generated reflection metadata entry has no reflectName: ";
            diagnostics += key;
            return false;
        }
        if (!names.insert(yamlString(*reflectedName)).second) {
            diagnostics = "generated reflection metadata contains a duplicate reflectName: ";
            diagnostics += key;
            return false;
        }
    }
    return true;
}

bool reflectionMetadataMatchesRuntimeReceipt(const std::filesystem::path& metadataPath,
    const ModuleRegistration& module,
    const arp::reflection::RegistrationReceipt& receipt,
    core::pmr::String& diagnostics)
{
    const auto read = core::readTextFile(metadataPath);
    if (!read.success) {
        diagnostics = read.diagnostics;
        return false;
    }
    YamlNode root;
    try {
        root = YamlNode::deserialize(read.text.data(), read.text.data() + read.text.size());
    } catch (const std::exception& error) {
        diagnostics = "generated reflection metadata parse failed: ";
        diagnostics += error.what();
        return false;
    }

    core::pmr::Set<core::pmr::String> expectedTypes{&core::defaultCpuMemoryResource()};
    core::pmr::Set<core::pmr::String> expectedFunctions{&core::defaultCpuMemoryResource()};
    core::pmr::Set<core::pmr::String> expectedEnums{&core::defaultCpuMemoryResource()};
    if (!readReflectionNameSet(root, "classes", expectedTypes, diagnostics)
        || !readReflectionNameSet(root, "functions", expectedFunctions, diagnostics)
        || !readReflectionNameSet(root, "enums", expectedEnums, diagnostics)) {
        return false;
    }

    core::pmr::Map<core::pmr::String, ReflectionMetadataClassContract> expectedClasses{
        &core::defaultCpuMemoryResource()};
    const auto* classes = findYamlValue(root, "classes");
    const auto metadataModule = yamlTextOrEmpty(root, "module");
    if (metadataModule.empty() || metadataModule != module.name()) {
        diagnostics = "generated reflection metadata module identity mismatch";
        return false;
    }
    arp::reflection::ReflectionRegistrationContract expectedDeclarations;
    expectedDeclarations.moduleIdentity = metadataModule;
    core::pmr::Set<core::pmr::String> expectedObjects{&core::defaultCpuMemoryResource()};
    core::pmr::Map<core::pmr::String, core::pmr::String> expectedObjectTypes{
        &core::defaultCpuMemoryResource()};
    auto addDeclaration =
        [&](std::string_view kind, const core::pmr::String& name, core::pmr::String contract) {
            arp::reflection::RegistrationContractRecord record;
            record.key.append(kind.data(), kind.size());
            record.key.push_back(':');
            record.key += name;
            record.contract = std::move(contract);
            expectedDeclarations.records.push_back(std::move(record));
        };
    core::pmr::Map<core::pmr::String, core::pmr::String> cppToReflectedName{
        &core::defaultCpuMemoryResource()};
    for (const auto& value : classes->get_value_ref<const YamlNode::sequence_type&>()) {
        const auto* cppName = findYamlValue(value, "cppName");
        const auto* reflectedName = findYamlValue(value, "reflectName");
        if (cppName == nullptr || !cppName->is_string() || reflectedName == nullptr
            || !reflectedName->is_string()) {
            diagnostics = "generated reflection class metadata has no type identity";
            return false;
        }
        const auto cppTypeName = yamlString(*cppName);
        const auto reflectedTypeName = yamlString(*reflectedName);
        if (!cppToReflectedName.emplace(cppTypeName, reflectedTypeName).second) {
            diagnostics = "generated reflection C++ type binding is duplicated";
            return false;
        }
        const auto separator = cppTypeName.rfind("::");
        if (separator != core::pmr::String::npos) {
            const auto shortName = cppTypeName.substr(separator + 2u);
            const auto existing = cppToReflectedName.find(shortName);
            if (existing != cppToReflectedName.end() && existing->second != reflectedTypeName) {
                diagnostics = "generated reflection short C++ type binding is ambiguous";
                return false;
            }
            cppToReflectedName.emplace(shortName, reflectedTypeName);
        }
    }
    const auto* bindingVersion = findYamlValue(root, "nativeTypeBindingsVersion");
    const auto* nativeBindings = findYamlValue(root, "nativeTypeBindings");
    if (bindingVersion == nullptr || !bindingVersion->is_integer()
        || bindingVersion->get_value<YamlNode::integer_type>() != 1 || nativeBindings == nullptr
        || !nativeBindings->is_sequence()) {
        diagnostics = "generated reflection native type binding contract is missing or unsupported";
        return false;
    }
    core::pmr::Set<core::pmr::String> boundSources{&core::defaultCpuMemoryResource()};
    for (const auto& binding : nativeBindings->get_value_ref<const YamlNode::sequence_type&>()) {
        if (!binding.is_string() || !boundSources.insert(yamlString(binding)).second) {
            diagnostics =
                "generated reflection native type binding source is invalid or duplicated";
            return false;
        }
        const auto source = yamlString(binding);
        const auto compiled = receipt.nativeTypeBindings.find(source);
        if (compiled == receipt.nativeTypeBindings.end() || compiled->second.empty()) {
            diagnostics = "runtime reflection native type binding is missing: ";
            diagnostics += source;
            return false;
        }
        const auto existing = cppToReflectedName.find(source);
        if (existing != cppToReflectedName.end() && existing->second != compiled->second) {
            diagnostics =
                "runtime reflection native type binding conflicts with a declared class: ";
            diagnostics += source;
            return false;
        }
        cppToReflectedName.insert_or_assign(source, compiled->second);
    }
    if (boundSources.size() != receipt.nativeTypeBindings.size()) {
        diagnostics = "runtime reflection native type binding has an undeclared source";
        return false;
    }
    for (const auto& value : classes->get_value_ref<const YamlNode::sequence_type&>()) {
        const auto name = yamlString(*findYamlValue(value, "reflectName"));
        ReflectionMetadataClassContract expected;
        expected.name = name;
        expected.cppName = yamlTextOrEmpty(value, "cppName");
        expected.moduleExport = yamlTextOrEmpty(value, "moduleExport");
        const auto ownership = yamlTextOrEmpty(value, "ownership");
        const auto lifecycle = yamlTextOrEmpty(value, "lifecycle");
        const auto validOwnership =
            ownership == "unspecified" || ownership == "Engine" || ownership == "Value";
        const auto validLifecycle =
            lifecycle == "unspecified" || lifecycle == "Explicit" || lifecycle == "Value";
        if (expected.cppName.empty() || !validOwnership || !validLifecycle) {
            diagnostics = "generated reflection class ownership/lifecycle contract is invalid";
            return false;
        }
        expected.ownership = ownership == "Engine" ? arp::reflection::RegistrationOwnership::Engine
            : ownership == "Value"                 ? arp::reflection::RegistrationOwnership::Value
                                   : arp::reflection::RegistrationOwnership::Unspecified;
        expected.lifecycle = lifecycle == "Explicit"
            ? arp::reflection::RegistrationLifecycle::Explicit
            : lifecycle == "Value" ? arp::reflection::RegistrationLifecycle::Value
                                   : arp::reflection::RegistrationLifecycle::Unspecified;
        if (!expected.moduleExport.empty()) {
            if (!expectedObjects.insert(expected.moduleExport).second) {
                diagnostics = "generated reflection module export is duplicated";
                return false;
            }
            expectedObjectTypes.emplace(expected.moduleExport, name);
        }
        core::pmr::String classContract(&core::defaultCpuMemoryResource());
        if (!buildClassDescriptorContract(value, classContract, diagnostics))
            return false;
        addDeclaration("class", name, std::move(classContract));
        const auto* defaultConstruct = findYamlValue(value, "defaultConstruct");
        const auto* constructors = findYamlValue(value, "constructors");
        const bool hasDefault = defaultConstruct != nullptr && defaultConstruct->is_boolean()
            && defaultConstruct->get_value<bool>();
        const bool hasConstructors = constructors != nullptr && constructors->is_sequence()
            && !constructors->get_value_ref<const YamlNode::sequence_type&>().empty();
        const auto constructorCount = constructors != nullptr && constructors->is_sequence()
            ? constructors->get_value_ref<const YamlNode::sequence_type&>().size()
            : 0u;
        if (constructorCount > 1u || (hasDefault && hasConstructors)) {
            diagnostics = "generated reflection class has multiple constructor paths";
            return false;
        }
        expected.hasConstructor = hasDefault || hasConstructors;
        if (hasConstructors) {
            const auto& constructor =
                constructors->get_value_ref<const YamlNode::sequence_type&>().front();
            const auto* parameters = findYamlValue(constructor, "parameters");
            if (parameters == nullptr || !parameters->is_sequence()) {
                diagnostics = "generated reflection constructor has no parameter contract";
                return false;
            }
            for (const auto& parameter :
                parameters->get_value_ref<const YamlNode::sequence_type&>()) {
                const auto cppType = yamlTextOrEmpty(parameter, "cppType");
                if (cppType.empty()) {
                    diagnostics = "generated reflection constructor parameter has no C++ type";
                    return false;
                }
                expected.constructorParameterTypes.push_back(
                    metadataRuntimeTypeContract(cppType, cppToReflectedName));
            }
        }
        if (hasDefault || hasConstructors) {
            expected.fields.insert(core::pmr::String("__new__", &core::defaultCpuMemoryResource()));
            expected.fields.insert(
                core::pmr::String("__init__", &core::defaultCpuMemoryResource()));
        }
        const auto* bases = findYamlValue(value, "bases");
        if (bases == nullptr || !bases->is_sequence()) {
            diagnostics = "generated reflection class metadata has no bases contract";
            return false;
        }
        for (const auto& base : bases->get_value_ref<const YamlNode::sequence_type&>()) {
            if (!base.is_string()) {
                diagnostics = "generated reflection base metadata is invalid";
                return false;
            }
            auto baseName = yamlString(base);
            const auto resolved = cppToReflectedName.find(baseName);
            expected.bases.insert(
                resolved == cppToReflectedName.end() ? std::move(baseName) : resolved->second);
        }
        for (const auto* key : {"properties", "methods"}) {
            const auto* entries = findYamlValue(value, key);
            if (entries == nullptr || !entries->is_sequence()) {
                diagnostics = "generated reflection class metadata is incomplete";
                return false;
            }
            for (const auto& entry : entries->get_value_ref<const YamlNode::sequence_type&>()) {
                const auto* fieldName = findYamlValue(
                    entry, std::string_view(key) == "methods" ? "reflectName" : "name");
                if (fieldName == nullptr || !fieldName->is_string()) {
                    diagnostics = "generated reflection class member has no name";
                    return false;
                }
                const auto memberName = yamlString(*fieldName);
                expected.fields.insert(memberName);
                if (std::string_view(key) == "properties") {
                    const auto cppType = yamlTextOrEmpty(entry, "cppType");
                    if (cppType.empty()) {
                        diagnostics = "generated reflection property has no C++ type";
                        return false;
                    }
                    expected.propertyTypes.emplace(
                        memberName, metadataRuntimeTypeContract(cppType, cppToReflectedName));
                }
            }
        }
        expectedClasses.emplace(name, std::move(expected));
    }

    core::pmr::Map<core::pmr::String, ReflectionMetadataCallableContract> expectedCallables{
        &core::defaultCpuMemoryResource()};
    for (const auto& value : classes->get_value_ref<const YamlNode::sequence_type&>()) {
        const auto reflectedClass = yamlTextOrEmpty(value, "reflectName");
        const auto* methods = findYamlValue(value, "methods");
        if (methods == nullptr || !methods->is_sequence()) {
            diagnostics = "generated reflection class has no method contract";
            return false;
        }
        for (const auto& method : methods->get_value_ref<const YamlNode::sequence_type&>()) {
            ReflectionMetadataCallableContract expected;
            expected.ownerType = reflectedClass;
            expected.memberName = yamlTextOrEmpty(method, "reflectName");
            expected.targetId = metadataModule;
            expected.targetId += ".";
            expected.targetId += reflectedClass;
            expected.targetId += ".";
            expected.targetId += expected.memberName;
            expected.bindingId = "arp-reflect:";
            expected.bindingId += expected.targetId;
            expected.bindingId += "/v1";
            expected.maySuspend = yamlBoolOr(method, "maySuspend");
            expected.isStatic = yamlBoolOr(method, "static");
            expected.returnType = metadataRuntimeTypeContract(
                yamlTextOrEmpty(method, "returnType"), cppToReflectedName);
            expected.parameterList = metadataParameterList(method, cppToReflectedName, diagnostics);
            if (expected.returnType.empty() || !diagnostics.empty())
                return false;
            core::pmr::String seed("reflection-method|", &core::defaultCpuMemoryResource());
            seed += expected.targetId;
            seed += "|return:";
            seed += yamlTextOrEmpty(method, "returnType");
            seed += "|args:";
            seed += yamlTextOrEmpty(method, "args");
            const auto* parameters = findYamlValue(method, "parameters");
            if (parameters == nullptr || !parameters->is_sequence()) {
                diagnostics = "generated reflection method has no parameters";
                return false;
            }
            const auto arity = parameters->get_value_ref<const YamlNode::sequence_type&>().size();
            const auto arityText = std::to_string(arity);
            seed += "|arity:";
            seed.append(arityText.data(), arityText.size());
            seed += yamlBoolOr(method, "static") ? "|static:1" : "|static:0";
            seed += expected.maySuspend ? "|suspend:1" : "|suspend:0";
            seed += yamlBoolOr(method, "const") ? "|const:1" : "|const:0";
            seed += yamlBoolOr(method, "volatile") ? "|volatile:1" : "|volatile:0";
            seed += yamlBoolOr(method, "noexcept") ? "|noexcept:1" : "|noexcept:0";
            seed += "|ref:";
            seed += yamlTextOrEmpty(method, "refQualifier");
            seed +=
                yamlBoolOr(method, "nullableReturn") ? "|nullable-return:1" : "|nullable-return:0";
            seed += "|return-ref:";
            seed += yamlTextOrEmpty(method, "returnReferenceKind");
            if (!appendParameterContract(method, seed, expected.fixedArity, diagnostics)) {
                return false;
            }
            expected.signatureHash = sha256Contract(seed);
            if (expected.memberName.empty()
                || !expectedCallables.emplace(expected.targetId, std::move(expected)).second) {
                diagnostics = "generated reflection method contract is duplicated";
                return false;
            }
        }
    }
    const auto* functions = findYamlValue(root, "functions");
    for (const auto& value : functions->get_value_ref<const YamlNode::sequence_type&>()) {
        core::pmr::String functionDescriptor(&core::defaultCpuMemoryResource());
        if (!buildFunctionDescriptorContract(value, functionDescriptor, diagnostics)) {
            return false;
        }
        addDeclaration(
            "function", yamlTextOrEmpty(value, "reflectName"), std::move(functionDescriptor));
        ReflectionMetadataCallableContract expected;
        expected.memberName = yamlTextOrEmpty(value, "reflectName");
        expected.targetId = metadataModule;
        expected.targetId += ".";
        expected.targetId += expected.memberName;
        expected.bindingId = "arp-reflect:";
        expected.bindingId += expected.targetId;
        expected.bindingId += "/v1";
        expected.maySuspend = yamlBoolOr(value, "maySuspend");
        expected.isStatic = true;
        expected.returnType =
            metadataRuntimeTypeContract(yamlTextOrEmpty(value, "returnType"), cppToReflectedName);
        expected.parameterList = metadataParameterList(value, cppToReflectedName, diagnostics);
        if (expected.returnType.empty() || !diagnostics.empty())
            return false;
        core::pmr::String seed("reflection-function|", &core::defaultCpuMemoryResource());
        seed += expected.targetId;
        seed += "|return:";
        seed += yamlTextOrEmpty(value, "returnType");
        seed += "|args:";
        seed += yamlTextOrEmpty(value, "args");
        const auto* parameters = findYamlValue(value, "parameters");
        if (parameters == nullptr || !parameters->is_sequence()) {
            diagnostics = "generated reflection function has no parameters";
            return false;
        }
        const auto arity = parameters->get_value_ref<const YamlNode::sequence_type&>().size();
        const auto arityText = std::to_string(arity);
        seed += "|arity:";
        seed.append(arityText.data(), arityText.size());
        seed += expected.maySuspend ? "|suspend:1" : "|suspend:0";
        seed += yamlBoolOr(value, "nullableReturn") ? "|nullable-return:1" : "|nullable-return:0";
        seed += "|return-ref:";
        seed += yamlTextOrEmpty(value, "returnReferenceKind");
        seed += "|defaults:";
        seed += yamlTextOrEmpty(value, "defaultArguments");
        if (!appendParameterContract(value, seed, expected.fixedArity, diagnostics)) {
            return false;
        }
        expected.signatureHash = sha256Contract(seed);
        if (expected.memberName.empty()
            || !expectedCallables.emplace(expected.targetId, std::move(expected)).second) {
            diagnostics = "generated reflection function contract is duplicated";
            return false;
        }
    }
    const auto* enums = findYamlValue(root, "enums");
    for (const auto& value : enums->get_value_ref<const YamlNode::sequence_type&>()) {
        const auto name = yamlTextOrEmpty(value, "reflectName");
        core::pmr::String enumDescriptor(&core::defaultCpuMemoryResource());
        if (name.empty() || !buildEnumDescriptorContract(value, enumDescriptor, diagnostics)) {
            return false;
        }
        addDeclaration("enum", name, std::move(enumDescriptor));
    }

    auto actualNames = [](const auto& bindings) {
        core::pmr::Set<core::pmr::String> result{&core::defaultCpuMemoryResource()};
        for (const auto& binding : bindings)
            result.insert(binding.name);
        return result;
    };
    if (actualNames(module.types()) != expectedTypes
        || actualNames(module.functions()) != expectedFunctions
        || actualNames(module.enums()) != expectedEnums
        || actualNames(module.objects()) != expectedObjects) {
        diagnostics = "runtime reflection module receipt does not match generated metadata";
        return false;
    }
    for (const auto& object : module.objects()) {
        const auto expectedType = expectedObjectTypes.find(object.name);
        const auto* actualClass = object.value.class_();
        if (expectedType == expectedObjectTypes.end() || actualClass == nullptr
            || metadataRuntimeTypeContract(actualClass->nativeTypeName(), cppToReflectedName)
                != expectedType->second) {
            diagnostics =
                "runtime reflection module object does not match its generated class contract";
            return false;
        }
    }

    if (receipt.declarations.canonicalText() != expectedDeclarations.canonicalText()) {
        diagnostics =
            "runtime reflection canonical descriptor receipt does not match generated metadata";
        for (const auto& expected : expectedDeclarations.records) {
            const auto actual = std::find_if(receipt.declarations.records.begin(),
                receipt.declarations.records.end(),
                [&](const auto& record) {
                    return record.key == expected.key;
                });
            if (actual != receipt.declarations.records.end()
                && actual->contract == expected.contract)
                continue;
            diagnostics += "; record=";
            diagnostics += expected.key;
            diagnostics += "; expected=";
            diagnostics += expected.contract;
            diagnostics += "; actual=";
            diagnostics +=
                actual == receipt.declarations.records.end() ? "<missing>" : actual->contract;
            break;
        }
        return false;
    }

    core::pmr::Set<core::pmr::String> supportNames{&core::defaultCpuMemoryResource()};
    for (const auto& support : receipt.nativeSupportTypes) {
        if (boundSources.find(support.sourceType) == boundSources.end() || support.typeName.empty()
            || !supportNames.insert(support.typeName).second
            || static_cast<unsigned>(support.shape)
                > static_cast<unsigned>(arp::reflection::RegistrationNativeShape::OpaqueToken)) {
            diagnostics = "runtime reflection native support declaration is invalid";
            return false;
        }
    }
    core::pmr::Set<core::pmr::String> receiptNames{&core::defaultCpuMemoryResource()};
    for (const auto& cls : receipt.classes) {
        if (supportNames.erase(cls.typeName) != 0u) {
            if (cls.reflectedName != cls.typeName || !cls.bases.empty() || !cls.members.empty()
                || cls.size == 0u || cls.alignment == 0u
                || cls.ownership != arp::reflection::RegistrationOwnership::Unspecified
                || cls.lifecycle != arp::reflection::RegistrationLifecycle::Unspecified
                || !cls.moduleExport.empty()
                || expectedTypes.find(cls.reflectedName) != expectedTypes.end()
                || expectedEnums.find(cls.reflectedName) != expectedEnums.end()) {
                diagnostics =
                    "runtime reflection native support type has undeclared class semantics";
                return false;
            }
            continue;
        }
        receiptNames.insert(cls.reflectedName);
        const auto expected = expectedClasses.find(cls.reflectedName);
        if (expected == expectedClasses.end()) {
            if (expectedEnums.find(cls.reflectedName) == expectedEnums.end()) {
                diagnostics = "runtime reflection transaction declared an unexpected class: ";
                diagnostics += cls.typeName;
                diagnostics += "; reflected=";
                diagnostics += cls.reflectedName;
                return false;
            }
            if (cls.members.size() != 1u
                || receiptMemberName(cls.members.front()) != cls.reflectedName) {
                diagnostics = "runtime reflection enum receipt does not match generated metadata";
                return false;
            }
            if (cls.ownership != arp::reflection::RegistrationOwnership::Unspecified
                || cls.lifecycle != arp::reflection::RegistrationLifecycle::Unspecified
                || !cls.moduleExport.empty()) {
                diagnostics = "runtime reflection enum has an invalid class semantics contract";
                return false;
            }
            continue;
        }
        if (cls.ownership != expected->second.ownership
            || cls.lifecycle != expected->second.lifecycle
            || cls.moduleExport != expected->second.moduleExport) {
            diagnostics = "runtime reflection class semantics does not match generated metadata";
            return false;
        }
        core::pmr::Set<core::pmr::String> bases{&core::defaultCpuMemoryResource()};
        bases.insert(cls.bases.begin(), cls.bases.end());
        if (bases != expected->second.bases) {
            diagnostics = "runtime reflection class receipt does not match generated bases";
            return false;
        }
        core::pmr::Set<core::pmr::String> fields{&core::defaultCpuMemoryResource()};
        core::pmr::String actualNewParameters(&core::defaultCpuMemoryResource());
        core::pmr::String actualInitParameters(&core::defaultCpuMemoryResource());
        bool hasNew = false;
        bool hasInit = false;
        for (const auto& member : cls.members) {
            const auto memberName = receiptMemberName(member);
            fields.insert(memberName);
            const auto property = expected->second.propertyTypes.find(memberName);
            if (property != expected->second.propertyTypes.end()) {
                if (member.rfind("property:", 0u) != 0u
                    || receiptMemberField(member, "type") != property->second) {
                    diagnostics =
                        "runtime reflection property type does not match generated metadata";
                    diagnostics += ": class=";
                    diagnostics += cls.reflectedName;
                    diagnostics += " property=";
                    diagnostics += memberName;
                    diagnostics += " actual=";
                    diagnostics += receiptMemberField(member, "type");
                    diagnostics += " expected=";
                    diagnostics += property->second;
                    return false;
                }
            }
            if (memberName == "__new__") {
                hasNew = true;
                actualNewParameters = receiptMemberField(member, "params");
            } else if (memberName == "__init__") {
                hasInit = true;
                actualInitParameters = receiptMemberField(member, "params");
            }
        }
        if (fields != expected->second.fields) {
            diagnostics = "runtime reflection class receipt does not match generated members";
            return false;
        }
        core::pmr::String expectedConstructorParameters(&core::defaultCpuMemoryResource());
        for (const auto& parameter : expected->second.constructorParameterTypes) {
            if (!expectedConstructorParameters.empty())
                expectedConstructorParameters.push_back(',');
            expectedConstructorParameters += parameter;
        }
        core::pmr::String expectedInitParameters("void*", &core::defaultCpuMemoryResource());
        if (!expectedConstructorParameters.empty()) {
            expectedInitParameters.push_back(',');
            expectedInitParameters += expectedConstructorParameters;
        }
        if (expected->second.hasConstructor != (hasNew && hasInit)
            || (expected->second.hasConstructor
                && (actualNewParameters != expectedConstructorParameters
                    || actualInitParameters != expectedInitParameters))) {
            diagnostics =
                "runtime reflection constructor signature does not match generated metadata";
            diagnostics += ": expected-new=";
            diagnostics += expectedConstructorParameters;
            diagnostics += " actual-new=";
            diagnostics += actualNewParameters;
            diagnostics += " expected-init=";
            diagnostics += expectedInitParameters;
            diagnostics += " actual-init=";
            diagnostics += actualInitParameters;
            return false;
        }
    }
    auto expectedDeclaredNames = expectedTypes;
    expectedDeclaredNames.insert(expectedEnums.begin(), expectedEnums.end());
    if (receiptNames != expectedDeclaredNames || !supportNames.empty()) {
        diagnostics = "runtime reflection transaction receipt is incomplete";
        return false;
    }
    if (module.reflectionCallables().size() != expectedCallables.size()) {
        diagnostics = "runtime reflection callable receipt is incomplete";
        return false;
    }
    for (const auto& binding : module.reflectionCallables()) {
        const auto expected = expectedCallables.find(binding.contract.targetId);
        if (expected == expectedCallables.end() || binding.key != expected->first
            || binding.contract.bindingId != expected->second.bindingId
            || binding.contract.signatureHash != expected->second.signatureHash
            || binding.contract.fixedArity != expected->second.fixedArity
            || binding.contract.maySuspend != expected->second.maySuspend
            || binding.contract.lifetimeVersion != 1u) {
            diagnostics = "runtime reflection callable contract does not match generated metadata";
            diagnostics += ": target=";
            diagnostics += binding.contract.targetId;
            if (expected != expectedCallables.end()) {
                diagnostics += " actual-hash=";
                diagnostics += binding.contract.signatureHash;
                diagnostics += " expected-hash=";
                diagnostics += expected->second.signatureHash;
                diagnostics += " actual-key=";
                diagnostics += binding.key;
                diagnostics += " expected-key=";
                diagnostics += expected->first;
                diagnostics += " actual-arity=";
                diagnostics += std::to_string(binding.contract.fixedArity).c_str();
                diagnostics += " expected-arity=";
                diagnostics += std::to_string(expected->second.fixedArity).c_str();
                diagnostics += " actual-suspend=";
                diagnostics += binding.contract.maySuspend ? "1" : "0";
                diagnostics += " expected-suspend=";
                diagnostics += expected->second.maySuspend ? "1" : "0";
                diagnostics += " lifetime=";
                diagnostics += std::to_string(binding.contract.lifetimeVersion).c_str();
            }
            return false;
        }
        const void* expectedIdentity = nullptr;
        core::UniquePtr<arp::reflection::function> actualFunction;
        if (expected->second.ownerType.empty()) {
            for (const auto& function : module.functions()) {
                if (function.name == expected->second.memberName) {
                    expectedIdentity = function.function.identity();
                    actualFunction = core::makeUnique<arp::reflection::function>(function.function);
                    break;
                }
            }
        } else {
            for (const auto& type : module.types()) {
                if (type.name != expected->second.ownerType || type.classInfo == nullptr) {
                    continue;
                }
                const auto member = type.classInfo->get_field(expected->second.memberName);
                if (member.is_function()) {
                    const auto& reflected = member.cast<const arp::reflection::function&>();
                    expectedIdentity = reflected.identity();
                    actualFunction = core::makeUnique<arp::reflection::function>(reflected);
                }
                break;
            }
        }
        if (expectedIdentity == nullptr || binding.contract.functionIdentity != expectedIdentity
            || actualFunction == nullptr) {
            diagnostics = "runtime reflection callable owner identity mismatch";
            return false;
        }
        core::pmr::String actualParameters(&core::defaultCpuMemoryResource());
        const auto parameters = actualFunction->params_info();
        for (std::size_t index = 0u; index < parameters.size(); ++index) {
            if (index != 0u)
                actualParameters.push_back(',');
            actualParameters += arp::reflection::registrationTypeContract(
                parameters[index].ti, parameters[index].is_const, parameters[index].is_volatile);
        }
        if (arp::reflection::registrationTypeContract(actualFunction->return_type())
                != expected->second.returnType
            || actualParameters != expected->second.parameterList
            || actualFunction->is_static() != expected->second.isStatic
            || static_cast<int>(parameters.size()) != expected->second.fixedArity) {
            diagnostics = "runtime reflection callable signature does not match generated metadata";
            diagnostics += ": target=";
            diagnostics += binding.contract.targetId;
            diagnostics += " actual-return=";
            diagnostics += arp::reflection::registrationTypeContract(actualFunction->return_type());
            diagnostics += " expected-return=";
            diagnostics += expected->second.returnType;
            diagnostics += " actual-params=";
            diagnostics += actualParameters;
            diagnostics += " expected-params=";
            diagnostics += expected->second.parameterList;
            diagnostics += " actual-static=";
            diagnostics += actualFunction->is_static() ? "1" : "0";
            diagnostics += " expected-static=";
            diagnostics += expected->second.isStatic ? "1" : "0";
            diagnostics += " actual-arity=";
            diagnostics += std::to_string(parameters.size()).c_str();
            diagnostics += " expected-arity=";
            diagnostics += std::to_string(expected->second.fixedArity).c_str();
            return false;
        }
    }
    return true;
}

void addProcessArgument(core::ProcessRunRequest& request, std::string_view value)
{
    request.arguments.emplace_back(value.data(), value.size());
}

void addProcessPathArgument(core::ProcessRunRequest& request, const std::filesystem::path& value)
{
    const auto text = value.generic_string();
    addProcessArgument(request, std::string_view(text.data(), text.size()));
}

std::atomic<std::uint64_t> stagingSequence{0u};

core::pmr::String uniqueStagingSuffix()
{
    core::pmr::String suffix(&core::defaultCpuMemoryResource());
#if defined(_WIN32) || defined(__CYGWIN__)
    core::appendUnsigned(suffix, GetCurrentProcessId());
    suffix.push_back('-');
    core::appendUnsigned(suffix, GetCurrentThreadId());
#else
    core::appendUnsigned(suffix, static_cast<std::uint64_t>(::getpid()));
    suffix.push_back('-');
    core::appendUnsigned(suffix,
        static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())));
#endif
    suffix.push_back('-');
    core::appendUnsigned(suffix, stagingSequence.fetch_add(1u, std::memory_order_relaxed));
    suffix.push_back('-');
    core::appendUnsigned(suffix,
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    return suffix;
}

class StagingDirectoryScope final {
public:
    explicit StagingDirectoryScope(std::filesystem::path path) : path_(std::move(path)) {}

    ~StagingDirectoryScope()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

private:
    std::filesystem::path path_;
};

bool nonEmptyRegularFile(const std::filesystem::path& path)
{
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error
        && std::filesystem::file_size(path, error) != 0u && !error;
}

bool atomicReplaceFile(const std::filesystem::path& source, const std::filesystem::path& target)
{
#if defined(_WIN32) || defined(__CYGWIN__)
    return MoveFileExA(source.string().c_str(),
               target.string().c_str(),
               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
        != FALSE;
#else
    return std::rename(source.string().c_str(), target.string().c_str()) == 0;
#endif
}

core::pmr::String fileVersionFingerprint(const std::filesystem::path& path);

core::pmr::String identityMarkerText(std::string_view identity,
    const std::filesystem::path& artifact,
    const std::filesystem::path& companionA = {},
    const std::filesystem::path& companionB = {})
{
    const auto artifactIdentity = fileVersionFingerprint(artifact);
    if (artifactIdentity == "missing")
        return {};
    core::pmr::String markerText("type: arp.script.plugin-cache-completion\n"
                                 "contractVersion: 1\nidentity: ",
        &core::defaultCpuMemoryResource());
    markerText.append(identity.data(), identity.size());
    markerText += "\nartifactSha256: ";
    markerText += artifactIdentity;
    if (!companionA.empty()) {
        const auto hash = fileVersionFingerprint(companionA);
        if (hash == "missing")
            return {};
        markerText += "\ncompanionASha256: ";
        markerText += hash;
    }
    if (!companionB.empty()) {
        const auto hash = fileVersionFingerprint(companionB);
        if (hash == "missing")
            return {};
        markerText += "\ncompanionBSha256: ";
        markerText += hash;
    }
    markerText.push_back('\n');
    return markerText;
}

bool publishIdentityMarker(const std::filesystem::path& marker,
    const std::filesystem::path& artifact,
    std::string_view identity,
    const std::filesystem::path& companionA = {},
    const std::filesystem::path& companionB = {})
{
    const auto markerText = identityMarkerText(identity, artifact, companionA, companionB);
    if (markerText.empty())
        return false;
    auto temporary = marker;
    temporary += ".tmp-";
    temporary += uniqueStagingSuffix().c_str();
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output.good())
            return false;
        output.write(markerText.data(), static_cast<std::streamsize>(markerText.size()));
        output.flush();
        if (!output.good())
            return false;
    }
    if (!atomicReplaceFile(temporary, marker)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return true;
}

bool markerMatches(const std::filesystem::path& marker,
    const std::filesystem::path& artifact,
    std::string_view identity,
    const std::filesystem::path& companionA = {},
    const std::filesystem::path& companionB = {})
{
    const auto read = core::readTextFile(marker);
    if (!read.success)
        return false;
    const auto expected = identityMarkerText(identity, artifact, companionA, companionB);
    if (expected.empty())
        return false;
    return std::string_view(read.text.data(), read.text.size()) == expected;
}

core::pmr::String fileVersionFingerprint(const std::filesystem::path& path)
{
    core::ByteBuffer digest{&core::defaultCpuMemoryResource()};
    if (!core::Sha256::digestFile(path, digest)) {
        return core::pmr::String("missing", &core::defaultCpuMemoryResource());
    }
    static constexpr char digits[] = "0123456789abcdef";
    core::pmr::String result("sha256:", &core::defaultCpuMemoryResource());
    result.reserve(7u + digest.size() * 2u);
    for (const auto byte : digest) {
        result.push_back(digits[(byte >> 4u) & 0x0fu]);
        result.push_back(digits[byte & 0x0fu]);
    }
    return result;
}

bool hasParentTraversal(const std::filesystem::path& path)
{
    for (const auto& component : path) {
        if (component == "..") {
            return true;
        }
    }
    return false;
}

bool validFlatIdentifier(std::string_view value)
{
    if (value.empty()
        || !((value.front() >= 'a' && value.front() <= 'z')
            || (value.front() >= 'A' && value.front() <= 'Z') || value.front() == '_')) {
        return false;
    }
    for (const char ch : value.substr(1u)) {
        if (!(ch == '_' || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
                || (ch >= '0' && ch <= '9'))) {
            return false;
        }
    }
    return true;
}

core::pmr::String defaultCompilerCommand()
{
    if (const char* configured = std::getenv("ARP_SCRIPT_CPP_COMPILER")) {
        if (*configured != '\0') {
            return core::pmr::String(configured, &core::defaultCpuMemoryResource());
        }
    }
#if defined(_WIN32) || defined(__CYGWIN__)
    return core::pmr::String("g++", &core::defaultCpuMemoryResource());
#elif defined(__APPLE__)
    return core::pmr::String("clang++", &core::defaultCpuMemoryResource());
#else
    return core::pmr::String("c++", &core::defaultCpuMemoryResource());
#endif
}

core::pmr::String commandVersionFingerprint(const core::pmr::String& command);

std::filesystem::path resolveCommandPath(const core::pmr::String& command);

bool appendDirectoryContract(
    core::pmr::String& contract, std::string_view logicalRoot, const std::filesystem::path& root);

core::pmr::String digestName(const core::pmr::String& manifest,
    const core::pmr::String& source,
    const core::pmr::String& compiler,
    const std::filesystem::path& includeRoot,
    const core::pmr::String& hostLibraryVersion,
    const core::pmr::String& hostLinkInputVersion,
    const core::pmr::Vector<std::filesystem::path>& additionalIncludeRoots,
    const core::pmr::String& reflectionModule,
    const core::pmr::Vector<core::pmr::String>& reflectionHeaders,
    const core::pmr::String& reflectionCodegenVersion,
    bool moduleDirectoryLayout)
{
    core::ImmutableBuildInputContract input;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    const auto add = [&](std::string_view key, std::string_view value) {
        return input.add(key, value, diagnostics);
    };
    if (!add("manifest", manifest) || !add("source", source))
        return {};
    const auto resolvedCompiler = resolveCommandPath(compiler);
    const std::filesystem::path compilerPath =
        resolvedCompiler.empty() ? std::filesystem::path(compiler.c_str()) : resolvedCompiler;
    const auto compilerLabel = compilerPath.filename().generic_string();
    const auto compilerFingerprint = commandVersionFingerprint(compiler);
    if (!add("compiler-label", compilerLabel) || !add("compiler-fingerprint", compilerFingerprint)
        || !add("host-library", hostLibraryVersion)
        || !add("host-link-input", hostLinkInputVersion)) {
        return {};
    }
    core::pmr::String directoryContract(&core::defaultCpuMemoryResource());
    if (!appendDirectoryContract(directoryContract, "include-root", includeRoot)
        || !add("include-root", directoryContract)) {
        return {};
    }
    for (std::size_t index = 0u; index < additionalIncludeRoots.size(); ++index) {
        directoryContract.clear();
        if (!appendDirectoryContract(
                directoryContract, "additional-include-root", additionalIncludeRoots[index])) {
            return {};
        }
        core::pmr::String key("additional-include-root-", &core::defaultCpuMemoryResource());
        core::appendUnsigned(key, index);
        if (!add(key, directoryContract))
            return {};
    }
    if (!add("reflection-module", reflectionModule))
        return {};
    for (std::size_t index = 0u; index < reflectionHeaders.size(); ++index) {
        core::pmr::String key("reflection-header-", &core::defaultCpuMemoryResource());
        core::appendUnsigned(key, index);
        if (!add(key, reflectionHeaders[index]))
            return {};
    }
    if (!add("reflection-codegen", reflectionCodegenVersion) || !add("reflection-call-abi", "v5")
        || !add("module-layout",
            moduleDirectoryLayout ? std::string_view("directory") : std::string_view("flat"))) {
        return {};
    }
    auto result = input.fingerprint();
    if (result.rfind("sha256:", 0u) == 0u)
        result.erase(0u, 7u);
    return result;
}

bool equivalentCanonicalPath(const std::filesystem::path& lhs, const std::filesystem::path& rhs)
{
    std::error_code lhsError;
    std::error_code rhsError;
    const auto canonicalLhs = std::filesystem::weakly_canonical(lhs, lhsError);
    const auto canonicalRhs = std::filesystem::weakly_canonical(rhs, rhsError);
    if (lhsError || rhsError)
        return false;
#if defined(_WIN32) || defined(__CYGWIN__)
    auto lhsText = canonicalLhs.generic_wstring();
    auto rhsText = canonicalRhs.generic_wstring();
    std::transform(lhsText.begin(), lhsText.end(), lhsText.begin(), ::towlower);
    std::transform(rhsText.begin(), rhsText.end(), rhsText.begin(), ::towlower);
    return lhsText == rhsText;
#else
    return canonicalLhs == canonicalRhs;
#endif
}

#if defined(_WIN32) || defined(__CYGWIN__)
bool loadedModulePathMatches(HMODULE module, const std::filesystem::path& expected)
{
    core::pmr::Vector<wchar_t> buffer(512u, L'\0', &core::defaultCpuMemoryResource());
    for (;;) {
        const auto length =
            GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0u)
            return false;
        if (length < buffer.size() - 1u) {
            return equivalentCanonicalPath(std::filesystem::path(buffer.data()), expected);
        }
        if (buffer.size() >= 32768u)
            return false;
        buffer.resize(buffer.size() * 2u);
    }
}
#endif

core::pmr::String commandVersionFingerprint(const core::pmr::String& command)
{
    const auto candidate = resolveCommandPath(command);
    std::error_code candidateError;
    if (!candidate.empty() && std::filesystem::is_regular_file(candidate, candidateError)
        && !candidateError)
        return fileVersionFingerprint(candidate);

    // A bare command is resolved by the host process. Keep the unresolved
    // command itself in the contract instead of treating it as a cache hit
    // with a fake file timestamp or size fingerprint.
    core::pmr::String input("command:\n", &core::defaultCpuMemoryResource());
    const std::filesystem::path commandPath(command.c_str());
    const auto commandLabel = commandPath.filename().generic_string();
    input.append(commandLabel.data(), commandLabel.size());
    const auto digest = core::Sha256::digest(core::StringView(input.data(), input.size()));
    static constexpr char digits[] = "0123456789abcdef";
    core::pmr::String result("sha256:", &core::defaultCpuMemoryResource());
    result.reserve(7u + digest.size() * 2u);
    for (const auto byte : digest) {
        result.push_back(digits[(byte >> 4u) & 0x0fu]);
        result.push_back(digits[byte & 0x0fu]);
    }
    return result;
}

std::filesystem::path resolveCommandPath(const core::pmr::String& command)
{
    const std::filesystem::path direct(command.c_str());
    std::error_code directError;
    if (!direct.empty()
        && (direct.has_parent_path() || std::filesystem::is_regular_file(direct, directError))) {
        return direct;
    }
    if (directError)
        return {};
    const char* pathEnvironment = std::getenv("PATH");
    if (pathEnvironment == nullptr)
        return {};

#if defined(_WIN32) || defined(__CYGWIN__)
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif
    const std::string_view pathText(pathEnvironment);
    std::size_t begin = 0u;
    while (begin <= pathText.size()) {
        const auto end = pathText.find(separator, begin);
        const auto length = end == std::string_view::npos ? pathText.size() - begin : end - begin;
        core::pmr::String directory(
            pathText.data() + begin, length, &core::defaultCpuMemoryResource());
        std::filesystem::path candidate(directory.c_str());
        candidate /= command.c_str();
#if defined(_WIN32) || defined(__CYGWIN__)
        if (candidate.extension().empty())
            candidate += ".exe";
#endif
        std::error_code candidateError;
        if (std::filesystem::is_regular_file(candidate, candidateError) && !candidateError)
            return candidate;
        if (end == std::string_view::npos)
            break;
        begin = end + 1u;
    }
    return {};
}

bool appendDirectoryContract(
    core::pmr::String& contract, std::string_view logicalRoot, const std::filesystem::path& root)
{
    std::error_code rootError;
    if (root.empty() || !std::filesystem::is_directory(root, rootError) || rootError)
        return false;

    core::pmr::Vector<std::filesystem::path> files{&core::defaultCpuMemoryResource()};
    std::error_code iteratorError;
    for (std::filesystem::recursive_directory_iterator iterator(root, iteratorError), end;
        !iteratorError && iterator != end;
        iterator.increment(iteratorError)) {
        std::error_code fileError;
        if (iterator->is_regular_file(fileError))
            files.push_back(iterator->path());
        if (fileError) {
            iteratorError = fileError;
            break;
        }
    }
    if (iteratorError)
        return false;
    std::sort(files.begin(), files.end());

    for (const auto& path : files) {
        const auto relative = path.lexically_relative(root);
        if (relative.empty())
            return false;
        const auto fingerprint = fileVersionFingerprint(path);
        if (fingerprint == "missing")
            return false;
        contract.append(logicalRoot.data(), logicalRoot.size());
        contract.push_back(':');
        const auto relativeText = relative.generic_string();
        contract.append(relativeText.data(), relativeText.size());
        contract.push_back('\n');
        contract += fingerprint;
        contract.push_back('\n');
    }
    return true;
}

core::pmr::String reflectionRegistryFingerprint(const std::filesystem::path& metadataPath)
{
    const auto metadata = core::readTextFile(metadataPath);
    if (!metadata.success) {
        return {};
    }
    const auto digest =
        core::Sha256::digest(core::StringView(metadata.text.data(), metadata.text.size()));
    static constexpr char digits[] = "0123456789abcdef";
    core::pmr::String result("sha256:", &core::defaultCpuMemoryResource());
    result.reserve(7u + digest.size() * 2u);
    for (const auto byte : digest) {
        result.push_back(digits[(byte >> 4u) & 0x0fu]);
        result.push_back(digits[byte & 0x0fu]);
    }
    return result;
}

std::filesystem::path dynamicLibraryPath(
    const std::filesystem::path& directory, std::string_view moduleName, std::string_view digest)
{
#if defined(_WIN32) || defined(__CYGWIN__)
    constexpr const char* extension = ".dll";
#elif defined(__APPLE__)
    constexpr const char* extension = ".dylib";
#else
    constexpr const char* extension = ".so";
#endif
    core::pmr::String fileName(
        moduleName.data(), moduleName.size(), &core::defaultCpuMemoryResource());
    fileName += "-";
    fileName.append(digest.data(), digest.size());
    fileName += extension;
    return directory / fileName;
}

std::filesystem::path hostLinkInputPath(const std::filesystem::path& hostLibrary)
{
#if defined(_WIN32) || defined(__CYGWIN__)
    auto stem = hostLibrary.stem().generic_string();
    if (stem.rfind("lib", 0u) == 0u)
        stem.erase(0u, 3u);
    return hostLibrary.parent_path() / ("lib" + stem + ".dll.a");
#else
    return hostLibrary;
#endif
}

struct ParsedManifest {
    core::pmr::String module{&core::defaultCpuMemoryResource()};
    core::pmr::String source{&core::defaultCpuMemoryResource()};
    core::pmr::String entry{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::pmr::String> includeDirs{&core::defaultCpuMemoryResource()};
    core::pmr::String reflectionModule{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::pmr::String> reflectionHeaders{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ScriptPluginServiceRequirement> requiredServices{
        &core::defaultCpuMemoryResource()};
    std::uint32_t moduleVersion = 1u;
    core::pmr::String exportContractHash{&core::defaultCpuMemoryResource()};
    core::pmr::String reflectionRegistryHash{&core::defaultCpuMemoryResource()};
};

bool parseManifest(const std::filesystem::path& path,
    const core::pmr::String& text,
    ParsedManifest& out,
    core::pmr::String& diagnostics)
{
    YamlNode root;
    try {
        root = YamlNode::deserialize(text.data(), text.data() + text.size());
    } catch (const std::exception& error) {
        diagnostics = pathDiagnostic("C++ plugin manifest parse failed: ", path);
        diagnostics += ": ";
        diagnostics += error.what();
        return false;
    }
    if (!root.is_mapping()) {
        diagnostics = pathDiagnostic("C++ plugin manifest root must be a mapping: ", path);
        return false;
    }

    const auto* type = findYamlValue(root, "type");
    const auto* contractVersion = findYamlValue(root, "contractVersion");
    const auto* module = findYamlValue(root, "module");
    const auto* source = findYamlValue(root, "source");
    const auto* entry = findYamlValue(root, "entry");
    if (type == nullptr || !type->is_string() || yamlString(*type) != "arp.script.cpp-plugin"
        || contractVersion == nullptr || !contractVersion->is_integer() || module == nullptr
        || !module->is_string() || source == nullptr || !source->is_string() || entry == nullptr
        || !entry->is_string()) {
        diagnostics = pathDiagnostic(
            "C++ plugin manifest requires type arp.script.cpp-plugin, contractVersion 2, module, source, and entry: ",
            path);
        return false;
    }
    if (contractVersion->get_value<YamlNode::integer_type>() != 2) {
        diagnostics = pathDiagnostic(
            "C++ plugin manifest contractVersion 1 is incompatible; expected contractVersion 2: ",
            path);
        return false;
    }

    out.module = yamlString(*module);
    out.source = yamlString(*source);
    out.entry = yamlString(*entry);
    out.reflectionModule = out.module;
    if (!validFlatIdentifier(std::string_view(out.module.data(), out.module.size()))
        || !validFlatIdentifier(std::string_view(out.entry.data(), out.entry.size()))) {
        diagnostics = pathDiagnostic(
            "C++ plugin module and entry must be flat identifiers (dots are not supported): ",
            path);
        return false;
    }

    if (const auto* moduleVersion = findYamlValue(root, "moduleVersion")) {
        if (!moduleVersion->is_integer()
            || moduleVersion->get_value<YamlNode::integer_type>() <= 0) {
            diagnostics = pathDiagnostic("C++ plugin moduleVersion must be positive: ", path);
            return false;
        }
        out.moduleVersion =
            static_cast<std::uint32_t>(moduleVersion->get_value<YamlNode::integer_type>());
    }
    if (const auto* hash = findYamlValue(root, "exportContractHash")) {
        if (!hash->is_string()) {
            diagnostics = pathDiagnostic("C++ plugin exportContractHash must be a string: ", path);
            return false;
        }
        out.exportContractHash = yamlString(*hash);
    }
    if (out.exportContractHash.empty()) {
        diagnostics = pathDiagnostic(
            "C++ plugin manifest requires exportContractHash in contract v2: ", path);
        return false;
    }
    if (const auto* reflectionHash = findYamlValue(root, "reflectionRegistryHash")) {
        if (!reflectionHash->is_string()) {
            diagnostics =
                pathDiagnostic("C++ plugin reflectionRegistryHash must be a string: ", path);
            return false;
        }
        out.reflectionRegistryHash = yamlString(*reflectionHash);
    }
    const auto* serviceList = findYamlValue(root, "requiredServices");
    if (serviceList == nullptr)
        serviceList = findYamlValue(root, "services");
    if (serviceList != nullptr) {
        if (!serviceList->is_sequence()) {
            diagnostics = pathDiagnostic("C++ plugin requiredServices must be a sequence: ", path);
            return false;
        }
        for (const auto& service : serviceList->get_value_ref<const YamlNode::sequence_type&>()) {
            if (!service.is_mapping()) {
                diagnostics =
                    pathDiagnostic("C++ plugin requiredServices entries must be mappings: ", path);
                return false;
            }
            const auto* id = findYamlValue(service, "id");
            const auto* version = findYamlValue(service, "version");
            if (id == nullptr || !id->is_string() || version == nullptr || !version->is_integer()
                || version->get_value<YamlNode::integer_type>() <= 0) {
                diagnostics =
                    pathDiagnostic("C++ plugin service requires id and positive version: ", path);
                return false;
            }
            out.requiredServices.push_back(ScriptPluginServiceRequirement{yamlString(*id),
                static_cast<std::uint32_t>(version->get_value<YamlNode::integer_type>())});
        }
    }

    if (const auto* includeDirs = findYamlValue(root, "includeDirs")) {
        if (!includeDirs->is_sequence()) {
            diagnostics = pathDiagnostic("C++ plugin includeDirs must be a sequence: ", path);
            return false;
        }
        for (const auto& includeDir :
            includeDirs->get_value_ref<const YamlNode::sequence_type&>()) {
            if (!includeDir.is_string()) {
                diagnostics =
                    pathDiagnostic("C++ plugin includeDirs entries must be strings: ", path);
                return false;
            }
            const auto value = yamlString(includeDir);
            const std::filesystem::path includePath(value.c_str());
            if (includePath.empty() || hasParentTraversal(includePath)) {
                diagnostics = pathDiagnostic(
                    "C++ plugin includeDirs cannot escape the manifest directory: ", path);
                return false;
            }
            out.includeDirs.push_back(value);
        }
    }
    if (const auto* reflectionModule = findYamlValue(root, "reflectionModule")) {
        if (!reflectionModule->is_string()) {
            diagnostics = pathDiagnostic("C++ plugin reflectionModule must be a string: ", path);
            return false;
        }
        out.reflectionModule = yamlString(*reflectionModule);
    }
    if (!validFlatIdentifier(
            std::string_view(out.reflectionModule.data(), out.reflectionModule.size()))) {
        diagnostics =
            pathDiagnostic("C++ plugin reflectionModule must be a flat identifier: ", path);
        return false;
    }
    if (const auto* reflectionHeaders = findYamlValue(root, "reflectionHeaders")) {
        if (!reflectionHeaders->is_sequence()) {
            diagnostics = pathDiagnostic("C++ plugin reflectionHeaders must be a sequence: ", path);
            return false;
        }
        for (const auto& header :
            reflectionHeaders->get_value_ref<const YamlNode::sequence_type&>()) {
            if (!header.is_string()) {
                diagnostics =
                    pathDiagnostic("C++ plugin reflectionHeaders entries must be strings: ", path);
                return false;
            }
            const auto value = yamlString(header);
            const std::filesystem::path headerPath(value.c_str());
            if (headerPath.empty() || headerPath.is_absolute() || hasParentTraversal(headerPath)) {
                diagnostics = pathDiagnostic(
                    "C++ plugin reflectionHeaders cannot escape the manifest directory: ", path);
                return false;
            }
            out.reflectionHeaders.push_back(value);
        }
    }
    return true;
}

struct PluginLifetime final {
#if defined(_WIN32) || defined(__CYGWIN__)
    HMODULE hostHandle = nullptr;
    HMODULE pluginHandle = nullptr;
#else
    void* hostHandle = nullptr;
    void* pluginHandle = nullptr;
#endif
    ~PluginLifetime()
    {
#if defined(_WIN32) || defined(__CYGWIN__)
        if (pluginHandle != nullptr)
            FreeLibrary(pluginHandle);
        if (hostHandle != nullptr)
            FreeLibrary(hostHandle);
#else
        if (pluginHandle != nullptr)
            dlclose(pluginHandle);
        if (hostHandle != nullptr)
            dlclose(hostHandle);
#endif
    }
};

class PublishedServiceFreePluginRegistry final {
public:
    std::shared_ptr<ModuleExports> find(std::string_view identity)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const core::pmr::String key(
            identity.data(), identity.size(), &core::defaultCpuMemoryResource());
        const auto found = modules_.find(key);
        return found == modules_.end() ? nullptr : found->second;
    }

    bool publish(std::string_view identity, const std::shared_ptr<ModuleExports>& exports)
    {
        if (!exports)
            return false;
        std::lock_guard<std::mutex> lock(mutex_);
        core::pmr::String key(identity.data(), identity.size(), &core::defaultCpuMemoryResource());
        const auto found = modules_.find(key);
        if (found != modules_.end())
            return found->second == exports;
        modules_.emplace(std::move(key), exports);
        return true;
    }

private:
    std::mutex mutex_;
    core::pmr::Map<core::pmr::String, std::shared_ptr<ModuleExports>> modules_{
        &core::defaultCpuMemoryResource()};
};

PublishedServiceFreePluginRegistry& publishedServiceFreePlugins()
{
    static PublishedServiceFreePluginRegistry registry;
    return registry;
}

} // namespace

struct CppPluginImporter::Impl final {
    explicit Impl(CppPluginImporterDesc value) : desc(std::move(value)) {}

    CppPluginImporterDesc desc;
    std::recursive_mutex mutex;
    core::pmr::Map<core::pmr::String, std::shared_ptr<ModuleExports>> modules{
        &core::defaultCpuMemoryResource()};
    core::pmr::String lastDiagnostic{&core::defaultCpuMemoryResource()};
};

CppPluginImporterDesc::CppPluginImporterDesc() : compilerCommand(defaultCompilerCommand())
{}

CppPluginImporter::CppPluginImporter(CppPluginImporterDesc desc)
    : impl_(core::makeUnique<Impl>(std::move(desc)))
{}

CppPluginImporter::~CppPluginImporter() = default;
CppPluginImporter::CppPluginImporter(CppPluginImporter&&) noexcept = default;
CppPluginImporter& CppPluginImporter::operator=(CppPluginImporter&&) noexcept = default;

std::shared_ptr<ModuleExports> CppPluginImporter::loadModule(const core::pmr::String& moduleName)
{
    std::lock_guard<std::recursive_mutex> lock(impl_->mutex);
    // Toolchain work (reading tool binaries, generating reflection, and
    // compiling a host plugin) is host-owned and must not consume the active
    // ScriptVM invocation quota. Values copied into the script context after
    // this function returns are still charged by the caller's quota scope.
    core::CpuMemoryQuotaScope hostToolchainQuota(nullptr);
    static thread_local std::uint32_t consistencyAttempt = 0u;
    constexpr std::uint32_t kMaxConsistencyAttempts = 3u;
    impl_->lastDiagnostic.clear();
    const auto cached = impl_->modules.find(moduleName);
    if (cached != impl_->modules.end()) {
        return cached->second;
    }
    if (impl_->desc.sourceRoot.empty() || impl_->desc.cacheRoot.empty()
        || impl_->desc.includeRoot.empty() || impl_->desc.hostLibrary.empty()) {
        impl_->lastDiagnostic =
            "C++ plugin importer requires sourceRoot, cacheRoot, includeRoot, and hostLibrary";
        return nullptr;
    }

    const core::pmr::String moduleNameText(
        moduleName.data(), moduleName.size(), &core::defaultCpuMemoryResource());
    const auto moduleDirectory = impl_->desc.sourceRoot / moduleNameText;
    const auto primaryFileName = moduleNameText + "_module.arpplugin";
    const auto legacyFileName = moduleNameText + ".arpplugin";
    const auto primaryManifestPath = impl_->desc.moduleDirectoryLayout
        ? moduleDirectory / primaryFileName
        : impl_->desc.sourceRoot / primaryFileName;
    const auto legacyManifestPath = impl_->desc.moduleDirectoryLayout
        ? moduleDirectory / legacyFileName
        : impl_->desc.sourceRoot / legacyFileName;
    auto manifestPath = primaryManifestPath;
    auto manifestRead = core::readTextFile(manifestPath);
    if (!manifestRead.success) {
        manifestPath = legacyManifestPath;
        manifestRead = core::readTextFile(manifestPath);
    }
    if (!manifestRead.success) {
        // Existing repositories may use a descriptive filename such as
        // \`arp.scene_module.arpplugin\` while the manifest module identifier
        // remains the required flat \`arp_scene\`. Resolve that filename from
        // the manifest field, never from a dotted import name.
        std::error_code directoryError;
        for (const auto& candidateEntry :
            std::filesystem::directory_iterator(impl_->desc.sourceRoot, directoryError)) {
            if (directoryError || !candidateEntry.is_regular_file()
                || candidateEntry.path().extension() != ".arpplugin") {
                continue;
            }
            const auto candidateRead = core::readTextFile(candidateEntry.path());
            if (!candidateRead.success) {
                continue;
            }
            ParsedManifest candidateManifest;
            core::pmr::String candidateDiagnostics(&core::defaultCpuMemoryResource());
            if (parseManifest(candidateEntry.path(),
                    candidateRead.text,
                    candidateManifest,
                    candidateDiagnostics)
                && candidateManifest.module == moduleName) {
                manifestPath = candidateEntry.path();
                manifestRead = candidateRead;
                break;
            }
        }
    }
    if (!manifestRead.success) {
        impl_->lastDiagnostic = manifestRead.diagnostics;
        return nullptr;
    }
    ParsedManifest manifest;
    if (!parseManifest(manifestPath, manifestRead.text, manifest, impl_->lastDiagnostic)) {
        return nullptr;
    }
    if (manifest.module != moduleName) {
        impl_->lastDiagnostic =
            pathDiagnostic("C++ plugin manifest module does not match import name: ", manifestPath);
        return nullptr;
    }

    const std::filesystem::path sourceRelative(manifest.source.c_str());
    if (sourceRelative.empty() || sourceRelative.is_absolute()
        || hasParentTraversal(sourceRelative)) {
        impl_->lastDiagnostic = pathDiagnostic(
            "C++ plugin source must be a relative path inside the manifest directory: ",
            manifestPath);
        return nullptr;
    }
    const auto sourcePath = manifestPath.parent_path() / sourceRelative;
    const auto sourceRead = core::readTextFile(sourcePath);
    if (!sourceRead.success) {
        impl_->lastDiagnostic = sourceRead.diagnostics;
        return nullptr;
    }
    if (manifest.reflectionHeaders.empty()) {
        impl_->lastDiagnostic = pathDiagnostic(
            "C++ plugin manifest requires non-empty reflectionHeaders; all script-visible static exports must be code-generated: ",
            manifestPath);
        return nullptr;
    }
    core::pmr::Vector<core::pmr::String> reflectionHeaderSources{&core::defaultCpuMemoryResource()};
    const auto resolveReflectionHeader = [&](const core::pmr::String& header) {
        const std::filesystem::path relative(header.c_str());
        const auto manifestRelative = manifestPath.parent_path() / relative;
        if (std::filesystem::exists(manifestRelative)) {
            return manifestRelative;
        }
        // Domain manifests use `src/...` to name repository source headers so
        // the same manifest can drive CMake and the dynamic importer. The
        // fallback is rooted at the host's declared includeRoot and never
        // accepts parent traversal.
        const core::pmr::String value(
            header.data(), header.size(), &core::defaultCpuMemoryResource());
        if (value.rfind("src/", 0) == 0 && !impl_->desc.includeRoot.empty()) {
            return impl_->desc.includeRoot / value.substr(4).c_str();
        }
        return manifestRelative;
    };
    for (const auto& header : manifest.reflectionHeaders) {
        const auto headerPath = resolveReflectionHeader(header);
        const auto headerRead = core::readTextFile(headerPath);
        if (!headerRead.success) {
            impl_->lastDiagnostic = headerRead.diagnostics;
            return nullptr;
        }
        reflectionHeaderSources.push_back(headerRead.text);
    }
    if (impl_->desc.compilerCommand.empty()) {
        impl_->lastDiagnostic = "C++ plugin compilerCommand is empty";
        return nullptr;
    }

    std::filesystem::path reflectionCodegen = impl_->desc.reflectionCodegenCommand;
    if (reflectionCodegen.empty()) {
#if defined(_WIN32) || defined(__CYGWIN__)
        reflectionCodegen = impl_->desc.hostLibrary.parent_path() / "arp_reflect_codegen.exe";
#else
        reflectionCodegen = impl_->desc.hostLibrary.parent_path() / "arp_reflect_codegen";
#endif
    }
    if (!manifest.reflectionHeaders.empty() && !std::filesystem::exists(reflectionCodegen)) {
        impl_->lastDiagnostic =
            pathDiagnostic("C++ plugin reflection code generator not found: ", reflectionCodegen);
        return nullptr;
    }

    const auto hostLinkInput = hostLinkInputPath(impl_->desc.hostLibrary);
    const auto preliminaryHostHash = fileVersionFingerprint(impl_->desc.hostLibrary);
    const auto preliminaryHostLinkHash = fileVersionFingerprint(hostLinkInput);
    if (preliminaryHostHash == "missing" || preliminaryHostLinkHash == "missing") {
        impl_->lastDiagnostic = "C++ plugin host library or deterministic link input is missing";
        return nullptr;
    }
    const auto preliminaryDigest = digestName(manifestRead.text,
        sourceRead.text,
        impl_->desc.compilerCommand,
        impl_->desc.includeRoot,
        preliminaryHostHash,
        preliminaryHostLinkHash,
        impl_->desc.additionalIncludeRoots,
        manifest.reflectionModule,
        reflectionHeaderSources,
        fileVersionFingerprint(reflectionCodegen),
        impl_->desc.moduleDirectoryLayout);
    if (preliminaryDigest.empty()) {
        impl_->lastDiagnostic =
            "C++ plugin cache identity could not be materialized from declared inputs";
        return nullptr;
    }
    const auto outputDir = impl_->desc.cacheRoot / manifest.module.c_str();
    std::error_code error;
    std::filesystem::create_directories(outputDir, error);
    if (error) {
        impl_->lastDiagnostic =
            pathDiagnostic("failed to create C++ plugin cache directory: ", outputDir);
        return nullptr;
    }

    const auto lockPath = outputDir / ("." + preliminaryDigest + ".lock");
    auto artifactLock = core::makeUnique<core::ProcessIdentityLock>(
        lockPath, std::string_view(preliminaryDigest.data(), preliminaryDigest.size()));
    if (!artifactLock->locked()) {
        impl_->lastDiagnostic =
            pathDiagnostic("failed to acquire C++ plugin cache identity lock: ", outputDir);
        return nullptr;
    }
    const auto retryConsistency = [&](std::string_view diagnostic) {
        artifactLock.reset();
        if (++consistencyAttempt >= kMaxConsistencyAttempts) {
            --consistencyAttempt;
            impl_->lastDiagnostic.assign(diagnostic.data(), diagnostic.size());
            return std::shared_ptr<ModuleExports>{};
        }
        auto retry = loadModule(moduleName);
        --consistencyAttempt;
        return retry;
    };
    const auto hostInputsUnchanged = [&] {
        return fileVersionFingerprint(impl_->desc.hostLibrary) == preliminaryHostHash
            && fileVersionFingerprint(hostLinkInput) == preliminaryHostLinkHash;
    };
    // The identity lock makes this directory single-writer. Keep the path
    // deliberately short: Windows toolchains still apply MAX_PATH to the
    // compiler's log/output handles even when the final DLL path is valid.
    constexpr std::size_t kStagingIdentityTagLength = 16u;
    const auto stagingDirectory = outputDir
        / (".s-" + preliminaryDigest.substr(0u, kStagingIdentityTagLength) + "-"
            + uniqueStagingSuffix());
    std::filesystem::remove_all(stagingDirectory, error);
    if (error) {
        impl_->lastDiagnostic = pathDiagnostic(
            "failed to clear C++ plugin identity staging directory: ", stagingDirectory);
        return nullptr;
    }
    std::filesystem::create_directories(stagingDirectory, error);
    if (error) {
        impl_->lastDiagnostic = pathDiagnostic(
            "failed to create C++ plugin identity staging directory: ", stagingDirectory);
        return nullptr;
    }
    StagingDirectoryScope stagingScope(stagingDirectory);

    core::ImmutableBuildInputSnapshot snapshot(stagingDirectory / "i");
    core::pmr::String snapshotDiagnostics{&core::defaultCpuMemoryResource()};
    const auto moduleRoot = manifestPath.parent_path();
    const auto snapshotModuleRoot = snapshot.path("module-root");
    const auto snapshotIncludeRoot = snapshot.path("include-root");
    const auto compilerExecutable = resolveCommandPath(impl_->desc.compilerCommand);
    if (compilerExecutable.empty()) {
        impl_->lastDiagnostic =
            "C++ plugin compiler executable could not be resolved for snapshot capture";
        return nullptr;
    }
    const auto codegenFileName = reflectionCodegen.filename().generic_string();
    auto& memoryResource = core::defaultCpuMemoryResource();
    const auto codegenLogicalName = core::concatPmr({"tools/", codegenFileName}, &memoryResource);
    bool capturedHostLinkInput = true;
#if defined(_WIN32) || defined(__CYGWIN__)
    capturedHostLinkInput =
        snapshot.captureFile("host/link-input", hostLinkInput, snapshotDiagnostics);
#endif
    if (!snapshot.captureDirectory("module-root", moduleRoot, snapshotDiagnostics)
        || !snapshot.captureDirectory("include-root", impl_->desc.includeRoot, snapshotDiagnostics)
        || !capturedHostLinkInput
        || !snapshot.captureExecutable(
            codegenLogicalName, reflectionCodegen, snapshotDiagnostics)) {
        impl_->lastDiagnostic = snapshotDiagnostics;
        return nullptr;
    }
    const auto compilerSnapshot =
        core::CompilerToolchainLayout().captureSnapshot(snapshot, "tc", compilerExecutable);
    if (!compilerSnapshot.success) {
        impl_->lastDiagnostic = compilerSnapshot.diagnostics;
        return nullptr;
    }
    core::pmr::Vector<std::filesystem::path> snapshotAdditionalIncludeRoots{
        &core::defaultCpuMemoryResource()};
    for (std::size_t index = 0u; index < impl_->desc.additionalIncludeRoots.size(); ++index) {
        core::pmr::String logical("additional-include-", &core::defaultCpuMemoryResource());
        core::appendUnsigned(logical, index);
        if (!snapshot.captureDirectory(
                logical, impl_->desc.additionalIncludeRoots[index], snapshotDiagnostics)) {
            impl_->lastDiagnostic = snapshotDiagnostics;
            return nullptr;
        }
        snapshotAdditionalIncludeRoots.push_back(snapshot.path(logical));
    }

    const auto snapshotManifestPath = snapshotModuleRoot / manifestPath.filename();
    const auto snapshotSourcePath = snapshotModuleRoot / sourceRelative;
    const auto snapshotManifestRead = core::readTextFile(snapshotManifestPath);
    const auto snapshotSourceRead = core::readTextFile(snapshotSourcePath);
    if (!snapshotManifestRead.success || !snapshotSourceRead.success) {
        impl_->lastDiagnostic = "C++ plugin immutable snapshot is missing manifest or source";
        return nullptr;
    }
    const auto snapshotPathFor = [&](const std::filesystem::path& original) {
        const auto moduleRelative = original.lexically_relative(moduleRoot);
        if (!moduleRelative.empty() && *moduleRelative.begin() != "..")
            return snapshotModuleRoot / moduleRelative;
        const auto includeRelative = original.lexically_relative(impl_->desc.includeRoot);
        if (!includeRelative.empty() && *includeRelative.begin() != "..")
            return snapshotIncludeRoot / includeRelative;
        return std::filesystem::path{};
    };
    core::pmr::Vector<core::pmr::String> snapshotHeaderSources{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::filesystem::path> snapshotHeaderPaths{&core::defaultCpuMemoryResource()};
    for (const auto& header : manifest.reflectionHeaders) {
        const auto path = snapshotPathFor(resolveReflectionHeader(header));
        const auto read = core::readTextFile(path);
        if (path.empty() || !read.success) {
            impl_->lastDiagnostic =
                "C++ plugin reflection header is outside the immutable snapshot";
            return nullptr;
        }
        snapshotHeaderPaths.push_back(path);
        snapshotHeaderSources.push_back(read.text);
    }
    const auto snapshotCompilerPath = compilerSnapshot.compilerExecutable;
    const auto snapshotCompilerPathText = snapshotCompilerPath.generic_string();
    core::pmr::String snapshotCompilerCommand(snapshotCompilerPathText.data(),
        snapshotCompilerPathText.size(),
        &core::defaultCpuMemoryResource());
    const auto snapshotCodegen = snapshot.path(codegenLogicalName);
#if defined(_WIN32) || defined(__CYGWIN__)
    const auto snapshotHostLinkInput = snapshot.path("host/link-input");
#else
    const auto& snapshotHostLinkInput = hostLinkInput;
#endif
    const auto snapshotInputDigest = digestName(snapshotManifestRead.text,
        snapshotSourceRead.text,
        snapshotCompilerCommand,
        snapshotIncludeRoot,
        fileVersionFingerprint(impl_->desc.hostLibrary),
        fileVersionFingerprint(snapshotHostLinkInput),
        snapshotAdditionalIncludeRoots,
        manifest.reflectionModule,
        snapshotHeaderSources,
        fileVersionFingerprint(snapshotCodegen),
        impl_->desc.moduleDirectoryLayout);
    if (snapshotInputDigest.empty() || snapshotInputDigest != preliminaryDigest) {
        return retryConsistency(
            "C++ plugin inputs did not stabilize within the consistency policy");
    }
    core::pmr::String artifactContract(snapshotInputDigest, &core::defaultCpuMemoryResource());
    artifactContract.push_back('\n');
    artifactContract += snapshot.fingerprint();
    auto artifactIdentity =
        sha256Contract(std::string_view(artifactContract.data(), artifactContract.size()));
    artifactIdentity.erase(0u, 7u);
    const auto& digest = artifactIdentity;
    const auto outputPath = dynamicLibraryPath(outputDir,
        std::string_view(manifest.module.data(), manifest.module.size()),
        std::string_view(digest.data(), digest.size()));

    core::pmr::String reflectionStem(
        manifest.module.data(), manifest.module.size(), &core::defaultCpuMemoryResource());
    reflectionStem += "-";
    reflectionStem += digest;
    reflectionStem += ".reflection";
    const auto reflectionMetaPath = outputDir / (reflectionStem + ".json").c_str();
    const auto generatedReflectionPath = outputDir / (reflectionStem + ".gen.cpp").c_str();
    const auto stagedReflectionMetaPath = stagingDirectory / "reflection.json";
    const auto stagedGeneratedReflectionPath = stagingDirectory / "reflection.gen.cpp";
    const auto stagedArtifact = stagingDirectory / "plugin.dll";
    auto completionMarker = outputPath;
    completionMarker += ".complete";
    const bool artifactReady = nonEmptyRegularFile(outputPath)
        && markerMatches(completionMarker,
            outputPath,
            std::string_view(digest.data(), digest.size()),
            generatedReflectionPath,
            reflectionMetaPath);
    if (!hostInputsUnchanged()) {
        return retryConsistency(
            "C++ plugin host inputs changed before tool execution on every consistency attempt");
    }
    if (!artifactReady && !manifest.reflectionHeaders.empty()) {
        const auto reflectionLogPath = stagedGeneratedReflectionPath;
        auto reflectionLogFile = reflectionLogPath;
        reflectionLogFile += ".log";
        error.clear();
        std::filesystem::create_directories(reflectionLogFile.parent_path(), error);
        if (error) {
            impl_->lastDiagnostic = pathDiagnostic(
                "failed to prepare C++ plugin reflection staging directory: ", reflectionLogFile);
            return nullptr;
        }
        core::ProcessRunRequest request;
        request.executable = snapshotCodegen;
        request.outputFile = reflectionLogFile;
        addProcessArgument(request, "--module");
        addProcessArgument(request,
            std::string_view(manifest.reflectionModule.data(), manifest.reflectionModule.size()));
        addProcessArgument(request, "--out-json");
        addProcessPathArgument(request, stagedReflectionMetaPath);
        addProcessArgument(request, "--out-cpp");
        addProcessPathArgument(request, stagedGeneratedReflectionPath);
        for (const auto& header : snapshotHeaderPaths) {
            addProcessArgument(request, "--header");
            addProcessPathArgument(request, header);
        }
        const auto process = core::ProcessRunner().run(request);
        if (!process.ok() || !nonEmptyRegularFile(stagedGeneratedReflectionPath)
            || !nonEmptyRegularFile(stagedReflectionMetaPath)) {
            impl_->lastDiagnostic = pathDiagnostic(
                "failed to generate C++ plugin reflection; see log: ", reflectionLogFile);
            if (!process.diagnostic.empty()) {
                impl_->lastDiagnostic += "; runner=";
                impl_->lastDiagnostic += process.diagnostic;
            }
            appendBoundedProcessLog(impl_->lastDiagnostic, reflectionLogFile);
            return nullptr;
        }
    }

    core::pmr::String reflectionHash(
        manifest.reflectionRegistryHash, &core::defaultCpuMemoryResource());
    if (!manifest.reflectionHeaders.empty()) {
        const auto& metadataInput = artifactReady ? reflectionMetaPath : stagedReflectionMetaPath;
        reflectionHash = reflectionRegistryFingerprint(metadataInput);
        if (reflectionHash.empty()) {
            impl_->lastDiagnostic = pathDiagnostic(
                "generated C++ plugin reflection metadata is missing: ", reflectionMetaPath);
            return nullptr;
        }
    }

    if (!artifactReady) {
        if (!hostInputsUnchanged()) {
            return retryConsistency(
                "C++ plugin host inputs changed before compilation on every consistency attempt");
        }
        auto logPath = stagedArtifact;
        logPath += ".compile.log";
        error.clear();
        std::filesystem::create_directories(logPath.parent_path(), error);
        if (error) {
            impl_->lastDiagnostic =
                pathDiagnostic("failed to prepare C++ plugin compile staging directory: ", logPath);
            return nullptr;
        }
        core::ProcessRunRequest request;
        request.executable = std::filesystem::path(snapshotCompilerCommand.c_str());
        request.outputFile = logPath;
        core::CompilerToolchainLayout::appendCompilerArguments(compilerSnapshot.layout, request);
        addProcessArgument(request, "-std=c++17");
        addProcessArgument(request, "-shared");
#if defined(__MINGW32__)
        // Reflection-heavy plugins can exceed COFF's default section count.
        addProcessArgument(request, "-Wa,-mbig-obj");
#endif
#if !defined(_WIN32) && !defined(__CYGWIN__)
        addProcessArgument(request, "-fPIC");
#endif
        core::pmr::String includeArgument("-I", &core::defaultCpuMemoryResource());
        includeArgument += snapshotIncludeRoot.generic_string().c_str();
        addProcessArgument(request, includeArgument);
        for (const auto& includeRoot : snapshotAdditionalIncludeRoots) {
            core::pmr::String argument("-I", &core::defaultCpuMemoryResource());
            argument += includeRoot.generic_string().c_str();
            addProcessArgument(request, argument);
        }
        for (const auto& includeDir : manifest.includeDirs) {
            const auto includePath = snapshotModuleRoot / includeDir.c_str();
            core::pmr::String argument("-I", &core::defaultCpuMemoryResource());
            argument += includePath.generic_string().c_str();
            addProcessArgument(request, argument);
        }
        // Keep the translation unit before the host import library. MinGW's
        // one-pass archive processing otherwise sees -larp_engine before the
        // plugin references exist and leaves the reflection symbols unresolved.
        addProcessPathArgument(request, snapshotSourcePath);
        if (!manifest.reflectionHeaders.empty()) {
            addProcessPathArgument(request, stagedGeneratedReflectionPath);
        }
        addProcessPathArgument(request, snapshotHostLinkInput);
#if !defined(_WIN32) && !defined(__CYGWIN__)
        core::pmr::String rpathArgument("-Wl,-rpath,", &core::defaultCpuMemoryResource());
        rpathArgument += impl_->desc.hostLibrary.parent_path().generic_string().c_str();
        addProcessArgument(request, rpathArgument);
#endif
        addProcessArgument(request, "-o");
        addProcessPathArgument(request, stagedArtifact);
        const auto process = core::ProcessRunner().run(request);
        if (!process.ok() || !nonEmptyRegularFile(stagedArtifact)) {
            impl_->lastDiagnostic =
                pathDiagnostic("failed to compile C++ plugin; see log: ", logPath);
            if (!process.diagnostic.empty()) {
                impl_->lastDiagnostic += "; runner=";
                impl_->lastDiagnostic += process.diagnostic;
            }
            appendBoundedProcessLog(impl_->lastDiagnostic, logPath);
            return nullptr;
        }
    }

    if (!snapshot.verifySourcesUnchanged(snapshotDiagnostics)) {
        core::pmr::String diagnostic("C++ plugin inputs changed during every consistency attempt",
            &core::defaultCpuMemoryResource());
        if (!snapshotDiagnostics.empty()) {
            diagnostic += ": ";
            diagnostic += snapshotDiagnostics;
        }
        return retryConsistency(diagnostic);
    }
    if (!hostInputsUnchanged()) {
        return retryConsistency("C++ plugin host inputs changed during every consistency attempt");
    }

    if (!artifactReady) {
        if ((!manifest.reflectionHeaders.empty()
                && (!atomicReplaceFile(stagedGeneratedReflectionPath, generatedReflectionPath)
                    || !atomicReplaceFile(stagedReflectionMetaPath, reflectionMetaPath)))
            || !atomicReplaceFile(stagedArtifact, outputPath)) {
            std::error_code cleanupError;
            std::filesystem::remove(completionMarker, cleanupError);
            impl_->lastDiagnostic =
                pathDiagnostic("failed to publish complete C++ plugin artifact set: ", outputPath);
            return nullptr;
        }
        if (!publishIdentityMarker(completionMarker,
                outputPath,
                std::string_view(digest.data(), digest.size()),
                generatedReflectionPath,
                reflectionMetaPath)) {
            impl_->lastDiagnostic = pathDiagnostic(
                "failed to publish C++ plugin completion marker: ", completionMarker);
            return nullptr;
        }
    }

    const auto expectedHostHash = preliminaryHostHash;
    const auto expectedPluginHash = fileVersionFingerprint(outputPath);
    if (expectedHostHash == "missing" || expectedPluginHash == "missing"
        || fileVersionFingerprint(impl_->desc.hostLibrary) != expectedHostHash) {
        impl_->lastDiagnostic = "C++ plugin host or artifact identity changed before module load";
        return nullptr;
    }

    for (const auto& service : manifest.requiredServices) {
        if (impl_->desc.services == nullptr
            || impl_->desc.services->find(service.serviceId, service.version) == nullptr) {
            impl_->lastDiagnostic = "C++ plugin requires unavailable service ";
            impl_->lastDiagnostic += service.serviceId;
            impl_->lastDiagnostic += "/v";
            impl_->lastDiagnostic += std::to_string(service.version).c_str();
            return nullptr;
        }
    }
    if (manifest.requiredServices.empty()) {
        auto published =
            publishedServiceFreePlugins().find(std::string_view(digest.data(), digest.size()));
        if (published) {
            impl_->modules.emplace(moduleName, published);
            return published;
        }
    }

    auto lifetime = core::makeShared<PluginLifetime>();
#if defined(_WIN32) || defined(__CYGWIN__)
    lifetime->hostHandle = LoadLibraryA(impl_->desc.hostLibrary.string().c_str());
    if (lifetime->hostHandle == nullptr) {
        impl_->lastDiagnostic =
            pathDiagnostic("failed to load C++ plugin host library: ", impl_->desc.hostLibrary);
        return nullptr;
    }
    lifetime->pluginHandle = LoadLibraryA(outputPath.string().c_str());
    if (lifetime->pluginHandle == nullptr) {
        impl_->lastDiagnostic = pathDiagnostic("failed to load C++ plugin library: ", outputPath);
        return nullptr;
    }
    if (!loadedModulePathMatches(lifetime->hostHandle, impl_->desc.hostLibrary)
        || !loadedModulePathMatches(lifetime->pluginHandle, outputPath)) {
        impl_->lastDiagnostic = "C++ plugin loaded module path does not match its cache identity";
        return nullptr;
    }
    auto entry = reinterpret_cast<ArpScriptPluginEntryV2>(
        GetProcAddress(lifetime->pluginHandle, manifest.entry.c_str()));
#else
    lifetime->hostHandle = dlopen(impl_->desc.hostLibrary.string().c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (lifetime->hostHandle == nullptr) {
        impl_->lastDiagnostic =
            pathDiagnostic("failed to load C++ plugin host library: ", impl_->desc.hostLibrary);
        return nullptr;
    }
    lifetime->pluginHandle = dlopen(outputPath.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (lifetime->pluginHandle == nullptr) {
        impl_->lastDiagnostic = pathDiagnostic("failed to load C++ plugin library: ", outputPath);
        return nullptr;
    }
    auto entry = reinterpret_cast<ArpScriptPluginEntryV2>(
        dlsym(lifetime->pluginHandle, manifest.entry.c_str()));
#endif
    if (fileVersionFingerprint(impl_->desc.hostLibrary) != expectedHostHash
        || fileVersionFingerprint(outputPath) != expectedPluginHash) {
        impl_->lastDiagnostic = "C++ plugin host or artifact identity changed during module load";
        return nullptr;
    }
    if (entry == nullptr) {
        impl_->lastDiagnostic = pathDiagnostic("C++ plugin entry point not found: ", outputPath);
        return nullptr;
    }

    arp::reflection::RegistrationTransaction reflectionTransaction(
        std::string_view(manifest.module.data(), manifest.module.size()),
        std::string_view(digest.data(), digest.size()));
    if (!manifest.reflectionHeaders.empty())
        reflectionTransaction.requireDeclaredContracts();
    ScriptPluginRegistrar registrar(
        core::pmr::String(
            manifest.module.data(), manifest.module.size(), &core::defaultCpuMemoryResource()),
        impl_->desc.services);
    ArpScriptPluginHostV2 host{
        ARP_SCRIPT_PLUGIN_ABI_VERSION_V2,
        &registrar,
        impl_->desc.services,
        impl_->desc.platform.empty() ? "native" : impl_->desc.platform.c_str(),
        impl_->desc.buildConfiguration.empty() ? "debug" : impl_->desc.buildConfiguration.c_str(),
        "script,reflection,services",
    };
    int registrationResult = 0;
    {
        arp::reflection::RegistrationScope registrationScope(reflectionTransaction);
        try {
            registrationResult = entry(&host);
        } catch (const std::exception& error) {
            impl_->lastDiagnostic = "C++ plugin reflection/module registration threw: ";
            impl_->lastDiagnostic += error.what();
            return nullptr;
        } catch (...) {
            impl_->lastDiagnostic =
                "C++ plugin reflection/module registration threw a non-standard exception";
            return nullptr;
        }
    }
    if (!reflectionTransaction.valid()) {
        impl_->lastDiagnostic = "C++ plugin reflection transaction rejected registration: ";
        impl_->lastDiagnostic += reflectionTransaction.diagnostics();
        return nullptr;
    }
    if (registrationResult == 0) {
        impl_->lastDiagnostic = pathDiagnostic(
            "C++ plugin reflection/module registration rejected module: ", outputPath);
        return nullptr;
    }
    if (registrar.explicitDomainExportCount() == 0u) {
        impl_->lastDiagnostic =
            pathDiagnostic("C++ plugin registered no explicit domain exports: ", outputPath);
        return nullptr;
    }

    const auto runtimeReceipt = reflectionTransaction.receipt();
    {
        arp::reflection::RegistrationScope validationScope(reflectionTransaction);
        if (!manifest.reflectionHeaders.empty()
            && !reflectionMetadataMatchesRuntimeReceipt(reflectionMetaPath,
                *registrar.reflectionModule(),
                runtimeReceipt,
                impl_->lastDiagnostic)) {
            return nullptr;
        }
    }
    reflectionHash = runtimeReceipt.reflectionContractFingerprint();
    if (!manifest.reflectionRegistryHash.empty()
        && manifest.reflectionRegistryHash != reflectionHash) {
        impl_->lastDiagnostic = pathDiagnostic(
            "C++ plugin reflectionRegistryHash does not match the validated runtime contract: ",
            manifestPath);
        return nullptr;
    }
    if (manifest.reflectionHeaders.empty() && !runtimeReceipt.classes.empty()) {
        impl_->lastDiagnostic = "C++ plugin declared reflection types without generated metadata";
        return nullptr;
    }

    if (!registrar.reflectionModule()->remapReflectionTypes(reflectionTransaction)) {
        impl_->lastDiagnostic = "C++ plugin reflection publication callable mapping failed";
        return nullptr;
    }
    auto exports = registrar.build();
    if (!exports || exports->symbols.empty()) {
        impl_->lastDiagnostic =
            pathDiagnostic("C++ plugin registered no module exports: ", outputPath);
        return nullptr;
    }
    exports->moduleVersion = manifest.moduleVersion;
    exports->exportContractHash = manifest.exportContractHash;
    exports->reflectionRegistryHash = std::move(reflectionHash);
    exports->requiredServices = manifest.requiredServices;
    exports->nativeLifetime = lifetime;

    core::pmr::Map<core::pmr::String, std::shared_ptr<ModuleExports>> pending{
        &core::defaultCpuMemoryResource()};
    pending.emplace(moduleName, exports);
    arp::reflection::RegistrationReceipt committedReceipt;
    core::pmr::String commitDiagnostics{&core::defaultCpuMemoryResource()};
    if (!reflectionTransaction.setOwner(lifetime)) {
        impl_->lastDiagnostic = "C++ plugin reflection owner staging failed: ";
        impl_->lastDiagnostic += reflectionTransaction.diagnostics();
        return nullptr;
    }
    const auto validatedReceiptFingerprint = runtimeReceipt.fingerprint();
    if (!reflectionTransaction.commit(
            validatedReceiptFingerprint, committedReceipt, commitDiagnostics)) {
        impl_->lastDiagnostic = "C++ plugin reflection commit failed: ";
        impl_->lastDiagnostic += commitDiagnostics;
        return nullptr;
    }
    if (manifest.requiredServices.empty()
        && !publishedServiceFreePlugins().publish(
            std::string_view(digest.data(), digest.size()), exports)) {
        impl_->lastDiagnostic = "C++ plugin publication registry rejected the committed module";
        return nullptr;
    }
    impl_->modules.merge(pending);
    return exports;
}

const core::pmr::String& CppPluginImporter::lastDiagnostics() const
{
    return impl_->lastDiagnostic;
}

} // namespace arp::scriptvm
