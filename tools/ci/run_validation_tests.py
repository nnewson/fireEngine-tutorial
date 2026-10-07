"""Run Linux CTest with affirmative, per-device-test validation-layer evidence.

This is a CI policy, not an application requirement. Never force-enable the
layer: its insertion must come from the Debug application's own selection.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

LAYER = "VK_LAYER_KHRONOS_validation"
# These insertion messages are emitted by the pinned Vulkan Loader 1.4.357.
# Enumeration or "Loading layer library" alone does not prove chain insertion.
INSERTIONS = (
    f'Insert instance layer "{LAYER}"',
    f'Inserted device layer "{LAYER}"',
)
SYNC_SETTINGS = {
    "VK_LAYER_VALIDATE_SYNC=1",
    "VK_KHRONOS_VALIDATION_SYNCVAL_SHADER_ACCESSES_HEURISTIC=true",
}
CONTROL_NAMES = {
    "fireEngineTutorialAnimatedCubeSmoke",
    "fireEngineTutorialShadowSyncValidation",
}
OUTPUT_LIMIT = 1024 * 1024
TRUNCATION_MARKERS = (
    "Test Output for this test has been truncated",
    "[This part of the test output was removed since it exceeds the threshold of ",
)


class GateError(RuntimeError):
    """A failed prerequisite or test is never a successful fault control."""


class MissingValidation(GateError):
    """Tests passed, but their actual loader traces did not establish validation."""


def require(condition, message):
    if not condition:
        raise GateError(message)


def registered_tests(document):
    tests = document["tests"]
    names = [test["name"] for test in tests]
    require(names and len(names) == len(set(names)), "Empty or duplicate test registry")
    device_names = set()
    for test in tests:
        name = test["name"]
        properties = {prop["name"]: prop["value"] for prop in test["properties"]}
        labels = properties.get("LABELS", [])
        require(
            labels in (["device-free"], ["vulkan-device"]),
            f"{name}: expected exactly one test-class label",
        )
        if labels == ["device-free"]:
            continue
        device_names.add(name)
        require(properties.get("TIMEOUT") == 30, f"{name}: missing 30-second bound")
        require(
            properties.get("RESOURCE_LOCK") == ["fireEngineTutorialVulkan"],
            f"{name}: missing device resource lock",
        )
        require(
            "Vulkan validation error:" in properties.get("FAIL_REGULAR_EXPRESSION", []),
            f"{name}: missing validation-error rejection",
        )
        settings = properties.get("ENVIRONMENT", [])
        validation_settings = {entry for entry in settings if entry.startswith("VK_")}
        expected = SYNC_SETTINGS if name.endswith("SyncValidation") else set()
        require(
            validation_settings == expected, f"{name}: unexpected validation settings"
        )
    require(CONTROL_NAMES <= device_names, "Ordinary/sync control tests are missing")
    return set(names), device_names


def passed_outputs(xml_path, expected_names):
    root = ET.parse(xml_path).getroot()
    cases = root.findall(".//testcase")
    names = [case.get("name") for case in cases]
    require(
        len(names) == len(set(names)) and set(names) == expected_names,
        "JUnit test identities differ from the requested CTest registry",
    )
    outputs = {}
    for case in cases:
        name = case.get("name")
        require(
            case.get("status") == "run"
            and not any(
                case.find(tag) is not None for tag in ("failure", "error", "skipped")
            ),
            f"{name}: not a passing executed test",
        )
        output = case.findtext("system-out", "")
        # CTest's default passing-output limit is only 1024 bytes. Retain a large
        # explicit limit and fail closed if it is reached or CTest truncates it.
        require(
            len(output.encode("utf-8")) < OUTPUT_LIMIT
            and not any(marker in output for marker in TRUNCATION_MARKERS),
            f"{name}: incomplete output; increase the recorded-output bound",
        )
        require(
            "Vulkan validation error:" not in output,
            f"{name}: validation error in saved output",
        )
        outputs[name] = output
    return outputs


def require_validation(outputs, device_names, library):
    missing = []
    for name in sorted(device_names):
        for insertion in INSERTIONS:
            paths = re.findall(re.escape(insertion) + r" \(([^\n]+)\)", outputs[name])
            if not paths or any(Path(path).resolve() != library for path in paths):
                missing.append(f"{name}: missing pinned {insertion}")
    if missing:
        raise MissingValidation("\n".join(missing))


def require_missing_layer_control(outputs, library):
    # Both actual tests must have completed successfully first. An unrelated
    # crash, timeout, skipped test or validation error cannot satisfy this arm.
    for name, output in outputs.items():
        require(
            not any(insertion in output for insertion in INSERTIONS),
            f"{name}: empty search path still inserted the layer",
        )
    try:
        require_validation(outputs, CONTROL_NAMES, library)
    except MissingValidation as error:
        print(
            f"Missing-layer control rejected by the activation gate:\n{error}",
            flush=True,
        )
    else:
        raise GateError("Missing-layer control did not exercise the activation gate")


def prepare_layer(installed, destination):
    manifest = (
        installed / "share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json"
    )
    document = json.loads(manifest.read_text())
    layer = document["layer"]
    require(
        layer["name"] == LAYER and layer["api_version"] == "1.4.357",
        "Recheck the layer/loader contract when changing the vcpkg pin",
    )
    library = (installed / "lib/libVkLayer_khronos_validation.so").resolve(strict=True)
    # The upstream Unix install manifest uses a basename. A private copy with
    # an absolute path avoids dependence on ld.so's system search directories
    # and lets the trace guard distinguish this layer from a global SDK copy.
    require(
        Path(layer["library_path"]).name == library.name,
        "Unexpected validation-layer library in the installed manifest",
    )
    layer["library_path"] = str(library)
    destination.mkdir()
    (destination / manifest.name).write_text(json.dumps(document, indent=2) + "\n")
    print(f"Validation layer: {layer['api_version']} at {library}", flush=True)
    return library


def validation_environment(layer_directory, empty_directory):
    environment = os.environ.copy()
    # Reject inherited overrides instead of allowing a workstation/vkconfig
    # setting to force the layer, disable checks, or turn ordinary runs into sync.
    forbidden = (
        "VK_INSTANCE_LAYERS",
        "VK_LOADER_LAYERS_ENABLE",
        "VK_LOADER_LAYERS_DISABLE",
        "VK_LOADER_LAYERS_ALLOW",
        "VK_LOADER_SETTINGS_PATH",
        "VK_LAYER_SETTINGS_PATH",
        "VK_LAYER_ENABLES",
        "VK_LAYER_DISABLES",
        "VK_LAYER_VALIDATE_SYNC",
        "VK_KHRONOS_VALIDATION_SYNCVAL_SHADER_ACCESSES_HEURISTIC",
    )
    require(
        not any(environment.get(key) for key in forbidden),
        "Unset inherited validation/layer overrides before running the CI gate",
    )
    environment.update(
        {
            "VK_LAYER_PATH": str(layer_directory),
            "VK_IMPLICIT_LAYER_PATH": str(empty_directory),
            "VK_LOADER_DEBUG": "layer",
        }
    )
    return environment


def run_ctest(build, output, name, environment, expected_names, selection=()):
    xml_path = output / f"{name}.xml"
    log_path = output / f"{name}.log"
    command = [
        "ctest",
        "--test-dir",
        str(build),
        "--output-on-failure",
        "--no-tests=error",
        "--output-junit",
        str(xml_path),
        "--test-output-size-passed",
        str(OUTPUT_LIMIT),
        "--test-output-size-failed",
        str(OUTPUT_LIMIT),
        *selection,
    ]
    print(f"Running {name}; output: {log_path}", flush=True)
    with log_path.open("w") as log:
        result = subprocess.run(
            command,
            env=environment,
            stdout=log,
            stderr=subprocess.STDOUT,
            timeout=1800,
            check=False,
        )
    print(log_path.read_text(), end="", flush=True)
    require(result.returncode == 0, f"{name}: CTest exited {result.returncode}")
    return passed_outputs(xml_path, expected_names)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--installed-dir", type=Path, required=True)
    args = parser.parse_args()
    build = args.build_dir.resolve(strict=True)
    require(
        "CMAKE_BUILD_TYPE:STRING=Debug"
        in (build / "CMakeCache.txt").read_text().splitlines(),
        "The validation gate requires a Debug build",
    )
    evidence = build / "ci-validation"
    evidence.mkdir(exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=evidence))
    print(f"Validation evidence: {output}", flush=True)
    empty = output / "empty-layers"
    empty.mkdir()
    layer_directory = output / "layers"
    library = prepare_layer(args.installed_dir.resolve(strict=True), layer_directory)
    environment = validation_environment(layer_directory, empty)
    registry = subprocess.run(
        ["ctest", "--test-dir", str(build), "--show-only=json-v1"],
        text=True,
        capture_output=True,
        timeout=60,
        check=True,
    )
    (output / "registry.json").write_text(registry.stdout)
    names, device_names = registered_tests(json.loads(registry.stdout))
    outputs = run_ctest(build, output, "suite", environment, names)
    require_validation(outputs, device_names, library)
    print(
        f"All {len(names)} tests passed; pinned validation inserted in all "
        f"{len(device_names)} device tests (instance and device).",
        flush=True,
    )

    selection = ("-R", "^(" + "|".join(sorted(CONTROL_NAMES)) + ")$")
    absent_environment = {**environment, "VK_LAYER_PATH": str(empty)}
    absent = run_ctest(
        build, output, "missing-layer", absent_environment, CONTROL_NAMES, selection
    )
    require_missing_layer_control(absent, library)
    restored = run_ctest(
        build, output, "restored-layer", environment, CONTROL_NAMES, selection
    )
    require_validation(restored, CONTROL_NAMES, library)
    print(
        "Ordinary/sync missing-layer control rejected; restored runs validated.",
        flush=True,
    )


if __name__ == "__main__":
    try:
        main()
    except (
        GateError,
        OSError,
        ValueError,
        KeyError,
        ET.ParseError,
        subprocess.SubprocessError,
    ) as error:
        sys.exit(f"Validation gate failed: {error}")
