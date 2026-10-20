#!/usr/bin/env python3
"""Deterministically wrap a self-contained glTF 2.0 JSON asset as GLB."""

from __future__ import annotations

import argparse
import base64
import json
import struct
from pathlib import Path


def pad(data: bytes, alignment: int, fill: bytes) -> bytes:
    padding = (-len(data)) % alignment
    return data + fill * padding


def convert(source: Path, destination: Path) -> None:
    document = json.loads(source.read_text(encoding="utf-8"))
    if document.get("asset", {}).get("version") != "2.0":
        raise ValueError("source must be a glTF 2.0 document")

    buffers = document.get("buffers")
    if not isinstance(buffers, list) or len(buffers) != 1:
        raise ValueError("source must contain exactly one embedded buffer")

    buffer = buffers[0]
    uri = buffer.get("uri", "")
    prefix = "data:application/octet-stream;base64,"
    if not uri.startswith(prefix):
        raise ValueError("buffer must use an embedded application/octet-stream data URI")

    binary = base64.b64decode(uri[len(prefix) :], validate=True)
    if buffer.get("byteLength") != len(binary):
        raise ValueError("decoded buffer length does not match buffers[0].byteLength")

    del buffer["uri"]
    json_chunk = pad(
        json.dumps(document, ensure_ascii=False, separators=(",", ":")).encode("utf-8"),
        4,
        b" ",
    )
    binary_chunk = pad(binary, 4, b"\0")
    total_length = 12 + 8 + len(json_chunk) + 8 + len(binary_chunk)
    glb = b"".join(
        (
            struct.pack("<4sII", b"glTF", 2, total_length),
            struct.pack("<I4s", len(json_chunk), b"JSON"),
            json_chunk,
            struct.pack("<I4s", len(binary_chunk), b"BIN\0"),
            binary_chunk,
        )
    )

    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(glb)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    convert(args.source, args.destination)


if __name__ == "__main__":
    main()
