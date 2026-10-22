#include "core/io/file_io.h"
#include "core/memory/cpu_allocator.h"
#include "resource/internal_asset_package.h"
#include "resource/prefab_asset_resources.h"
#include "scriptvm/runtime/value.h"
#include "tools/asset_importer/script_runner.h"

#include <cstdio>
#include <filesystem>
#include <string>

#ifndef ARP_SOURCE_DIR
#define ARP_SOURCE_DIR "."
#endif

int main(int argc, char** argv)
{
    const std::filesystem::path root = ARP_SOURCE_DIR;
    const auto executablePath = argc > 0 ? std::filesystem::path(argv[0]) : std::filesystem::path();
    const auto hostLibrary = executablePath.empty()
        ? root / "build" / "win-auto-debug" / "arp_engine.dll"
        : executablePath.parent_path() / "arp_engine.dll";

    arp::tools::asset_importer::ScriptToolRunner runner(root,
        root / "modules",
        std::filesystem::temp_directory_path() / "arp_asset_importer_script_smoke",
        hostLibrary);
    arp::core::pmr::Vector<arp::scriptvm::Value> args{&arp::core::defaultCpuMemoryResource()};
    args.emplace_back(arp::scriptvm::makeString(
        (root / "tests" / "fixtures" / "asset_importer" / "unity_scene.yaml").generic_string()));
    args.emplace_back(arp::scriptvm::makeString(root.generic_string()));

    const auto result =
        runner.runFile(root / "tests" / "fixtures" / "asset_importer" / "script_smoke.arp", args);
    if (!result.success) {
        std::printf("FAIL: %s\n", result.diagnostics.c_str());
        return 1;
    }
    int status = -1;
    if (!arp::scriptvm::tryGetInt(result.value, status) || status != 0) {
        std::printf("FAIL: script returned status %d\n", status);
        return 1;
    }

    const auto typedYaml = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_yaml_typed_values.arp", args);
    if (!typedYaml.success) {
        std::printf("FAIL: typed YAML accessors: %s\n", typedYaml.diagnostics.c_str());
        return 1;
    }
    int typedYamlStatus = -1;
    if (!arp::scriptvm::tryGetInt(typedYaml.value, typedYamlStatus) || typedYamlStatus != 0) {
        std::printf("FAIL: typed YAML accessors returned status %d\n", typedYamlStatus);
        return 1;
    }

    const auto unityImport = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp", args);
    if (!unityImport.success) {
        std::printf("FAIL: unity import script: %s\n", unityImport.diagnostics.c_str());
        return 1;
    }
    int unityStatus = -1;
    if (!arp::scriptvm::tryGetInt(unityImport.value, unityStatus) || unityStatus != 0) {
        std::printf("FAIL: unity import returned status %d\n", unityStatus);
        return 1;
    }

    const auto modelMetaPath = root / "tests" / "fixtures" / "asset_importer" / "unity_project"
        / "Assets" / "Characters" / "Player.fbx.meta";
    args[0] = arp::scriptvm::makeString(modelMetaPath.generic_string());
    const auto modelMeta = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp", args);
    if (!modelMeta.success) {
        std::printf("FAIL: Unity ModelImporter meta: %s\n", modelMeta.diagnostics.c_str());
        return 1;
    }
    int modelMetaStatus = -1;
    if (!arp::scriptvm::tryGetInt(modelMeta.value, modelMetaStatus) || modelMetaStatus != 0) {
        std::printf("FAIL: Unity ModelImporter meta returned status %d\n", modelMetaStatus);
        return 1;
    }

    const auto materialPath = root / "tests" / "fixtures" / "asset_importer" / "unity_material.mat";
    args[0] = arp::scriptvm::makeString(materialPath.generic_string());
    const auto material = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp", args);
    if (!material.success) {
        std::printf("FAIL: Unity Material import: %s\n", material.diagnostics.c_str());
        return 1;
    }
    int materialStatus = -1;
    if (!arp::scriptvm::tryGetInt(material.value, materialStatus) || materialStatus != 0) {
        std::printf("FAIL: Unity Material import returned status %d\n", materialStatus);
        return 1;
    }

    const auto controllerPath =
        root / "tests" / "fixtures" / "asset_importer" / "unity_controller.controller";
    args[0] = arp::scriptvm::makeString(controllerPath.generic_string());
    const auto controller = runner.runFile(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp", args);
    if (!controller.success) {
        std::printf("FAIL: Unity Animator Controller import: %s\n", controller.diagnostics.c_str());
        return 1;
    }
    int controllerStatus = -1;
    if (!arp::scriptvm::tryGetInt(controller.value, controllerStatus) || controllerStatus != 0) {
        std::printf("FAIL: Unity Animator Controller returned status %d\n", controllerStatus);
        return 1;
    }

    const auto particlePath =
        root / "tests" / "fixtures" / "asset_importer" / "unity_particle.prefab";
    const auto particleReportPath = std::filesystem::temp_directory_path()
        / "arp_asset_importer_script_smoke_particle_report.yaml";
    arp::core::pmr::Vector<arp::scriptvm::Value> particleArgs{
        &arp::core::defaultCpuMemoryResource()};
    particleArgs.emplace_back(arp::scriptvm::makeString(particlePath.generic_string()));
    particleArgs.emplace_back(arp::scriptvm::makeString(particleReportPath.generic_string()));
    const auto particle = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "import_particle",
        particleArgs);
    if (!particle.success) {
        std::printf("FAIL: particle importer: %s\n", particle.diagnostics.c_str());
        return 1;
    }
    int particleStatus = -1;
    if (!arp::scriptvm::tryGetInt(particle.value, particleStatus) || particleStatus != 0) {
        std::printf("FAIL: particle importer returned status %d\n", particleStatus);
        return 1;
    }
    const auto particleReport = arp::core::readTextFile(particleReportPath);
    if (!particleReport.success
        || particleReport.text.find("kind: particle") == std::string::npos) {
        std::printf("FAIL: particle report contents are invalid\n");
        return 1;
    }

    const auto prefabReportPath = std::filesystem::temp_directory_path()
        / "arp_asset_importer_script_smoke_prefab_report.yaml";
    arp::core::pmr::Vector<arp::scriptvm::Value> prefabReportArgs{
        &arp::core::defaultCpuMemoryResource()};
    prefabReportArgs.emplace_back(arp::scriptvm::makeString(particlePath.generic_string()));
    prefabReportArgs.emplace_back(arp::scriptvm::makeString(root.generic_string()));
    prefabReportArgs.emplace_back(arp::scriptvm::makeString(prefabReportPath.generic_string()));
    const auto prefabReport = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "export",
        prefabReportArgs);
    if (!prefabReport.success) {
        std::printf("FAIL: prefab report: %s\n", prefabReport.diagnostics.c_str());
        return 1;
    }
    int prefabReportStatus = -1;
    if (!arp::scriptvm::tryGetInt(prefabReport.value, prefabReportStatus)
        || prefabReportStatus != 0) {
        std::printf("FAIL: prefab report returned status %d\n", prefabReportStatus);
        return 1;
    }
    const auto prefabReportText = arp::core::readTextFile(prefabReportPath);
    if (!prefabReportText.success || prefabReportText.text.find("kind: prefab") == std::string::npos
        || prefabReportText.text.find("particleSystemCount: 1") == std::string::npos) {
        std::printf("FAIL: prefab report contents are invalid\n");
        return 1;
    }

    const auto particlePackagePath =
        std::filesystem::temp_directory_path() / "arp_asset_importer_script_smoke_particle_package";
    const auto particleProjectRoot =
        std::filesystem::temp_directory_path() / "arp_asset_importer_script_smoke_particle_project";
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
    arp::core::pmr::Vector<arp::scriptvm::Value> particlePackageArgs{
        &arp::core::defaultCpuMemoryResource()};
    particlePackageArgs.emplace_back(arp::scriptvm::makeString(particlePath.generic_string()));
    particlePackageArgs.emplace_back(
        arp::scriptvm::makeString(particlePackagePath.generic_string()));
    particlePackageArgs.emplace_back(arp::scriptvm::makeString(std::string_view("ScriptParticle")));
    particlePackageArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("particle/script")));
    particlePackageArgs.emplace_back(
        arp::scriptvm::makeString(std::string_view("Prototype Complete")));
    particlePackageArgs.emplace_back(
        arp::scriptvm::makeString(particleProjectRoot.generic_string()));
    const auto particlePackage = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "import_particle_package",
        particlePackageArgs);
    if (!particlePackage.success) {
        std::printf("FAIL: particle package importer: %s\n", particlePackage.diagnostics.c_str());
        return 1;
    }
    int particlePackageStatus = -1;
    if (!arp::scriptvm::tryGetInt(particlePackage.value, particlePackageStatus)
        || particlePackageStatus != 0) {
        std::printf("FAIL: particle package importer returned status %d\n", particlePackageStatus);
        return 1;
    }
    const auto particleManifest =
        arp::core::readTextFile(particlePackagePath / "manifest.arpkg.yaml");
    const auto particleEnvelope =
        arp::core::readTextFile(particlePackagePath / "particle-effect.arpres.yaml");
    const auto particleTextureEnvelope =
        arp::core::readTextFile(particlePackagePath / "particle-texture.arpres.yaml");
    if (!particleManifest.success || !particleEnvelope.success || !particleTextureEnvelope.success
        || particleManifest.text.find("ScriptParticle") == std::string::npos
        || particleManifest.text.find("resourceType: TextureResource") == std::string::npos
        || particleEnvelope.text.find("ParticleEffectResource") == std::string::npos
        || particleEnvelope.text.find("prefabBindingId: ") == std::string::npos
        || particleEnvelope.text.find("name: TestParticle") == std::string::npos
        || particleEnvelope.text.find("maxParticles: 1024") == std::string::npos
        || particleEnvelope.text.find("shaderPackageId: b60b8b6c-1d8d-4e8b-8c9d-9f1c2d3e4f5a")
            == std::string::npos
        || particleEnvelope.text.find("materialAssetId: 01234567-89ab-cdef-0123-456789abcdef")
            == std::string::npos
        || particleEnvelope.text.find("textureBindingId: abcdefab-cdef-abcd-efab-cdefabcdefab")
            == std::string::npos
        || particleEnvelope.text.find("textureAssetId: abcdefab-cdef-abcd-efab-cdefabcdefab")
            == std::string::npos
        || particleEnvelope.text.find("seed: 42") == std::string::npos
        || particleEnvelope.text.find("particleLifetimeSeconds: 1") == std::string::npos
        || particleEnvelope.text.find("materialBindingId: 01234567-89ab-cdef-0123-456789abcdef")
            == std::string::npos
        || particleEnvelope.text.find("particleSystemCount:") != std::string::npos
        || particleEnvelope.text.find("rendererCount:") != std::string::npos
        || particleEnvelope.text.find("materialGuid:") != std::string::npos) {
        std::printf("FAIL: particle package contents are invalid\n");
        return 1;
    }
    if (particleEnvelope.text.find("usage: texture") == std::string::npos
        || particleTextureEnvelope.text.find("resourceType: TextureResource") == std::string::npos
        || particleTextureEnvelope.text.find("sourcePath: Assets/Materials/Particle.png")
            == std::string::npos) {
        std::printf("FAIL: particle texture resource closure is invalid\n");
        return 1;
    }
    const auto particlePackageDescription =
        arp::resource::InternalAssetPackageReader().read(particlePackagePath);
    if (!particlePackageDescription.success
        || particlePackageDescription.package.entries.size() != 3u) {
        std::printf("FAIL: particle package reader validation: %s\n",
            particlePackageDescription.diagnostics.c_str());
        return 1;
    }
    arp::resource::registerPrefabAssetReflection();
    arp::resource::ResourceManager particleResourceManager;
    const auto particlePackageLoad = arp::resource::InternalAssetPackageReader().load(
        particlePackagePath, particleResourceManager);
    if (!particlePackageLoad.success) {
        std::printf(
            "FAIL: particle typed package load: %s\n", particlePackageLoad.diagnostics.c_str());
        return 1;
    }
    const auto characterFolder = root / "3rd" / "assimp-6.0.5" / "test" / "models" / "FBX";
    const auto characterPackagePath = std::filesystem::temp_directory_path()
        / "arp_asset_importer_script_smoke_character_package";
    arp::core::pmr::Vector<arp::scriptvm::Value> characterPackageArgs{
        &arp::core::defaultCpuMemoryResource()};
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
    const auto characterPackage = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "import_character_package",
        characterPackageArgs);
    if (!characterPackage.success) {
        std::printf("FAIL: character package importer: %s\n", characterPackage.diagnostics.c_str());
        return 1;
    }
    int characterPackageStatus = -1;
    if (!arp::scriptvm::tryGetInt(characterPackage.value, characterPackageStatus)
        || characterPackageStatus != 0) {
        std::printf(
            "FAIL: character package importer returned status %d\n", characterPackageStatus);
        return 1;
    }
    const auto characterManifest =
        arp::core::readTextFile(characterPackagePath / "manifest.arpkg.yaml");
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
    if (!characterManifest.success || !characterShader.success || !characterPrefab.success
        || characterManifest.text.find("name: Script Character") == std::string::npos
        || characterManifest.text.find("resourceType: PrefabResource") == std::string::npos
        || characterManifest.text.find("usage: model") == std::string::npos
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
        || characterPrefab.text.find("type: PrefabResource") == std::string::npos
        || characterPrefab.text.find("runtimePayloadId: ") == std::string::npos
        || characterPrefab.text.find("sourceResourceId: ") == std::string::npos
        || !characterRuntimePayload.success
        || characterRuntimePayload.text.find("type: PrefabRuntimePayloadResource")
            == std::string::npos
        || characterRuntimePayload.text.find("blobPath: blobs/prefab-runtime.arpa.bin")
            == std::string::npos
        || !std::filesystem::exists(characterPackagePath / "blobs" / "prefab-runtime.arpa.bin")) {
        std::printf("FAIL: character package contents are invalid\n");
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
        std::printf("FAIL: character package entry count %d\n", characterEntryCount);
        return 1;
    }
    const auto characterRuntimeLoad =
        arp::resource::loadPrefabRuntimePayloadFromPackage(characterPackagePath,
            arp::core::pmr::String("character/script", &arp::core::defaultCpuMemoryResource()));
    if (!characterRuntimeLoad.success || characterRuntimeLoad.asset.clips.empty()
        || characterRuntimeLoad.defaultStateName.empty()) {
        std::printf(
            "FAIL: character runtime payload load: %s\n", characterRuntimeLoad.diagnostics.c_str());
        return 1;
    }

    const auto reportPath =
        std::filesystem::temp_directory_path() / "arp_asset_importer_script_smoke_report.yaml";
    arp::core::pmr::Vector<arp::scriptvm::Value> reportArgs{&arp::core::defaultCpuMemoryResource()};
    reportArgs.emplace_back(arp::scriptvm::makeString(
        (root / "tests" / "fixtures" / "asset_importer" / "unity_scene.unity").generic_string()));
    reportArgs.emplace_back(arp::scriptvm::makeString(root.generic_string()));
    reportArgs.emplace_back(arp::scriptvm::makeString(reportPath.generic_string()));
    const auto report = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "export",
        reportArgs);
    if (!report.success) {
        std::printf("FAIL: unity import report: %s\n", report.diagnostics.c_str());
        return 1;
    }
    int reportStatus = -1;
    if (!arp::scriptvm::tryGetInt(report.value, reportStatus) || reportStatus != 0) {
        std::printf("FAIL: unity import report returned status %d\n", reportStatus);
        return 1;
    }
    const auto reportText = arp::core::readTextFile(reportPath);
    if (!reportText.success || reportText.text.find("type: UnityImportReport") == std::string::npos
        || reportText.text.find("kind: scene") == std::string::npos
        || reportText.text.find("scene:") == std::string::npos) {
        std::printf("FAIL: unity import smoke report contents are invalid\n");
        return 1;
    }

    const auto texturePath = root / "3rd" / "assimp-6.0.5" / "test" / "models" / "glTF2"
        / "BoxTextured-glTF" / "CesiumLogoFlat.png";
    const auto textureReportPath = std::filesystem::temp_directory_path()
        / "arp_asset_importer_script_smoke_texture_report.yaml";
    arp::core::pmr::Vector<arp::scriptvm::Value> textureArgs{
        &arp::core::defaultCpuMemoryResource()};
    textureArgs.emplace_back(arp::scriptvm::makeString(texturePath.generic_string()));
    textureArgs.emplace_back(arp::scriptvm::makeString(root.generic_string()));
    textureArgs.emplace_back(arp::scriptvm::makeString(textureReportPath.generic_string()));
    const auto textureReport = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "export",
        textureArgs);
    if (!textureReport.success) {
        std::printf("FAIL: texture import smoke: %s\n", textureReport.diagnostics.c_str());
        return 1;
    }
    int textureStatus = -1;
    if (!arp::scriptvm::tryGetInt(textureReport.value, textureStatus) || textureStatus != 0) {
        std::printf("FAIL: texture import smoke returned status %d\n", textureStatus);
        return 1;
    }
    const auto textureReportText = arp::core::readTextFile(textureReportPath);
    if (!textureReportText.success
        || textureReportText.text.find("kind: texture") == std::string::npos
        || textureReportText.text.find("mime: image/png") == std::string::npos
        || textureReportText.text.find("width: 211") == std::string::npos
        || textureReportText.text.find("height: 211") == std::string::npos) {
        std::printf("FAIL: texture import smoke report contents are invalid\n");
        return 1;
    }

    const auto textureMetaPath = root / "tests" / "fixtures" / "asset_importer" / "unity_project"
        / "Assets" / "Materials" / "Particle.png.meta";
    arp::core::pmr::Vector<arp::scriptvm::Value> textureMetaArgs{
        &arp::core::defaultCpuMemoryResource()};
    textureMetaArgs.emplace_back(arp::scriptvm::makeString(textureMetaPath.generic_string()));
    textureMetaArgs.emplace_back(arp::scriptvm::makeString(root.generic_string()));
    const auto textureMeta =
        runner.runFile(root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
            textureMetaArgs);
    if (!textureMeta.success) {
        std::printf("FAIL: texture meta dispatch smoke: %s\n", textureMeta.diagnostics.c_str());
        return 1;
    }
    const auto textureMetaReport = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "export",
        [&]() {
            arp::core::pmr::Vector<arp::scriptvm::Value> values{
                &arp::core::defaultCpuMemoryResource()};
            values.emplace_back(arp::scriptvm::makeString(textureMetaPath.generic_string()));
            values.emplace_back(arp::scriptvm::makeString(root.generic_string()));
            values.emplace_back(arp::scriptvm::makeString(textureReportPath.generic_string()));
            return values;
        }());
    if (!textureMetaReport.success) {
        std::printf("FAIL: texture meta report smoke: %s\n", textureMetaReport.diagnostics.c_str());
        return 1;
    }
    const auto textureMetaReportText = arp::core::readTextFile(textureReportPath);
    if (!textureMetaReportText.success
        || textureMetaReportText.text.find("kind: texture_meta") == std::string::npos
        || textureMetaReportText.text.find("importer: TextureImporter") == std::string::npos) {
        std::printf("FAIL: texture meta smoke report contents are invalid\n");
        return 1;
    }
    arp::core::pmr::Vector<arp::scriptvm::Value> databaseArgs{
        &arp::core::defaultCpuMemoryResource()};
    databaseArgs.emplace_back(arp::scriptvm::makeString(
        (root / "tests" / "fixtures" / "asset_importer" / "unity_project").generic_string()));
    databaseArgs.emplace_back(arp::scriptvm::makeString(std::string_view("player_fbx_guid")));
    const auto databaseLookup = runner.runFileFunction(
        root / "tests" / "fixtures" / "asset_importer" / "script_unity_import.arp",
        "inspect_database",
        databaseArgs);
    if (!databaseLookup.success) {
        std::printf("FAIL: unity asset database lookup: %s\n", databaseLookup.diagnostics.c_str());
        return 1;
    }
    int databaseLookupStatus = -1;
    if (!arp::scriptvm::tryGetInt(databaseLookup.value, databaseLookupStatus)
        || databaseLookupStatus != 0) {
        std::printf("FAIL: unity asset database lookup returned status %d\n", databaseLookupStatus);
        return 1;
    }
    std::printf("asset importer script smoke passed\n");
    return 0;
}
