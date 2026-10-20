import fs from "node:fs";
import path from "node:path";

const buildDir = path.resolve(process.argv[2] ?? ".");
const testNames = [
  "arp_scriptvm_typed_ir_tests",
  "arp_scriptvm_semantic_runtime_tests",
  "arp_scriptvm_aot_v5_scene_behavior_smoke",
];

function fail(message) {
  console.error(`Wasm package contract failed: ${message}`);
  process.exitCode = 1;
}

function fileInfo(file) {
  const stat = fs.statSync(file);
  if (!stat.isFile() || stat.size === 0)
    throw new Error(`artifact is empty or not a file: ${file}`);
  return stat.size;
}

function findNativeSharedLibraries(root) {
  const result = [];
  const visit = (directory) => {
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
      const fullPath = path.join(directory, entry.name);
      if (entry.isDirectory()) {
        visit(fullPath);
      } else if (entry.isFile() && /\.(dll|so|dylib)$/i.test(entry.name)) {
        result.push(path.relative(root, fullPath));
      }
    }
  };
  visit(root);
  return result;
}

try {
  if (!fs.existsSync(buildDir) || !fs.statSync(buildDir).isDirectory())
    throw new Error(`build directory does not exist: ${buildDir}`);

  const artifacts = [];
  for (const name of testNames) {
    const jsPath = path.join(buildDir, `${name}.js`);
    const wasmPath = path.join(buildDir, `${name}.wasm`);
    const jsSize = fileInfo(jsPath);
    const wasmSize = fileInfo(wasmPath);
    const js = fs.readFileSync(jsPath, "utf8");
    const wasm = fs.readFileSync(wasmPath);
    if (!js.includes("emscripten") && !js.includes("WebAssembly"))
      throw new Error(`${path.basename(jsPath)} is not an Emscripten loader`);
    if (wasm.length < 8 || wasm.readUInt32BE(0) !== 0x0061736d
        || wasm[4] !== 0x01 || wasm[5] !== 0x00
        || wasm[6] !== 0x00 || wasm[7] !== 0x00)
      throw new Error(`${path.basename(wasmPath)} has an invalid Wasm header`);
    if (!js.includes(path.basename(wasmPath)))
      throw new Error(`${path.basename(jsPath)} does not reference its Wasm payload`);
    artifacts.push({ name, js: jsSize, wasm: wasmSize });
  }

  const nativeFiles = findNativeSharedLibraries(buildDir);
  if (nativeFiles.length !== 0)
    throw new Error(`native shared libraries are present: ${nativeFiles.join(", ")}`);

  const report = {
    type: "arp.scriptvm.wasm-package-contract",
    contractVersion: 1,
    buildDirectory: buildDir,
    emscripten: true,
    artifacts,
    nativeSharedLibraries: nativeFiles,
  };
  fs.writeFileSync(path.join(buildDir, "scriptvm-wasm-package-contract.json"),
    `${JSON.stringify(report, null, 2)}\n`);
  console.log(`Wasm package contract passed: ${artifacts.length} JS/Wasm pairs`);
} catch (error) {
  fail(error instanceof Error ? error.message : String(error));
}
