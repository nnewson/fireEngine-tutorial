"""Recheck a saved Stage 7 session without launching the application."""

import argparse
import hashlib
import json
import sys
from pathlib import Path

from benchmark_provenance import condition_issues, observed_conditions
from benchmark_report import (
    CRITICAL,
    D,
    RECORD,
    RESET,
    RESET_SPAN,
    SHADOW,
    SUM_RECORD,
    SUM_RESET,
    MeasurementError,
    parse_report,
    quantities,
    require,
)

SCHEMA = 1
WORKLOADS = (1000, 10000)


def arm_spec(name, draws, mode):
    return {"id": name, "draws": draws, "mode": mode}


def triplet_specs(draws, replacement=False):
    prefix = f"{'retry-' if replacement else ''}{draws}"
    return [
        arm_spec(f"{prefix}-{name}", draws, mode)
        for name, mode in (("A", "one"), ("X", "two"), ("B", "one"))
    ]


def initial_specs():
    # Automatic selection checks are qualification, not members of the matrix.
    return [arm_spec(f"qualify-{n}-auto", n, "auto") for n in WORKLOADS] + [
        arm_spec("one-draw", 1, "one"),
        *triplet_specs(1000),
        *triplet_specs(10000),
        *(arm_spec(f"{n}-direct", n, "direct") for n in WORKLOADS),
    ]


def arguments(spec):
    args = ["--benchmark", str(spec["draws"])]
    if spec["mode"] == "direct":
        args += ["--forward-direct-primary"]
    elif spec["mode"] != "auto":
        args += [
            "--forward-recording-participants",
            "2" if spec["mode"] == "two" else "1",
        ]
    return args


def compare(a, x, b):
    control = (a + b) / 2
    drift = abs(b - a)
    improvement = control - x
    direction = "unresolved"
    if abs(improvement) > drift:
        direction = "improvement" if improvement > 0 else "regression"
    return {
        "A_us": a,
        "X_us": x,
        "B_us": b,
        "control_us": control,
        "drift_us": drift,
        "improvement_us": improvement,
        "reduction": improvement / control if control else None,
        "distance_to_A_us": abs(x - a),
        "distance_to_B_us": abs(x - b),
        "classification": direction,
    }


def comparisons(reports, specs):
    arms = [reports[spec["id"]] for spec in specs]
    return {
        name: compare(*(arm["quantities"][name + "_us"] for arm in arms))
        for name in ("A_phase", "F_phase")
    }


def next_spec(reports):
    """Only primary drift can add a triplet, after all initial arms complete."""
    for spec in initial_specs():
        if spec["id"] not in reports:
            return spec
    for draws in WORKLOADS:
        if (
            comparisons(reports, triplet_specs(draws))["A_phase"]["classification"]
            == "unresolved"
        ):
            for spec in triplet_specs(draws, replacement=True):
                if spec["id"] not in reports:
                    return spec
    return None


def pass_costs(means):
    q = quantities(means)
    shadow, forward, active = (
        q[key] for key in ("S_phase_us", "F_phase_us", "A_phase_us")
    )
    return {
        **q,
        "shadow_reset_us": means[SHADOW[0]],
        "shadow_recording_us": means[SHADOW[1]],
        "shadow_share": shadow / active,
        "serial_measured_pass_us": shadow + forward,
        "idealized_overlapped_us": max(shadow, forward),
        "removable_measured_work_bound_us": min(shadow, forward),
        "measured_active_reduction_bound": min(shadow, forward) / active,
    }


def overlap(means):
    serial = means[SUM_RESET] + means[SUM_RECORD]
    return {
        "critical_us": means[CRITICAL],
        "serial_reset_record_us": serial,
        "complete_overlap_observed": means[CRITICAL] < serial,
        "reset_span_us": means[RESET_SPAN],
        "summed_reset_us": means[SUM_RESET],
        "reset_diagnostic_overlap": means[RESET_SPAN] < means[SUM_RESET],
        "scope": "comparison of reported means; reset-only relation is diagnostic",
    }


def forward_model(control, candidate, f1, f0):
    t1, t2 = quantities(control)["A_phase_us"], quantities(candidate)["A_phase_us"]
    w, r = control[RESET], control[RECORD]
    actual = 1 - t2 / t1
    endpoints = {}
    issues = []
    if f0 > f1:
        issues.append("F0 exceeds F1; fixed-cost bracket is inverted")
    for name, fixed in (("F1", f1), ("F0", f0)):
        variable = w - fixed
        p = (variable + r) / t1
        ideal = p / 2
        if variable < 0:
            issues.append(f"{name}: negative variable-reset work")
        if not 0 < p <= 1:
            issues.append(f"{name}: p outside (0, 1]")
        endpoints[name] = {
            "fixed_reset_us": fixed,
            "variable_reset_us": variable,
            "p": p,
            "ideal_reduction": ideal,
            "materialization": actual / ideal if ideal > 0 else None,
            "predicted_critical_us": fixed + variable / 2 + r / 2,
        }
    return {
        "T1_us": t1,
        "T2_us": t2,
        "W_us": w,
        "R_us": r,
        "F1_us": f1,
        "F0_us": f0,
        "actual_reduction": actual,
        "speedup": t1 / t2,
        "endpoints": endpoints,
        "issues": issues,
    }


def derive(reports):
    require(
        next_spec(reports) is None, "incomplete session; no final triplet selection"
    )
    cells = {}
    for draws in WORKLOADS:
        original_specs = triplet_specs(draws)
        original = comparisons(reports, original_specs)
        retried = original["A_phase"]["classification"] == "unresolved"
        selected_specs = triplet_specs(draws, replacement=retried)
        selected = comparisons(reports, selected_specs)
        a, x, b = (reports[spec["id"]] for spec in selected_specs)
        control = {
            name: (value + b["means"][name]) / 2 for name, value in a["means"].items()
        }
        direct_id = f"{draws}-direct"
        direct = reports[direct_id]
        cells[str(draws)] = {
            "selected_arms": [spec["id"] for spec in selected_specs],
            "ancillary_arms": ["one-draw", direct_id],
            "original_status": "void/context" if retried else "selected",
            "original_comparisons": original,
            "selected_comparisons": selected,
            "descriptive_only": selected["A_phase"]["classification"] == "unresolved",
            "model": forward_model(
                control,
                x["means"],
                reports["one-draw"]["means"][RESET],
                direct["means"][RESET],
            ),
            "one_control": pass_costs(control),
            "two": pass_costs(x["means"]),
            "direct": pass_costs(direct["means"]),
            "overlap": overlap(x["means"]),
        }
    return cells


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def completed_triplets(reports):
    """Visibility for every complete triplet, never partial-session authority."""
    result = []
    for draws in WORKLOADS:
        for replacement in (False, True):
            specs = triplet_specs(draws, replacement)
            if all(spec["id"] in reports for spec in specs):
                result.append(
                    {
                        "draws": draws,
                        "kind": "replacement" if replacement else "original",
                        "arms": [spec["id"] for spec in specs],
                        "comparisons": comparisons(reports, specs),
                        "authority": "non-authoritative failed-session context",
                    }
                )
    return result


def checked_artifacts(directory, entry, name):
    """Missing terminal artifacts must be explicitly sealed as missing."""
    outputs = {}
    for stream in ("stdout", "stderr", "telemetry"):
        path = directory / (
            f"{name}.telemetry.jsonl"
            if stream == "telemetry"
            else f"{name}.{stream}.log"
        )
        expected = (
            entry["telemetry_sha256"]
            if stream == "telemetry"
            else entry["log_sha256"][stream]
        )
        if expected is None:
            require(
                entry["status"] == "rejected" and not path.exists(),
                f"{name}: missing or unsealed {stream} artifact",
            )
            outputs[stream] = None
        else:
            require(digest(path) == expected, f"{name}: changed {stream} artifact")
            outputs[stream] = path.read_text(errors="strict")
    return outputs


def load_session(directory, partial=False):
    """Replay ordering, file integrity, provenance invariants and every report."""
    session = json.loads((directory / "session.json").read_text())
    require(session["schema"] == SCHEMA, "unknown session schema")
    require(
        session["status"] == ("failed" if partial else "complete"),
        "partial replay requires a sealed failed session; normal replay requires completion",
    )
    if partial:
        require(bool(session.get("failure")), "failed session lacks a failure record")
    reports = {}
    metadata = None
    observations = {}
    rejected = []
    provenance = session["provenance"]
    require(
        digest(directory / "build-provenance.json")
        == provenance["build_provenance_sha256"],
        "changed or missing build provenance",
    )
    for index, entry in enumerate(session["arms"]):
        expected = next_spec(reports)
        require(
            expected is not None and entry["spec"] == expected,
            "missing, repeated, reordered or unauthorized arm",
        )
        name = expected["id"]
        terminal = (
            partial
            and index == len(session["arms"]) - 1
            and entry["status"] == "rejected"
        )
        require(
            entry["status"] == "accepted" or terminal,
            f"{name}: rejected/unsealed arm outside failed-session tail",
        )
        require(
            entry["environment"] == provenance["child_environment"],
            f"{name}: mixed environment",
        )
        require(
            entry["before"] == provenance["fingerprints"],
            f"{name}: changed source/binary/configuration before launch",
        )
        command = [provenance["executable"], *arguments(expected)]
        require(entry["command"] == command, f"{name}: wrong command")
        result = entry.get("process")
        if result is not None:
            require(result["command"] == command, f"{name}: wrong command")
        require(
            entry["retry_reason"]
            == json.loads(json_text(retry_reason(reports, expected))),
            f"{name}: inconsistent retry decision",
        )
        # Integrity failures are never treated as a legitimate failed arm.
        outputs = checked_artifacts(directory, entry, name)
        try:
            require(outputs["telemetry"] is not None, f"{name}: no telemetry")
            try:
                observations[name] = observed_conditions(
                    directory / f"{name}.telemetry.jsonl"
                )
            except (ValueError, KeyError, TypeError) as error:
                raise MeasurementError(f"{name}: invalid telemetry: {error}") from error
            issues = condition_issues(observations, provenance["conditions"])
            require(not issues, "; ".join(issues))
            require(
                entry.get("after") == provenance["fingerprints"],
                f"{name}: changed or missing source/binary/configuration after launch",
            )
            require(
                result is not None
                and result["returncode"] == 0
                and "error" not in result,
                f"{name}: unsuccessful or interrupted child",
            )
            require(
                outputs["stdout"] is not None and outputs["stderr"] is not None,
                f"{name}: no completed output",
            )
            report = parse_report(
                outputs["stdout"],
                outputs["stderr"],
                expected["draws"],
                expected["mode"],
                session["role"],
            )
            if metadata is None:
                metadata = report["metadata"]
            require(
                metadata == report["metadata"],
                f"{name}: mixed device/presentation/resource metadata",
            )
        except MeasurementError as error:
            if not terminal:
                raise
            rejected.append({"arm": name, "reason": str(error)})
        else:
            # Even a terminal interruption after validation cannot hide a
            # completed triplet in non-authoritative replay.
            reports[name] = report
    return {
        "schema": SCHEMA,
        "session_id": session["session_id"],
        "role": session["role"],
        "status": session["status"],
        "authority": "non-authoritative failed-session context"
        if partial
        else "complete session; apply registered session-history rule",
        "failure": session.get("failure"),
        "rejected_arms": rejected,
        "decision_bearing": session["role"].startswith("laptop-"),
        "provenance": provenance,
        "metadata": metadata,
        "cells": {} if partial else derive(reports),
        "completed_triplets": completed_triplets(reports) if partial else [],
        "observed_conditions": observations,
        "condition_issues": condition_issues(observations, provenance["conditions"]),
        "reports": reports,
        "limits": [
            "host phase means, not complete pass time or GPU time",
            "minimum of means is a loose measured-work bound, not observed speedup",
            "direct arms are unbracketed attribution controls",
            "unresolved primary results remain unclassified; secondary cannot rescue them",
            "policy verdict requires reviewing both laptop sessions",
        ],
    }


def retry_reason(reports, spec):
    if not spec["id"].startswith("retry-"):
        return None
    return comparisons(reports, triplet_specs(spec["draws"]))["A_phase"]


def json_text(value):
    def encode(item):
        if isinstance(item, D):
            return str(item)
        raise TypeError(f"cannot serialize {type(item)}")

    return json.dumps(value, indent=2, default=encode, allow_nan=False) + "\n"


def markdown(analysis):
    lines = [
        "# Two-pass host rebaseline",
        "",
        f"Role: `{analysis['role']}`.",
        f"Authority: {analysis['authority']}.",
        "",
        "Values below are derived from rounded arithmetic means. Raw phase statistics",
        "and all report shares are retained in results.json and the original logs.",
        "",
    ]
    if analysis["status"] == "failed":
        lines += [
            "FAILED SESSION — NON-AUTHORITATIVE. No cells may be carried into a new session.",
            f"Failure: {analysis['failure']}",
            "",
            "| Completed triplet | Comparison | Classification | Reduction |",
            "| --- | --- | --- | --- |",
        ]
        for triplet in analysis["completed_triplets"]:
            for name, comparison in triplet["comparisons"].items():
                lines.append(
                    f"| {', '.join(triplet['arms'])} | {name} | {comparison['classification']} | {100 * comparison['reduction']:.2f}% |"
                )
        for rejected in analysis["rejected_arms"]:
            lines += ["", f"Not admitted: {rejected['arm']}: {rejected['reason']}."]
        lines += [""]
    lines += [
        "## Observed conditions",
        "",
        f"Declared: {json.dumps(analysis['provenance']['conditions'], sort_keys=True)}",
        "",
        "| Arm | Governors by policy | AC state | Unavailable governor samples |",
        "| --- | --- | --- | --- |",
    ]
    for name, observation in analysis["observed_conditions"].items():
        governors = (
            "; ".join(
                f"{policy}: {', '.join(values)}"
                for policy, values in observation["governors"].items()
            )
            or "unavailable"
        )
        lines.append(
            f"| {name} | {governors} | {', '.join(observation['ac_states'])} | {observation['governor_unavailable_samples']} / {observation['samples']} |"
        )
    lines += [
        "",
        "Condition flags: "
        + (
            "; ".join(analysis["condition_issues"])
            or "no observed contradiction/change; unavailable readings do not prove stability"
        )
        + ".",
        "",
    ]
    for workload, cell in analysis["cells"].items():
        lines += [
            f"## {workload} draws",
            "",
            f"Selected: {', '.join(cell['selected_arms'])}.",
            f"Original: {cell['original_status']}. Ancillary: {', '.join(cell['ancillary_arms'])}.",
            "",
            "| Comparison | A / X / B (us) | Drift (us) | Reduction | Verdict |",
            "| --- | --- | --- | --- | --- |",
        ]
        for name, comparison in cell["selected_comparisons"].items():
            arms = " / ".join(f"{comparison[n + '_us']:.3f}" for n in ("A", "X", "B"))
            lines.append(
                f"| {name} | {arms} | {comparison['drift_us']:.3f} | {100 * comparison['reduction']:.2f}% | {comparison['classification']} |"
            )
        lines += [
            "",
            "| Path | Active (us) | Shadow (us) | Forward (us) | Shadow share | Removable bound (us) |",
            "| --- | --- | --- | --- | --- | --- |",
        ]
        for name in ("one_control", "two", "direct"):
            q = cell[name]
            lines.append(
                f"| {name} | {q['A_phase_us']:.3f} | {q['S_phase_us']:.3f} | {q['F_phase_us']:.3f} | {100 * q['shadow_share']:.2f}% | {q['removable_measured_work_bound_us']:.3f} |"
            )
        model = cell["model"]
        lines += [
            "",
            f"Forward model: T1={model['T1_us']:.3f}, T2={model['T2_us']:.3f}, W={model['W_us']:.3f}, R={model['R_us']:.3f} us.",
            f"Fixed-reset inputs: F1={model['F1_us']:.3f}, F0={model['F0_us']:.3f} us.",
        ]
        for name, endpoint in model["endpoints"].items():
            materialization = endpoint["materialization"]
            rendered = (
                "undefined"
                if materialization is None
                else f"{100 * materialization:.2f}%"
            )
            lines.append(
                f"{name}: p={endpoint['p']:.6f}, ideal reduction={100 * endpoint['ideal_reduction']:.2f}%, materialization={rendered}."
            )
        lines += [
            f"Model issues: {'; '.join(model['issues']) or 'none observed'}.",
            f"Complete participant overlap: {cell['overlap']['complete_overlap_observed']}; reset-only diagnostic: {cell['overlap']['reset_diagnostic_overlap']}.",
        ]
        if cell["descriptive_only"]:
            lines += [
                "Primary remains unresolved: these selected-triplet quantities are descriptive only."
            ]
        lines += [""]
    lines += ["## Limits", "", *[f"- {limit}." for limit in analysis["limits"]], ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    parser.add_argument(
        "--partial",
        action="store_true",
        help="audit a failed session as non-authoritative context",
    )
    parser.add_argument("--format", choices=("json", "markdown"), default="json")
    args = parser.parse_args()
    try:
        analysis = load_session(args.session, partial=args.partial)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"NOT ANALYZED: {error}", file=sys.stderr)
        return 1
    # Read-only replay: callers choose where to save stdout. No old evidence is overwritten.
    print(
        markdown(analysis) if args.format == "markdown" else json_text(analysis), end=""
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
