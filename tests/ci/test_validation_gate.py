"""Device-free controls for the separate CI validation-evidence gate."""

import contextlib
import copy
import importlib.util
import io
import json
import shutil
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[2] / "tools/ci/run_validation_tests.py"
SPEC = importlib.util.spec_from_file_location("validation_gate", SCRIPT)
gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gate)
LIBRARY = Path("/pinned/lib/libVkLayer_khronos_validation.so")


def trace(library=LIBRARY):
    return "\n".join(
        f"INFO | LAYER: {message} ({library})" for message in gate.INSERTIONS
    )


def registry():
    tests = []
    for name in sorted(gate.CONTROL_NAMES):
        properties = {
            "LABELS": ["vulkan-device"],
            "TIMEOUT": 30.0,
            "RESOURCE_LOCK": ["fireEngineTutorialVulkan"],
            "FAIL_REGULAR_EXPRESSION": ["Vulkan validation error:"],
        }
        if name.endswith("SyncValidation"):
            properties["ENVIRONMENT"] = sorted(gate.SYNC_SETTINGS)
        tests.append(
            {
                "name": name,
                "properties": [
                    {"name": key, "value": value} for key, value in properties.items()
                ],
            }
        )
    tests.append(
        {
            "name": "cpu-test",
            "properties": [{"name": "LABELS", "value": ["device-free"]}],
        }
    )
    return {"tests": tests}


class RegistryTests(unittest.TestCase):
    def test_complete_registration(self):
        names, devices = gate.registered_tests(registry())
        self.assertEqual(devices, gate.CONTROL_NAMES)
        self.assertEqual(names, devices | {"cpu-test"})

    def test_each_device_property_is_required(self):
        original = registry()
        for index, test in enumerate(original["tests"][:-1]):
            for property_index, prop in enumerate(test["properties"]):
                with self.subTest(test=test["name"], property=prop["name"]):
                    changed = copy.deepcopy(original)
                    del changed["tests"][index]["properties"][property_index]
                    with self.assertRaises(gate.GateError):
                        gate.registered_tests(changed)

    def test_sync_requires_both_settings(self):
        for setting in gate.SYNC_SETTINGS:
            changed = registry()
            sync = next(
                test
                for test in changed["tests"]
                if test["name"].endswith("SyncValidation")
            )
            environment = next(
                prop for prop in sync["properties"] if prop["name"] == "ENVIRONMENT"
            )
            environment["value"].remove(setting)
            with self.subTest(setting=setting), self.assertRaises(gate.GateError):
                gate.registered_tests(changed)

    def test_duplicate_or_missing_control_does_not_pass(self):
        for tests in ([], registry()["tests"] * 2, registry()["tests"][1:]):
            with self.subTest(tests=tests), self.assertRaises(gate.GateError):
                gate.registered_tests({"tests": tests})


class EvidenceTests(unittest.TestCase):
    def read_cases(self, cases, expected=None):
        root = ET.Element("testsuite")
        for attributes, children in cases:
            case = ET.SubElement(root, "testcase", attributes)
            for tag, text in children.items():
                ET.SubElement(case, tag).text = text
        with tempfile.TemporaryDirectory() as directory:
            xml_path = Path(directory) / "ctest.xml"
            ET.ElementTree(root).write(xml_path)
            return gate.passed_outputs(xml_path, expected or {"device"})

    def test_full_success_and_wrapper_output(self):
        output = "Capture succeeded.\n" + trace() + "\nPresented 3 frames."
        outputs = self.read_cases(
            [({"name": "device", "status": "run"}, {"system-out": output})]
        )
        gate.require_validation(outputs, {"device"}, LIBRARY)

    def test_enumeration_loading_instance_only_and_wrong_library_do_not_pass(self):
        candidates = (
            "",
            f"Instance Layers: {gate.LAYER}",
            f"Loading layer library {LIBRARY}",
            trace().splitlines()[0],
            trace().splitlines()[1],
            trace(Path("/system/lib/libVkLayer_khronos_validation.so")),
        )
        for output in candidates:
            with self.subTest(output=output), self.assertRaises(gate.MissingValidation):
                gate.require_validation({"device": output}, {"device"}, LIBRARY)

    def test_every_device_test_must_activate_validation(self):
        with self.assertRaisesRegex(gate.MissingValidation, "wrapped"):
            gate.require_validation(
                {"direct": trace(), "wrapped": ""}, {"direct", "wrapped"}, LIBRARY
            )

    def test_incomplete_failed_skipped_duplicate_and_unexpected_runs_rejected(self):
        for status, child in (
            ("run", "failure"),
            ("run", "error"),
            ("notrun", "skipped"),
            ("disabled", "system-out"),
        ):
            with (
                self.subTest(status=status, child=child),
                self.assertRaises(gate.GateError),
            ):
                self.read_cases([({"name": "device", "status": status}, {child: ""})])
        passing = ({"name": "device", "status": "run"}, {"system-out": trace()})
        for cases in ([], [passing, passing]):
            with self.subTest(cases=cases), self.assertRaises(gate.GateError):
                self.read_cases(cases)
        with self.assertRaises(gate.GateError):
            self.read_cases([passing], {"different-test"})

    def test_truncation_and_validation_errors_rejected_even_with_trace(self):
        for output in (
            *(trace() + marker for marker in gate.TRUNCATION_MARKERS),
            "x" * gate.OUTPUT_LIMIT,
            trace() + "\nVulkan validation error: deliberate fault",
        ):
            with self.subTest(output=output[:80]), self.assertRaises(gate.GateError):
                self.read_cases(
                    [({"name": "device", "status": "run"}, {"system-out": output})]
                )

    def test_live_control_must_fail_the_same_gate(self):
        absent = {
            name: "Presented frames without validation." for name in gate.CONTROL_NAMES
        }
        with contextlib.redirect_stdout(io.StringIO()):
            gate.require_missing_layer_control(absent, LIBRARY)
        for name in gate.CONTROL_NAMES:
            with self.subTest(name=name), self.assertRaises(gate.GateError):
                gate.require_missing_layer_control({**absent, name: trace()}, LIBRARY)
        # Prove the control rejects an accidentally weakened, always-green gate.
        with (
            patch.object(gate, "require_validation"),
            self.assertRaisesRegex(gate.GateError, "did not exercise"),
        ):
            gate.require_missing_layer_control(absent, LIBRARY)


class ConfigurationTests(unittest.TestCase):
    def test_environment_is_child_only_and_does_not_force_validation(self):
        with patch.dict(gate.os.environ, {"KEEP": "value"}, clear=True):
            environment = gate.validation_environment(Path("/layers"), Path("/empty"))
            self.assertEqual(environment["VK_LAYER_PATH"], "/layers")
            self.assertEqual(environment["VK_IMPLICIT_LAYER_PATH"], "/empty")
            self.assertEqual(environment["VK_LOADER_DEBUG"], "layer")
            self.assertNotIn("VK_INSTANCE_LAYERS", environment)
            self.assertNotIn("VK_LAYER_VALIDATE_SYNC", environment)
            self.assertEqual(dict(gate.os.environ), {"KEEP": "value"})

    def test_inherited_override_is_rejected(self):
        with (
            patch.dict(gate.os.environ, {"VK_INSTANCE_LAYERS": gate.LAYER}, clear=True),
            self.assertRaises(gate.GateError),
        ):
            gate.validation_environment(Path("/layers"), Path("/empty"))

    def test_private_manifest_selects_installed_library_without_editing_original(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = (
                root / "share/vulkan/explicit_layer.d/VkLayer_khronos_validation.json"
            )
            manifest.parent.mkdir(parents=True)
            original = json.dumps(
                {
                    "layer": {
                        "name": gate.LAYER,
                        "api_version": "1.4.357",
                        "library_path": "libVkLayer_khronos_validation.so",
                    }
                }
            )
            manifest.write_text(original)
            library = root / "lib/libVkLayer_khronos_validation.so"
            library.parent.mkdir()
            library.touch()
            with contextlib.redirect_stdout(io.StringIO()):
                actual = gate.prepare_layer(root, root / "private")
            copied = json.loads((root / "private" / manifest.name).read_text())
            self.assertEqual(actual, library.resolve())
            self.assertEqual(copied["layer"]["library_path"], str(actual))
            self.assertEqual(manifest.read_text(), original)


@unittest.skipUnless(
    shutil.which("ctest") and shutil.which("cmake"), "CMake is required"
)
class CTestIntegrationTests(unittest.TestCase):
    def test_actual_truncation_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            cmake = Path(shutil.which("cmake")).as_posix()
            (build / "CTestTestfile.cmake").write_text(
                f'add_test("device" "{cmake}" "-E" "echo" "{"x" * 256}")\n'
            )
            with (
                patch.object(gate, "OUTPUT_LIMIT", 128),
                contextlib.redirect_stdout(io.StringIO()),
                self.assertRaisesRegex(gate.GateError, "incomplete output"),
            ):
                gate.run_ctest(
                    build, build, "truncated", dict(gate.os.environ), {"device"}
                )
            xml = (build / "truncated.xml").read_text()
            self.assertTrue(any(marker in xml for marker in gate.TRUNCATION_MARKERS))

    def test_actual_junit_retains_trace_and_missing_layer_control_rejects_green_tests(
        self,
    ):
        # Real CTest output, not hand-written XML: the trace is deliberately past
        # its default 1024-byte passing-output limit. No Vulkan device is used.
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            cmake = Path(shutil.which("cmake")).as_posix()
            ctest_file = build / "CTestTestfile.cmake"
            for phase, output in (
                ("active", "x" * 2048 + "\n" + trace()),
                ("absent", "Completed without validation"),
                ("restored", trace()),
            ):
                commands = [
                    f'add_test("{name}" "{cmake}" "-E" "echo" [=[{output}]=])'
                    for name in sorted(gate.CONTROL_NAMES)
                ]
                ctest_file.write_text("\n".join(commands) + "\n")
                with contextlib.redirect_stdout(io.StringIO()):
                    outputs = gate.run_ctest(
                        build, build, phase, dict(gate.os.environ), gate.CONTROL_NAMES
                    )
                    if phase == "absent":
                        gate.require_missing_layer_control(outputs, LIBRARY)
                    else:
                        gate.require_validation(outputs, gate.CONTROL_NAMES, LIBRARY)


if __name__ == "__main__":
    unittest.main()
