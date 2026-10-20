"""Record queried device ceilings without granting bindless execution."""
import argparse
import datetime
import hashlib
import json
import pathlib

import yaml

from browser_contract_runner import BrowserContractRunner


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vulkan-report", type=pathlib.Path, required=True)
    parser.add_argument("--browser", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    browser_output = args.output / "webgpu-device-limits"
    browser_output.mkdir(exist_ok=True)
    root = pathlib.Path(__file__).resolve().parents[1]
    source = root / "tests/fixtures/bindless_device_budget_probe.html"
    browser = BrowserContractRunner(args.browser, source.parent, source.name, browser_output, 45).run()
    if not browser.get("success") or browser.get("runtimeCapabilityGranted") is not False:
        raise RuntimeError(browser.get("diagnostic", "BINDLESS-DEVICE-LIMIT-QUERY-FAILED"))
    raw = args.vulkan_report.read_bytes()
    device = json.loads(raw)["capabilities"]["device"]
    properties = device["properties"]["VkPhysicalDeviceProperties"]
    # Descriptor indexing was promoted in Vulkan 1.2. Vulkan Profiles exports
    # the promoted structure for a core-capable device, rather than its alias.
    api = properties["apiVersion"]
    core_version = (api & 0x1FFFFFFF) >= ((1 << 22) | (2 << 12))
    feature_structure = "VkPhysicalDeviceVulkan12Features" if core_version else "VkPhysicalDeviceDescriptorIndexingFeatures"
    indexing = device["features"][feature_structure]
    report = {
        "type": "BindlessDeviceLimitObservations", "contractVersion": 1,
        "observedAtUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "scope": "physical-ceilings-only-not-complete-workload-budget",
        "runtimeCapabilityGranted": False, "publicExecutionValidated": False,
        "Vulkan": {
            "source": str(args.vulkan_report.resolve()),
            "sourceSha256": hashlib.sha256(raw).hexdigest(),
            "deviceName": properties["deviceName"],
            "vendorId": properties["vendorID"], "deviceId": properties["deviceID"],
            "driverVersion": properties["driverVersion"], "apiVersion": properties["apiVersion"],
            "limits": properties["limits"],
            "featureStructure": {"name": feature_structure, "values": indexing},
        },
        "WebGPU": browser,
        "remainingAuthority": [
            "Exact runtime adapter/device and requested limits",
            "Complete physical table plus active/reserved/snapshot/fault footprint",
            "Exact stage operations, formats and sampling equivalence",
            "Native payload and completion/lifetime validation",
        ],
    }
    target = args.output / "device-limit-observations.yaml"
    target.write_text(yaml.safe_dump(report, sort_keys=False), encoding="utf-8")
    print(target.resolve())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
