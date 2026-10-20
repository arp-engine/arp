"""Generate a review inventory, never an execution/capability approval.

Reuses the shared C++ tokenizer to exclude comment/literal fake labels. The
inventory deliberately retains missing cases and conditional branches as open
review items; it cannot replace compiled contract tests or native smoke.
"""
from pathlib import Path
import argparse
import hashlib
import re
import subprocess
import sys

import yaml

from cpp_source_evidence import switch_regions, tokens


class RhiBackendInventory:
    ENUM_TYPES = ("Backend", "BackendPreference")
    SOURCE_ROOTS = ("src", "editor", "examples", "tests")
    SOURCE_SUFFIXES = frozenset((".h", ".hh", ".hpp", ".inl", ".ipp", ".cpp", ".cc", ".cxx", ".mm"))

    def __init__(self, root):
        self.root = Path(root).resolve()

    def inspect(self, relative):
        path = self.root / relative
        # Hash the same bytes that were parsed, even when another task edits a
        # file during collection. The multi-file inventory is not atomic.
        source_bytes = path.read_bytes()
        source_identity = dict(path=relative.as_posix(),
                               sha256=hashlib.sha256(source_bytes).hexdigest())
        try:
            source = source_bytes.decode("utf-8-sig")
        except UnicodeDecodeError as error:
            # No guessed codec, lossy decoding or omission: retain the raw file
            # identity and make the entire source scan observably incomplete.
            bom_bytes = 3 if source_bytes.startswith(b'\xef\xbb\xbf') else 0
            return dict(**source_identity, parseComplete=False,
                        backendReferenceCount=None, backendPreferenceReferenceCount=None,
                        backendTypeTokenCount=None, backendPreferenceTypeTokenCount=None,
                        switches=[], reviewStatus="Open",
                        diagnostic=dict(id="RHI-INVENTORY-SOURCE-ENCODING",
                                        requiredEncoding="UTF-8 (optional BOM)",
                                        byteOffset=error.start + bom_bytes,
                                        reason=error.reason))
        lexical = [t for t in tokens(source) if not t[0].startswith(('//', '/*'))]
        # Comments and strings cannot introduce C++ backend references.
        references = [lexical[i][0] for i in range(len(lexical) - 2)
                      if lexical[i][0] in self.ENUM_TYPES and lexical[i + 1][0] == "::"]
        type_tokens = [token[0] for token in lexical if token[0] in self.ENUM_TYPES]
        if not type_tokens:
            return None
        switches = []
        for region in switch_regions(source):
            labels = [re.sub(r"\s+", "", label) for label in region["labels"]]
            enum_types = sorted({enum for enum in self.ENUM_TYPES for label in labels
                                 if re.search(r"(?:^case|::)" + enum + r"::", label)})
            backend_labels = [label for label in labels if any(
                re.search(r"(?:^case|::)" + enum + r"::", label) for enum in enum_types)]
            if not backend_labels:
                continue
            switches.append(dict(line=source.count("\n", 0, region["start"]) + 1,
                                 expression=region["expression"].strip(),
                                 labels=backend_labels,
                                 enumTypes=enum_types,
                                 explicitWebGl=any(label.endswith(enum + "::WebGL:")
                                     for label in backend_labels for enum in enum_types),
                                 hasDefault="default:" in labels,
                                 reviewStatus="Open"))
        return dict(**source_identity, parseComplete=True,
                    backendReferenceCount=references.count("Backend"),
                    backendPreferenceReferenceCount=references.count("BackendPreference"),
                    backendTypeTokenCount=type_tokens.count("Backend"),
                    backendPreferenceTypeTokenCount=type_tokens.count("BackendPreference"), switches=switches,
                    reviewStatus="Open")

    def collect(self):
        files = []
        source_count = 0
        parsed_count = 0
        for directory in self.SOURCE_ROOTS:
            for path in sorted((self.root / directory).rglob("*")):
                if not path.is_file() or path.suffix.lower() not in self.SOURCE_SUFFIXES:
                    continue
                source_count += 1
                record = self.inspect(path.relative_to(self.root))
                if record is None or record["parseComplete"]:
                    parsed_count += 1
                if record:
                    files.append(record)
        switches = [region for record in files for region in record["switches"]]
        return dict(type="RhiBackendReviewInventory", contractVersion=3,
                    evidenceKind="LexicalInventoryOnly", approvalGranted=False,
                    scanComplete=parsed_count == source_count,
                    snapshotKind="PerFileReadNoGlobalAtomicity",
                    sourceRoots=list(self.SOURCE_ROOTS), sourceSuffixes=sorted(self.SOURCE_SUFFIXES),
                    generatorSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    tokenizerSha256=hashlib.sha256(Path(__file__).with_name(
                        "cpp_source_evidence.py").read_bytes()).hexdigest(),
                    revision=subprocess.check_output(["git", "rev-parse", "HEAD"],
                                                     cwd=self.root, text=True).strip(),
                    counts=dict(sourceFiles=source_count, parsedSourceFiles=parsed_count,
                                sourceDiagnostics=source_count - parsed_count,
                                files=len(files), backendSwitches=len(switches),
                                switchesWithoutExplicitWebGl=sum(not r["explicitWebGl"] for r in switches)),
                    files=files)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    report = RhiBackendInventory(args.root).collect()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(yaml.safe_dump(report, sort_keys=False), encoding="utf-8")
    print(yaml.safe_dump(report["counts"], sort_keys=False).strip())
    for record in report["files"]:
        if not record["parseComplete"]:
            diagnostic = record["diagnostic"]
            print(f'{diagnostic["id"]}: {record["path"]}: '
                  f'byte {diagnostic["byteOffset"]}: {diagnostic["reason"]}', file=sys.stderr)
    return 0 if report["scanComplete"] else 2


if __name__ == "__main__":
    sys.exit(main())
