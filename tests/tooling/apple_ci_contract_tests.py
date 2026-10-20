from __future__ import annotations

import importlib.util
import plistlib
import stat
import sys
import tempfile
import unittest
import xml.etree.ElementTree as element_tree
import zipfile
from datetime import datetime, timedelta, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


class RecordingCommandRunner:
    dry_run = True

    def __init__(self) -> None:
        self.commands: list[tuple[str, ...]] = []

    def run(self, arguments, cwd: Path) -> None:
        del cwd
        self.commands.append(tuple(str(argument) for argument in arguments))


class WritingCommandRunner(RecordingCommandRunner):
    dry_run = False


class AppleCiContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.build_module = load_module("arp_build_contract", ROOT / "build.py")
        cls.apple_ci = load_module("arp_apple_ci_contract", ROOT / "ci" / "apple_build.py")
        cls.apple_signing = load_module(
            "arp_apple_signing_contract", ROOT / "ci" / "apple_signing.py")

    def test_ios_module_is_a_deterministic_xcode_bundle(self) -> None:
        build_module = self.build_module
        build_module.BUILD_PLATFORM = "ios"
        build_module.BUILD_BACKEND = "metal"
        modules = build_module._auto_discover_modules()

        app = modules["arp_ios_smoke_app"]
        metal = modules["arp_rhi_metal"]
        self.assertEqual(app.type, "bundle")
        self.assertEqual(app.condition, "BUILD_PLATFORM == 'ios'")
        self.assertEqual(app.bundle_identifier, "com.animerenderpipeline.metal-smoke")
        self.assertIn("arp_rhi_metal", app.deps)
        self.assertTrue(all(path.endswith(".mm") for path in metal.platform_srcs["apple"]))
        self.assertTrue(all(
            not path.endswith(".mm") for path in metal.platform_srcs["non_apple"]))

    def test_generated_cmake_owns_the_xcode_project_contract(self) -> None:
        build_module = self.build_module
        build_module.BUILD_PLATFORM = "ios"
        build_module.BUILD_BACKEND = "metal"
        modules = build_module._auto_discover_modules()
        selected = {
            name: module
            for name, module in modules.items()
            if module.condition is None
            or eval(module.condition, vars(build_module))
        }

        with tempfile.TemporaryDirectory() as temporary_directory:
            output = Path(temporary_directory) / "CMakeLists.txt"
            writer = build_module.CmakeWriter(
                selected,
                "build/ios-metal",
                "debug",
                "ios",
                "metal",
                "clang",
                "clang++",
                "ninja",
                "",
                "/usr/local/bin/slangc",
            )
            writer.generate(str(output))
            cmake = output.read_text(encoding="utf-8")

        self.assertIn("add_executable(arp_ios_smoke_app MACOSX_BUNDLE", cmake)
        self.assertIn("MACOSX_BUNDLE_INFO_PLIST", cmake)
        self.assertIn("XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER", cmake)
        self.assertIn("ARP_IOS_BUNDLE_IDENTIFIER", cmake)
        self.assertIn("XCODE_ATTRIBUTE_SKIP_INSTALL NO", cmake)
        self.assertIn("target_link_libraries(arp_platform PUBLIC arp_rhi_metal)", cmake)
        self.assertIn("if(APPLE)\n    target_sources(arp_rhi_metal", cmake)
        self.assertIn("if(NOT APPLE)\n    target_sources(arp_rhi_metal", cmake)
        self.assertIn("add_dependencies(arp_ios_smoke_app arp_compile_shaders)", cmake)
        self.assertIn("$<TARGET_BUNDLE_DIR:arp_ios_smoke_app>/shaders", cmake)

    def test_ci_pipeline_uses_unsigned_device_build_and_both_metal_sdks(self) -> None:
        apple_ci = self.apple_ci
        runner = RecordingCommandRunner()
        config = apple_ci.AppleBuildConfig(
            source_dir=ROOT,
            build_root=ROOT / "build" / "apple-ci-contract",
            configuration="Debug",
            slangc=Path("/usr/local/bin/slangc"),
            parallel=2,
        )
        pipelines = apple_ci.ApplePipelineFactory.create("all", config, runner)
        for pipeline in pipelines:
            pipeline.execute()

        rendered = "\n".join(" ".join(command) for command in runner.commands)
        self.assertIn("ctest --test-dir", rendered)
        self.assertIn("arp_rhi_metal_device_contract_tests", rendered)
        self.assertIn("arp_rhi_metal_runtime_hash_contract_tests", rendered)
        self.assertIn("-G Xcode", rendered)
        self.assertIn("-DCMAKE_SYSTEM_NAME=iOS", rendered)
        self.assertIn("-sdk iphoneos", rendered)
        self.assertIn("CODE_SIGNING_ALLOWED=NO", rendered)
        self.assertIn("arp_ios_smoke_app", rendered)

    def test_sideload_pipeline_packages_deterministic_unsigned_ipa(self) -> None:
        apple_ci = self.apple_ci
        runner = RecordingCommandRunner()
        config = apple_ci.AppleBuildConfig(
            source_dir=ROOT,
            build_root=ROOT / "build" / "apple-sideload-contract",
            configuration="Debug",
            slangc=Path("/usr/local/bin/slangc"),
            parallel=2,
        )
        pipeline = apple_ci.ApplePipelineFactory.create(
            "ios-sideload", config, runner)[0]
        self.assertIsInstance(pipeline, apple_ci.SideloadIosMetalPipeline)
        pipeline.execute()
        rendered = "\n".join(" ".join(command) for command in runner.commands)
        self.assertIn("CODE_SIGNING_ALLOWED=NO", rendered)
        self.assertIn("CONFIGURATION_BUILD_DIR=", rendered)
        self.assertNotIn("DEVELOPMENT_TEAM=", rendered)

        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            app = root / "arp_ios_smoke_app.app"
            shaders = app / "shaders"
            app.mkdir(parents=True)
            shaders.mkdir(parents=True)
            plist = {
                "CFBundleIdentifier": "com.animerenderpipeline.metal-smoke",
                "CFBundleExecutable": "arp_ios_smoke_app",
            }
            (app / "Info.plist").write_bytes(plistlib.dumps(plist))
            executable = app / "arp_ios_smoke_app"
            executable.write_bytes(b"test executable")
            executable.chmod(0o755)
            (app / "asset_manifest.yaml").write_text(
                "type: ArpIosSmokeBundle\ncontractVersion: 1\n",
                encoding="utf-8",
            )
            (shaders / "smoke.metal").write_text(
                "// generated MSL\n", encoding="utf-8")
            (shaders / "smoke.reflection.yaml").write_text(
                "type: ShaderReflection\n", encoding="utf-8")
            first_ipa = root / "first.ipa"
            second_ipa = root / "second.ipa"
            packager = apple_ci.UnsignedIpaPackager()
            packager.package(
                app, first_ipa, "com.animerenderpipeline.metal-smoke")
            packager.package(
                app, second_ipa, "com.animerenderpipeline.metal-smoke")

            self.assertEqual(first_ipa.read_bytes(), second_ipa.read_bytes())
            with zipfile.ZipFile(first_ipa) as archive:
                names = set(archive.namelist())
                executable_entry = archive.getinfo(
                    "Payload/arp_ios_smoke_app.app/arp_ios_smoke_app")
            self.assertIn(
                "Payload/arp_ios_smoke_app.app/Info.plist", names)
            self.assertIn(
                "Payload/arp_ios_smoke_app.app/asset_manifest.yaml",
                names,
            )
            self.assertFalse(any("_CodeSignature" in name for name in names))
            self.assertTrue(
                (executable_entry.external_attr >> 16) & stat.S_IXUSR)

            (app / "embedded.mobileprovision").write_bytes(b"unexpected")
            with self.assertRaises(apple_ci.AppleBuildError):
                packager.package(
                    app, root / "invalid.ipa",
                    "com.animerenderpipeline.metal-smoke")

    def test_signed_ios_pipeline_archives_and_exports_ad_hoc_ipa(self) -> None:
        apple_ci = self.apple_ci
        runner = RecordingCommandRunner()
        signing = apple_ci.IosSigningConfig(
            team_id="ABCDE12345",
            bundle_identifier="com.animerenderpipeline.metal-smoke",
            provisioning_profile_specifier="11111111-2222-3333-4444-555555555555",
            signing_certificate="Apple Distribution",
        )
        config = apple_ci.AppleBuildConfig(
            source_dir=ROOT,
            build_root=ROOT / "build" / "apple-signed-contract",
            configuration="Release",
            slangc=Path("/usr/local/bin/slangc"),
            parallel=2,
            ios_signing=signing,
        )
        pipeline = apple_ci.ApplePipelineFactory.create(
            "ios-signed", config, runner)[0]
        pipeline.execute()

        rendered = "\n".join(" ".join(command) for command in runner.commands)
        self.assertIn("-DARP_IOS_BUNDLE_IDENTIFIER=com.animerenderpipeline.metal-smoke", rendered)
        self.assertIn("-scheme arp_ios_smoke_app", rendered)
        self.assertIn("-destination generic/platform=iOS", rendered)
        self.assertIn("DEVELOPMENT_TEAM=ABCDE12345", rendered)
        self.assertIn("CODE_SIGN_STYLE=Manual", rendered)
        self.assertIn("PROVISIONING_PROFILE_SPECIFIER=11111111-2222-3333-4444-555555555555", rendered)
        self.assertIn("clean archive", rendered)
        self.assertIn("xcodebuild -exportArchive", rendered)

        with tempfile.TemporaryDirectory() as temporary_directory:
            writable_config = apple_ci.AppleBuildConfig(
                source_dir=ROOT,
                build_root=Path(temporary_directory),
                configuration="Release",
                slangc=Path("/usr/local/bin/slangc"),
                parallel=2,
                ios_signing=signing,
            )
            writable_pipeline = apple_ci.SignedIosMetalPipeline(
                writable_config, WritingCommandRunner())
            export_options = plistlib.loads(
                writable_pipeline.write_export_options().read_bytes())
        self.assertEqual(export_options["method"], "release-testing")
        self.assertEqual(export_options["signingStyle"], "manual")
        self.assertEqual(export_options["teamID"], "ABCDE12345")
        self.assertEqual(
            export_options["provisioningProfiles"][
                "com.animerenderpipeline.metal-smoke"],
            "11111111-2222-3333-4444-555555555555")

    def test_ad_hoc_profile_contract_requires_exact_app_and_devices(self) -> None:
        signing = self.apple_signing
        team_id = "ABCDE12345"
        bundle_identifier = "com.animerenderpipeline.metal-smoke"
        profile = {
            "UUID": "11111111-2222-3333-4444-555555555555",
            "Name": "ARP Ad Hoc",
            "TeamIdentifier": [team_id],
            "Platform": ["iOS"],
            "ExpirationDate": datetime.now(timezone.utc) + timedelta(days=30),
            "ProvisionedDevices": ["device-udid"],
            "Entitlements": {
                "application-identifier": f"{team_id}.{bundle_identifier}",
                "com.apple.developer.team-identifier": team_id,
                "get-task-allow": False,
            },
            "DeveloperCertificates": [b"certificate"],
        }
        payload = plistlib.dumps(profile)
        contract = signing.ProvisioningProfileContract.parse_ad_hoc(
            payload, bundle_identifier)
        self.assertEqual(contract.team_id, team_id)
        self.assertEqual(contract.provisioned_device_count, 1)

        profile["ProvisionedDevices"] = []
        with self.assertRaises(signing.AppleSigningError):
            signing.ProvisioningProfileContract.parse_ad_hoc(
                plistlib.dumps(profile), bundle_identifier)

        profile["ProvisionedDevices"] = ["device-udid"]
        profile["Entitlements"]["application-identifier"] = (
            f"{team_id}.com.example.wrong-app")
        with self.assertRaises(signing.AppleSigningError):
            signing.ProvisioningProfileContract.parse_ad_hoc(
                plistlib.dumps(profile), bundle_identifier)

    def test_signed_workflow_keeps_secrets_out_of_arguments(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "ios-signed.yml").read_text(
            encoding="utf-8")
        self.assertIn("APPLE_CERTIFICATE_P12_BASE64: ${{ secrets.", workflow)
        self.assertIn("python3 ci/apple_signing.py install", workflow)
        self.assertIn("python3 ci/apple_build.py ios-signed", workflow)
        self.assertIn("python3 ci/apple_signing.py verify-ipa", workflow)
        self.assertIn("if-no-files-found: error", workflow)

    def test_unsigned_workflow_publishes_local_signing_ipa(self) -> None:
        workflow = (ROOT / ".github" / "workflows" / "apple-metal.yml").read_text(
            encoding="utf-8")
        self.assertIn("python3 ci/apple_build.py ios-sideload", workflow)
        self.assertIn("arp-ios-unsigned-${{ github.sha }}", workflow)
        self.assertIn("arp_ios_smoke_unsigned.ipa", workflow)
        self.assertNotIn("APPLE_CERTIFICATE_PASSWORD", workflow)

    def test_info_plist_and_yaml_authoring_contract_are_explicit(self) -> None:
        plist_path = ROOT / "src" / "platform" / "ios_app" / "Info.plist"
        element_tree.parse(plist_path)

        manifest = (
            ROOT / "src" / "platform" / "ios_app" / "Resources" / "asset_manifest.yaml"
        ).read_text(encoding="utf-8")
        self.assertTrue(manifest.startswith("type: ArpIosSmokeBundle\n"))
        self.assertIn("contractVersion: 1\n", manifest)
        self.assertIn("status: Prototype Complete\n", manifest)


if __name__ == "__main__":
    unittest.main()
