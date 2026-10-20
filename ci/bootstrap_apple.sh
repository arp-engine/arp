#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "bootstrap_apple.sh requires macOS" >&2
  exit 1
fi

if ! command -v brew >/dev/null 2>&1; then
  echo "Homebrew is required on the Apple CI host" >&2
  exit 1
fi

packages=()
command -v ninja >/dev/null 2>&1 || packages+=(ninja)
command -v slangc >/dev/null 2>&1 || packages+=(shader-slang)

if (( ${#packages[@]} > 0 )); then
  brew install "${packages[@]}"
fi

xcode-select -p
xcodebuild -version
cmake --version
ninja --version
slangc -version
