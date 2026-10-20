import base64
import hashlib
import json
import struct
import urllib.parse
import urllib.request
from pathlib import Path


SOURCE_COMMIT = "db3df04d1e4714298a09510b26fb6de6645138a2"
ASSET_ROOT = Path(__file__).resolve().parents[1] / "assets" / "characters"
SOURCE_ROOT = (
    "Ultimate Modular Women - April 2022/Individual Characters/glTF/"
)
MODELS = {
    "Casual": "B0FE6E92219CD71808844A20A1A8B960FD1CF640A6546DC6362B5ADD6604E87C",
    "Formal": "FDFCF454C4DE037D31973EB28D8591BD7763035D49F46B380E63B1C832DC7DDF",
}
LICENSE_SHA256 = "E8DBF915A2B82229913E301A0787696611241BDEFEC4832BC084F54161DB1EFE"


def download(path):
    url = (
        "https://raw.githubusercontent.com/agentkaerf/FreeModels/"
        + SOURCE_COMMIT
        + "/"
        + urllib.parse.quote(path)
    )
    with urllib.request.urlopen(url, timeout=60) as response:
        return response.read()


def verify_hash(data, expected, name):
    actual = hashlib.sha256(data).hexdigest().upper()
    if actual != expected:
        raise ValueError(f"ARP-QUEEN-ASSET-HASH: {name}")


def to_glb(source):
    document = json.loads(source)
    if document.get("asset", {}).get("version") != "2.0":
        raise ValueError("ARP-QUEEN-GLTF-VERSION")
    if document.get("images"):
        raise ValueError("ARP-QUEEN-GLTF-UNEXPECTED-IMAGES")
    buffers = document.get("buffers", [])
    if len(buffers) != 1:
        raise ValueError("ARP-QUEEN-GLTF-BUFFER-COUNT")
    uri = buffers[0].get("uri", "")
    prefix = "data:application/octet-stream;base64,"
    if not uri.startswith(prefix):
        raise ValueError("ARP-QUEEN-GLTF-EXTERNAL-BUFFER")
    binary = base64.b64decode(uri[len(prefix) :], validate=True)
    if len(binary) != buffers[0].get("byteLength"):
        raise ValueError("ARP-QUEEN-GLTF-BUFFER-LENGTH")
    del buffers[0]["uri"]

    json_chunk = json.dumps(
        document, ensure_ascii=False, separators=(",", ":")
    ).encode("utf-8")
    json_chunk += b" " * ((-len(json_chunk)) % 4)
    binary += b"\0" * ((-len(binary)) % 4)
    total_length = 12 + 8 + len(json_chunk) + 8 + len(binary)
    return b"".join(
        (
            struct.pack("<III", 0x46546C67, 2, total_length),
            struct.pack("<II", len(json_chunk), 0x4E4F534A),
            json_chunk,
            struct.pack("<II", len(binary), 0x004E4942),
            binary,
        )
    )


def main():
    ASSET_ROOT.mkdir(parents=True, exist_ok=True)
    for name, expected_hash in MODELS.items():
        source = download(SOURCE_ROOT + name + ".gltf")
        verify_hash(source, expected_hash, name)
        (ASSET_ROOT / f"{name}.glb").write_bytes(to_glb(source))

    license_text = download("Ultimate Modular Women - April 2022/License.txt")
    verify_hash(license_text, LICENSE_SHA256, "License.txt")
    (ASSET_ROOT / "License.txt").write_bytes(license_text)


if __name__ == "__main__":
    main()
