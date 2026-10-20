"""Closed VisibilityKernel/1 inputs shared by offline and runtime provenance."""
from pathlib import Path

import yaml


ENTRIES = tuple("visibility" + phase + "ComputeMain"
                for phase in ("Reset", "Cull", "Emit", "Finalize"))
ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "shaders/renderer/gpu_visibility_bindless.slang"
LIBRARIES = tuple(ROOT / path for path in (
    "shaders/core/instance_transform.slang",
    "shaders/core/gpu_visibility.slang",
    "shaders/core/gpu_visibility_kernel.slang"))
CONSUMER_SOURCE = ROOT / "shaders/renderer/gpu_visibility_consumer.slang"
CONSUMER_LIBRARIES = (*LIBRARIES, SOURCE)
CONSUMER_ENTRIES = (*ENTRIES, "visibilityVertexMain", "visibilityFragmentMain")


def schema(consumer=False):
    name = "gpu_visibility_consumer" if consumer else "gpu_visibility"
    return yaml.safe_load((ROOT / f"shaders/renderer/{name}.bindless.yaml").read_text(encoding="utf-8"))


def authored_source(consumer=False):
    paths = (*CONSUMER_LIBRARIES, CONSUMER_SOURCE) if consumer else (*LIBRARIES, SOURCE)
    return "\n".join(path.read_text(encoding="utf-8") for path in paths)
