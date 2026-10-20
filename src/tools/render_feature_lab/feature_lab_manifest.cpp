#include "tools/render_feature_lab/feature_lab_manifest.h"

#include <cmath>
#include <fkYAML/Node.hpp>
#include <fstream>
#include <sstream>

namespace arp::tools::render_feature_lab {
namespace {

using YamlNode = fkyaml::node;

const YamlNode* field(const YamlNode& mapping, const char* name)
{
    if (!mapping.is_mapping())
        return nullptr;
    const auto& values = mapping.get_value_ref<const YamlNode::mapping_type&>();
    const auto found = values.find(YamlNode(name));
    return found == values.end() ? nullptr : &found->second;
}

core::pmr::String stringValue(const YamlNode& node)
{
    const auto& value = node.get_value_ref<const YamlNode::string_type&>();
    return core::pmr::String(value.data(), value.size());
}

bool appendStringSequence(const YamlNode* node, core::pmr::Vector<core::pmr::String>& output)
{
    if (node == nullptr)
        return true;
    if (!node->is_sequence())
        return false;
    const auto& values = node->get_value_ref<const YamlNode::sequence_type&>();
    for (const auto& value : values) {
        if (!value.is_string())
            return false;
        output.push_back(stringValue(value));
    }
    return true;
}

bool numberValue(const YamlNode& node, float& output)
{
    if (node.is_float_number()) {
        output = static_cast<float>(node.get_value<YamlNode::float_number_type>());
    } else if (node.is_integer()) {
        output = static_cast<float>(node.get_value<YamlNode::integer_type>());
    } else {
        return false;
    }
    return std::isfinite(output);
}

bool float3Value(const YamlNode* node, float output[3])
{
    if (node == nullptr || !node->is_sequence())
        return false;
    const auto& values = node->get_value_ref<const YamlNode::sequence_type&>();
    if (values.size() != 3u)
        return false;
    for (std::size_t index = 0u; index < 3u; ++index) {
        if (!numberValue(values[index], output[index]))
            return false;
    }
    return true;
}

bool parseCamera(const YamlNode* node, FeatureLabCamera& camera)
{
    if (node == nullptr)
        return true;
    if (!node->is_mapping() || !float3Value(field(*node, "position"), camera.position)
        || !float3Value(field(*node, "target"), camera.target)) {
        return false;
    }
    const auto* fov = field(*node, "vertical_fov_degrees");
    if (fov == nullptr || !numberValue(*fov, camera.verticalFovDegrees)
        || camera.verticalFovDegrees <= 1.0f || camera.verticalFovDegrees >= 179.0f) {
        return false;
    }
    camera.declared = true;
    return true;
}

bool parseAcceptance(const YamlNode* node, core::pmr::Vector<FeatureLabAcceptanceThreshold>& output)
{
    if (node == nullptr)
        return true;
    if (!node->is_sequence())
        return false;
    const auto& values = node->get_value_ref<const YamlNode::sequence_type&>();
    for (const auto& value : values) {
        const auto* attachment = field(value, "attachment");
        const auto* metric = field(value, "metric");
        if (attachment == nullptr || !attachment->is_string() || metric == nullptr
            || !metric->is_string()) {
            return false;
        }
        FeatureLabAcceptanceThreshold threshold;
        threshold.attachment = stringValue(*attachment);
        threshold.metric = stringValue(*metric);
        const auto* minimum = field(value, "min");
        const auto* maximum = field(value, "max");
        if ((minimum != nullptr && !numberValue(*minimum, threshold.minimum))
            || (maximum != nullptr && !numberValue(*maximum, threshold.maximum))
            || (minimum == nullptr && maximum == nullptr)
            || threshold.minimum > threshold.maximum) {
            return false;
        }
        output.push_back(std::move(threshold));
    }
    return true;
}

template <typename T, typename Equal>
void appendUnique(core::pmr::Vector<T>& output, const core::pmr::Vector<T>& input, Equal equal)
{
    for (const auto& value : input) {
        bool duplicate = false;
        for (const auto& existing : output)
            duplicate = duplicate || equal(existing, value);
        if (!duplicate)
            output.push_back(value);
    }
}

bool containsString(
    const core::pmr::Vector<core::pmr::String>& values, const core::pmr::String& value)
{
    for (const auto& candidate : values)
        if (candidate == value)
            return true;
    return false;
}

} // namespace

FeatureLabManifestResult loadFeatureLabManifest(const char* path)
{
    FeatureLabManifestResult result;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        result.diagnostics = "FEATURE-LAB-MANIFEST-NOT-FOUND";
        return result;
    }
    std::ostringstream text;
    text << input.rdbuf();
    YamlNode root;
    try {
        const auto source = text.str();
        root = YamlNode::deserialize(source.begin(), source.end());
    } catch (const std::exception& exception) {
        result.diagnostics = "FEATURE-LAB-MANIFEST-YAML-INVALID: ";
        result.diagnostics += exception.what();
        return result;
    }
    const auto* version = field(root, "version");
    const auto* cases = field(root, "cases");
    if (version == nullptr || !version->is_integer() || cases == nullptr || !cases->is_sequence()) {
        result.diagnostics = "FEATURE-LAB-MANIFEST-SCHEMA-INVALID";
        return result;
    }
    result.manifest.version =
        static_cast<std::uint32_t>(version->get_value<YamlNode::integer_type>());
    if (result.manifest.version != 1u) {
        result.diagnostics = "FEATURE-LAB-MANIFEST-VERSION-UNSUPPORTED";
        return result;
    }
    const auto& entries = cases->get_value_ref<const YamlNode::sequence_type&>();
    for (const auto& entry : entries) {
        const auto* id = field(entry, "id");
        if (id == nullptr || !id->is_string()) {
            result.diagnostics = "FEATURE-LAB-CASE-ID-REQUIRED";
            return result;
        }
        FeatureLabCase testCase;
        testCase.id = stringValue(*id);
        if (const auto* parent = field(entry, "parent")) {
            if (!parent->is_string()) {
                result.diagnostics = "FEATURE-LAB-CASE-PARENT-INVALID";
                return result;
            }
            testCase.parent = stringValue(*parent);
        }
        if (!appendStringSequence(field(entry, "features"), testCase.features)
            || !appendStringSequence(
                field(entry, "required_capabilities"), testCase.requiredCapabilities)
            || !appendStringSequence(field(entry, "attachments"), testCase.attachments)
            || !parseCamera(field(entry, "camera"), testCase.camera)
            || !parseAcceptance(field(entry, "acceptance"), testCase.acceptance)) {
            result.diagnostics = "FEATURE-LAB-CASE-SEQUENCE-INVALID";
            return result;
        }
        if (const auto* frames = field(entry, "frames")) {
            if (!frames->is_integer()) {
                result.diagnostics = "FEATURE-LAB-CASE-FRAMES-INVALID";
                return result;
            }
            testCase.defaultFrames =
                static_cast<std::uint32_t>(frames->get_value<YamlNode::integer_type>());
            if (testCase.defaultFrames == 0u) {
                result.diagnostics = "FEATURE-LAB-CASE-FRAMES-MUST-BE-NONZERO";
                return result;
            }
        }
        if (findFeatureLabCase(result.manifest, testCase.id) != nullptr) {
            result.diagnostics = "FEATURE-LAB-CASE-ID-DUPLICATE";
            return result;
        }
        result.manifest.cases.push_back(std::move(testCase));
    }
    result.success = true;
    return result;
}

const FeatureLabCase* findFeatureLabCase(
    const FeatureLabManifest& manifest, const core::pmr::String& id) noexcept
{
    for (const auto& testCase : manifest.cases) {
        if (testCase.id == id)
            return &testCase;
    }
    return nullptr;
}

bool resolveFeatureLabCase(const FeatureLabManifest& manifest,
    const FeatureLabCase& selected,
    core::pmr::Vector<core::pmr::String>& features,
    core::pmr::String& diagnostics)
{
    ResolvedFeatureLabCase resolved;
    const auto success = resolveFeatureLabCase(manifest, selected, resolved, diagnostics);
    if (success)
        features = std::move(resolved.features);
    return success;
}

bool resolveFeatureLabCase(const FeatureLabManifest& manifest,
    const FeatureLabCase& selected,
    ResolvedFeatureLabCase& resolved,
    core::pmr::String& diagnostics)
{
    resolved = {};
    resolved.id = selected.id;
    resolved.defaultFrames = selected.defaultFrames;
    core::pmr::Vector<const FeatureLabCase*> chain;
    const auto* current = &selected;
    while (current != nullptr) {
        if (chain.size() > manifest.cases.size()) {
            diagnostics = "FEATURE-LAB-PARENT-CYCLE";
            return false;
        }
        chain.push_back(current);
        if (current->parent.empty())
            break;
        current = findFeatureLabCase(manifest, current->parent);
        if (current == nullptr) {
            diagnostics = "FEATURE-LAB-PARENT-NOT-FOUND";
            return false;
        }
    }
    for (auto iterator = chain.rbegin(); iterator != chain.rend(); ++iterator) {
        const auto& source = **iterator;
        appendUnique(resolved.features, source.features, [](const auto& lhs, const auto& rhs) {
            return lhs == rhs;
        });
        appendUnique(resolved.requiredCapabilities,
            source.requiredCapabilities,
            [](const auto& lhs, const auto& rhs) {
                return lhs == rhs;
            });
        appendUnique(
            resolved.attachments, source.attachments, [](const auto& lhs, const auto& rhs) {
                return lhs == rhs;
            });
        appendUnique(resolved.acceptance, source.acceptance, [](const auto& lhs, const auto& rhs) {
            return lhs.attachment == rhs.attachment && lhs.metric == rhs.metric;
        });
        if (source.camera.declared) {
            resolved.camera = source.camera;
        }
    }
    if (!resolved.camera.declared) {
        diagnostics = "FEATURE-LAB-CAMERA-REQUIRED";
        return false;
    }
    for (const auto& threshold : resolved.acceptance) {
        if (!containsString(resolved.attachments, threshold.attachment)) {
            diagnostics = "FEATURE-LAB-ACCEPTANCE-ATTACHMENT-NOT-DECLARED";
            return false;
        }
    }
    return true;
}

} // namespace arp::tools::render_feature_lab
