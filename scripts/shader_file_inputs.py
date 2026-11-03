"""Shader-tool input policy over the shared bounded FileService client."""
from io import BytesIO, TextIOWrapper
from pathlib import Path

from tool_file_inventory import ToolFileInventoryClient


class ShaderFileInputs:
    def __init__(self, executable):
        self._files = ToolFileInventoryClient(executable)

    def read_bytes(self, path):
        return self._files.read_file(path).data

    def read_text(self, path):
        return self.decode_text(self.read_bytes(path))

    @staticmethod
    def decode_text(data):
        # Match the original Python open(encoding="utf-8", newline=None):
        # preserve a UTF-8 BOM character and use universal newline decoding.
        with TextIOWrapper(BytesIO(data), encoding="utf-8", newline=None) as source:
            return source.read()

    def digest(self, path):
        # Metadata-only receipts reuse the same FileService acquisition/hash,
        # without sending a DLL/tool binary back through the payload channel.
        source = Path(path)
        return self._files.inspect([source], source.parent)[0]["sha256"]
