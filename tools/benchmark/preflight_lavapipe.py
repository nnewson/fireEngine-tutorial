"""Qualify laptop Lavapipe with the system inventory and the actual application.

This is not the Stage 7 measurement runner. Keep both invocations and their
diagnostics, including failures; no package, driver, or system setting is changed.
"""

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ENVIRONMENT_KEYS = (
    "LP_NUM_THREADS",
    "GALLIUM_DRIVER",
    "DRI_PRIME",
    "VK_DRIVER_FILES",
    "VK_ICD_FILENAMES",
    "VK_ADD_DRIVER_FILES",
    "VK_LOADER_DRIVERS_SELECT",
    "VK_LOADER_DRIVERS_DISABLE",
    "__NV_PRIME_RENDER_OFFLOAD",
    "__NV_PRIME_RENDER_OFFLOAD_PROVIDER",
    "__VK_LAYER_NV_optimus",
    "__GLX_VENDOR_LIBRARY_NAME",
    "DISPLAY",
    "WAYLAND_DISPLAY",
    "XDG_SESSION_TYPE",
    "LD_LIBRARY_PATH",
    "LD_PRELOAD",
    "VK_LOADER_DEBUG",
    "VK_LOADER_SETTINGS_PATH",
    "VK_LAYER_PATH",
    "VK_ADD_LAYER_PATH",
    "VK_IMPLICIT_LAYER_PATH",
    "VK_INSTANCE_LAYERS",
    "VK_LOADER_LAYERS_ENABLE",
    "VK_LOADER_LAYERS_DISABLE",
    "VK_LOADER_LAYERS_ALLOW",
    "VK_LAYER_SETTINGS_PATH",
    "VK_LAYER_ENABLES",
    "VK_LAYER_DISABLES",
    "VK_LAYER_VALIDATE_SYNC",
    "VK_KHRONOS_VALIDATION_SYNCVAL_SHADER_ACCESSES_HEURISTIC",
)


def environment_snapshot(environment):
    """Capture only named settings/prefixes, distinguishing unset from empty."""
    keys = set(ENVIRONMENT_KEYS) | {
        key for key in environment if key.startswith(("MESA_", "GALLIVM_"))
    }
    return {key: environment.get(key) for key in sorted(keys)}


def lavapipe_environment(environment, manifest):
    child = environment.copy()
    # Use one manifest with both old and new loaders. If both variables exist,
    # VK_DRIVER_FILES wins; matching values avoid different driver selections.
    child["VK_DRIVER_FILES"] = str(manifest)
    child["VK_ICD_FILENAMES"] = str(manifest)
    return child


def file_identity(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return {"path": str(path), "sha256": digest.hexdigest()}


def git_output(*arguments):
    result = subprocess.run(
        ["git", "-C", str(ROOT), *arguments],
        capture_output=True,
        text=True,
        check=True,
        timeout=10,
    )
    return result.stdout.strip()


def run_check(name, command, output, environment, timeout):
    """Save partial output and failure status even for a timeout or launch error."""
    result = {"command": command, "cwd": str(ROOT), "timeout_seconds": timeout}
    started = time.monotonic()
    with (
        (output / f"{name}.stdout.log").open("wb") as stdout,
        (output / f"{name}.stderr.log").open("wb") as stderr,
    ):
        try:
            child = subprocess.run(
                command,
                cwd=ROOT,
                env=environment,
                stdout=stdout,
                stderr=stderr,
                timeout=timeout,
                check=False,
            )
            result["returncode"] = child.returncode
            if child.returncode != 0:
                result["error"] = f"exit {child.returncode}"
        except subprocess.TimeoutExpired:
            # subprocess.run kills and waits for this direct child on timeout.
            result.update(returncode=None, error=f"timeout after {timeout}s")
        except OSError as error:
            result.update(returncode=None, error=f"could not launch: {error}")
    result["elapsed_seconds"] = time.monotonic() - started
    return result


def require_line(output, pattern, description):
    if len(re.findall(pattern, output, re.MULTILINE)) != 1:
        raise ValueError(f"expected one {description}")


def check_inventory(output):
    # Check the physical device, not the loader's 'Vulkan Instance Version'.
    names = re.findall(r"^\s*driverName\s*=\s*(.+?)\s*$", output, re.MULTILINE)
    if len(names) != 1 or names[0].lower() not in ("llvmpipe", "lavapipe"):
        raise ValueError("inventory must identify exactly one Lavapipe driver")
    versions = re.findall(
        r"^\s*apiVersion\s*=\s*(\d+)\.(\d+)\.\d+\s*$", output, re.MULTILINE
    )
    if len(versions) != 1 or tuple(map(int, versions[0])) < (1, 4):
        raise ValueError("inventory must report physical-device Vulkan 1.4 or later")


def check_application(output):
    # This is a completion/identity check, not a phase parser or timing gate.
    require_line(output, r"^  Driver: [^\n]+$", "selected driver")
    for pattern, description in (
        (r"^Phase-level CPU benchmark$", "benchmark report"),
        (r"^  Build configuration: Release$", "Release configuration"),
        (r"^  Driver: (?:llvmpipe|lavapipe) \([^\n]+\)$", "Lavapipe driver"),
        (r"^  Workload: instances=1, nodes=2, draws=1$", "one-draw workload"),
        (r"^  Recorded packets per frame: shadow=1, forward=1$", "two-pass workload"),
        (
            r"^  Frames: 16 warm-up, 64 measured, \d+ discarded attempts$",
            "completed frame sequence",
        ),
        (
            r"^  Forward draw bindings are cached independently inside each command buffer\.$",
            "report footer",
        ),
    ):
        require_line(output, pattern, description)


def qualify(output, results):
    failures = []
    for name, check in (
        ("vulkaninfo", check_inventory),
        ("benchmark", check_application),
    ):
        result = results[name]
        stdout = (output / f"{name}.stdout.log").read_text(errors="replace")
        stderr = (output / f"{name}.stderr.log").read_text(errors="replace")
        combined = stdout + "\n" + stderr
        result["validation_warnings"] = combined.count("Vulkan validation warning:")
        if result.get("returncode") != 0:
            failures.append(f"{name}: {result.get('error', 'missing successful exit')}")
        try:
            check(stdout)
            if "Vulkan validation error:" in combined:
                raise ValueError("application validation error in saved output")
        except ValueError as error:
            failures.append(f"{name}: {error}")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--icd", required=True, type=Path, help="one Lavapipe JSON manifest"
    )
    parser.add_argument(
        "--executable", type=Path, default=ROOT / "build-benchmark/fireEngineTutorial"
    )
    parser.add_argument("--output-root", type=Path, default=ROOT / "build-evidence")
    parser.add_argument(
        "--timeout", type=int, default=60, help="seconds per invocation"
    )
    args = parser.parse_args()
    if sys.platform != "linux" or os.geteuid() == 0:
        parser.error("run as your normal desktop user on Linux, not through sudo")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    try:
        manifest = args.icd.resolve(strict=True)
        executable = args.executable.resolve(strict=True)
    except OSError as error:
        parser.error(str(error))
    if not manifest.is_file() or ":" in str(manifest):
        parser.error("--icd must name one file, not a directory or manifest list")
    if not executable.is_file() or not os.access(executable, os.X_OK):
        parser.error("--executable must be an executable file; build Release first")

    inherited = os.environ.copy()
    environment = lavapipe_environment(inherited, manifest)
    args.output_root.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="lavapipe-preflight-", dir=args.output_root))
    print(f"Logs: {output.resolve()}", flush=True)
    record = {
        "purpose": "qualification only; not a Stage 7 measurement",
        "platform": platform.platform(),
        "source_revision": git_output("rev-parse", "HEAD"),
        "source_status": git_output("status", "--short"),
        "executable": file_identity(executable),
        "manifest": file_identity(manifest),
        "manifest_text": manifest.read_text(),
        "inherited_environment": environment_snapshot(inherited),
        "child_environment": environment_snapshot(environment),
        "status": "incomplete",
        "results": {},
    }
    summary = output / "summary.json"
    summary.write_text(json.dumps(record, indent=2) + "\n")
    commands = (
        ("vulkaninfo", [shutil.which("vulkaninfo") or "vulkaninfo", "--summary"]),
        ("benchmark", [str(executable), "--benchmark", "1"]),
    )
    # Still try the app if inventory fails: it uses its own linked loader and
    # may give a more useful surface/feature rejection. Both must pass overall.
    for name, command in commands:
        result = run_check(name, command, output, environment, args.timeout)
        record["results"][name] = result
        summary.write_text(json.dumps(record, indent=2) + "\n")
        print(f"{name}: {result.get('error', 'exit 0')}", flush=True)
    failures = qualify(output, record["results"])
    record.update(status="failed" if failures else "qualified", failures=failures)
    summary.write_text(json.dumps(record, indent=2) + "\n")
    if failures:
        print("NOT QUALIFIED:\n" + "\n".join(failures), file=sys.stderr)
        print(
            "Review the saved logs before changing the venue or driver.",
            file=sys.stderr,
        )
        return 1
    print("Lavapipe preflight passed. Qualification only; no baseline acquired.")
    print("Return this log directory for review, including stderr and summary.json.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
