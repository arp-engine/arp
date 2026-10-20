# GlbImageSource/1

Status: Contract Complete; native consumer validation pending.

The shared GLB importer resolves each `images[]` entry from exactly one
declared source: a binary `bufferView` within the GLB BIN chunk, or an RFC 2397
`data:<mime>;base64,<payload>` URI. For data URIs, the MIME type is taken from
the URI and must match an independently declared `mimeType`, if present.
The existing `core::Base64` decoder handles the payload. Decoded bytes are
passed unchanged to the existing image decoder; no generated texture, inferred
alpha, guessed path or default image is allowed. Relative/external URI sources
are not self-contained in a GLB and currently fail explicitly.

Ambiguous or missing sources, invalid ranges, malformed data URIs and MIME
conflicts cause deterministic importer failure. The stable diagnostics are
`ASSET-GLB-IMAGE-SOURCE-INVALID`, `ASSET-GLB-IMAGE-DATA-URI-INVALID`,
`ASSET-GLB-IMAGE-MIME-CONFLICT` and `ASSET-GLB-IMAGE-URI-UNSUPPORTED`.
Static, named-variant and animated GLB import all use the same parser and
propagate its failure. Image decode itself remains a separate consumer gate.

This is a CPU asset-format conversion before RHI submission. Null, Vulkan,
Metal and WebGPU receive identical imported image bytes and material metadata;
the four backend conclusion is Equivalent for this importer change. Native
Metal validation must verify the masked-foliage texture and full scene.
