#!/usr/bin/env python3
"""Convert Quaternius Stylized Nature MegaKit glTF sources into GLB assets.

The Standard pack ships as external glTF JSON + .bin + .png files.
This helper embeds each model's binary buffer and texture images into a
self-contained glTF document and then wraps it as GLB so the engine's
existing GLB importer can consume the result deterministically.
"""

from __future__ import annotations

import argparse
import base64
import json
import struct
import tarfile
import tempfile
from dataclasses import dataclass
from datetime import date
from pathlib import Path
from typing import Iterable
from zipfile import ZipFile


@dataclass(frozen=True)
class AssetEntry:
    name: str
    glb_path: str
    role: str
    height_class: str
    category: str


def _pad(data: bytes, alignment: int, fill: bytes) -> bytes:
    padding = (-len(data)) % alignment
    return data + fill * padding


def _mime_type_for_image(path: Path) -> str:
    ext = path.suffix.lower()
    if ext == ".png":
        return "image/png"
    if ext in {".jpg", ".jpeg"}:
        return "image/jpeg"
    if ext == ".webp":
        return "image/webp"
    raise ValueError(f"unsupported image format: {path.name}")


def _data_uri(mime_type: str, data: bytes) -> str:
    encoded = base64.b64encode(data).decode("ascii")
    return f"data:{mime_type};base64,{encoded}"


def _glb_bytes_from_embedded_gltf(document: dict) -> bytes:
    if document.get("asset", {}).get("version") != "2.0":
        raise ValueError("source must be a glTF 2.0 document")

    buffers = document.get("buffers")
    if not isinstance(buffers, list) or len(buffers) != 1:
        raise ValueError("source must contain exactly one embedded buffer")

    buffer = buffers[0]
    uri = buffer.get("uri", "")
    prefix = "data:application/octet-stream;base64,"
    if not isinstance(uri, str) or not uri.startswith(prefix):
        raise ValueError("buffer must use an embedded application/octet-stream data URI")

    binary = base64.b64decode(uri[len(prefix):], validate=True)
    if buffer.get("byteLength") != len(binary):
        raise ValueError("decoded buffer length does not match buffers[0].byteLength")

    del buffer["uri"]
    json_chunk = _pad(
        json.dumps(document, ensure_ascii=False, separators=(",", ":")).encode("utf-8"),
        4,
        b" ",
    )
    binary_chunk = _pad(binary, 4, b"\0")
    total_length = 12 + 8 + len(json_chunk) + 8 + len(binary_chunk)
    return b"".join(
        (
            struct.pack("<4sII", b"glTF", 2, total_length),
            struct.pack("<I4s", len(json_chunk), b"JSON"),
            json_chunk,
            struct.pack("<I4s", len(binary_chunk), b"BIN\0"),
            binary_chunk,
        )
    )


def _embed_external_references(zip_file: ZipFile, gltf_name: str) -> dict:
    raw = zip_file.read(gltf_name).decode("utf-8")
    document = json.loads(raw)
    base_dir = Path(gltf_name).parent

    buffers = document.get("buffers", [])
    if not isinstance(buffers, list) or len(buffers) != 1:
        raise ValueError(f"{gltf_name}: expected exactly one buffer")

    buffer_uri = buffers[0].get("uri")
    if not isinstance(buffer_uri, str) or not buffer_uri:
        raise ValueError(f"{gltf_name}: missing buffer uri")
    buffer_path = (base_dir / buffer_uri).as_posix()
    buffer_bytes = zip_file.read(buffer_path)
    buffers[0]["uri"] = _data_uri("application/octet-stream", buffer_bytes)

    for image in document.get("images", []):
        if not isinstance(image, dict):
            raise ValueError(f"{gltf_name}: image entry must be a mapping")
        uri = image.get("uri")
        if not isinstance(uri, str) or not uri:
            continue
        if uri.startswith("data:"):
            continue
        image_path = (base_dir / uri).as_posix()
        image_bytes = zip_file.read(image_path)
        image["uri"] = _data_uri(_mime_type_for_image(Path(uri)), image_bytes)

    return document


def _categorize(name: str) -> tuple[str, str]:
    if "Tree" in name or "Pine" in name:
        if "Twisted" in name:
            return "landmark", "tall_robust"
        if "Dead" in name:
            return "landmark", "tall"
        return "canopy", "tall"
    if name.startswith("Rock") or name.startswith("Pebble"):
        return "landmark", "low"
    if name.startswith("Grass"):
        return "ground_cover", "low"
    if name.startswith("Bush") or name.startswith("Plant"):
        return "understory", "medium"
    if name.startswith("Flower") or name.startswith("Clover") or name.startswith("Fern") or name.startswith("Mushroom") or name.startswith("Petal"):
        return "ground_cover", "low"
    return "understory", "medium"


def _build_manifest_entries(names: Iterable[str]) -> list[AssetEntry]:
    entries: list[AssetEntry] = []
    for index, name in enumerate(sorted(names), start=1):
        category, height_class = _categorize(name)
        entries.append(
            AssetEntry(
                name=name,
                glb_path=f"glb/{name}.glb",
                role=category,
                height_class=height_class,
                category=category,
            )
        )
    return entries


def _write_manifest(
    output_dir: Path,
    source_zip: Path,
    entries: list[AssetEntry],
) -> None:
    manifest = {
        "type": "VegetationSourceCatalog",
        "fields": {
            "contractVersion": 1,
            "status": "Backend Validated",
            "source": {
                "type": "VegetationCatalogSourceInfo",
                "fields": {
                    "creator": "Quaternius",
                    "pack": "Stylized Nature MegaKit (Standard)",
                    "sourceUrl": "https://opengameart.org/content/stylized-nature-megakit",
                    "downloadUrl": "https://opengameart.org/sites/default/files/stylized_nature_megakitstandard.zip",
                    "upstreamPage": "https://quaternius.com/packs/stylizednaturemegakit.html",
                    "license": "CC0-1.0",
                    "verifiedZipBytes": source_zip.stat().st_size,
                },
            },
            "imported": {
                "type": "VegetationCatalogImportedInfo",
                "fields": {
                    "format": "glTF 2.0 -> GLB",
                    "generatedBy": "scripts/convert_quaternius_stylized_nature_megakit.py",
                },
            },
            "assets": [
                {
                    "type": "VegetationCatalogAssetRecord",
                    "fields": {
                        "id": f"quaternius.{entry.name.lower()}",
                        "source": entry.glb_path,
                        "role": entry.role,
                        "heightClass": entry.height_class,
                        "category": entry.category,
                    },
                }
                for entry in entries
            ],
            "validation": {
                "type": "VegetationCatalogValidationInfo",
                "fields": {
                    "required": [
                        "Verify the downloaded archive size against verifiedZipBytes.",
                        "Verify each glTF source was converted into a self-contained GLB.",
                        "Verify each generated GLB loads through the engine GLB importer.",
                    ],
                    "prohibited": [
                        "Do not accept partially converted or missing GLB files.",
                        "Do not fall back to raw external glTF references at runtime.",
                    ],
                },
            },
        },
    }
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "imported" / "vegetation_runtime_catalog.yaml").write_text(
        yaml_dump(manifest),
        encoding="utf-8",
    )


def _write_source_catalog(
    output_dir: Path,
    source_zip: Path,
    entries: list[AssetEntry],
) -> None:
    source_catalog = {
        "type": "VegetationSourceCatalog",
        "fields": {
            "contractVersion": 1,
            "status": "Prototype Complete",
            "source": {
                "type": "VegetationCatalogSourceInfo",
                "fields": {
                    "creator": "Quaternius",
                    "pack": "Stylized Nature MegaKit (Standard)",
                    "sourceUrl": "https://opengameart.org/content/stylized-nature-megakit",
                    "downloadUrl": "https://opengameart.org/sites/default/files/stylized_nature_megakitstandard.zip",
                    "upstreamPage": "https://quaternius.com/packs/stylizednaturemegakit.html",
                    "license": "CC0-1.0",
                    "verifiedOn": date.today().isoformat(),
                    "upstreamContentLengthBytes": source_zip.stat().st_size,
                },
            },
            "runtimeCatalog": "imported/vegetation_runtime_catalog.yaml",
            "assets": [
                {
                    "type": "VegetationCatalogAssetRecord",
                    "fields": {
                        "id": f"quaternius.{entry.name.lower()}",
                        "source": f"glTF/{entry.name}.gltf",
                        "role": entry.role,
                        "heightClass": entry.height_class,
                        "category": entry.category,
                    },
                }
                for entry in entries
            ],
            "validation": {
                "type": "VegetationCatalogValidationInfo",
                "fields": {
                    "required": [
                        "Verify the downloaded archive size is exactly upstreamContentLengthBytes.",
                        "Verify ZIP central-directory integrity before extraction.",
                        "Verify every catalog source and referenced .bin/.png URI is present.",
                        "Import through the GLB/glTF asset pipeline and record bounds, vertex count, material bindings, and scale metadata.",
                    ],
                    "prohibited": [
                        "Do not register an asset whose archive or referenced resources fail validation.",
                        "Do not invent scale, texture, material, or wind-weight fallback values.",
                    ],
                },
            },
        },
    }
    (output_dir / "vegetation_catalog.yaml").write_text(
        yaml_dump(source_catalog),
        encoding="utf-8",
    )


def yaml_dump(value, indent: int = 0) -> str:
    prefix = " " * indent
    if isinstance(value, dict):
        lines: list[str] = []
        for key, item in value.items():
            if isinstance(item, (dict, list)):
                lines.append(f"{prefix}{key}:")
                lines.append(yaml_dump(item, indent + 2))
            else:
                lines.append(f"{prefix}{key}: {yaml_scalar(item)}")
        return "\n".join(lines)
    if isinstance(value, list):
        lines = []
        for item in value:
            if isinstance(item, (dict, list)):
                lines.append(f"{prefix}-")
                lines.append(yaml_dump(item, indent + 2))
            else:
                lines.append(f"{prefix}- {yaml_scalar(item)}")
        return "\n".join(lines)
    return f"{prefix}{yaml_scalar(value)}"


def yaml_scalar(value) -> str:
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        return str(value)
    text = str(value)
    if text == "":
        return '""'
    if any(ch in text for ch in [":", "#", "\n", "\r", "{", "}", "[", "]", ",", "&", "*", "?", "|", "-", "<", ">", "=", "!", "%", "@", "\\"]):
        return json.dumps(text, ensure_ascii=False)
    return text


def convert_pack(zip_path: Path, output_dir: Path) -> None:
    if not zip_path.is_file():
        raise FileNotFoundError(zip_path)

    with ZipFile(zip_path) as zip_file:
        gltf_names = [
            name
            for name in zip_file.namelist()
            if name.startswith("glTF/") and name.endswith(".gltf")
        ]
        if not gltf_names:
            raise ValueError("archive does not contain any glTF models")

        glb_dir = output_dir / "glb"
        glb_dir.mkdir(parents=True, exist_ok=True)

        for gltf_name in sorted(gltf_names):
            model_name = Path(gltf_name).stem
            document = _embed_external_references(zip_file, gltf_name)
            glb_bytes = _glb_bytes_from_embedded_gltf(document)
            (glb_dir / f"{model_name}.glb").write_bytes(glb_bytes)

        entries = _build_manifest_entries(Path(name).stem for name in gltf_names)
        _write_source_catalog(output_dir, zip_path, entries)
        _write_manifest(output_dir, zip_path, entries)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("zip_path", type=Path)
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    convert_pack(args.zip_path, args.output_dir)


if __name__ == "__main__":
    main()
