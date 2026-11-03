"""Bounded offline file receipts acquired through the engine FileService."""
from dataclasses import dataclass
from pathlib import Path
import base64
import binascii
import hashlib
import re
import subprocess

import yaml


@dataclass(frozen=True)
class FileInputReceipt:
    data: bytes
    sha256: str


class ToolFileInventoryClient:
    request_type = "arp.tool-file-inventory-request"
    response_type = "arp.tool-file-inventory"
    diagnostic_prefix = "TOOL-FILE-INVENTORY-"

    # Explicit control/preparation budgets. Payload memory is separately bounded
    # by the largest declared file; files are acquired one at a time.
    control_bytes = 16 * 1024 * 1024
    control_allocation_bytes = 64 * 1024 * 1024

    def __init__(self, executable):
        self.executable = Path(executable)
        if not self.executable.is_file():
            raise RuntimeError(self.diagnostic_prefix + "TOOL-MISSING")

    def inspect(self, paths, root):
        return self._acquire(paths, root, payload=False)

    def read_file(self, path):
        source = Path(path)
        result = self._acquire([source], source.parent, payload=True)[0]
        return FileInputReceipt(result["data"], result["sha256"])

    def _acquire(self, paths, root, *, payload):
        files = [{"name": path.relative_to(root).as_posix(), "path": str(path.resolve()),
                  "bytes": path.stat().st_size} for path in paths]
        if not files:
            raise RuntimeError(self.diagnostic_prefix + "EMPTY")
        largest = max(1, *(entry["bytes"] for entry in files))
        document = {"type": self.request_type, "contractVersion": 1,
                    "maxFiles": len(files), "maxFileBytes": largest,
                    "maxTotalBytes": sum(entry["bytes"] for entry in files),
                    "maxPathBytes": max(len(entry[key].encode("utf-8")) for entry in files
                                        for key in ("name", "path")), "files": files}
        encoded_bytes = 0
        if payload:
            document["contractVersion"] = 2
            document["payloadEncoding"] = "base64"
            document["maxPayloadBytes"] = document["maxTotalBytes"]
            encoded_bytes = sum(((entry["bytes"] + 2) // 3) * 4 for entry in files)
            if encoded_bytes > self.control_bytes:
                raise RuntimeError(self.diagnostic_prefix + "RESPONSE-BUDGET")
        request = yaml.safe_dump(document, sort_keys=False, allow_unicode=True).encode("utf-8")
        if len(request) > self.control_bytes:
            raise RuntimeError(self.diagnostic_prefix + "CONTROL-BUDGET")
        result = subprocess.run([str(self.executable), "--request-limit-bytes", str(self.control_bytes),
                                 "--response-limit-bytes", str(self.control_bytes),
                                 "--preparation-bytes", str(largest + 2 * encoded_bytes + self.control_allocation_bytes)],
                                input=request, capture_output=True, check=False)
        if result.returncode:
            raise RuntimeError(result.stderr.decode("utf-8", errors="replace").strip()
                               or self.diagnostic_prefix + "FAILED")
        if len(result.stdout) > self.control_bytes:
            raise RuntimeError(self.diagnostic_prefix + "RESPONSE-BUDGET")
        response = yaml.safe_load(result.stdout)
        response_fields = {"type", "contractVersion", "files"}
        if payload:
            response_fields.add("payloadEncoding")
        if (not isinstance(response, dict) or set(response) != response_fields
                or response["type"] != self.response_type
                or type(response["contractVersion"]) is not int
                or response["contractVersion"] != (2 if payload else 1)
                or (payload and response["payloadEncoding"] != "base64")
                or not isinstance(response["files"], list) or len(response["files"]) != len(files)):
            raise RuntimeError(self.diagnostic_prefix + "RESPONSE-CONTRACT")
        entry_fields = {"path", "bytes", "sha256"}
        if payload:
            entry_fields.add("data")
        for expected, entry in zip(files, response["files"]):
            if (not isinstance(entry, dict) or set(entry) != entry_fields
                    or entry["path"] != expected["name"] or type(entry["bytes"]) is not int
                    or entry["bytes"] != expected["bytes"] or not isinstance(entry["sha256"], str)
                    or not re.fullmatch("[0-9a-f]{64}", entry["sha256"])):
                raise RuntimeError(self.diagnostic_prefix + "RESPONSE-FILE")
            if payload:
                if not isinstance(entry["data"], str):
                    raise RuntimeError(self.diagnostic_prefix + "PAYLOAD-ENCODING")
                try:
                    data = base64.b64decode(entry["data"], validate=True)
                except (ValueError, binascii.Error) as error:
                    raise RuntimeError(self.diagnostic_prefix + "PAYLOAD-ENCODING") from error
                if (len(data) != entry["bytes"]
                        or base64.b64encode(data).decode("ascii") != entry["data"]
                        or hashlib.sha256(data).hexdigest() != entry["sha256"]):
                    raise RuntimeError(self.diagnostic_prefix + "PAYLOAD-INTEGRITY")
                entry["data"] = data
        return response["files"]
