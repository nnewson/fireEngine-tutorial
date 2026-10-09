"""Read-only build/environment provenance and low-rate host telemetry."""

import json
import os
import platform
import re
import threading
import time
from pathlib import Path

from benchmark_report import require
from preflight_lavapipe import ROOT, environment_snapshot, file_identity, git_output

FORBIDDEN = (
    "VK_INSTANCE_LAYERS",
    "VK_LOADER_LAYERS_ENABLE",
    "VK_LOADER_LAYERS_DISABLE",
    "VK_LOADER_LAYERS_ALLOW",
    "VK_LOADER_SETTINGS_PATH",
    "VK_LAYER_SETTINGS_PATH",
    "VK_LAYER_PATH",
    "VK_ADD_LAYER_PATH",
    "VK_IMPLICIT_LAYER_PATH",
    "VK_LAYER_ENABLES",
    "VK_LAYER_DISABLES",
    "VK_LAYER_VALIDATE_SYNC",
    "VK_KHRONOS_VALIDATION_SYNCVAL_SHADER_ACCESSES_HEURISTIC",
    "VK_LOADER_DEBUG",
    "TSAN_OPTIONS",
    "ASAN_OPTIONS",
    "UBSAN_OPTIONS",
)
SEARCH_ENVIRONMENT = (
    "XDG_CONFIG_HOME",
    "XDG_CONFIG_DIRS",
    "XDG_DATA_HOME",
    "XDG_DATA_DIRS",
)


def measurement_environment(inherited, icd):
    require(
        not any(inherited.get(name) for name in FORBIDDEN),
        "unset inherited validation, layer-path, tracing and sanitizer overrides; do not time them",
    )
    child = inherited.copy()
    if icd is not None:
        child.update(VK_DRIVER_FILES=str(icd), VK_ICD_FILENAMES=str(icd))
    return child


def snapshot(environment):
    return {
        **environment_snapshot(environment),
        **{name: environment.get(name) for name in FORBIDDEN + SEARCH_ENVIRONMENT},
    }


def read_optional(path):
    try:
        return {"value": path.read_text().strip()}
    except OSError as error:
        return {"unavailable": str(error)}


def host_sample(sysroot=Path("/sys"), procroot=Path("/proc")):
    """Raw readings retain units in filenames; unavailable sensors are explicit."""
    paths = []
    policies = sorted((sysroot / "devices/system/cpu/cpufreq").glob("policy*"))
    for policy in policies:
        paths += [
            policy / name
            for name in (
                "scaling_governor",
                "scaling_cur_freq",
                "cpuinfo_cur_freq",
                "scaling_min_freq",
                "scaling_max_freq",
            )
        ]
    paths += sorted(
        (sysroot / "devices/system/cpu").glob("cpu*/thermal_throttle/*throttle_count")
    )
    for zone in sorted((sysroot / "class/thermal").glob("thermal_zone*")):
        paths += [zone / "type", zone / "temp"]
    for hwmon in sorted((sysroot / "class/hwmon").glob("hwmon*")):
        paths += [
            hwmon / "name",
            *sorted(hwmon.glob("temp*_input")),
            *sorted(hwmon.glob("temp*_label")),
        ]
    for supply in sorted((sysroot / "class/power_supply").glob("*")):
        paths += [supply / name for name in ("type", "online", "status")]
    readings = {str(path): read_optional(path) for path in paths}
    return {
        "monotonic_seconds": time.monotonic(),
        "load_average": os.getloadavg(),
        "cpu_counters": read_optional(procroot / "stat"),
        "frequency_availability": f"{len(policies)} cpufreq policies"
        if policies
        else "unavailable: no cpufreq policies",
        "readings": readings,
        "availability_note": "missing files/readings are not evidence of no throttling; no sysfs sensors"
        if not paths
        else "raw unprivileged readings; brief throttling can be missed",
    }


class Telemetry:
    """One observer per arm, before/after and at one-second intervals."""

    def __init__(self, path, sample=host_sample):
        self.path = path
        self.sample = sample
        self.stop = threading.Event()
        self.thread = None
        self.error = None

    def emit(self, phase):
        try:
            value = {"phase": phase, **self.sample()}
        except Exception as error:
            # Sensor failure must not turn into an unreported dead sampler.
            value = {"phase": phase, "unavailable": str(error)}
        self.stream.write(json.dumps(value) + "\n")
        self.stream.flush()

    def monitor(self):
        try:
            while not self.stop.wait(1):
                self.emit("during")
        except Exception as error:
            self.error = error

    def __enter__(self):
        self.stream = self.path.open("w")
        self.emit("before")
        self.thread = threading.Thread(target=self.monitor, daemon=True)
        self.thread.start()
        return self

    def __exit__(self, *_):
        self.stop.set()
        self.thread.join()
        try:
            self.emit("after")
        finally:
            self.stream.close()
        if self.error is not None:
            raise self.error


def cache_values(path):
    return dict(
        re.findall(r"^([^/#\n][^:\n]*):[^=\n]+=(.*)$", path.read_text(), re.MULTILINE)
    )


def build_files(build):
    cache = build / "CMakeCache.txt"
    values = cache_values(cache)
    require(
        values.get("CMAKE_BUILD_TYPE") == "Release", "configure a Release build first"
    )
    require(
        not any(
            "-fsanitize" in value for name, value in values.items() if "FLAGS" in name
        ),
        "use an unsanitized Release build for measurements",
    )
    require(
        Path(values["CMAKE_HOME_DIRECTORY"]).resolve() == ROOT,
        "build belongs to another source checkout",
    )
    version = ".".join(
        values[f"CMAKE_CACHE_{part}_VERSION"] for part in ("MAJOR", "MINOR", "PATCH")
    )
    compiler = build / "CMakeFiles" / version / "CMakeCXXCompiler.cmake"
    installed = Path(values["VCPKG_INSTALLED_DIR"])
    return {
        "executable": build / "fireEngineTutorial",
        "forward_shader": build / "shaders/forward.spv",
        "shadow_shader": build / "shaders/shadow.spv",
        "cmake_cache": cache,
        "compiler": compiler,
        "dependencies": installed / "vcpkg/status",
        "manifest": ROOT / "vcpkg.json",
        "registry": ROOT / "vcpkg-configuration.json",
    }


def fingerprints(files):
    require(
        git_output("status", "--porcelain", "--untracked-files=no") == "",
        "commit/review tracked changes before acquiring measurements",
    )
    return {
        "source_revision": git_output("rev-parse", "HEAD"),
        "files": {name: file_identity(path) for name, path in files.items()},
    }


def provenance(build, icd, conditions, inherited, child, output):
    files = build_files(build)
    if icd is not None:
        files["icd"] = icd
    baseline = fingerprints(files)
    require(
        os.access(files["executable"], os.X_OK), "build the Release application first"
    )
    # Copy small text provenance so transferred evidence does not depend on the laptop.
    texts = {
        name: path.read_text()
        for name, path in files.items()
        if name not in ("executable", "forward_shader", "shadow_shader")
    }
    (output / "build-provenance.json").write_text(json.dumps(texts, indent=2) + "\n")
    layer_paths = (
        Path("/usr/share/vulkan/implicit_layer.d"),
        Path("/usr/local/share/vulkan/implicit_layer.d"),
        Path("/etc/vulkan/implicit_layer.d"),
        Path(os.environ.get("XDG_DATA_HOME", str(Path.home() / ".local/share")))
        / "vulkan/implicit_layer.d",
    )
    layers = {
        str(path): read_optional(path)
        for directory in layer_paths
        for path in sorted(directory.glob("*.json"))
    }
    drm = {
        str(path): read_optional(path)
        for pattern in (
            "card*/device/vendor",
            "card*/device/device",
            "card*/device/boot_vga",
            "card*-*/status",
        )
        for path in sorted(Path("/sys/class/drm").glob(pattern))
    }
    cpu = read_optional(Path("/proc/cpuinfo"))
    return files, {
        "platform": platform.platform(),
        "machine": platform.machine(),
        "cpu_inventory": cpu,
        "logical_cpu_count": os.cpu_count(),
        "source_status": git_output("status", "--short"),
        "fingerprints": baseline,
        "executable": str(files["executable"]),
        "inherited_environment": snapshot(inherited),
        "child_environment": snapshot(child),
        "conditions": conditions,
        "drm_inventory": drm,
        "implicit_layer_manifests": layers,
        "implicit_layer_limit": "non-exhaustive standard-directory inventory, not a trace of insertion; see operator conditions and XDG settings",
        "presentation_route_limit": "offload settings/device identity do not establish a PRIME copy",
        "telemetry_interval_seconds": 1,
        "binary_source_limit": "hashes and checkout recorded; build-current-source requirement is not an attestation",
    }


def load_conditions(path, role):
    conditions = json.loads(path.read_text())
    require(isinstance(conditions, dict), "conditions must be a JSON object")
    for key in (
        "background_load",
        "cooldown",
        "presentation_route",
        "implicit_layers",
        "cpu_governor",
    ):
        value = conditions.get(key)
        require(
            isinstance(value, str) and value.strip() and "REPLACE" not in value,
            f"record conditions.{key}; 'not established' is allowed for unknown provenance",
        )
    if role.startswith("laptop-"):
        require(
            conditions.get("ac_power") is True,
            "decision-bearing laptop sessions require declared AC power",
        )
    return conditions


def observed_conditions(path):
    """Summarize observed policy/power, not frequency or thermal verdicts."""
    rows = [json.loads(line) for line in path.read_text().splitlines()]
    require(
        len(rows) >= 2
        and rows[0]["phase"] == "before"
        and rows[-1]["phase"] == "after"
        and all(row["phase"] == "during" for row in rows[1:-1]),
        "incomplete telemetry sequence",
    )
    governors = {}
    ac_states = set()
    governor_samples = []
    for row in rows:
        readings = row.get("readings", {})
        current = {
            name: value.get("value")
            for name, value in readings.items()
            if name.endswith("/scaling_governor")
        }
        governor_samples.append(current)
        for name, value in current.items():
            if value is not None:
                governors.setdefault(name, set()).add(value)
        # Battery status and unrelated USB supplies are not proof of AC power.
        mains = [
            name.removesuffix("/type")
            for name, value in readings.items()
            if "/power_supply/" in name
            and name.endswith("/type")
            and value.get("value") == "Mains"
        ]
        online = [readings.get(name + "/online", {}).get("value") for name in mains]
        state = (
            "online"
            if "1" in online
            else (
                "offline"
                if online and all(value == "0" for value in online)
                else "unavailable"
            )
        )
        ac_states.add(state)
    policies = set().union(*governor_samples)
    return {
        "samples": len(rows),
        "governors": {name: sorted(values) for name, values in governors.items()},
        "governor_unavailable_samples": sum(
            not sample or any(sample.get(name) is None for name in policies)
            for sample in governor_samples
        ),
        "ac_states": sorted(ac_states),
    }


def condition_issues(observations, declared):
    """Reject observed contradictions/changes; unavailable is not a contradiction."""
    governors = {}
    ac_states = set()
    for observation in observations.values():
        for name, values in observation["governors"].items():
            governors.setdefault(name, set()).update(values)
        ac_states.update(set(observation["ac_states"]) - {"unavailable"})
    issues = []
    expected = declared.get("cpu_governor", "not established")
    for name, values in sorted(governors.items()):
        if len(values) > 1:
            issues.append(f"governor changed: {name}: {', '.join(sorted(values))}")
        if expected != "not established" and values != {expected}:
            issues.append(f"governor contradicts declared {expected}: {name}")
    if len(ac_states) > 1:
        issues.append("AC power changed within session")
    if declared.get("ac_power") is True and "offline" in ac_states:
        issues.append("observed AC offline contradicts declared AC power")
    if declared.get("ac_power") is False and "online" in ac_states:
        issues.append("observed AC online contradicts declared battery operation")
    return issues
