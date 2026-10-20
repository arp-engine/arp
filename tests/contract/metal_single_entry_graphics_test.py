"""Ordinary stage lowering consumes only sealed single-entry facts."""
import argparse
import copy
import pathlib
import yaml
from single_entry_shader_artifact_test import ArtifactContractFixture


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True)
    parser.add_argument("--publication", action="append", required=True)
    args = parser.parse_args()
    calls = 0
    for publication in args.publication:
        publication = pathlib.Path(publication)
        fixture = ArtifactContractFixture(args.executable, publication)
        metadata = yaml.safe_load(publication.read_text())
        if metadata["stage"] == "Compute":
            fixture.verify("RHI-METAL-ORDINARY-GRAPHICS-ENTRY-STAGE-MISMATCH")
        else:
            fixture.verify()
            reflection = yaml.safe_load((publication.parent / metadata["files"]["reflection"]["path"]).read_text())
            maps = reflection["metalBindingSpace"]
            if maps:
                fixture.verify("RHI-METAL-ORDINARY-GRAPHICS-MAPPING-DUPLICATE", reflection_edit=lambda n:
                    n["metalBindingSpace"].append(copy.deepcopy(n["metalBindingSpace"][0])))
                fixture.verify("MetalBindingSpace/1", reflection_edit=lambda n:
                    n.update(metalBindingSpaceStatus="Unavailable"))
            if reflection["metalArgumentBuffer"]:
                fixture.verify("RHI-METAL-ORDINARY-GRAPHICS-MAPPING-DUPLICATE", reflection_edit=lambda n:
                    n["metalArgumentBuffer"].append(copy.deepcopy(n["metalArgumentBuffer"][0])))
                fixture.verify("logical descriptor metadata", reflection_edit=lambda n:
                    n["metalBindingSpace"][0].update(active=True, metalSlot=128, physicalType="SampledTexture"))
        calls += fixture.calls
    print(f"Ordinary sealed graphics stage lowering: {calls} real artifact mapping cases pass")


if __name__ == "__main__":
    main()
