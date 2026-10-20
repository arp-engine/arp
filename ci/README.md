# Apple CI

Status: Contract Complete; signed CI host Prototype Complete. A successful
GitHub run produces an Ad Hoc signed IPA, but physical-device execution remains
the Platform Validated gate.

The Apple CI entry point generates both build systems from the checked-in
CMake contract:

- `python3 ci/apple_build.py macos` builds and runs the macOS Metal contracts.
- `python3 ci/apple_build.py ios` generates
  `build/ci-apple/ios-metal/AnimeRenderPipeline.xcodeproj`, builds the unsigned
  device app, and validates generated MSL with the iPhoneOS Metal compiler.
- `python3 ci/apple_build.py ios-sideload` performs the same unsigned build,
  validates the app bundle contract, and packages
  `build/ci-apple/ios-metal/sideload/arp_ios_smoke_unsigned.ipa` for local
  re-signing by Sideloadly or AltStore.
- `python3 ci/apple_build.py ios-signed --configuration Release` archives the
  same generated project and exports
  `build/ci-apple/ios-metal/export/arp_ios_smoke_app.ipa` with explicit manual
  Ad Hoc signing.
- `python3 ci/apple_build.py all` runs both pipelines.

Run `bash ci/bootstrap_apple.sh` once on a clean macOS host. Set
`SLANGC_EXECUTABLE` when `slangc` is not on `PATH`.

## Free Apple ID local signing

The regular `.github/workflows/apple-metal.yml` workflow needs no Apple
certificate, provisioning profile, paid Developer Program membership, or Apple
ID secret. It publishes the `arp-ios-unsigned-<commit>` artifact containing a
standard unsigned `Payload/arp_ios_smoke_app.app` IPA. The packager rejects
pre-existing signatures and embedded profiles, verifies the explicit Bundle
ID, executable, YAML resource manifest, generated MSL, and reflection, and
preserves executable permissions in a deterministic ZIP container.

### Sideloadly on Windows

1. Download and extract the `arp-ios-unsigned-<commit>` GitHub artifact.
2. Install current Apple device drivers and Sideloadly on the Windows PC.
3. Connect the iPhone over USB, unlock it, and trust the computer.
4. Drag `arp_ios_smoke_unsigned.ipa` into Sideloadly, select the iPhone, and
   sign with a free Apple ID locally.
5. On iOS 16 or later, enable **Settings > Privacy & Security > Developer
   Mode**, restart when requested, and trust the developer identity if iOS
   presents that prompt.

### AltStore on Windows

1. Install AltServer and its required Apple device components on Windows.
2. Use AltServer to install AltStore on the connected iPhone with the free
   Apple ID.
3. Transfer `arp_ios_smoke_unsigned.ipa` to the iPhone Files app, then use
   **AltStore > My Apps > +** to select and sign it.
4. Keep AltServer reachable periodically so AltStore can refresh the app.

Free-account signatures normally expire after about seven days and are subject
to Apple's active-app, App ID, entitlement, and device limits. Refresh or
reinstall before expiry. Never place the free Apple ID password, app-specific
password, or two-factor code in GitHub Actions: CI only builds the unsigned
contract artifact, while account authentication and signing stay on the local
Windows/iPhone path. This is Prototype Complete until an actual GitHub macOS
run and iPhone launch provide Backend Validated and Platform Validated evidence.

## GitHub Ad Hoc signing setup

The dedicated `.github/workflows/ios-signed.yml` workflow is manual so signing
secrets are never requested by pull requests. Before running it:

1. Register the target iPhone UDID in the Apple Developer portal.
2. Create an explicit App ID matching `com.animerenderpipeline.metal-smoke`.
3. Create an **Ad Hoc** provisioning profile for that App ID, include the
   target iPhone, and select an Apple Distribution certificate.
4. Export that certificate and private key from Keychain Access as a
   password-protected `.p12`.
5. Add these repository Actions secrets:
   `APPLE_CERTIFICATE_P12_BASE64`, `APPLE_CERTIFICATE_PASSWORD`, and
   `APPLE_PROVISIONING_PROFILE_BASE64`.
6. Optionally set the repository Actions variable `APPLE_BUNDLE_IDENTIFIER`
   when using a different explicit App ID.

On Windows, generate the two base64 values without changing the files:

```powershell
[Convert]::ToBase64String([IO.File]::ReadAllBytes("certificate.p12"))
[Convert]::ToBase64String([IO.File]::ReadAllBytes("profile.mobileprovision"))
```

Run **Actions > Signed iPhone IPA > Run workflow**. The job imports the
certificate into an ephemeral keychain, rejects expired/development/wildcard
or device-less profiles, builds the archive, exports one IPA, verifies its
signature and embedded profile, and removes the signing material. Download the
`arp-ios-<commit>` artifact; its IPA can only be installed on iPhones whose
UDIDs are included in that profile.

GitHub artifacts are authenticated ZIP downloads, not an iOS OTA install
service. From Windows, install the extracted IPA with a trusted device-install
tool such as Sideloadly, or add a separate HTTPS OTA/TestFlight distribution
step. TestFlight, physical-device lifecycle tests, Xcode GPU capture, and
thermal/memory-pressure evidence remain separate Platform Validated gates.
