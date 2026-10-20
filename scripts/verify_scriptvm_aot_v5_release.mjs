import fs from "node:fs";
import path from "node:path";

const [jsArgument, wasmArgument, manifestArgument, reportArgument,
    generatedDirectoryArgument, linkMapArgument, outputArgument] = process.argv.slice(2);

if (!outputArgument) {
    throw new Error(
        "usage: verify_scriptvm_aot_v5_release.mjs <loader.js> <module.wasm> "
        + "<manifest.yaml> <package-report.txt> <generated-dir> <link-map> <output.yaml>");
}

const inputs = {
    js: path.resolve(jsArgument),
    wasm: path.resolve(wasmArgument),
    manifest: path.resolve(manifestArgument),
    report: path.resolve(reportArgument),
    generatedDirectory: path.resolve(generatedDirectoryArgument),
    linkMap: path.resolve(linkMapArgument),
    output: path.resolve(outputArgument),
};

function requireFile(file) {
    const stat = fs.statSync(file);
    if (!stat.isFile() || stat.size === 0)
        throw new Error(`required artifact is empty or not a file: ${file}`);
    return stat.size;
}

function readVarUint(bytes, cursor) {
    let value = 0;
    let shift = 0;
    for (;;) {
        if (cursor.offset >= bytes.length || shift > 35)
            throw new Error("invalid Wasm unsigned LEB128 value");
        const byte = bytes[cursor.offset++];
        value += (byte & 0x7f) * (2 ** shift);
        if ((byte & 0x80) === 0)
            return value;
        shift += 7;
    }
}

function parseFixedMemory(bytes) {
    if (bytes.length < 8 || bytes.subarray(0, 4).toString("hex") !== "0061736d"
        || bytes.subarray(4, 8).toString("hex") !== "01000000") {
        throw new Error("artifact does not have a Wasm v1 header");
    }

    const cursor = { offset: 8 };
    while (cursor.offset < bytes.length) {
        const sectionId = bytes[cursor.offset++];
        const sectionSize = readVarUint(bytes, cursor);
        const sectionEnd = cursor.offset + sectionSize;
        if (sectionEnd > bytes.length)
            throw new Error("Wasm section exceeds artifact size");
        if (sectionId !== 5) {
            cursor.offset = sectionEnd;
            continue;
        }

        const count = readVarUint(bytes, cursor);
        if (count !== 1)
            throw new Error(`strict Web AOT artifact must define one memory, found ${count}`);
        const flags = readVarUint(bytes, cursor);
        const minimumPages = readVarUint(bytes, cursor);
        if ((flags & 0x01) === 0)
            throw new Error("strict Web AOT memory has no maximum");
        const maximumPages = readVarUint(bytes, cursor);
        if (minimumPages !== maximumPages) {
            throw new Error(
                `strict Web AOT memory can grow: min=${minimumPages}, max=${maximumPages}`);
        }
        return { minimumPages, maximumPages, bytes: minimumPages * 65536 };
    }
    throw new Error("strict Web AOT artifact has no defined memory section");
}

function walkFiles(root) {
    const files = [];
    for (const entry of fs.readdirSync(root, { withFileTypes: true })) {
        const fullPath = path.join(root, entry.name);
        if (entry.isDirectory())
            files.push(...walkFiles(fullPath));
        else if (entry.isFile())
            files.push(fullPath);
    }
    return files;
}

function assertNoPattern(label, text, patterns) {
    for (const pattern of patterns) {
        if (pattern.expression.test(text))
            throw new Error(`${label} contains forbidden ${pattern.name}`);
    }
}

for (const file of [inputs.js, inputs.wasm, inputs.manifest, inputs.report, inputs.linkMap])
    requireFile(file);
if (!fs.statSync(inputs.generatedDirectory).isDirectory())
    throw new Error(`generated package directory is missing: ${inputs.generatedDirectory}`);

const js = fs.readFileSync(inputs.js, "utf8");
const wasm = fs.readFileSync(inputs.wasm);
const manifest = fs.readFileSync(inputs.manifest, "utf8");
const report = fs.readFileSync(inputs.report, "utf8");
const linkMap = fs.readFileSync(inputs.linkMap, "utf8");

const executionKinds = [...manifest.matchAll(/^\s*execution-kind:\s*(\S+)\s*$/gm)]
    .map((match) => match[1]);
if (!/^type:\s*arp\.script\.aot-package\/v1\s*$/m.test(manifest)
    || !/^contract-version:\s*5\s*$/m.test(manifest)) {
    throw new Error("generated manifest is not an arp.script.aot-package/v1 v5 manifest");
}
if (!/^platforms:\s*\[[^\]]*web-wasm[^\]]*\]\s*$/m.test(manifest))
    throw new Error("generated manifest does not declare web-wasm");
if (executionKinds.length === 0 || executionKinds.some((kind) => kind !== "DirectNative"))
    throw new Error("generated manifest contains a non-DirectNative function entry");
if (!/^Rejected functions:\s*0\s*$/m.test(report)
    || !/^Generic executor entries:\s*0\s*$/m.test(report)) {
    throw new Error("package report contains rejected or generic-executor entries");
}

const forbiddenExtensions = new Set([".arp", ".bc", ".bytecode"]);
const forbiddenFiles = walkFiles(inputs.generatedDirectory)
    .filter((file) => forbiddenExtensions.has(path.extname(file).toLowerCase()));
if (forbiddenFiles.length !== 0) {
    throw new Error(
        `generated package contains source or bytecode: ${forbiddenFiles.join(", ")}`);
}

const forbiddenPatterns = [
    { name: "ArpScript source path", expression: /\.arp(?:\0|[\s"'])/i },
    { name: "Interpreter implementation", expression: /Interpreter/ },
    { name: "bytecode implementation", expression: /bytecode/i },
    { name: "generic IR executor", expression: /IrExecutorPrototypeLegacy|executeAotFunction|executeAotNativeValueFrameMachine/ },
    { name: "Legacy AOT implementation", expression: /LegacyAotFunctionExecutionKind/ },
    { name: "frontend symbol", expression: /arp::scriptvm::(?:Lexer|Parser|Compiler|Vm)::/ },
    { name: "mangled frontend symbol", expression: /_ZN3arp8scriptvm(?:5Lexer|6Parser|8Compiler|2Vm)/ },
];
assertNoPattern("JavaScript loader", js, forbiddenPatterns);
assertNoPattern("Wasm payload", wasm.toString("latin1"), forbiddenPatterns);
assertNoPattern("Wasm link map", linkMap, forbiddenPatterns);
if (/\beval\s*\(|new\s+Function\s*\(/.test(js))
    throw new Error("JavaScript loader contains runtime code evaluation");

const memory = parseFixedMemory(wasm);
const output = [
    "type: arp.script.aot-v5-release-audit",
    "contract-version: 1",
    "platform: web-wasm",
    "status: passed",
    `direct-native-functions: ${executionKinds.length}`,
    "rejected-functions: 0",
    "generic-executor-entries: 0",
    "source-files: 0",
    "bytecode-files: 0",
    "runtime-eval: false",
    "memory-growth: false",
    `initial-memory-pages: ${memory.minimumPages}`,
    `maximum-memory-pages: ${memory.maximumPages}`,
    `memory-bytes: ${memory.bytes}`,
    `javascript-bytes: ${requireFile(inputs.js)}`,
    `wasm-bytes: ${requireFile(inputs.wasm)}`,
    "",
].join("\n");
fs.mkdirSync(path.dirname(inputs.output), { recursive: true });
fs.writeFileSync(inputs.output, output, "utf8");
console.log(
    `Web AOT v5 release audit passed: ${executionKinds.length} DirectNative functions, `
    + `${memory.bytes} fixed memory bytes`);
