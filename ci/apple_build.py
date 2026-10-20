#!/usr/bin/env python3
"""Deterministic macOS Metal and unsigned/signed iOS Xcode CI entry point."""

from __future__ import annotations

import argparse
import os
import platform
import plistlib
import shutil
import stat
import subprocess
import sys
import zipfile
from abc import ABC, abstractmethod
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence


class AppleBuildError(RuntimeError):
    pass


DEFAULT_IOS_BUNDLE_IDENTIFIER = "com.animerenderpipeline.metal-smoke"


@dataclass(frozen=True)
class IosSigningConfig:
    team_id: str
    bundle_identifier: str
    provisioning_profile_specifier: str
    signing_certificate: str


@dataclass(frozen=True)
class AppleBuildConfig:
    source_dir: Path
    build_root: Path
    configuration: str
    slangc: Path
    parallel: int
    ios_bundle_identifier: str = DEFAULT_IOS_BUNDLE_IDENTIFIER
    ios_signing: IosSigningConfig | None = None


class CommandRunner:
    def __init__(self, dry_run: bool = False) -> None:
        self._dry_run = dry_run

    @property
    def dry_run(self) -> bool:
        return self._dry_run

    def run(self, arguments: Sequence[str], cwd: Path) -> None:
        rendered = subprocess.list2cmdline([str(argument) for argument in arguments])
        print(f"[apple-ci] {rendered}", flush=True)
        if self._dry_run:
            return
        result = subprocess.run(
            [str(argument) for argument in arguments],
            cwd=str(cwd),
            check=False,
        )
        if result.returncode != 0:
            raise AppleBuildError(
                f"command failed with exit code {result.returncode}: {rendered}")


class AppleBuildPipeline(ABC):
    def __init__(self, config: AppleBuildConfig, runner: CommandRunner) -> None:
        self.config = config
        self.runner = runner

    @property
    @abstractmethod
    def build_dir(self) -> Path:
        raise NotImplementedError

    @property
    @abstractmethod
    def metal_sdk(self) -> str:
        raise NotImplementedError

    @abstractmethod
    def configure(self) -> None:
        raise NotImplementedError

    @abstractmethod
    def build_and_test(self) -> None:
        raise NotImplementedError

    def execute(self) -> None:
        self.validate_environment()
        self.configure()
        self.build_and_test()
        self.compile_generated_msl()

    def validate_environment(self) -> None:
        if platform.system() != "Darwin" and not self.runner.dry_run:
            raise AppleBuildError("Apple CI pipelines require a macOS host")
        for tool in ("cmake", "ninja", "python3", "xcodebuild", "xcrun"):
            if shutil.which(tool) is None and not self.runner.dry_run:
                raise AppleBuildError(f"required tool is unavailable: {tool}")
        if not self.config.slangc.is_file() and not self.runner.dry_run:
            raise AppleBuildError(
                f"SLANGC_EXECUTABLE does not name a file: {self.config.slangc}")

    def cmake_configure(self, generator: str, extra: Iterable[str]) -> None:
        arguments = [
            "cmake",
            "-S",
            str(self.config.source_dir),
            "-B",
            str(self.build_dir),
            "-G",
            generator,
            f"-DSLANGC_EXECUTABLE={self.config.slangc}",
            "-DARP_ENABLE_VENDORED_ASSIMP=OFF",
            *extra,
        ]
        self.runner.run(arguments, self.config.source_dir)

    def compile_generated_msl(self) -> None:
        shader_dir = self.build_dir / "shaders"
        shader_paths = sorted(shader_dir.glob("*.metal"))
        if not shader_paths and not self.runner.dry_run:
            raise AppleBuildError(f"no generated MSL artifacts found under {shader_dir}")

        air_dir = self.build_dir / "metal-air" / self.metal_sdk
        if not self.runner.dry_run:
            air_dir.mkdir(parents=True, exist_ok=True)
        for shader_path in shader_paths:
            output_path = air_dir / f"{shader_path.stem}.air"
            self.runner.run(
                [
                    "xcrun",
                    "--sdk",
                    self.metal_sdk,
                    "metal",
                    "-c",
                    str(shader_path),
                    "-o",
                    str(output_path),
                ],
                self.config.source_dir,
            )


class MacOSMetalPipeline(AppleBuildPipeline):
    @property
    def build_dir(self) -> Path:
        return self.config.build_root / "macos-metal"

    @property
    def metal_sdk(self) -> str:
        return "macosx"

    def configure(self) -> None:
        self.cmake_configure(
            "Ninja",
            [
                f"-DCMAKE_BUILD_TYPE={self.config.configuration}",
                "-DCMAKE_C_COMPILER=clang",
                "-DCMAKE_CXX_COMPILER=clang++",
            ],
        )

    def build_and_test(self) -> None:
        self.runner.run(
            [
                "cmake",
                "--build",
                str(self.build_dir),
                "--target",
                "arp_compile_shaders",
                "arp_shader_tests",
                "arp_ios_platform_contract_tests",
                "arp_rhi_metal_device_contract_tests",
                "arp_rhi_metal_runtime_hash_contract_tests",
                "--parallel",
                str(self.config.parallel),
            ],
            self.config.source_dir,
        )
        self.runner.run(
            [
                "ctest",
                "--test-dir",
                str(self.build_dir),
                "-R",
                "^(arp_shader_tests|arp_ios_platform_contract_tests|arp_rhi_metal_device_contract_tests|arp_rhi_metal_runtime_hash_contract_tests)$",
                "--output-on-failure",
            ],
            self.config.source_dir,
        )


class UnsignedIpaPackager:
    _archive_timestamp = (1980, 1, 1, 0, 0, 0)

    def package(
        self,
        app_path: Path,
        output_path: Path,
        expected_bundle_identifier: str,
    ) -> None:
        if not app_path.is_dir() or app_path.is_symlink():
            raise AppleBuildError(f"unsigned app bundle is missing: {app_path}")
        info_path = app_path / "Info.plist"
        if not info_path.is_file():
            raise AppleBuildError(f"app bundle has no Info.plist: {info_path}")
        try:
            info = plistlib.loads(info_path.read_bytes())
        except (plistlib.InvalidFileException, ValueError, TypeError) as error:
            raise AppleBuildError(f"app Info.plist is invalid: {error}") from error
        if info.get("CFBundleIdentifier") != expected_bundle_identifier:
            raise AppleBuildError(
                "unsigned app bundle identifier does not match the packaging "
                f"contract: {info.get('CFBundleIdentifier')}")
        executable_name = info.get("CFBundleExecutable")
        if not isinstance(executable_name, str) or not executable_name:
            raise AppleBuildError("app Info.plist has no CFBundleExecutable")
        executable_path = app_path / executable_name
        if not executable_path.is_file():
            raise AppleBuildError(
                f"app executable is missing: {executable_path}")
        if (app_path / "_CodeSignature").exists():
            raise AppleBuildError(
                "sideload input must be unsigned; _CodeSignature is present")
        if (app_path / "embedded.mobileprovision").exists():
            raise AppleBuildError(
                "sideload input must not contain embedded.mobileprovision")
        manifest_path = app_path / "asset_manifest.yaml"
        if not manifest_path.is_file():
            raise AppleBuildError(
                f"app resource contract is missing: {manifest_path}")
        shader_path = app_path / "shaders"
        if not any(shader_path.glob("*.metal")):
            raise AppleBuildError(
                f"app bundle contains no generated MSL shaders: {shader_path}")
        if not any(shader_path.glob("*.reflection.yaml")):
            raise AppleBuildError(
                f"app bundle contains no shader reflection: {shader_path}")

        files = sorted(
            (path for path in app_path.rglob("*") if path.is_file()),
            key=lambda path: path.relative_to(app_path).as_posix(),
        )
        for path in app_path.rglob("*"):
            if path.is_symlink():
                raise AppleBuildError(
                    f"symlinks are outside the unsigned IPA contract: {path}")
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(
            output_path,
            mode="w",
            compression=zipfile.ZIP_DEFLATED,
            compresslevel=9,
        ) as archive:
            for source_path in files:
                relative_path = source_path.relative_to(app_path).as_posix()
                archive_path = f"Payload/{app_path.name}/{relative_path}"
                archive_info = zipfile.ZipInfo(
                    archive_path, date_time=self._archive_timestamp)
                archive_info.create_system = 3
                mode = 0o755 if source_path == executable_path else 0o644
                archive_info.external_attr = (stat.S_IFREG | mode) << 16
                archive_info.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(archive_info, source_path.read_bytes())
        print(f"[apple-ci] unsigned sideload IPA: {output_path}", flush=True)


class IosMetalPipeline(AppleBuildPipeline):
    @property
    def build_dir(self) -> Path:
        return self.config.build_root / "ios-metal"

    @property
    def metal_sdk(self) -> str:
        return "iphoneos"

    def configure(self) -> None:
        extra = [
            "-DCMAKE_SYSTEM_NAME=iOS",
            "-DCMAKE_OSX_SYSROOT=iphoneos",
            "-DCMAKE_OSX_ARCHITECTURES=arm64",
        ]
        extra.append(
            "-DARP_IOS_BUNDLE_IDENTIFIER="
            f"{self.config.ios_bundle_identifier}")
        self.cmake_configure(
            "Xcode",
            extra,
        )

    @property
    def project_path(self) -> Path:
        return self.build_dir / "AnimeRenderPipeline.xcodeproj"

    @property
    def unsigned_products_dir(self) -> Path:
        return self.build_dir / "unsigned-products"

    @property
    def unsigned_app_path(self) -> Path:
        return self.unsigned_products_dir / "arp_ios_smoke_app.app"

    def build_shaders(self) -> None:
        self.runner.run(
            [
                "cmake",
                "--build",
                str(self.build_dir),
                "--config",
                self.config.configuration,
                "--target",
                "arp_compile_shaders",
                "--parallel",
                str(self.config.parallel),
            ],
            self.config.source_dir,
        )
        if not self.project_path.is_dir() and not self.runner.dry_run:
            raise AppleBuildError(
                f"generated Xcode project is missing: {self.project_path}")

    def build_and_test(self) -> None:
        self.build_shaders()
        self.runner.run(
            [
                "xcodebuild",
                "-project",
                str(self.project_path),
                "-target",
                "arp_ios_smoke_app",
                "-configuration",
                self.config.configuration,
                "-sdk",
                "iphoneos",
                "CODE_SIGNING_ALLOWED=NO",
                "CODE_SIGNING_REQUIRED=NO",
                f"CONFIGURATION_BUILD_DIR={self.unsigned_products_dir}",
                "build",
            ],
            self.config.source_dir,
        )


class SideloadIosMetalPipeline(IosMetalPipeline):
    @property
    def unsigned_ipa_path(self) -> Path:
        return self.build_dir / "sideload" / "arp_ios_smoke_unsigned.ipa"

    def build_and_test(self) -> None:
        super().build_and_test()
        if self.runner.dry_run:
            print(
                "[apple-ci] package unsigned sideload IPA: "
                f"{self.unsigned_app_path} -> {self.unsigned_ipa_path}",
                flush=True,
            )
            return
        UnsignedIpaPackager().package(
            self.unsigned_app_path,
            self.unsigned_ipa_path,
            self.config.ios_bundle_identifier,
        )


class SignedIosMetalPipeline(IosMetalPipeline):
    @property
    def signing(self) -> IosSigningConfig:
        if self.config.ios_signing is None:
            raise AppleBuildError(
                "ios-signed requires team ID, bundle ID, and provisioning profile")
        if (self.config.ios_signing.bundle_identifier
                != self.config.ios_bundle_identifier):
            raise AppleBuildError(
                "iOS signing and CMake bundle identifiers do not match")
        return self.config.ios_signing

    @property
    def archive_path(self) -> Path:
        return self.build_dir / "archive" / "arp_ios_smoke_app.xcarchive"

    @property
    def export_path(self) -> Path:
        return self.build_dir / "export"

    def write_export_options(self) -> Path:
        options_path = self.build_dir / "ExportOptions.plist"
        options = {
            "destination": "export",
            "method": "release-testing",
            "provisioningProfiles": {
                self.signing.bundle_identifier:
                    self.signing.provisioning_profile_specifier,
            },
            "signingCertificate": self.signing.signing_certificate,
            "signingStyle": "manual",
            "stripSwiftSymbols": True,
            "teamID": self.signing.team_id,
        }
        if not self.runner.dry_run:
            options_path.parent.mkdir(parents=True, exist_ok=True)
            with options_path.open("wb") as options_file:
                plistlib.dump(options, options_file, sort_keys=True)
        return options_path

    def build_and_test(self) -> None:
        self.build_shaders()
        signing = self.signing
        self.runner.run(
            [
                "xcodebuild",
                "-project",
                str(self.project_path),
                "-scheme",
                "arp_ios_smoke_app",
                "-configuration",
                self.config.configuration,
                "-sdk",
                "iphoneos",
                "-destination",
                "generic/platform=iOS",
                "-archivePath",
                str(self.archive_path),
                f"DEVELOPMENT_TEAM={signing.team_id}",
                f"PRODUCT_BUNDLE_IDENTIFIER={signing.bundle_identifier}",
                "CODE_SIGN_STYLE=Manual",
                f"CODE_SIGN_IDENTITY={signing.signing_certificate}",
                "PROVISIONING_PROFILE_SPECIFIER="
                f"{signing.provisioning_profile_specifier}",
                "clean",
                "archive",
            ],
            self.config.source_dir,
        )
        export_options = self.write_export_options()
        self.runner.run(
            [
                "xcodebuild",
                "-exportArchive",
                "-archivePath",
                str(self.archive_path),
                "-exportOptionsPlist",
                str(export_options),
                "-exportPath",
                str(self.export_path),
            ],
            self.config.source_dir,
        )
        if not self.runner.dry_run:
            ipa_paths = sorted(self.export_path.glob("*.ipa"))
            if len(ipa_paths) != 1:
                raise AppleBuildError(
                    "signed export must produce exactly one IPA under "
                    f"{self.export_path}; found {len(ipa_paths)}")
            expected_ipa_path = self.export_path / "arp_ios_smoke_app.ipa"
            actual_ipa_path = ipa_paths[0]
            if actual_ipa_path != expected_ipa_path:
                if expected_ipa_path.exists():
                    raise AppleBuildError(
                        f"refusing to replace existing IPA: {expected_ipa_path}")
                actual_ipa_path.rename(expected_ipa_path)
            print(f"[apple-ci] signed IPA: {expected_ipa_path}", flush=True)


class ApplePipelineFactory:
    @staticmethod
    def create(
        name: str,
        config: AppleBuildConfig,
        runner: CommandRunner,
    ) -> list[AppleBuildPipeline]:
        if name == "macos":
            return [MacOSMetalPipeline(config, runner)]
        if name == "ios":
            return [IosMetalPipeline(config, runner)]
        if name == "ios-sideload":
            return [SideloadIosMetalPipeline(config, runner)]
        if name == "ios-signed":
            if config.ios_signing is None:
                raise AppleBuildError(
                    "ios-signed requires APPLE_TEAM_ID and "
                    "APPLE_PROVISIONING_PROFILE_SPECIFIER")
            return [SignedIosMetalPipeline(config, runner)]
        if name == "all":
            return [MacOSMetalPipeline(config, runner), IosMetalPipeline(config, runner)]
        raise AppleBuildError(f"unknown Apple pipeline: {name}")


def resolve_slangc(value: str | None, dry_run: bool) -> Path:
    candidate = value or os.environ.get("SLANGC_EXECUTABLE") or shutil.which("slangc")
    if candidate:
        return Path(candidate).expanduser().resolve()
    if dry_run:
        return Path("/usr/local/bin/slangc")
    raise AppleBuildError(
        "slangc is required; install Homebrew shader-slang or set SLANGC_EXECUTABLE")


def resolve_ios_signing(
    args: argparse.Namespace,
    bundle_identifier: str,
) -> IosSigningConfig | None:
    if args.pipeline != "ios-signed":
        return None

    team_id = args.team_id or os.environ.get("APPLE_TEAM_ID")
    profile = (args.provisioning_profile_specifier
               or os.environ.get("APPLE_PROVISIONING_PROFILE_SPECIFIER"))
    if not team_id or not profile:
        raise AppleBuildError(
            "ios-signed requires APPLE_TEAM_ID and "
            "APPLE_PROVISIONING_PROFILE_SPECIFIER")
    return IosSigningConfig(
        team_id=team_id,
        bundle_identifier=bundle_identifier,
        provisioning_profile_specifier=profile,
        signing_certificate=(args.signing_certificate
                             or os.environ.get("APPLE_SIGNING_CERTIFICATE")
                             or "Apple Distribution"),
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "pipeline",
        choices=("macos", "ios", "ios-sideload", "ios-signed", "all"),
    )
    parser.add_argument("--source-dir", default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-root", default=None)
    parser.add_argument("--configuration", choices=("Debug", "Release"), default="Debug")
    parser.add_argument("--slangc", default=None)
    parser.add_argument("--parallel", type=int, default=2)
    parser.add_argument("--team-id", default=None)
    parser.add_argument("--bundle-identifier", default=None)
    parser.add_argument("--provisioning-profile-specifier", default=None)
    parser.add_argument("--signing-certificate", default=None)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    source_dir = Path(args.source_dir).resolve()
    build_root = (Path(args.build_root).resolve() if args.build_root
                  else source_dir / "build" / "ci-apple")
    bundle_identifier = (
        args.bundle_identifier
        or os.environ.get("APPLE_BUNDLE_IDENTIFIER")
        or DEFAULT_IOS_BUNDLE_IDENTIFIER
    )
    runner = CommandRunner(args.dry_run)
    config = AppleBuildConfig(
        source_dir=source_dir,
        build_root=build_root,
        configuration=args.configuration,
        slangc=resolve_slangc(args.slangc, args.dry_run),
        parallel=max(1, args.parallel),
        ios_bundle_identifier=bundle_identifier,
        ios_signing=resolve_ios_signing(args, bundle_identifier),
    )
    try:
        for pipeline in ApplePipelineFactory.create(args.pipeline, config, runner):
            pipeline.execute()
    except AppleBuildError as error:
        print(f"[apple-ci] ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
