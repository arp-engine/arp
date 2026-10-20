"""Read scoped source evidence after responsibility-based file consolidation."""
from pathlib import Path
import re
import yaml


def fragment(source, identifier):
    mapping = Path(__file__).resolve().parents[1] / "fixtures/rhi_source_sections.yaml"
    sections = yaml.safe_load(mapping.read_text(encoding="utf-8"))["sections"]
    title = sections[identifier]
    begin = "// region " + title + "\n"
    end = "// endregion " + title
    if source.count(begin) != 1 or source.count(end) != 1:
        raise ValueError("Missing or ambiguous source fragment: " + identifier)
    # Imports are now file dependencies. Only import lines accompany the selected
    # responsibility; unrelated implementation bodies cannot satisfy assertions.
    imports = "\n".join(re.findall(r'^\s*#include[^\n]+', source, re.M))
    return imports + "\n" + source.split(begin, 1)[1].split(end, 1)[0]


def decision_bodies(root, source):
    # Follow only explicit policy calls from the selected implementation. Never
    # concatenate an entire policy file: unrelated contracts must not satisfy a test.
    calls=re.findall(r"(?:semantics::)?((?:BindlessPreparation|CommandDraw|Resource|Descriptor|PipelineCreation|Query|Transfer)Lowering)\s*::\s*(\w+)\s*(?:<[^;{}]+?>\s*)?\(", source)
    result=[]
    for owner, method in dict.fromkeys(calls):
        for path in (root / "src/rhi/semantics").glob("*lowering.h"):
            policy=path.read_text(encoding="utf-8-sig")
            if not re.search(r"class\s+"+owner+r"\b",policy):
                continue
            match=re.search(r"\b"+method+r"\s*\([^;{}]*\)[^;{}]*\{",policy)
            if not match:
                continue
            opening=match.end()-1
            # Braces in comments/literals are masked before balancing.
            masked=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"',
                          lambda m:" "*len(m[0]),policy,flags=re.S)
            depth=1;ending=opening+1
            while depth and ending<len(policy):
                depth+=(masked[ending]=="{")-(masked[ending]=="}")
                ending+=1
            if depth:
                raise ValueError("Unbalanced policy method: " + method)
            result.append(policy[match.start():ending])
    return "\n".join(result)
