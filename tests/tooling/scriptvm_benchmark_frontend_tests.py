"""Capture deterministic frontend rejections excluded from timing results."""
import argparse
import json
import subprocess
import tempfile
from pathlib import Path


class FrontendCapabilityProbe:
    def __init__(self, compiler: Path):
        self.compiler=compiler.resolve()

    def run(self) -> list[dict]:
        cases=(
            ("string_slice", 'def probe(n: int) -> int:\n    var text: string = "abc"\n    return len(text[0:2])\n', "expected ']' after index"),
            ("string_index", 'def probe(n: int) -> int:\n    var text: string = "abc"\n    return len(text[0])\n', "type 'string' is not indexable"),
        )
        template=(Path(__file__).resolve().parents[1]/"fixtures/aot/stress.package.yaml").read_text(encoding="utf-8")
        lines=template.splitlines()
        entries=[i for i,line in enumerate(lines) if line.strip().startswith("entries:")]
        if len(entries)!=1:
            raise RuntimeError("expected one explicit corpus entry contract")
        lines[entries[0]]="    entries: [probe]"
        manifest="\n".join(lines)+"\n"
        diagnostics=[]
        with tempfile.TemporaryDirectory(prefix="arp-bench-frontend-") as temporary:
            root=Path(temporary)
            (root/"stress.package.yaml").write_text(manifest,encoding="utf-8")
            for name,source,diagnostic in cases:
                (root/"stress.arp").write_text(source,encoding="utf-8")
                result=subprocess.run([str(self.compiler),"package",str(root/"stress.package.yaml"),
                    "--output",str(root/name)],capture_output=True,text=True,encoding="utf-8",timeout=30)
                if result.returncode==0 or diagnostic not in result.stderr:
                    raise RuntimeError(f"{name}: capability changed, update benchmark corpus: {result.stdout} {result.stderr}")
                diagnostics.append(dict(case=name,status="DiagnosticFailure",diagnostic=diagnostic,
                    actual_stderr=result.stderr.strip(),exit_code=result.returncode))
        return diagnostics


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("compiler",type=Path)
    parser.add_argument("--output",type=Path)
    args=parser.parse_args()
    results=FrontendCapabilityProbe(args.compiler).run()
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        # JSON strings/lists are a valid YAML scalar/flow representation.
        args.output.write_text("type: arp.script.benchmark-diagnostics/v1\ncontractVersion: 1\nprobes: "+
            json.dumps(results,ensure_ascii=False)+"\n",encoding="utf-8")
    print("Frontend capability probes passed: 2/2 deterministic rejections")
