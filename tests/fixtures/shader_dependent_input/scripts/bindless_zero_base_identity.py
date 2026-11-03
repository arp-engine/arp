"""Emit runtime provenance checks from the authoritative closed Slang generator."""
import argparse
import hashlib
import pathlib
import tempfile

from bindless_codegen import (compile_bindless_package, generate_bindless,
                              read_bindless_publication, zero_base_indirect_producer_schema,
                              zero_base_indirect_consumer_schema, _ZERO_BASE_INDIRECT_CONSUMER_ENTRIES)
import gpu_visibility_codegen as visibility


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--slangc", type=pathlib.Path)
    parser.add_argument("--spirv-reflect", type=pathlib.Path)
    args = parser.parse_args()
    reflect = args.spirv_reflect
    if args.slangc and not reflect:
        reflect = args.slangc.with_name("spirv-reflect" + args.slangc.suffix)
    target_tools = bool(args.slangc and args.slangc.is_file() and reflect and reflect.is_file())
    lines = ["#pragma once", "namespace arp::rhi::compiler_contract {"]
    programs = ((zero_base_indirect_producer_schema(), ["arpZeroBaseIndirectMain"], "zeroBaseIndirect"),
                (zero_base_indirect_consumer_schema(), _ZERO_BASE_INDIRECT_CONSUMER_ENTRIES, "zeroBaseIndirectConsumer"))
    for schema, entries, prefix in programs:
        for mode in ("Native", "Emulated"):
            generated = generate_bindless(schema, mode)
            digest = hashlib.sha256((generated.source + "\n").encode()).hexdigest()
            lines.append('inline constexpr char ' + prefix + mode + 'SourceSha256[] = "' + digest + '";')
            identity = ""
            if target_tools:
                with tempfile.TemporaryDirectory(prefix="arp-zero-base-identity-") as directory:
                    package = compile_bindless_package(schema, None, entries, mode, directory, args.slangc, reflect)
                    identity = read_bindless_publication(package)["shaderPackageIdentity"]
            # A build without the shader toolchain cannot certify executable provenance.
            lines.append('inline constexpr char ' + prefix + mode + 'PackageSha256[] = "' + identity + '";')
    for consumer in (False, True):
        prefix = "gpuVisibilityConsumer" if consumer else "gpuVisibility"
        for mode in ("Native", "Emulated"):
            generated = generate_bindless(visibility.schema(consumer), mode)
            source = generated.source + "\n" + visibility.authored_source(consumer)
            digest = hashlib.sha256(source.encode("utf-8")).hexdigest()
            lines.append('inline constexpr char ' + prefix + mode + 'SourceSha256[] = "' + digest + '";')
            identity = ""
            if target_tools:
                with tempfile.TemporaryDirectory(prefix="arp-visibility-identity-") as directory:
                    publication = compile_bindless_package(visibility.schema(consumer),
                        visibility.CONSUMER_SOURCE if consumer else visibility.SOURCE,
                        list(visibility.CONSUMER_ENTRIES if consumer else visibility.ENTRIES),
                        mode, directory, args.slangc, reflect,
                        visibility.CONSUMER_LIBRARIES if consumer else visibility.LIBRARIES)
                    identity = read_bindless_publication(publication)["shaderPackageIdentity"]
            lines.append('inline constexpr char ' + prefix + mode + 'PackageSha256[] = "' + identity + '";')
    lines.append("}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="ascii")


if __name__ == "__main__":
    main()
