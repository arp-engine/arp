# Packaging And Runtime Distribution

Status: Contract Complete.

`src/core/package_manifest.h` defines the default distribution manifest:

- backend-neutral public headers
- backend/internal headers kept out of public API
- runtime asset directories
- shader reflection package flag
- pipeline cache package flag
- versioned package identity

Future CMake install/export rules should use this manifest shape as the source of
truth for public API boundaries.
