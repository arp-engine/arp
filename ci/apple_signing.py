#!/usr/bin/env python3
"""Install and validate deterministic Ad Hoc iOS signing material."""

from __future__ import annotations

import argparse
import base64
import binascii
import os
import platform
import plistlib
import re
import secrets
import shutil
import subprocess
import sys
import tempfile
import zipfile
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Mapping, Sequence


DEFAULT_BUNDLE_IDENTIFIER = "com.animerenderpipeline.metal-smoke"
IDENTIFIER_PATTERN = re.compile(r"^[A-Za-z0-9.-]+$")
UUID_PATTERN = re.compile(r"^[A-Fa-f0-9-]+$")


class AppleSigningError(RuntimeError):
    pass


class SecureCommandRunner:
    def run(
        self,
        arguments: Sequence[str],
        *,
        sensitive_values: Sequence[str] = (),
        capture_output: bool = False,
    ) -> bytes:
        rendered_arguments = [str(argument) for argument in arguments]
        rendered = subprocess.list2cmdline(rendered_arguments)
        for sensitive_value in sensitive_values:
            if sensitive_value:
                rendered = rendered.replace(sensitive_value, "<redacted>")
        print(f"[apple-signing] {rendered}", flush=True)
        result = subprocess.run(
            rendered_arguments,
            check=False,
            stdout=subprocess.PIPE if capture_output else None,
            stderr=subprocess.PIPE,
        )
        if result.returncode != 0:
            diagnostic = result.stderr.decode("utf-8", errors="replace").strip()
            raise AppleSigningError(
                f"command failed with exit code {result.returncode}: "
                f"{rendered}\n{diagnostic}")
        return result.stdout if capture_output else b""


@dataclass(frozen=True)
class SigningInputs:
    certificate_p12: bytes
    certificate_password: str
    provisioning_profile: bytes
    bundle_identifier: str

    @classmethod
    def from_environment(cls, environment: Mapping[str, str]) -> "SigningInputs":
        bundle_identifier = (
            environment.get("APPLE_BUNDLE_IDENTIFIER")
            or DEFAULT_BUNDLE_IDENTIFIER
        )
        if not IDENTIFIER_PATTERN.fullmatch(bundle_identifier):
            raise AppleSigningError(
                "APPLE_BUNDLE_IDENTIFIER contains unsupported characters")
        return cls(
            certificate_p12=decode_required_base64(
                environment, "APPLE_CERTIFICATE_P12_BASE64"),
            certificate_password=require_environment(
                environment, "APPLE_CERTIFICATE_PASSWORD"),
            provisioning_profile=decode_required_base64(
                environment, "APPLE_PROVISIONING_PROFILE_BASE64"),
            bundle_identifier=bundle_identifier,
        )


@dataclass(frozen=True)
class ProvisioningProfileContract:
    uuid: str
    name: str
    team_id: str
    bundle_identifier: str
    expiration: datetime
    provisioned_device_count: int

    @classmethod
    def parse_ad_hoc(
        cls,
        payload: bytes,
        expected_bundle_identifier: str,
    ) -> "ProvisioningProfileContract":
        try:
            profile = plistlib.loads(payload)
        except (plistlib.InvalidFileException, ValueError, TypeError) as error:
            raise AppleSigningError(
                f"provisioning profile payload is not a plist: {error}") from error
        if not isinstance(profile, dict):
            raise AppleSigningError("provisioning profile root must be a dictionary")

        uuid = require_plist_string(profile, "UUID")
        if not UUID_PATTERN.fullmatch(uuid):
            raise AppleSigningError("provisioning profile UUID is malformed")
        name = require_plist_string(profile, "Name")
        team_identifiers = profile.get("TeamIdentifier")
        if not isinstance(team_identifiers, list) or len(team_identifiers) != 1:
            raise AppleSigningError(
                "provisioning profile must declare exactly one TeamIdentifier")
        team_id = team_identifiers[0]
        if not isinstance(team_id, str) or not IDENTIFIER_PATTERN.fullmatch(team_id):
            raise AppleSigningError("provisioning profile TeamIdentifier is malformed")

        platforms = profile.get("Platform")
        if not isinstance(platforms, list) or "iOS" not in platforms:
            raise AppleSigningError("provisioning profile does not target iOS")
        expiration = profile.get("ExpirationDate")
        if not isinstance(expiration, datetime):
            raise AppleSigningError("provisioning profile has no ExpirationDate")
        expiration_utc = (expiration.replace(tzinfo=timezone.utc)
                          if expiration.tzinfo is None
                          else expiration.astimezone(timezone.utc))
        if expiration_utc <= datetime.now(timezone.utc):
            raise AppleSigningError("provisioning profile is expired")

        devices = profile.get("ProvisionedDevices")
        if not isinstance(devices, list) or not devices:
            raise AppleSigningError(
                "profile contains no ProvisionedDevices; an Ad Hoc profile with "
                "the target iPhone UDID is required")
        if profile.get("ProvisionsAllDevices") is True:
            raise AppleSigningError(
                "enterprise profiles are outside the Ad Hoc signing contract")

        entitlements = profile.get("Entitlements")
        if not isinstance(entitlements, dict):
            raise AppleSigningError("provisioning profile has no Entitlements")
        if entitlements.get("get-task-allow") is not False:
            raise AppleSigningError(
                "development profiles are outside the Ad Hoc signing contract")
        application_identifier = entitlements.get("application-identifier")
        expected_application_identifier = (
            f"{team_id}.{expected_bundle_identifier}")
        if application_identifier != expected_application_identifier:
            raise AppleSigningError(
                "profile application-identifier does not exactly match "
                f"{expected_application_identifier}")
        if entitlements.get("com.apple.developer.team-identifier") != team_id:
            raise AppleSigningError(
                "profile entitlement team identifier does not match TeamIdentifier")
        certificates = profile.get("DeveloperCertificates")
        if not isinstance(certificates, list) or not certificates:
            raise AppleSigningError(
                "provisioning profile contains no developer certificates")

        return cls(
            uuid=uuid,
            name=name,
            team_id=team_id,
            bundle_identifier=expected_bundle_identifier,
            expiration=expiration_utc,
            provisioned_device_count=len(devices),
        )


class AppleSigningInstaller:
    def __init__(
        self,
        runner: SecureCommandRunner,
        runner_temp: Path,
        environment_file: Path,
    ) -> None:
        self.runner = runner
        self.runner_temp = runner_temp
        self.environment_file = environment_file
        self.keychain_path = runner_temp / "arp-ios-signing.keychain-db"

    def install(self, inputs: SigningInputs) -> ProvisioningProfileContract:
        require_macos()
        self.runner_temp.mkdir(parents=True, exist_ok=True)
        certificate_path = self.runner_temp / "arp-ios-signing.p12"
        raw_profile_path = self.runner_temp / "arp-ios-signing.mobileprovision"
        certificate_path.write_bytes(inputs.certificate_p12)
        raw_profile_path.write_bytes(inputs.provisioning_profile)
        certificate_path.chmod(0o600)
        raw_profile_path.chmod(0o600)

        keychain_password = secrets.token_urlsafe(32)
        installed_profile: Path | None = None
        try:
            self.runner.run(
                ["security", "create-keychain", "-p", keychain_password,
                 str(self.keychain_path)],
                sensitive_values=[keychain_password],
            )
            self.runner.run(
                ["security", "set-keychain-settings", "-lut", "21600",
                 str(self.keychain_path)])
            self.runner.run(
                ["security", "unlock-keychain", "-p", keychain_password,
                 str(self.keychain_path)],
                sensitive_values=[keychain_password],
            )
            self.runner.run(
                ["security", "import", str(certificate_path), "-P",
                 inputs.certificate_password, "-A", "-t", "cert", "-f",
                 "pkcs12", "-k", str(self.keychain_path)],
                sensitive_values=[inputs.certificate_password],
            )
            self.runner.run(
                ["security", "set-key-partition-list", "-S",
                 "apple-tool:,apple:,codesign:", "-s", "-k",
                 keychain_password, str(self.keychain_path)],
                sensitive_values=[keychain_password],
            )
            self.runner.run(
                ["security", "list-keychains", "-d", "user", "-s",
                 str(self.keychain_path)])

            decoded_profile = self.runner.run(
                ["security", "cms", "-D", "-i", str(raw_profile_path)],
                capture_output=True,
            )
            contract = ProvisioningProfileContract.parse_ad_hoc(
                decoded_profile, inputs.bundle_identifier)
            profiles_dir = (
                Path.home() / "Library" / "MobileDevice" /
                "Provisioning Profiles")
            profiles_dir.mkdir(parents=True, exist_ok=True)
            installed_profile = profiles_dir / f"{contract.uuid}.mobileprovision"
            shutil.copyfile(raw_profile_path, installed_profile)
            installed_profile.chmod(0o600)
            self._write_environment(contract, installed_profile)
            print(
                "[apple-signing] installed Ad Hoc profile "
                f"'{contract.name}' for {contract.bundle_identifier}; "
                f"devices={contract.provisioned_device_count}, "
                f"expires={contract.expiration.isoformat()}",
                flush=True,
            )
            return contract
        except Exception:
            self.cleanup(self.runner, self.keychain_path, installed_profile)
            raise
        finally:
            certificate_path.unlink(missing_ok=True)
            raw_profile_path.unlink(missing_ok=True)

    def _write_environment(
        self,
        contract: ProvisioningProfileContract,
        installed_profile: Path,
    ) -> None:
        values = {
            "APPLE_TEAM_ID": contract.team_id,
            "APPLE_BUNDLE_IDENTIFIER": contract.bundle_identifier,
            "APPLE_PROVISIONING_PROFILE_SPECIFIER": contract.uuid,
            "APPLE_SIGNING_CERTIFICATE": "Apple Distribution",
            "APPLE_SIGNING_KEYCHAIN_PATH": str(self.keychain_path),
            "APPLE_PROVISIONING_PROFILE_PATH": str(installed_profile),
        }
        self.environment_file.parent.mkdir(parents=True, exist_ok=True)
        with self.environment_file.open("a", encoding="utf-8", newline="\n") as output:
            for key, value in values.items():
                if "\n" in value or "\r" in value:
                    raise AppleSigningError(
                        f"environment value contains a newline: {key}")
                output.write(f"{key}={value}\n")

    @staticmethod
    def cleanup(
        runner: SecureCommandRunner,
        keychain_path: Path | None,
        profile_path: Path | None,
    ) -> None:
        if profile_path is not None and profile_path.is_file():
            profile_path.unlink()
        if keychain_path is not None and keychain_path.exists():
            runner.run(["security", "delete-keychain", str(keychain_path)])


class SignedIpaVerifier:
    def __init__(self, runner: SecureCommandRunner) -> None:
        self.runner = runner

    def verify(self, ipa_path: Path, expected_bundle_identifier: str) -> None:
        require_macos()
        if not ipa_path.is_file():
            raise AppleSigningError(f"IPA does not exist: {ipa_path}")
        with tempfile.TemporaryDirectory(prefix="arp-ipa-verify-") as directory:
            destination = Path(directory)
            with zipfile.ZipFile(ipa_path) as archive:
                self._validate_archive_paths(archive)
                archive.extractall(destination)
            apps = list((destination / "Payload").glob("*.app"))
            if len(apps) != 1:
                raise AppleSigningError(
                    f"IPA must contain exactly one app; found {len(apps)}")
            app_path = apps[0]
            info = plistlib.loads((app_path / "Info.plist").read_bytes())
            actual_bundle_identifier = info.get("CFBundleIdentifier")
            if actual_bundle_identifier != expected_bundle_identifier:
                raise AppleSigningError(
                    "IPA bundle identifier does not match expected value: "
                    f"{actual_bundle_identifier}")
            self.runner.run(
                ["codesign", "--verify", "--deep", "--strict", str(app_path)])
            embedded_profile = app_path / "embedded.mobileprovision"
            decoded_profile = self.runner.run(
                ["security", "cms", "-D", "-i", str(embedded_profile)],
                capture_output=True,
            )
            contract = ProvisioningProfileContract.parse_ad_hoc(
                decoded_profile, expected_bundle_identifier)
            print(
                "[apple-signing] verified installable Ad Hoc IPA: "
                f"{ipa_path}; devices={contract.provisioned_device_count}",
                flush=True,
            )

    @staticmethod
    def _validate_archive_paths(archive: zipfile.ZipFile) -> None:
        for member in archive.infolist():
            path = Path(member.filename)
            if path.is_absolute() or ".." in path.parts:
                raise AppleSigningError(
                    f"IPA contains an unsafe archive path: {member.filename}")


def require_environment(environment: Mapping[str, str], name: str) -> str:
    value = environment.get(name)
    if not value:
        raise AppleSigningError(f"required environment variable is missing: {name}")
    return value


def decode_required_base64(environment: Mapping[str, str], name: str) -> bytes:
    encoded = require_environment(environment, name)
    try:
        decoded = base64.b64decode(encoded, validate=True)
    except (binascii.Error, ValueError) as error:
        raise AppleSigningError(f"{name} is not valid base64") from error
    if not decoded:
        raise AppleSigningError(f"{name} decoded to an empty payload")
    return decoded


def require_plist_string(profile: Mapping[str, object], key: str) -> str:
    value = profile.get(key)
    if not isinstance(value, str) or not value:
        raise AppleSigningError(f"provisioning profile has no valid {key}")
    return value


def require_macos() -> None:
    if platform.system() != "Darwin":
        raise AppleSigningError("Apple signing operations require macOS")
    for tool in ("security", "codesign"):
        if shutil.which(tool) is None:
            raise AppleSigningError(f"required Apple signing tool is unavailable: {tool}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    install_parser = subparsers.add_parser("install")
    install_parser.add_argument(
        "--environment-file", default=os.environ.get("GITHUB_ENV"))
    subparsers.add_parser("cleanup")
    verify_parser = subparsers.add_parser("verify-ipa")
    verify_parser.add_argument("--ipa", required=True)
    verify_parser.add_argument(
        "--bundle-identifier",
        default=os.environ.get(
            "APPLE_BUNDLE_IDENTIFIER", DEFAULT_BUNDLE_IDENTIFIER),
    )
    args = parser.parse_args()

    runner = SecureCommandRunner()
    try:
        if args.command == "install":
            if not args.environment_file:
                raise AppleSigningError(
                    "GITHUB_ENV or --environment-file is required")
            installer = AppleSigningInstaller(
                runner,
                Path(os.environ.get("RUNNER_TEMP", tempfile.gettempdir())),
                Path(args.environment_file),
            )
            installer.install(SigningInputs.from_environment(os.environ))
        elif args.command == "cleanup":
            keychain = os.environ.get("APPLE_SIGNING_KEYCHAIN_PATH")
            profile = os.environ.get("APPLE_PROVISIONING_PROFILE_PATH")
            AppleSigningInstaller.cleanup(
                runner,
                Path(keychain) if keychain else None,
                Path(profile) if profile else None,
            )
        elif args.command == "verify-ipa":
            SignedIpaVerifier(runner).verify(
                Path(args.ipa), args.bundle_identifier)
        else:
            raise AppleSigningError(f"unsupported command: {args.command}")
    except AppleSigningError as error:
        print(f"[apple-signing] ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
