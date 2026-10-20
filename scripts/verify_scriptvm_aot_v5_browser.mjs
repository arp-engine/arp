import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { extname, join, normalize, resolve, sep } from "node:path";

const [rootArgument, browserArgument] = process.argv.slice(2);
if (!rootArgument || !browserArgument) {
    throw new Error("usage: verify_scriptvm_aot_v5_browser.mjs <artifact-dir> <browser>");
}
const root = resolve(rootArgument);
const contentTypes = new Map([
    [".html", "text/html; charset=utf-8"],
    [".js", "text/javascript; charset=utf-8"],
    [".wasm", "application/wasm"],
]);
const server = createServer(async (request, response) => {
    try {
        const requestPath = new URL(request.url, "http://127.0.0.1").pathname;
        const relative = requestPath === "/" ? "scriptvm_aot_v5_smoke.html"
            : decodeURIComponent(requestPath.slice(1));
        const path = normalize(join(root, relative));
        if (path !== root && !path.startsWith(root + sep)) {
            response.writeHead(403).end();
            return;
        }
        const bytes = await readFile(path);
        response.writeHead(200, {
            "Content-Type": contentTypes.get(extname(path)) ?? "application/octet-stream",
            "Cross-Origin-Embedder-Policy": "require-corp",
            "Cross-Origin-Opener-Policy": "same-origin",
            "Cache-Control": "no-store",
        });
        response.end(bytes);
    } catch {
        response.writeHead(404).end();
    }
});
await new Promise((resolveListen) => server.listen(0, "127.0.0.1", resolveListen));
const port = server.address().port;
const profile = await mkdtemp(join(tmpdir(), "arp-aot-v5-browser-"));
try {
    for (let run = 0; run < 2; ++run) {
        const output = await new Promise((resolveProcess, rejectProcess) => {
            const browser = spawn(browserArgument, [
                "--headless=new",
                "--disable-gpu",
                "--no-sandbox",
                "--no-first-run",
                `--user-data-dir=${profile}`,
                "--virtual-time-budget=10000",
                "--dump-dom",
                `http://127.0.0.1:${port}/scriptvm_aot_v5_smoke.html?run=${run}`,
            ], { stdio: ["ignore", "pipe", "pipe"] });
            let stdout = "";
            let stderr = "";
            browser.stdout.on("data", (chunk) => { stdout += chunk; });
            browser.stderr.on("data", (chunk) => { stderr += chunk; });
            browser.on("error", rejectProcess);
            browser.on("close", (code) => {
                if (code !== 0) rejectProcess(new Error(`browser exited ${code}: ${stderr}`));
                else resolveProcess(stdout);
            });
        });
        if (!output.includes('data-status="passed"')) {
            throw new Error(`browser DirectNative smoke run ${run} did not pass:\n${output}`);
        }
    }
    console.log("browser DirectNative smoke and reload passed");
} finally {
    server.close();
    await rm(profile, {
        recursive: true,
        force: true,
        maxRetries: 5,
        retryDelay: 100,
    });
}
