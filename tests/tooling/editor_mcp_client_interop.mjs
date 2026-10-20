import assert from "node:assert/strict";
import { pathToFileURL } from "node:url";

const [modulePath, endpoint, token, documentIdText] = process.argv.slice(2);
assert.ok(modulePath, "MCP client SDK module path is required");
assert.ok(endpoint, "editor MCP endpoint is required");
assert.ok(token, "editor MCP bearer token is required");
const documentId = Number(documentIdText);
assert.ok(Number.isSafeInteger(documentId) && documentId > 0, "valid document ID is required");

const { Client, StreamableHTTPClientTransport } = await import(pathToFileURL(modulePath));
const transport = new StreamableHTTPClientTransport(new URL(endpoint), {
  requestInit: { headers: { Authorization: `Bearer ${token}` } },
});
const client = new Client({ name: "arp-editor-mcp-interop-test", version: "1.0.0" }, { capabilities: {} });

try {
  await client.connect(transport);
  assert.equal(client.getNegotiatedProtocolVersion(), "2025-11-25", "MCP protocol version was not negotiated");
  const { tools } = await client.listTools();
  const toolNames = new Set(tools.map((tool) => tool.name));
  for (const name of ["editor.state.get", "editor.activate", "editor.search", "script.replace", "script.save", "script.undo", "script.redo"]) {
    assert.ok(toolNames.has(name), `MCP tools/list omitted ${name}`);
  }

  const unsupported = await client.callTool({ name: "unknown.tool", arguments: {} });
  assert.equal(unsupported.isError, true, "unsupported MCP tool was not reported as a tool error");

  const state = await client.callTool({ name: "editor.state.get", arguments: {} });
  assert.equal(state.isError, false, "editor.state.get failed");
  assert.equal(state.structuredContent?.status, "completed", "editor.state.get returned no command result");

  const idempotencyKey = "mcp-sdk-loopback-retry";
  const arguments_ = { documentId, text: "edited through MCP client SDK\n", idempotencyKey };
  const first = await client.callTool({ name: "script.replace", arguments: arguments_ });
  assert.equal(first.isError, false, "script.replace failed through MCP");
  assert.equal(first.structuredContent?.status, "completed", "script.replace did not complete");

  const replay = await client.callTool({ name: "script.replace", arguments: arguments_ });
  assert.equal(replay.isError, false, "idempotent MCP replay failed");
  assert.equal(replay.structuredContent?.stateVersion, first.structuredContent?.stateVersion,
    "idempotent MCP replay executed a second state change");
} finally {
  await client.close();
}
