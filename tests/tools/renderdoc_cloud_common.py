"""Shared native capture queries and diagnostic cloud display transform."""
import renderdoc as rd


def fields(node):
    return {node.GetChild(i).name: node.GetChild(i) for i in range(node.NumChildren())}


def number(node):
    return int(node.data.basic.u)


def succeeded(status):
    return status.code == rd.ResultCode.Succeeded if hasattr(status, "code") else status == rd.ReplayStatus.Succeeded


def resource(pipeline, reflection, binding):
    matches = [b.descriptor for entries, reflected in (
        (pipeline.GetReadOnlyResources(rd.ShaderStage.Fragment), reflection.readOnlyResources),
        (pipeline.GetReadWriteResources(rd.ShaderStage.Fragment), reflection.readWriteResources))
        for b in entries if reflected[b.access.index].fixedBindNumber == binding]
    assert len(matches) == 1, (binding, matches)
    return matches[0]


def display(value):
    linear = value / (1 + value)
    return round(255 * (12.92 * linear if linear <= .0031308 else 1.055 * linear ** (1 / 2.4) - .055))


