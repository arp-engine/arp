#!/usr/bin/env python3
"""Validate a RenderGraph resource-management RenderDoc XML export."""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import sys
import xml.etree.ElementTree as ET


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("xml", type=Path, help="XML produced by renderdoccmd convert")
    parser.add_argument("--capture", type=Path, help="RDC file used for SHA-256 evidence")
    parser.add_argument("--output", type=Path, help="Optional text report path")
    parser.add_argument("--expected-submits", type=int, default=4)
    parser.add_argument("--expected-presents", type=int, default=4)
    return parser.parse_args()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def enum_string(node: ET.Element | None) -> str:
    return "" if node is None else node.attrib.get("string", "")


def main() -> int:
    args = parse_args()
    if not args.xml.is_file():
        raise RuntimeError(f"RenderDoc XML does not exist: {args.xml}")
    if args.capture is not None and not args.capture.is_file():
        raise RuntimeError(f"RenderDoc capture does not exist: {args.capture}")

    root = ET.parse(args.xml).getroot()
    chunks = list(root.findall("./chunks/chunk"))
    names = [chunk.attrib.get("name", "") for chunk in chunks]

    count = lambda name: sum(chunk_name == name for chunk_name in names)
    queue_submits = count("vkQueueSubmit")
    queue_presents = count("vkQueuePresentKHR")
    pipeline_barriers = count("vkCmdPipelineBarrier")
    create_images = count("vkCreateImage")
    bind_images = count("vkBindImageMemory")
    allocate_memory = count("vkAllocateMemory")

    alias_images = 0
    present_barriers = 0
    render_pass_clears: list[tuple[int, tuple[float, float, float, float]]] = []
    for chunk in chunks:
        if chunk.attrib.get("name") == "vkCreateImage":
            flags = chunk.find(".//enum[@name='flags']")
            if "VK_IMAGE_CREATE_ALIAS_BIT" in enum_string(flags):
                alias_images += 1
        if chunk.attrib.get("name") == "vkCmdPipelineBarrier":
            for layout in chunk.findall(".//enum[@name='newLayout']"):
                if enum_string(layout) == "VK_IMAGE_LAYOUT_PRESENT_SRC_KHR":
                    present_barriers += 1
        if chunk.attrib.get("name") == "vkCmdBeginRenderPass":
            extent = chunk.find(
                ".//struct[@name='renderArea']/struct[@name='extent']")
            width = extent.find("./uint[@name='width']") if extent is not None else None
            height = extent.find("./uint[@name='height']") if extent is not None else None
            values = chunk.findall(
                ".//array[@name='pClearValues']/struct[1]"
                "/struct[@name='color']/array[@name='float32']/float")
            if width is not None and height is not None and len(values) >= 4:
                color = tuple(float(value.text or "0") for value in values[:4])
                render_pass_clears.append(
                    (int(width.text or "0") * int(height.text or "0"), color))

    max_clear_area = max((item[0] for item in render_pass_clears), default=0)
    backbuffer_clears = [
        color for area, color in render_pass_clears if area == max_clear_area]
    final_clear = backbuffer_clears[-1] if backbuffer_clears else (0.0, 0.0, 0.0, 0.0)
    clear_luminance = (
        0.2126 * final_clear[0]
        + 0.7152 * final_clear[1]
        + 0.0722 * final_clear[2])
    clear_payload = ";".join(
        ",".join(f"{component:.9g}" for component in color)
        for color in backbuffer_clears).encode("ascii")
    clear_hash = hashlib.sha256(clear_payload).hexdigest().upper()

    debug_messages: list[tuple[str, str]] = []
    for message in root.findall(".//array[@name='DebugMessages']/struct"):
        severity = enum_string(message.find("./enum[@name='severity']"))
        description_node = message.find("./string[@name='description']")
        description = "" if description_node is None else (description_node.text or "")
        debug_messages.append((severity, description.strip()))
    high_messages = [message for message in debug_messages if message[0] == "High"]

    evidence = {
        "xml": str(args.xml.resolve()),
        "capture": str(args.capture.resolve()) if args.capture else "",
        "capture_sha256": sha256(args.capture) if args.capture else "",
        "queue_submit_count": queue_submits,
        "queue_present_count": queue_presents,
        "pipeline_barrier_count": pipeline_barriers,
        "present_barrier_count": present_barriers,
        "render_pass_count": count("vkCmdBeginRenderPass"),
        "backbuffer_clear_count": len(backbuffer_clears),
        "backbuffer_clear_rgba": ",".join(
            f"{component:.6f}" for component in final_clear),
        "backbuffer_clear_luminance_bt709": f"{clear_luminance:.6f}",
        "backbuffer_clear_sha256": clear_hash,
        "create_image_count": create_images,
        "alias_create_image_count": alias_images,
        "bind_image_memory_count": bind_images,
        "allocate_memory_count": allocate_memory,
        "debug_message_count": len(debug_messages),
        "high_severity_message_count": len(high_messages),
    }

    failures: list[str] = []
    if queue_submits != args.expected_submits:
        failures.append(
            f"queue submits: expected {args.expected_submits}, observed {queue_submits}")
    if queue_presents != args.expected_presents:
        failures.append(
            f"queue presents: expected {args.expected_presents}, observed {queue_presents}")
    if alias_images == 0:
        failures.append("no VK_IMAGE_CREATE_ALIAS_BIT image was captured")
    if bind_images <= allocate_memory:
        failures.append("capture does not prove multiple image bindings per allocation")
    if present_barriers < args.expected_presents:
        failures.append("not every presented frame recorded a Present layout barrier")
    if len(backbuffer_clears) != args.expected_presents:
        failures.append("not every presented frame recorded a backbuffer clear")
    if backbuffer_clears and any(
            color != backbuffer_clears[0] for color in backbuffer_clears[1:]):
        failures.append("backbuffer clear color changed across smoke frames")
    if high_messages:
        failures.append(f"{len(high_messages)} high-severity validation messages")

    lines = [f"{key}={value}" for key, value in evidence.items()]
    for severity, description in debug_messages:
        lines.append(f"debug={severity}:{description}")
    for failure in failures:
        lines.append(f"failure={failure}")
    lines.append("result=" + ("PASS" if not failures else "FAIL"))
    report = "\n".join(lines) + "\n"
    sys.stdout.write(report)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report, encoding="utf-8")
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
