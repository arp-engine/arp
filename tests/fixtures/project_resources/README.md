This is the immutable native exporter fixture for the mounted project resource
tests. Its declared package contains a perspective camera, directional light and
sky; the project also declares a bounded physics world and a source-only script.
The script retains its UTF-8 BOM and CRLF bytes for input identity tests.

The native test regenerates the same project using `ProjectSceneAuthoring`.
Wasm preloads these bytes because MEMFS does not implement the exclusive directory
publication required by the exporter. Both paths still use the real package and
closure readers and compare mounted candidates with their native/local baseline.

To create a new candidate fixture in an empty directory:

```powershell
build/win-vulkan-debug/editor/arp_editor_project_resource_source_tests.exe --write-fixtures <empty-directory>
```

Review the exported YAML and closure together before replacing golden files.
These fixtures do not prove browser filesystem writes or GPU rendering.
