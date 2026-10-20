#include "core/file_io.h"
#include "modules/yaml/yaml_module.h"
#include "scriptvm/runtime/cpp_plugin_importer.h"
#include "tools/asset_importer/script_runner.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>

#ifndef ARP_SOURCE_DIR
#define ARP_SOURCE_DIR "."
#endif

namespace {

using arp::core::defaultCpuMemoryResource;
using arp::core::pmr::String;

String moduleName(const char* value)
{
    return String(value, &defaultCpuMemoryResource());
}

void appendU32(std::string& output, std::uint32_t value)
{
    output.push_back(static_cast<char>(value & 0xffu));
    output.push_back(static_cast<char>((value >> 8u) & 0xffu));
    output.push_back(static_cast<char>((value >> 16u) & 0xffu));
    output.push_back(static_cast<char>((value >> 24u) & 0xffu));
}

void appendFloat(std::string& output, float value)
{
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32(output, bits);
}

bool writeMinimalAnimatedGlb(const std::filesystem::path& path)
{
    std::string binary;
    binary.reserve(100u);
    for (const float value : {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f}) {
        appendFloat(binary, value);
    }
    for (int index = 0; index < 3; ++index) {
        binary.append("\0\0\0\0", 4u);
    }
    for (int index = 0; index < 3; ++index) {
        appendFloat(binary, 1.0f);
        appendFloat(binary, 0.0f);
        appendFloat(binary, 0.0f);
        appendFloat(binary, 0.0f);
    }
    binary.push_back('\0');
    binary.push_back('\1');
    binary.push_back('\2');
    binary.push_back('\0');

    const std::string json = R"json({
"asset":{"version":"2.0"},
"buffers":[{"byteLength":100}],
"bufferViews":[
{"buffer":0,"byteOffset":0,"byteLength":36},
{"buffer":0,"byteOffset":36,"byteLength":12},
{"buffer":0,"byteOffset":48,"byteLength":48},
{"buffer":0,"byteOffset":96,"byteLength":3}],
"accessors":[
{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
{"bufferView":1,"componentType":5121,"count":3,"type":"VEC4"},
{"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},
{"bufferView":3,"componentType":5121,"count":3,"type":"SCALAR"}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2},"indices":3}]}],
"nodes":[{"name":"Root","mesh":0,"skin":0}],
"skins":[{"joints":[0]}]
})json";
    std::string paddedJson = json;
    while ((paddedJson.size() % 4u) != 0u) {
        paddedJson.push_back(' ');
    }

    std::string glb;
    glb.reserve(28u + paddedJson.size() + binary.size());
    appendU32(glb, 0x46546c67u);
    appendU32(glb, 2u);
    appendU32(glb, static_cast<std::uint32_t>(28u + paddedJson.size() + binary.size()));
    appendU32(glb, static_cast<std::uint32_t>(paddedJson.size()));
    appendU32(glb, 0x4e4f534au);
    glb.append(paddedJson);
    appendU32(glb, static_cast<std::uint32_t>(binary.size()));
    appendU32(glb, 0x004e4942u);
    glb.append(binary);

    std::ofstream file(path, std::ios::binary);
    if (!file.good()) {
        return false;
    }
    file.write(glb.data(), static_cast<std::streamsize>(glb.size()));
    return file.good();
}

} // namespace

int main(int argc, char** argv)
{
    const std::filesystem::path root = ARP_SOURCE_DIR;
    const auto executablePath = argc > 0 ? std::filesystem::path(argv[0]) : std::filesystem::path();
    const auto hostLibrary = executablePath.empty()
        ? root / "build" / "win-auto-debug" / "arp_engine.dll"
        : executablePath.parent_path() / "arp_engine.dll";

    arp::scriptvm::CppPluginImporterDesc desc;
    desc.sourceRoot = root / "modules";
    desc.cacheRoot = std::filesystem::temp_directory_path() / "arp_asset_importer_script_unit";
    desc.includeRoot = root / "src";
    desc.additionalIncludeRoots.push_back(root / "3rd" / "fkYAML" / "include");
    desc.hostLibrary = hostLibrary;
#if defined(ARP_SCRIPT_CPP_COMPILER_PATH)
    desc.compilerCommand =
        arp::core::pmr::String(ARP_SCRIPT_CPP_COMPILER_PATH, &defaultCpuMemoryResource());
#endif
    desc.moduleDirectoryLayout = true;

    arp::scriptvm::CppPluginImporter importer(std::move(desc));
    for (const char* name : {"io", "yaml", "asset", "arp_import_host"}) {
        const auto module = importer.loadModule(moduleName(name));
        if (!module || module->symbols.empty()) {
            std::printf("FAIL: module %s: %s\n", name, importer.lastDiagnostics().c_str());
            return 1;
        }
    }

    const auto requireType = [&](const char* moduleNameValue,
                                 const char* typeNameValue,
                                 std::initializer_list<const char*> propertyNames) {
        const auto module = importer.loadModule(moduleName(moduleNameValue));
        assert(module != nullptr);
        const auto found = module->symbols.find(moduleName(typeNameValue));
        assert(found != module->symbols.end());
        assert(arp::scriptvm::isReflectedTypeBinding(found->second));
        const auto* classInfo = arp::scriptvm::asReflectedTypeBinding(found->second);
        assert(classInfo != nullptr);
        const auto properties = classInfo->get_all_properties();
        for (const char* propertyName : propertyNames) {
            assert(properties.find(moduleName(propertyName)) != properties.end());
        }
    };
    requireType("io", "IoTextResult", {"success", "text", "diagnostics"});
    requireType("io", "IoFileList", {"success", "diagnostics"});
    requireType("yaml", "YamlDocument", {"opaque"});
    requireType("yaml", "YamlParseResult", {"success", "documents", "diagnostics"});
    requireType("asset", "ImportedModelResult", {"success", "diagnostics"});
    requireType("asset",
        "DecodedImageResult",
        {"success", "diagnostics", "width", "height", "srgb", "alpha", "rgba8"});
    requireType("asset",
        "ImportedAnimationClipResult",
        {"success", "name", "durationSeconds", "channelCount", "diagnostics"});
    requireType("asset",
        "RuntimePayloadResult",
        {"success", "blobPath", "blobHash", "blobByteSize", "diagnostics"});
    requireType("asset", "AvatarBinding", {"humanoid"});
    requireType("asset",
        "UnityAssetDatabaseResult",
        {"success", "assetCount", "errorCount", "warningCount", "diagnostics"});
    requireType("asset",
        "UnityAssetRecordResult",
        {"success", "guid", "metaPath", "assetPath", "diagnostics"});
    requireType("arp_import_host", "PathResult", {"success", "path", "diagnostics"});

    const auto yamlModule = importer.loadModule(moduleName("yaml"));
    assert(yamlModule != nullptr);
    const auto firstDocumentBinding = yamlModule->symbols.find(moduleName("first_document"));
    const auto kindCodeBinding = yamlModule->symbols.find(moduleName("kind_code"));
    const auto mappingBinding = yamlModule->symbols.find(moduleName("mapping"));
    assert(firstDocumentBinding != yamlModule->symbols.end());
    assert(kindCodeBinding != yamlModule->symbols.end());
    assert(mappingBinding != yamlModule->symbols.end());
    const auto* firstDocumentFunction =
        arp::scriptvm::asReflectedFunction(firstDocumentBinding->second);
    const auto* kindCodeFunction = arp::scriptvm::asReflectedFunction(kindCodeBinding->second);
    const auto* mappingFunction = arp::scriptvm::asReflectedFunction(mappingBinding->second);
    assert(firstDocumentFunction != nullptr);
    assert(kindCodeFunction != nullptr);
    assert(mappingFunction != nullptr);
    const auto unityYaml = arp::core::readTextFile(
        root / "tests" / "fixtures" / "asset_importer" / "unity_scene.yaml");
    assert(unityYaml.success);
    arp::reflection::hlist firstDocumentArgs(
        (arp::reflection::hlist::allocator_type(&defaultCpuMemoryResource())));
    firstDocumentArgs.emplace_back(unityYaml.text);
    const auto directDocument =
        firstDocumentFunction->call_v(arp::reflection::object::undefined(), firstDocumentArgs);
    const auto directDocumentNativeKind = fkyaml::to_string(
        directDocument.cast<arp::scriptvm::plugins::yaml::YamlNode>().native().get_type());
    const auto directFileId = directDocument.call("get", moduleName("__arp_unity_fileID"));
    const auto directFileIdNode = directFileId.cast<arp::scriptvm::plugins::yaml::YamlNode>();
    const auto directFileIdKind = fkyaml::to_string(directFileIdNode.native().get_type());
    const auto directFileIdValue = directFileIdNode.native().is_integer()
        ? directFileIdNode.native().get_value<std::int64_t>()
        : -1;
    arp::reflection::hlist noArgs(
        (arp::reflection::hlist::allocator_type(&defaultCpuMemoryResource())));
    const auto directMapping =
        mappingFunction->call_v(arp::reflection::object::undefined(), noArgs);
    arp::reflection::hlist kindCodeArgs(
        (arp::reflection::hlist::allocator_type(&defaultCpuMemoryResource())));
    kindCodeArgs.emplace_back(directDocument);
    const auto directKind =
        kindCodeFunction->call_v(arp::reflection::object::undefined(), kindCodeArgs);
    int directKindCode = -1;
    if (!arp::scriptvm::tryGetInt(directKind, directKindCode) || directKindCode != 1
        || directFileIdValue != 100) {
        const auto* directClass = directDocument.class_();
        const auto* mappingClass = directMapping.class_();
        std::printf(
            "FAIL: direct YAML reflection document kind %d, nativeKind=%s, fileIdKind=%s, fileId=%lld, undefined=%d, class=%s, mappingUndefined=%d, mappingClass=%s, firstArgc=%zu, mappingArgc=%zu, argTypeMatch=%d, returnTypeMatch=%d, mappingReturnTypeMatch=%d, firstWrapper=%p, mappingWrapper=%p\n",
            directKindCode,
            directDocumentNativeKind,
            directFileIdKind,
            static_cast<long long>(directFileIdValue),
            directDocument.is_undefined() ? 1 : 0,
            directClass == nullptr ? "<null>" : directClass->name().c_str(),
            directMapping.is_undefined() ? 1 : 0,
            mappingClass == nullptr ? "<null>" : mappingClass->name().c_str(),
            firstDocumentFunction->params_info().size(),
            mappingFunction->params_info().size(),
            (!firstDocumentFunction->params_info().empty()
                && firstDocumentFunction->params_info()[0].ti == firstDocumentArgs[0].typeinfo_())
                ? 1
                : 0,
            firstDocumentFunction->return_type() == directDocument.typeinfo_() ? 1 : 0,
            mappingFunction->return_type() == directMapping.typeinfo_() ? 1 : 0,
            firstDocumentFunction->identity(),
            mappingFunction->identity());
        return 1;
    }

    const auto importHost = importer.loadModule(moduleName("arp_import_host"));
    assert(importHost != nullptr);
    assert(importHost->symbols.find(moduleName("path_extension")) != importHost->symbols.end());
    assert(importHost->symbols.find(moduleName("path_replace_extension"))
        != importHost->symbols.end());

    const auto assetModule = importer.loadModule(moduleName("asset"));
    assert(assetModule != nullptr);
    assert(
        assetModule->symbols.find(moduleName("scan_unity_assets")) != assetModule->symbols.end());
    assert(assetModule->symbols.find(moduleName("find_unity_asset")) != assetModule->symbols.end());
    assert(
        assetModule->symbols.find(moduleName("retarget_fbx_clip")) != assetModule->symbols.end());
    assert(assetModule->symbols.find(moduleName("retarget_fbx_clip_with_avatar"))
        != assetModule->symbols.end());
    assert(assetModule->symbols.find(moduleName("decode_image")) != assetModule->symbols.end());
    const auto importedModelBinding = assetModule->symbols.find(moduleName("ImportedModelResult"));
    assert(importedModelBinding != assetModule->symbols.end());
    const auto* importedModelInfo =
        arp::scriptvm::asReflectedTypeBinding(importedModelBinding->second);
    assert(importedModelInfo != nullptr);
    const auto importedModelFields = importedModelInfo->get_all_fields();
    for (const char* methodName : {"submesh_count",
             "material_count",
             "material_name",
             "joint_name",
             "clip_count",
             "clip_name",
             "clip_duration",
             "clip_channel_count"}) {
        assert(importedModelFields.find(moduleName(methodName)) != importedModelFields.end());
    }

    if (importer.loadModule(moduleName("missing")) != nullptr
        || importer.lastDiagnostics().empty()) {
        std::printf("FAIL: missing module must fail with diagnostics\n");
        return 1;
    }

    arp::tools::asset_importer::ScriptToolRunner runner(root,
        root / "modules",
        std::filesystem::temp_directory_path() / "arp_asset_importer_script_unit",
        hostLibrary);
    arp::core::pmr::Vector<arp::scriptvm::Value> args{&defaultCpuMemoryResource()};
    args.emplace_back(arp::scriptvm::makeString(std::string_view("unused-input")));
    args.emplace_back(arp::scriptvm::makeString(root.generic_string()));
    const auto sourceResult = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "source_import_unit.arp", args);
    if (!sourceResult.success) {
        std::printf("FAIL: source module import: %s\n", sourceResult.diagnostics.c_str());
        return 1;
    }
    int sourceStatus = -1;
    if (!arp::scriptvm::tryGetInt(sourceResult.value, sourceStatus) || sourceStatus != 0) {
        std::printf("FAIL: source module returned status %d\n", sourceStatus);
        return 1;
    }

    const auto animatedFbxPath =
        root / "3rd" / "assimp-6.0.5" / "test" / "models" / "FBX" / "animation_with_skeleton.fbx";
    arp::core::pmr::Vector<arp::scriptvm::Value> retargetArgs{&defaultCpuMemoryResource()};
    retargetArgs.emplace_back(arp::scriptvm::makeString(animatedFbxPath.generic_string()));
    retargetArgs.emplace_back(arp::scriptvm::makeString(animatedFbxPath.generic_string()));
    const auto retargetResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_retarget_unit.arp",
        "retarget_first",
        retargetArgs);
    int retargetChannelCount = 0;
    if (!retargetResult.success
        || !arp::scriptvm::tryGetInt(retargetResult.value, retargetChannelCount)
        || retargetChannelCount <= 0) {
        std::printf("FAIL: typed FBX retarget: %s\n", retargetResult.diagnostics.c_str());
        return 1;
    }

    const auto humanoidRetargetResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_retarget_unit.arp",
        "retarget_humanoid_first",
        retargetArgs);
    int humanoidRetargetChannelCount = 0;
    if (!humanoidRetargetResult.success
        || !arp::scriptvm::tryGetInt(humanoidRetargetResult.value, humanoidRetargetChannelCount)
        || humanoidRetargetChannelCount <= 0) {
        std::printf(
            "FAIL: typed humanoid FBX retarget: %s\n", humanoidRetargetResult.diagnostics.c_str());
        return 1;
    }

    const auto emptyHumanoidRetargetResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_retarget_unit.arp",
        "retarget_empty_humanoid",
        retargetArgs);
    if (emptyHumanoidRetargetResult.success
        || emptyHumanoidRetargetResult.diagnostics.find(
               "humanoid AvatarBinding requires human bones and skeleton nodes")
            == std::string::npos
        || emptyHumanoidRetargetResult.diagnostics.find("retarget_empty_humanoid")
            == std::string::npos) {
        std::printf("FAIL: empty humanoid binding must raise with traceback: %s\n",
            emptyHumanoidRetargetResult.diagnostics.c_str());
        return 1;
    }

    const auto typedYamlResult = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_yaml_typed_values.arp", args);
    if (!typedYamlResult.success) {
        std::printf("FAIL: typed YAML accessors: %s\n", typedYamlResult.diagnostics.c_str());
        return 1;
    }
    int typedYamlStatus = -1;
    if (!arp::scriptvm::tryGetInt(typedYamlResult.value, typedYamlStatus) || typedYamlStatus != 0) {
        std::printf("FAIL: typed YAML accessors returned status %d\n", typedYamlStatus);
        return 1;
    }
    args[0] = arp::scriptvm::makeString(
        (root / "tests" / "fixtures" / "asset_importer" / "unity_particle_burst.prefab")
            .generic_string());
    const auto burstYamlResult = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_yaml_burst_values.arp", args);
    int burstMinCount = -1;
    if (!burstYamlResult.success || !arp::scriptvm::tryGetInt(burstYamlResult.value, burstMinCount)
        || burstMinCount != 2) {
        std::printf("FAIL: YAML burst integer access: %s\n", burstYamlResult.diagnostics.c_str());
        return 1;
    }
    const auto typedYamlFloatResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_yaml_typed_values.arp",
        "ratio_value",
        args);
    if (!typedYamlFloatResult.success) {
        std::printf(
            "FAIL: typed YAML float result: %s\n", typedYamlFloatResult.diagnostics.c_str());
        return 1;
    }

    args[0] = arp::scriptvm::makeString(
        (root / "tests" / "fixtures" / "asset_importer" / "unity_material.mat").generic_string());
    const auto yamlKeysResult = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_yaml_keys.arp", args);
    if (!yamlKeysResult.success) {
        std::printf("FAIL: YAML mapping key iteration: %s\n", yamlKeysResult.diagnostics.c_str());
        return 1;
    }
    double typedYamlFloat = 0.0;
    if (!arp::scriptvm::tryGetFloat(typedYamlFloatResult.value, typedYamlFloat)
        || typedYamlFloat < 1.49 || typedYamlFloat > 1.51) {
        std::printf("FAIL: typed YAML float value %.6f\n", typedYamlFloat);
        return 1;
    }
    const auto yamlFloatConstructResult = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_yaml_float_construct.arp", args);
    if (!yamlFloatConstructResult.success) {
        std::printf(
            "FAIL: YAML float construction: %s\n", yamlFloatConstructResult.diagnostics.c_str());
        return 1;
    }
    args[0] = arp::scriptvm::makeString(
        (root / "tests" / "fixtures" / "asset_importer" / "unity_scene.yaml").generic_string());
    const auto yamlAnchorResult = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_yaml_anchor.arp", args);
    if (!yamlAnchorResult.success) {
        std::printf("FAIL: YAML document anchor: %s\n", yamlAnchorResult.diagnostics.c_str());
        return 1;
    }

    args[0] = arp::scriptvm::makeString(
        (root / "tests" / "fixtures" / "asset_importer" / "unity_scene.yaml").generic_string());
    const auto unityImportResult = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp", args);
    if (!unityImportResult.success) {
        std::printf("FAIL: unity import script: %s\n", unityImportResult.diagnostics.c_str());
        return 1;
    }
    int unityImportStatus = -1;
    if (!arp::scriptvm::tryGetInt(unityImportResult.value, unityImportStatus)
        || unityImportStatus != 0) {
        std::printf("FAIL: unity import returned status %d\n", unityImportStatus);
        return 1;
    }

    const auto modelMetaPath = root / "tests" / "fixtures" / "asset_importer" / "unity_project"
        / "Assets" / "Characters" / "Player.fbx.meta";
    args[0] = arp::scriptvm::makeString(modelMetaPath.generic_string());
    const auto modelMetaResult = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp", args);
    if (!modelMetaResult.success) {
        std::printf("FAIL: Unity ModelImporter meta: %s\n", modelMetaResult.diagnostics.c_str());
        return 1;
    }
    int modelMetaStatus = -1;
    if (!arp::scriptvm::tryGetInt(modelMetaResult.value, modelMetaStatus) || modelMetaStatus != 0) {
        std::printf("FAIL: Unity ModelImporter meta returned status %d\n", modelMetaStatus);
        return 1;
    }

    const auto reportPath =
        std::filesystem::temp_directory_path() / "arp_asset_importer_script_unit_report.yaml";
    arp::core::pmr::Vector<arp::scriptvm::Value> reportArgs{&defaultCpuMemoryResource()};
    reportArgs.emplace_back(arp::scriptvm::makeString(
        (root / "tests" / "fixtures" / "asset_importer" / "unity_scene.unity").generic_string()));
    reportArgs.emplace_back(arp::scriptvm::makeString(root.generic_string()));
    reportArgs.emplace_back(arp::scriptvm::makeString(reportPath.generic_string()));
    const auto reportResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "export",
        reportArgs);
    if (!reportResult.success) {
        std::printf("FAIL: unity import report: %s\n", reportResult.diagnostics.c_str());
        return 1;
    }
    int reportStatus = -1;
    if (!arp::scriptvm::tryGetInt(reportResult.value, reportStatus) || reportStatus != 0) {
        std::printf("FAIL: unity import report returned status %d\n", reportStatus);
        return 1;
    }
    const auto reportText = arp::core::readTextFile(reportPath);
    if (!reportText.success || reportText.text.find("type: UnityImportReport") == std::string::npos
        || reportText.text.find("contractVersion: 1") == std::string::npos
        || reportText.text.find("kind: scene") == std::string::npos
        || reportText.text.find("scene:") == std::string::npos
        || reportText.text.find("status: 0") == std::string::npos) {
        std::printf("FAIL: unity import report contents are invalid\n");
        return 1;
    }

    reportArgs[1] = arp::scriptvm::makeString(
        (root / "tests" / "fixtures" / "asset_importer" / "unity_project").generic_string());
    const auto databaseReportResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "export",
        reportArgs);
    if (!databaseReportResult.success) {
        std::printf(
            "FAIL: unity asset database report: %s\n", databaseReportResult.diagnostics.c_str());
        return 1;
    }
    int databaseReportStatus = -1;
    if (!arp::scriptvm::tryGetInt(databaseReportResult.value, databaseReportStatus)
        || databaseReportStatus != 0) {
        std::printf("FAIL: unity asset database report returned status %d\n", databaseReportStatus);
        return 1;
    }
    const auto databaseReportText = arp::core::readTextFile(reportPath);
    if (!databaseReportText.success
        || databaseReportText.text.find("assetDatabase:") == std::string::npos
        || databaseReportText.text.find("assetCount: 4") == std::string::npos) {
        std::printf("FAIL: unity asset database report contents are invalid\n");
        return 1;
    }

    reportArgs[0] = arp::scriptvm::makeString(modelMetaPath.generic_string());
    const auto modelMetaReportResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "export",
        reportArgs);
    if (!modelMetaReportResult.success) {
        std::printf("FAIL: Unity ModelImporter meta report: %s\n",
            modelMetaReportResult.diagnostics.c_str());
        return 1;
    }
    int modelMetaReportStatus = -1;
    if (!arp::scriptvm::tryGetInt(modelMetaReportResult.value, modelMetaReportStatus)
        || modelMetaReportStatus != 0) {
        std::printf(
            "FAIL: Unity ModelImporter meta report returned status %d\n", modelMetaReportStatus);
        return 1;
    }
    const auto modelMetaReportText = arp::core::readTextFile(reportPath);
    if (!modelMetaReportText.success
        || modelMetaReportText.text.find("kind: model_meta") == std::string::npos
        || modelMetaReportText.text.find("animationType: 3") == std::string::npos
        || modelMetaReportText.text.find("avatarSetup: 1") == std::string::npos
        || modelMetaReportText.text.find("importAnimation: true") == std::string::npos
        || modelMetaReportText.text.find("clipAnimationCount: 1") == std::string::npos
        || modelMetaReportText.text.find("humanBoneCount: 1") == std::string::npos
        || modelMetaReportText.text.find("skeletonCount: 1") == std::string::npos
        || modelMetaReportText.text.find("avatarKind: Humanoid") == std::string::npos) {
        std::printf("FAIL: Unity ModelImporter meta report contents are invalid\n");
        return 1;
    }

    const auto textureMetaPath = root / "tests" / "fixtures" / "asset_importer" / "unity_project"
        / "Assets" / "Materials" / "Particle.png.meta";
    args[0] = arp::scriptvm::makeString(textureMetaPath.generic_string());
    const auto textureMetaResult = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp", args);
    if (!textureMetaResult.success) {
        std::printf("FAIL: Unity TextureImporter meta dispatch: %s\n",
            textureMetaResult.diagnostics.c_str());
        return 1;
    }
    reportArgs[0] = arp::scriptvm::makeString(textureMetaPath.generic_string());
    const auto textureMetaReportResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "export",
        reportArgs);
    if (!textureMetaReportResult.success) {
        std::printf("FAIL: Unity TextureImporter meta report: %s\n",
            textureMetaReportResult.diagnostics.c_str());
        return 1;
    }
    const auto textureMetaReportText = arp::core::readTextFile(reportPath);
    if (!textureMetaReportText.success
        || textureMetaReportText.text.find("kind: texture_meta") == std::string::npos
        || textureMetaReportText.text.find("importer: TextureImporter") == std::string::npos
        || textureMetaReportText.text.find("assetPath: ") == std::string::npos
        || textureMetaReportText.text.find("Particle.png") == std::string::npos) {
        std::printf("FAIL: Unity TextureImporter meta report contents are invalid\n");
        return 1;
    }

    const auto nativeMetaPath = root / "tests" / "fixtures" / "asset_importer" / "unity_project"
        / "Assets" / "Materials" / "Particle.mat.meta";
    args[0] = arp::scriptvm::makeString(nativeMetaPath.generic_string());
    const auto nativeMetaResult = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp", args);
    if (!nativeMetaResult.success) {
        std::printf("FAIL: Unity NativeFormatImporter meta dispatch: %s\n",
            nativeMetaResult.diagnostics.c_str());
        return 1;
    }
    reportArgs[0] = arp::scriptvm::makeString(nativeMetaPath.generic_string());
    const auto nativeMetaReportResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "export",
        reportArgs);
    if (!nativeMetaReportResult.success) {
        std::printf("FAIL: Unity NativeFormatImporter meta report: %s\n",
            nativeMetaReportResult.diagnostics.c_str());
        return 1;
    }
    const auto nativeMetaReportText = arp::core::readTextFile(reportPath);
    if (!nativeMetaReportText.success
        || nativeMetaReportText.text.find("kind: native_meta") == std::string::npos
        || nativeMetaReportText.text.find("importer: NativeFormatImporter") == std::string::npos) {
        std::printf("FAIL: Unity NativeFormatImporter meta report contents are invalid\n");
        return 1;
    }

    const auto metaDispatchRoot =
        std::filesystem::temp_directory_path() / "arp_asset_importer_script_unit_meta_dispatch";
    std::filesystem::create_directories(metaDispatchRoot);
    const auto unknownMetaPath = metaDispatchRoot / "unknown.asset.meta";
    std::ofstream unknownMeta(unknownMetaPath, std::ios::binary);
    unknownMeta << "fileFormatVersion: 2\n"
                << "guid: unknown_guid\n"
                << "UnknownImporter:\n"
                << "  serializedVersion: 1\n";
    unknownMeta.close();
    args[0] = arp::scriptvm::makeString(unknownMetaPath.generic_string());
    const auto unknownMetaResult = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp", args);
    if (unknownMetaResult.success
        || unknownMetaResult.diagnostics.find("Unity meta importer is unsupported")
            == std::string::npos
        || unknownMetaResult.diagnostics.find("main") == std::string::npos) {
        std::printf("FAIL: unknown Unity meta importer must raise: %s\n",
            unknownMetaResult.diagnostics.c_str());
        return 1;
    }
    const auto ambiguousMetaPath = metaDispatchRoot / "ambiguous.asset.meta";
    std::ofstream ambiguousMeta(ambiguousMetaPath, std::ios::binary);
    ambiguousMeta << "fileFormatVersion: 2\n"
                  << "guid: ambiguous_guid\n"
                  << "ModelImporter:\n"
                  << "  animationType: 0\n"
                  << "TextureImporter:\n"
                  << "  serializedVersion: 12\n";
    ambiguousMeta.close();
    args[0] = arp::scriptvm::makeString(ambiguousMetaPath.generic_string());
    const auto ambiguousMetaResult = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp", args);
    if (ambiguousMetaResult.success
        || ambiguousMetaResult.diagnostics.find("Unity meta has multiple importer components")
            == std::string::npos
        || ambiguousMetaResult.diagnostics.find("main") == std::string::npos) {
        std::printf("FAIL: ambiguous Unity meta importer must raise: %s\n",
            ambiguousMetaResult.diagnostics.c_str());
        return 1;
    }

    args[0] = arp::scriptvm::makeString(
        (root / "tests" / "fixtures" / "asset_importer" / "unity_invalid_model.meta")
            .generic_string());
    const auto invalidModelMetaResult = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp", args);
    if (invalidModelMetaResult.success
        || invalidModelMetaResult.diagnostics.find("Unity ModelImporter meta is invalid")
            == std::string::npos
        || invalidModelMetaResult.diagnostics.find("main") == std::string::npos) {
        std::printf("FAIL: invalid Unity ModelImporter meta must raise: %s\n",
            invalidModelMetaResult.diagnostics.c_str());
        return 1;
    }

    const auto requireInvalidModelMeta = [&](const char* fileName, const char* needle) {
        args[0] = arp::scriptvm::makeString(
            (root / "tests" / "fixtures" / "asset_importer" / fileName).generic_string());
        const auto result = runner.runFile(
            root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp", args);
        if (result.success
            || result.diagnostics.find("Unity ModelImporter meta is invalid") == std::string::npos
            || result.diagnostics.find(needle) == std::string::npos) {
            std::printf("FAIL: invalid model fixture %s must raise: %s\n",
                fileName,
                result.diagnostics.c_str());
            return false;
        }
        return true;
    };
    if (!requireInvalidModelMeta(
            "unity_invalid_model_duplicate_clip.meta", "duplicate clipAnimations.name")
        || !requireInvalidModelMeta(
            "unity_invalid_model_vec3.meta", "humanDescription.skeleton.position")
        || !requireInvalidModelMeta("unity_invalid_model_source_avatar.meta", "sourceAvatar")) {
        return 1;
    }

    const auto materialPath = root / "tests" / "fixtures" / "asset_importer" / "unity_material.mat";
    reportArgs[1] = arp::scriptvm::makeString(root.generic_string());
    args[0] = arp::scriptvm::makeString(materialPath.generic_string());
    const auto materialResult = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp", args);
    if (!materialResult.success) {
        std::printf("FAIL: Unity Material import: %s\n", materialResult.diagnostics.c_str());
        return 1;
    }
    int materialStatus = -1;
    if (!arp::scriptvm::tryGetInt(materialResult.value, materialStatus) || materialStatus != 0) {
        std::printf("FAIL: Unity Material import returned status %d\n", materialStatus);
        return 1;
    }
    reportArgs[0] = arp::scriptvm::makeString(materialPath.generic_string());
    const auto materialReportResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "export",
        reportArgs);
    if (!materialReportResult.success) {
        std::printf("FAIL: Unity Material report: %s\n", materialReportResult.diagnostics.c_str());
        return 1;
    }
    int materialReportStatus = -1;
    if (!arp::scriptvm::tryGetInt(materialReportResult.value, materialReportStatus)
        || materialReportStatus != 0) {
        std::printf("FAIL: Unity Material report returned status %d\n", materialReportStatus);
        return 1;
    }
    const auto materialReportText = arp::core::readTextFile(reportPath);
    if (!materialReportText.success
        || materialReportText.text.find("kind: material") == std::string::npos
        || materialReportText.text.find("shaderGuid: player_shader_guid") == std::string::npos
        || materialReportText.text.find("materialGuid: player_material_guid") == std::string::npos
        || materialReportText.text.find("mainTextureGuid: player_texture_guid") == std::string::npos
        || materialReportText.text.find("doubleSided: false") == std::string::npos
        || materialReportText.text.find("alphaMode: MASK") == std::string::npos
        || materialReportText.text.find("alphaClip: true") == std::string::npos
        || materialReportText.text.find("alphaCutoff: 0.42") == std::string::npos
        || materialReportText.text.find("property: _MainTex") == std::string::npos
        || materialReportText.text.find("textureGuid: detail_mask_guid") == std::string::npos
        || materialReportText.text.find("property: _Cull") == std::string::npos
        || materialReportText.text.find("property: _Cutoff") == std::string::npos
        || materialReportText.text.find("property: _Color") == std::string::npos) {
        std::printf("FAIL: Unity Material report contents are invalid\n");
        return 1;
    }

    const auto textureProjectRoot =
        std::filesystem::temp_directory_path() / "arp_asset_importer_script_unit_texture_project";
    const auto textureProjectMaterials = textureProjectRoot / "Assets" / "Materials";
    std::filesystem::create_directories(textureProjectMaterials);
    const auto sourceTexturePath = root / "3rd" / "assimp-6.0.5" / "test" / "models" / "glTF2"
        / "BoxTextured-glTF" / "CesiumLogoFlat.png";
    const auto projectTexturePath = textureProjectMaterials / "Particle.png";
    const auto projectTextureMetaPath = projectTexturePath.string() + ".meta";
    const auto projectMaterialPath = textureProjectMaterials / "Particle.mat";
    const auto projectMaterialMetaPath = projectMaterialPath.string() + ".meta";
    std::filesystem::copy_file(
        sourceTexturePath, projectTexturePath, std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(root / "tests" / "fixtures" / "asset_importer" / "unity_project"
            / "Assets" / "Materials" / "Particle.mat",
        projectMaterialPath,
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(root / "tests" / "fixtures" / "asset_importer" / "unity_project"
            / "Assets" / "Materials" / "Particle.mat.meta",
        projectMaterialMetaPath,
        std::filesystem::copy_options::overwrite_existing);
    std::ofstream textureMeta(projectTextureMetaPath, std::ios::binary);
    textureMeta << "fileFormatVersion: 2\n"
                << "guid: abcdefabcdefabcdefabcdefabcdefab\n"
                << "TextureImporter:\n"
                << "  serializedVersion: 12\n"
                << "  sRGBTexture: 0\n"
                << "  alphaIsTransparency: 1\n";
    textureMeta.close();
    reportArgs[0] = arp::scriptvm::makeString(projectMaterialPath.generic_string());
    reportArgs[1] = arp::scriptvm::makeString(textureProjectRoot.generic_string());
    const auto resolvedMaterialReportResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "export",
        reportArgs);
    if (!resolvedMaterialReportResult.success) {
        std::printf("FAIL: resolved Unity Material report: %s\n",
            resolvedMaterialReportResult.diagnostics.c_str());
        return 1;
    }
    const auto resolvedMaterialReportText = arp::core::readTextFile(reportPath);
    if (!resolvedMaterialReportText.success
        || resolvedMaterialReportText.text.find("textureBindings:") == std::string::npos
        || resolvedMaterialReportText.text.find("assetPath: Assets/Materials/Particle.png")
            == std::string::npos
        || resolvedMaterialReportText.text.find(
               "textureResourceId: abcdefab-cdef-abcd-efab-cdefabcdefab")
            == std::string::npos
        || resolvedMaterialReportText.text.find("width: 211") == std::string::npos
        || resolvedMaterialReportText.text.find("height: 211") == std::string::npos
        || resolvedMaterialReportText.text.find("srgb: false") == std::string::npos
        || resolvedMaterialReportText.text.find("alphaIsTransparency: true") == std::string::npos) {
        std::printf("FAIL: resolved Unity Material texture binding is invalid\n");
        return 1;
    }
    std::filesystem::remove(projectTexturePath);
    const auto missingResolvedTextureResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "export",
        reportArgs);
    if (missingResolvedTextureResult.success
        || missingResolvedTextureResult.diagnostics.find(
               "Unity material texture reference cannot be resolved")
            == std::string::npos
        || missingResolvedTextureResult.diagnostics.find("resolve_project_asset_or_raise")
            == std::string::npos) {
        std::printf("FAIL: missing resolved Unity texture must raise: %s\n",
            missingResolvedTextureResult.diagnostics.c_str());
        return 1;
    }

    const auto controllerPath =
        root / "tests" / "fixtures" / "asset_importer" / "unity_controller.controller";
    args[0] = arp::scriptvm::makeString(controllerPath.generic_string());
    const auto controllerResult = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp", args);
    if (!controllerResult.success) {
        std::printf(
            "FAIL: Unity Animator Controller import: %s\n", controllerResult.diagnostics.c_str());
        return 1;
    }
    int controllerStatus = -1;
    if (!arp::scriptvm::tryGetInt(controllerResult.value, controllerStatus)
        || controllerStatus != 0) {
        std::printf("FAIL: Unity Animator Controller returned status %d\n", controllerStatus);
        return 1;
    }
    reportArgs[0] = arp::scriptvm::makeString(controllerPath.generic_string());
    const auto controllerReportResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "export",
        reportArgs);
    if (!controllerReportResult.success) {
        std::printf("FAIL: Unity Animator Controller report: %s\n",
            controllerReportResult.diagnostics.c_str());
        return 1;
    }
    int controllerReportStatus = -1;
    if (!arp::scriptvm::tryGetInt(controllerReportResult.value, controllerReportStatus)
        || controllerReportStatus != 0) {
        std::printf(
            "FAIL: Unity Animator Controller report returned status %d\n", controllerReportStatus);
        return 1;
    }
    const auto controllerReportText = arp::core::readTextFile(reportPath);
    if (!controllerReportText.success
        || controllerReportText.text.find("kind: controller") == std::string::npos
        || controllerReportText.text.find("name: PlayerController") == std::string::npos
        || controllerReportText.text.find("stateCount: 1") == std::string::npos
        || controllerReportText.text.find("transitionCount: 1") == std::string::npos
        || controllerReportText.text.find("parameterCount: 1") == std::string::npos
        || controllerReportText.text.find("motionGuid: player_clip_guid") == std::string::npos
        || controllerReportText.text.find("defaultFloat: 0.5") == std::string::npos
        || controllerReportText.text.find("duration: 0.1") == std::string::npos
        || controllerReportText.text.find("exitTime: 0.75") == std::string::npos
        || controllerReportText.text.find("event: Speed") == std::string::npos) {
        std::printf("FAIL: Unity Animator Controller report contents are invalid\n");
        return 1;
    }

    const auto particlePath =
        root / "tests" / "fixtures" / "asset_importer" / "unity_particle.prefab";
    arp::core::pmr::Vector<arp::scriptvm::Value> particleArgs{&defaultCpuMemoryResource()};
    particleArgs.emplace_back(arp::scriptvm::makeString(particlePath.generic_string()));
    particleArgs.emplace_back(arp::scriptvm::makeString(reportPath.generic_string()));
    const auto particleResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "import_particle",
        particleArgs);
    if (!particleResult.success) {
        std::printf("FAIL: Unity Particle importer: %s\n", particleResult.diagnostics.c_str());
        return 1;
    }
    int particleStatus = -1;
    if (!arp::scriptvm::tryGetInt(particleResult.value, particleStatus) || particleStatus != 0) {
        std::printf("FAIL: Unity Particle importer returned status %d\n", particleStatus);
        return 1;
    }
    const auto particleReportText = arp::core::readTextFile(reportPath);
    if (!particleReportText.success
        || particleReportText.text.find("kind: particle") == std::string::npos
        || particleReportText.text.find("particleSystemCount: 1") == std::string::npos
        || particleReportText.text.find("rendererCount: 1") == std::string::npos
        || particleReportText.text.find("materialGuid: 0123456789abcdef0123456789abcdef")
            == std::string::npos) {
        std::printf("FAIL: Unity Particle report contents are invalid\n");
        return 1;
    }

    const auto particleProjectRoot =
        std::filesystem::temp_directory_path() / "arp_asset_importer_script_unit_particle_project";
    const auto particleProjectSourceRoot =
        root / "tests" / "fixtures" / "asset_importer" / "unity_project";
    std::filesystem::copy(particleProjectSourceRoot,
        particleProjectRoot,
        std::filesystem::copy_options::recursive
            | std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(root / "3rd" / "assimp-6.0.5" / "test" / "models" / "glTF2"
            / "BoxTextured-glTF" / "CesiumLogoFlat.png",
        particleProjectRoot / "Assets" / "Materials" / "Particle.png",
        std::filesystem::copy_options::overwrite_existing);

    reportArgs[0] = arp::scriptvm::makeString(particlePath.generic_string());
    reportArgs[1] = arp::scriptvm::makeString(root.generic_string());
    const auto prefabReportResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "export",
        reportArgs);
    if (!prefabReportResult.success) {
        std::printf("FAIL: Unity Prefab report: %s\n", prefabReportResult.diagnostics.c_str());
        return 1;
    }
    int prefabReportStatus = -1;
    if (!arp::scriptvm::tryGetInt(prefabReportResult.value, prefabReportStatus)
        || prefabReportStatus != 0) {
        std::printf("FAIL: Unity Prefab report returned status %d\n", prefabReportStatus);
        return 1;
    }
    const auto prefabReportText = arp::core::readTextFile(reportPath);
    if (!prefabReportText.success || prefabReportText.text.find("kind: prefab") == std::string::npos
        || prefabReportText.text.find("particleSystemCount: 1") == std::string::npos) {
        std::printf("FAIL: Unity Prefab report contents are invalid\n");
        return 1;
    }

    const auto requireInvalidPrefab = [&](const char* fileName, const char* needle) {
        reportArgs[0] = arp::scriptvm::makeString(
            (root / "tests" / "fixtures" / "asset_importer" / fileName).generic_string());
        const auto result = runner.runFileFunction(
            root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
            "export",
            reportArgs);
        if (result.success || result.diagnostics.find(needle) == std::string::npos
            || result.diagnostics.find("export") == std::string::npos) {
            std::printf("FAIL: invalid prefab fixture %s must raise: %s\n",
                fileName,
                result.diagnostics.c_str());
            return false;
        }
        return true;
    };
    if (!requireInvalidPrefab(
            "unity_invalid_prefab_component.prefab", "GameObject.m_Component.component")
        || !requireInvalidPrefab("unity_invalid_prefab_transform.prefab", "Transform.m_GameObject")
        || !requireInvalidPrefab("unity_invalid_prefab_target_type.prefab",
            "Transform.m_GameObject.fileID target type Transform")) {
        return 1;
    }

    const auto burstParticlePath =
        root / "tests" / "fixtures" / "asset_importer" / "unity_particle_burst.prefab";
    const auto burstParticlePackagePath = std::filesystem::temp_directory_path()
        / "arp_asset_importer_script_unit_burst_particle_package";
    arp::core::pmr::Vector<arp::scriptvm::Value> burstParticleArgs{&defaultCpuMemoryResource()};
    burstParticleArgs.emplace_back(arp::scriptvm::makeString(burstParticlePath.generic_string()));
    burstParticleArgs.emplace_back(
        arp::scriptvm::makeString(burstParticlePackagePath.generic_string()));
    burstParticleArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("ScheduledParticle")));
    burstParticleArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("particle/scheduled")));
    burstParticleArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("Prototype Complete")));
    burstParticleArgs.emplace_back(arp::scriptvm::makeString(particleProjectRoot.generic_string()));
    const auto burstParticleResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "import_particle_package",
        burstParticleArgs);
    if (!burstParticleResult.success) {
        std::printf(
            "FAIL: scheduled particle package: %s\n", burstParticleResult.diagnostics.c_str());
        return 1;
    }
    const auto burstParticleEnvelope =
        arp::core::readTextFile(burstParticlePackagePath / "particle-effect.arpres.yaml");
    if (!burstParticleEnvelope.success
        || burstParticleEnvelope.text.find("burstSchedule:") == std::string::npos
        || burstParticleEnvelope.text.find("time: 0.25") == std::string::npos
        || burstParticleEnvelope.text.find("minCount: 2") == std::string::npos
        || burstParticleEnvelope.text.find("maxCount: 4") == std::string::npos
        || burstParticleEnvelope.text.find("cycleCount: 2") == std::string::npos
        || burstParticleEnvelope.text.find("repeatInterval: 0.5") == std::string::npos) {
        std::printf("FAIL: scheduled particle burst contract was not serialized\n");
        return 1;
    }

    const auto invalidParticlePath =
        root / "tests" / "fixtures" / "asset_importer" / "unity_particle_missing_seed.prefab";
    arp::core::pmr::Vector<arp::scriptvm::Value> invalidParticleArgs{&defaultCpuMemoryResource()};
    invalidParticleArgs.emplace_back(
        arp::scriptvm::makeString(invalidParticlePath.generic_string()));
    invalidParticleArgs.emplace_back(
        arp::scriptvm::makeString((std::filesystem::temp_directory_path()
            / "arp_asset_importer_script_unit_invalid_particle_package")
                .generic_string()));
    invalidParticleArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("InvalidParticle")));
    invalidParticleArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("particle/invalid")));
    invalidParticleArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("Prototype Complete")));
    invalidParticleArgs.emplace_back(
        arp::scriptvm::makeString(particleProjectRoot.generic_string()));
    const auto invalidParticleResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "import_particle_package",
        invalidParticleArgs);
    if (invalidParticleResult.success
        || invalidParticleResult.diagnostics.find("ParticleSystem.randomSeed") == std::string::npos
        || invalidParticleResult.diagnostics.find("import_particle_package") == std::string::npos) {
        std::printf("FAIL: missing particle randomSeed must raise: %s\n",
            invalidParticleResult.diagnostics.c_str());
        return 1;
    }

    arp::core::pmr::Vector<arp::scriptvm::Value> invalidParticleReportArgs{
        &defaultCpuMemoryResource()};
    invalidParticleReportArgs.emplace_back(
        arp::scriptvm::makeString(invalidParticlePath.generic_string()));
    invalidParticleReportArgs.emplace_back(arp::scriptvm::makeString(reportPath.generic_string()));
    const auto invalidParticleReportResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "import_particle",
        invalidParticleReportArgs);
    if (invalidParticleReportResult.success
        || invalidParticleReportResult.diagnostics.find("ParticleSystem.randomSeed")
            == std::string::npos
        || invalidParticleReportResult.diagnostics.find("import_particle") == std::string::npos) {
        std::printf("FAIL: standalone particle report must raise on missing randomSeed: %s\n",
            invalidParticleReportResult.diagnostics.c_str());
        return 1;
    }

    const auto unsupportedParticlePath =
        root / "tests" / "fixtures" / "asset_importer" / "unity_particle_lights.prefab";
    arp::core::pmr::Vector<arp::scriptvm::Value> unsupportedParticleArgs{
        &defaultCpuMemoryResource()};
    unsupportedParticleArgs.emplace_back(
        arp::scriptvm::makeString(unsupportedParticlePath.generic_string()));
    unsupportedParticleArgs.emplace_back(
        arp::scriptvm::makeString((std::filesystem::temp_directory_path()
            / "arp_asset_importer_script_unit_unsupported_particle")
                .generic_string()));
    unsupportedParticleArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("UnsupportedParticle")));
    unsupportedParticleArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("particle/unsupported")));
    unsupportedParticleArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("Prototype Complete")));
    unsupportedParticleArgs.emplace_back(
        arp::scriptvm::makeString(particleProjectRoot.generic_string()));
    const auto unsupportedParticleResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "import_particle_package",
        unsupportedParticleArgs);
    if (unsupportedParticleResult.success
        || unsupportedParticleResult.diagnostics.find(
               "unsupported enabled module ParticleSystem.LightsModule")
            == std::string::npos
        || unsupportedParticleResult.diagnostics.find("import_particle_package")
            == std::string::npos) {
        std::printf("FAIL: enabled unsupported particle module must raise: %s\n",
            unsupportedParticleResult.diagnostics.c_str());
        return 1;
    }

    const auto unsupportedParticleFieldPath =
        root / "tests" / "fixtures" / "asset_importer" / "unity_particle_unsupported_field.prefab";
    arp::core::pmr::Vector<arp::scriptvm::Value> unsupportedParticleFieldArgs{
        &defaultCpuMemoryResource()};
    unsupportedParticleFieldArgs.emplace_back(
        arp::scriptvm::makeString(unsupportedParticleFieldPath.generic_string()));
    unsupportedParticleFieldArgs.emplace_back(
        arp::scriptvm::makeString((std::filesystem::temp_directory_path()
            / "arp_asset_importer_script_unit_unsupported_particle_field")
                .generic_string()));
    unsupportedParticleFieldArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("UnsupportedParticleField")));
    unsupportedParticleFieldArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("particle/unsupported-field")));
    unsupportedParticleFieldArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("Prototype Complete")));
    unsupportedParticleFieldArgs.emplace_back(
        arp::scriptvm::makeString(particleProjectRoot.generic_string()));
    const auto unsupportedParticleFieldResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "import_particle_package",
        unsupportedParticleFieldArgs);
    if (unsupportedParticleFieldResult.success
        || unsupportedParticleFieldResult.diagnostics.find(
               "unsupported enabled field ParticleSystem.InitialModule.unsupportedField")
            == std::string::npos
        || unsupportedParticleFieldResult.diagnostics.find("import_particle_package")
            == std::string::npos) {
        std::printf("FAIL: unsupported enabled particle field must raise: %s\n",
            unsupportedParticleFieldResult.diagnostics.c_str());
        return 1;
    }

    const auto requireInvalidParticlePackage = [&](const char* fileName, const char* needle) {
        unsupportedParticleFieldArgs[0] = arp::scriptvm::makeString(
            (root / "tests" / "fixtures" / "asset_importer" / fileName).generic_string());
        const auto result = runner.runFileFunction(
            root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
            "import_particle_package",
            unsupportedParticleFieldArgs);
        if (result.success || result.diagnostics.find(needle) == std::string::npos) {
            std::printf("FAIL: invalid particle fixture %s must raise: %s\n",
                fileName,
                result.diagnostics.c_str());
            return false;
        }
        return true;
    };
    if (!requireInvalidParticlePackage("unity_particle_enabled_missing_field.prefab",
            "missing enabled field ParticleSystem.InitialModule.startLifetime")
        || !requireInvalidParticlePackage("unity_particle_invalid_burst.prefab",
            "ParticleSystem.EmissionModule.bursts.entry.minCount")) {
        return 1;
    }

    const auto characterFolder = root / "3rd" / "assimp-6.0.5" / "test" / "models" / "FBX";
    const auto characterPackagePath =
        std::filesystem::temp_directory_path() / "arp_asset_importer_script_unit_character_package";
    arp::core::pmr::Vector<arp::scriptvm::Value> characterPackageArgs{&defaultCpuMemoryResource()};
    characterPackageArgs.emplace_back(arp::scriptvm::makeString(characterFolder.generic_string()));
    characterPackageArgs.emplace_back(
        arp::scriptvm::makeString(characterPackagePath.generic_string()));
    characterPackageArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("character/script")));
    characterPackageArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("Script Character")));
    characterPackageArgs.emplace_back(arp::scriptvm::makeString(root.generic_string()));
    characterPackageArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("shaders/samples/material_preview_pbr.slang")));
    characterPackageArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("shaders/samples/stylized_scene.slang")));
    characterPackageArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("shaders/gui/gui_overlay.slang")));
    characterPackageArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("shaders/samples/startup_splash.slang")));
    const auto characterPackageResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "import_character_package",
        characterPackageArgs);
    if (!characterPackageResult.success) {
        std::printf("FAIL: Unity character package importer: %s\n",
            characterPackageResult.diagnostics.c_str());
        return 1;
    }
    int characterPackageStatus = -1;
    if (!arp::scriptvm::tryGetInt(characterPackageResult.value, characterPackageStatus)
        || characterPackageStatus != 0) {
        std::printf("FAIL: Unity character package returned status %d\n", characterPackageStatus);
        return 1;
    }
    const auto characterManifest =
        arp::core::readTextFile(characterPackagePath / "manifest.arpkg.yaml");
    if (!characterManifest.success
        || characterManifest.text.find("name: Script Character") == std::string::npos
        || characterManifest.text.find("alias: character/script") == std::string::npos
        || characterManifest.text.find("resourceType: PrefabResource") == std::string::npos
        || characterManifest.text.find("usage: model") == std::string::npos) {
        std::printf("FAIL: Unity character package manifest contents are invalid\n");
        return 1;
    }
    std::size_t resourceIdOffset = 0;
    int characterEntryCount = 0;
    while ((resourceIdOffset = characterManifest.text.find("resourceId:", resourceIdOffset))
        != std::string::npos) {
        ++characterEntryCount;
        resourceIdOffset += std::string("resourceId:").size();
    }
    if (characterEntryCount != 22) {
        std::printf("FAIL: Unity character package entry count %d\n", characterEntryCount);
        return 1;
    }
    const auto characterShader =
        arp::core::readTextFile(characterPackagePath / "shader-basic.arpres.yaml");
    const auto characterClip =
        arp::core::readTextFile(characterPackagePath / "animation-clip.arpres.yaml");
    const auto characterMaterial =
        arp::core::readTextFile(characterPackagePath / "material.arpres.yaml");
    const auto characterSequence =
        arp::core::readTextFile(characterPackagePath / "sequence.arpres.yaml");
    const auto characterPrefab =
        arp::core::readTextFile(characterPackagePath / "prefab.arpres.yaml");
    const auto characterRuntimePayload =
        arp::core::readTextFile(characterPackagePath / "runtime-payload.arpres.yaml");
    if (!characterShader.success
        || characterShader.text.find("shaders/samples/material_preview_pbr.slang")
            == std::string::npos
        || characterShader.text.find(".spv") == std::string::npos
        || characterShader.text.find(".metal") == std::string::npos || !characterClip.success
        || characterClip.text.find("name: Prototype") != std::string::npos
        || characterClip.text.find("channelCount: 45") == std::string::npos
        || !characterMaterial.success
        || characterMaterial.text.find("SyntheticMaterial") != std::string::npos
        || characterMaterial.text.find("sourceResourceId: ") == std::string::npos
        || !characterSequence.success
        || characterSequence.text.find("recordedCurveCount: 45") == std::string::npos
        || !characterPrefab.success
        || characterPrefab.text.find("type: PrefabResource") == std::string::npos
        || characterPrefab.text.find("shaderPackageIds:") == std::string::npos
        || characterPrefab.text.find("runtimePayloadId: ") == std::string::npos
        || characterPrefab.text.find("sourceResourceId: ") == std::string::npos
        || !characterRuntimePayload.success
        || characterRuntimePayload.text.find("type: PrefabRuntimePayloadResource")
            == std::string::npos
        || characterRuntimePayload.text.find("blobPath: blobs/prefab-runtime.arpa.bin")
            == std::string::npos
        || !std::filesystem::exists(characterPackagePath / "blobs" / "prefab-runtime.arpa.bin")) {
        std::printf("FAIL: Unity character prefab envelope contents are invalid\n");
        return 1;
    }

    arp::core::pmr::Vector<arp::scriptvm::Value> invalidCharacterPackageArgs{
        &defaultCpuMemoryResource()};
    invalidCharacterPackageArgs.emplace_back(arp::scriptvm::makeString(
        (root / "tests" / "fixtures" / "asset_importer" / "missing_character_folder")
            .generic_string()));
    invalidCharacterPackageArgs.emplace_back(
        arp::scriptvm::makeString(characterPackagePath.generic_string()));
    invalidCharacterPackageArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("character/script")));
    invalidCharacterPackageArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("Script Character")));
    invalidCharacterPackageArgs.emplace_back(arp::scriptvm::makeString(root.generic_string()));
    invalidCharacterPackageArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("shaders/samples/material_preview_pbr.slang")));
    invalidCharacterPackageArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("shaders/samples/stylized_scene.slang")));
    invalidCharacterPackageArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("shaders/gui/gui_overlay.slang")));
    invalidCharacterPackageArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("shaders/samples/startup_splash.slang")));
    const auto invalidCharacterPackageResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "import_character_package",
        invalidCharacterPackageArgs);
    if (invalidCharacterPackageResult.success
        || invalidCharacterPackageResult.diagnostics.find(
               "Unity character folder is missing or empty")
            == std::string::npos
        || invalidCharacterPackageResult.diagnostics.find("import_character_package")
            == std::string::npos) {
        std::printf("FAIL: character package errors must raise with traceback: %s\n",
            invalidCharacterPackageResult.diagnostics.c_str());
        return 1;
    }

    arp::core::pmr::Vector<arp::scriptvm::Value> missingFolderArgs{&defaultCpuMemoryResource()};
    missingFolderArgs.emplace_back(arp::scriptvm::makeString(
        (root / "tests" / "fixtures" / "asset_importer" / "missing_character_folder")
            .generic_string()));
    missingFolderArgs.emplace_back(arp::scriptvm::makeString(reportPath.generic_string()));
    const auto missingFolderResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "import_character_folder",
        missingFolderArgs);
    if (missingFolderResult.success
        || missingFolderResult.diagnostics.find("Unity character folder is missing or empty")
            == std::string::npos
        || missingFolderResult.diagnostics.find("import_character_folder") == std::string::npos) {
        std::printf("FAIL: missing Unity character folder must raise: %s\n",
            missingFolderResult.diagnostics.c_str());
        return 1;
    }

    arp::core::pmr::Vector<arp::scriptvm::Value> incompleteFolderArgs{&defaultCpuMemoryResource()};
    incompleteFolderArgs.emplace_back(arp::scriptvm::makeString(
        (root / "tests" / "fixtures" / "asset_importer" / "unity_project" / "Assets" / "Characters")
            .generic_string()));
    incompleteFolderArgs.emplace_back(arp::scriptvm::makeString(reportPath.generic_string()));
    const auto incompleteFolderResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "import_character_folder",
        incompleteFolderArgs);
    if (incompleteFolderResult.success
        || incompleteFolderResult.diagnostics.find("Unity character model is invalid")
            == std::string::npos
        || incompleteFolderResult.diagnostics.find("import_character_folder")
            == std::string::npos) {
        std::printf("FAIL: incomplete Unity character folder must raise: %s\n",
            incompleteFolderResult.diagnostics.c_str());
        return 1;
    }

    arp::core::pmr::Vector<arp::scriptvm::Value> databaseArgs{&defaultCpuMemoryResource()};
    databaseArgs.emplace_back(arp::scriptvm::makeString(
        (root / "tests" / "fixtures" / "asset_importer" / "unity_project").generic_string()));
    databaseArgs.emplace_back(arp::scriptvm::makeString(std::string_view("player_fbx_guid")));
    const auto databaseLookupResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "inspect_database",
        databaseArgs);
    if (!databaseLookupResult.success) {
        std::printf(
            "FAIL: unity asset database lookup: %s\n", databaseLookupResult.diagnostics.c_str());
        return 1;
    }
    int databaseLookupStatus = -1;
    if (!arp::scriptvm::tryGetInt(databaseLookupResult.value, databaseLookupStatus)
        || databaseLookupStatus != 0) {
        std::printf("FAIL: unity asset database lookup returned status %d\n", databaseLookupStatus);
        return 1;
    }

    args[0] = arp::scriptvm::makeString(
        (root / "tests" / "fixtures" / "asset_importer" / "unity_invalid.yaml").generic_string());
    const auto invalidUnityImportResult = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp", args);
    if (invalidUnityImportResult.success
        || invalidUnityImportResult.diagnostics.find("unity YAML contains no supported object")
            == std::string::npos
        || invalidUnityImportResult.diagnostics.find("validate_unity_file") == std::string::npos) {
        std::printf("FAIL: invalid unity import must raise with traceback: %s\n",
            invalidUnityImportResult.diagnostics.c_str());
        return 1;
    }

    const auto modelPath =
        std::filesystem::temp_directory_path() / "arp_asset_importer_script_unit_minimal.glb";
    assert(writeMinimalAnimatedGlb(modelPath));
    reportArgs[0] = arp::scriptvm::makeString(modelPath.generic_string());
    const auto modelReportResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "export",
        reportArgs);
    if (!modelReportResult.success) {
        std::printf("FAIL: minimal GLB report: %s\n", modelReportResult.diagnostics.c_str());
        return 1;
    }
    int modelReportStatus = -1;
    if (!arp::scriptvm::tryGetInt(modelReportResult.value, modelReportStatus)
        || modelReportStatus != 0) {
        std::printf("FAIL: minimal GLB report returned status %d\n", modelReportStatus);
        return 1;
    }
    const auto modelReportText = arp::core::readTextFile(reportPath);
    if (!modelReportText.success || modelReportText.text.find("kind: glb") == std::string::npos
        || modelReportText.text.find("vertexCount: 3") == std::string::npos
        || modelReportText.text.find("indexCount: 3") == std::string::npos
        || modelReportText.text.find("jointCount: 1") == std::string::npos) {
        std::printf("FAIL: minimal GLB report contents are invalid\n");
        return 1;
    }

    const auto texturePath = root / "3rd" / "assimp-6.0.5" / "test" / "models" / "glTF2"
        / "BoxTextured-glTF" / "CesiumLogoFlat.png";
    reportArgs[0] = arp::scriptvm::makeString(texturePath.generic_string());
    const auto textureReportResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "export",
        reportArgs);
    if (!textureReportResult.success) {
        std::printf("FAIL: PNG texture report: %s\n", textureReportResult.diagnostics.c_str());
        return 1;
    }
    int textureReportStatus = -1;
    if (!arp::scriptvm::tryGetInt(textureReportResult.value, textureReportStatus)
        || textureReportStatus != 0) {
        std::printf("FAIL: PNG texture report returned status %d\n", textureReportStatus);
        return 1;
    }
    const auto textureReportText = arp::core::readTextFile(reportPath);
    if (!textureReportText.success
        || textureReportText.text.find("kind: texture") == std::string::npos
        || textureReportText.text.find("mime: image/png") == std::string::npos
        || textureReportText.text.find("width: 211") == std::string::npos
        || textureReportText.text.find("height: 211") == std::string::npos
        || textureReportText.text.find("srgb: true") == std::string::npos
        || textureReportText.text.find("rgba8ByteCount: 178084") == std::string::npos) {
        std::printf("FAIL: PNG texture report contents are invalid\n");
        return 1;
    }

    args[0] = arp::scriptvm::makeString(texturePath.generic_string());
    const auto textureMainResult = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp", args);
    if (!textureMainResult.success) {
        std::printf("FAIL: PNG texture validation: %s\n", textureMainResult.diagnostics.c_str());
        return 1;
    }
    int textureMainStatus = -1;
    if (!arp::scriptvm::tryGetInt(textureMainResult.value, textureMainStatus)
        || textureMainStatus != 0) {
        std::printf("FAIL: PNG texture validation returned status %d\n", textureMainStatus);
        return 1;
    }

    const auto hlistIntegerReproPath = std::filesystem::temp_directory_path()
        / "arp_asset_importer_script_unit_hlist_integer_repro.yaml";
    arp::core::pmr::Vector<arp::scriptvm::Value> hlistIntegerReproArgs{&defaultCpuMemoryResource()};
    hlistIntegerReproArgs.emplace_back(arp::scriptvm::makeString(std::string_view("unused-input")));
    hlistIntegerReproArgs.emplace_back(
        arp::scriptvm::makeString(hlistIntegerReproPath.generic_string()));
    const auto hlistIntegerReproResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_hlist_integer_repro.arp",
        "main",
        hlistIntegerReproArgs);
    if (!hlistIntegerReproResult.success) {
        std::printf("FAIL: hlist integer conversion repro: %s\n",
            hlistIntegerReproResult.diagnostics.c_str());
        return 1;
    }
    const auto hlistIntegerReproText = arp::core::readTextFile(hlistIntegerReproPath);
    if (!hlistIntegerReproText.success
        || hlistIntegerReproText.text.find("direct: 2") == std::string::npos
        || hlistIntegerReproText.text.find("explicitInt64: 2") == std::string::npos
        || hlistIntegerReproText.text.find("literalInt64: 2") == std::string::npos
        || hlistIntegerReproText.text.find("explicitInt: 2") == std::string::npos) {
        std::printf("FAIL: hlist integer conversion did not preserve integer values\n");
        return 1;
    }

    reportArgs[0] = arp::scriptvm::makeString(
        (root / "tests" / "fixtures" / "asset_importer" / "missing.png").generic_string());
    const auto missingTextureResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "export",
        reportArgs);
    if (missingTextureResult.success
        || missingTextureResult.diagnostics.find("Unity texture source does not exist")
            == std::string::npos
        || missingTextureResult.diagnostics.find("export") == std::string::npos
        || missingTextureResult.diagnostics.find("UnityTextureImporter.import_path")
            == std::string::npos) {
        std::printf("FAIL: missing PNG must raise with traceback: %s\n",
            missingTextureResult.diagnostics.c_str());
        return 1;
    }

    reportArgs[0] = arp::scriptvm::makeString(std::string_view("unsupported.bmp"));
    const auto unsupportedTextureResult = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "export",
        reportArgs);
    if (unsupportedTextureResult.success
        || unsupportedTextureResult.diagnostics.find("unsupported Unity import extension")
            == std::string::npos
        || unsupportedTextureResult.diagnostics.find("export") == std::string::npos) {
        std::printf("FAIL: unsupported texture extension must raise with traceback: %s\n",
            unsupportedTextureResult.diagnostics.c_str());
        return 1;
    }

    args[0] = arp::scriptvm::makeString(
        (root / "tests" / "fixtures" / "asset_importer" / "missing.glb").generic_string());
    const auto missingGlbResult = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp", args);
    if (missingGlbResult.success
        || missingGlbResult.diagnostics.find("GLB import failed") == std::string::npos
        || missingGlbResult.diagnostics.find("main") == std::string::npos) {
        std::printf("FAIL: missing GLB must raise with traceback: %s\n",
            missingGlbResult.diagnostics.c_str());
        return 1;
    }

    args[0] = arp::scriptvm::makeString(std::string_view("unsupported.txt"));
    const auto unsupportedResult = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp", args);
    if (unsupportedResult.success
        || unsupportedResult.diagnostics.find("unsupported Unity import extension")
            == std::string::npos
        || unsupportedResult.diagnostics.find("main") == std::string::npos) {
        std::printf("FAIL: unsupported extension must raise with traceback: %s\n",
            unsupportedResult.diagnostics.c_str());
        return 1;
    }

    args[0] = arp::scriptvm::makeString(std::string_view("unused-input"));
    const auto aliasResult = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_alias_type.arp", args);
    if (!aliasResult.success) {
        std::printf("FAIL: module alias type: %s\n", aliasResult.diagnostics.c_str());
        return 1;
    }
    int aliasStatus = -1;
    if (!arp::scriptvm::tryGetInt(aliasResult.value, aliasStatus) || aliasStatus != 1) {
        std::printf("FAIL: module alias returned status %d\n", aliasStatus);
        return 1;
    }

    const auto fromTypeResult = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_from_type.arp", args);
    if (!fromTypeResult.success) {
        std::printf("FAIL: explicit local type import: %s\n", fromTypeResult.diagnostics.c_str());
        return 1;
    }
    int fromTypeStatus = -1;
    if (!arp::scriptvm::tryGetInt(fromTypeResult.value, fromTypeStatus) || fromTypeStatus != 1) {
        std::printf("FAIL: explicit local type returned status %d\n", fromTypeStatus);
        return 1;
    }

    const auto unqualifiedResult = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_unqualified_type.arp", args);
    if (unqualifiedResult.success
        || unqualifiedResult.diagnostics.find("must be qualified") == std::string::npos) {
        std::printf("FAIL: unqualified module type must be rejected: %s\n",
            unqualifiedResult.diagnostics.c_str());
        return 1;
    }

    std::printf("asset importer script module unit test passed\n");
    return 0;
}
