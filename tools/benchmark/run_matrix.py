"""Acquire the fixed Stage 7 matrix in one sitting; never resume or auto-tune."""

import argparse
import os
import shutil
import sys
import tempfile
import uuid
from datetime import datetime, timezone
from pathlib import Path

from analyze_matrix import (
    SCHEMA,
    arguments,
    digest,
    json_text,
    load_session,
    markdown,
    next_spec,
    retry_reason,
)
from benchmark_provenance import (
    Telemetry,
    fingerprints,
    load_conditions,
    observed_conditions,
    condition_issues,
    measurement_environment,
    provenance,
    snapshot,
)
from benchmark_report import ROLES, parse_report, require
from preflight_lavapipe import ROOT, run_check


def timestamp():
    return datetime.now(timezone.utc).isoformat()


def save_session(output, session):
    # Replace only this newly allocated session's ledger, never a previous run.
    temporary = output / "session.pending.json"
    temporary.write_text(json_text(session))
    temporary.replace(output / "session.json")


def seal_artifacts(output, entry):
    name = entry["spec"]["id"]
    # A handled failure may reach here twice. Never bless changed evidence by
    # replacing a hash that was already sealed before the failure.
    if "log_sha256" not in entry:
        entry["log_sha256"] = {
            stream: digest(path)
            if (path := output / f"{name}.{stream}.log").is_file()
            else None
            for stream in ("stdout", "stderr")
        }
    if "telemetry_sha256" not in entry:
        path = output / f"{name}.telemetry.jsonl"
        entry["telemetry_sha256"] = digest(path) if path.is_file() else None


def acquire(
    output,
    session,
    executable,
    environment,
    current_fingerprints,
    run=run_check,
    telemetry=Telemetry,
    timeout=1200,
):
    """Dependencies are injectable for finite, device-free failure controls."""
    reports = {}
    metadata = None
    observations = {}
    save_session(output, session)
    try:
        while (spec := next_spec(reports)) is not None:
            name = spec["id"]
            before = current_fingerprints()
            require(
                before == session["provenance"]["fingerprints"],
                "source/build changed before arm",
            )
            entry = {
                "spec": spec,
                "status": "attempted",
                "command": [str(executable), *arguments(spec)],
                "retry_reason": retry_reason(reports, spec),
                "environment": snapshot(environment),
                "before": before,
                "started_utc": timestamp(),
            }
            require(
                entry["environment"] == session["provenance"]["child_environment"],
                "environment changed before arm",
            )
            session["arms"].append(entry)
            save_session(output, session)
            print(f"Running {name}", flush=True)
            with telemetry(output / f"{name}.telemetry.jsonl"):
                result = run(
                    name,
                    entry["command"],
                    output,
                    environment,
                    timeout,
                )
                entry["process"] = result
                save_session(output, session)
            entry["finished_utc"] = timestamp()
            seal_artifacts(output, entry)
            entry["after"] = current_fingerprints()
            save_session(output, session)
            observations[name] = observed_conditions(output / f"{name}.telemetry.jsonl")
            issues = condition_issues(observations, session["provenance"]["conditions"])
            require(not issues, "; ".join(issues))
            require(entry["after"] == before, "source/build changed during arm")
            require(
                result["returncode"] == 0 and "error" not in result,
                f"{name}: {result.get('error', 'child failed')}",
            )
            report = parse_report(
                (output / f"{name}.stdout.log").read_text(),
                (output / f"{name}.stderr.log").read_text(),
                spec["draws"],
                spec["mode"],
                session["role"],
            )
            if metadata is None:
                metadata = report["metadata"]
            require(
                metadata == report["metadata"],
                f"{name}: mixed device/presentation/resource metadata",
            )
            reports[name] = report
            entry["status"] = "accepted"
            save_session(output, session)
        session.update(status="complete", finished_utc=timestamp())
        save_session(output, session)
        # Exercise exactly the same raw-log replay that reviewers will use offline.
        analysis = load_session(output)
        (output / "results.json").write_text(json_text(analysis))
        (output / "results.md").write_text(markdown(analysis))
    except (Exception, KeyboardInterrupt) as error:
        if session["arms"] and session["arms"][-1]["status"] == "attempted":
            entry = session["arms"][-1]
            entry["status"] = "rejected"
            seal_artifacts(output, entry)
        session.update(
            status="failed",
            failure=str(error) or type(error).__name__,
            finished_utc=timestamp(),
        )
        save_session(output, session)
        # Keep earlier unfavorable triplets visible; no authority or resumption.
        try:
            partial = load_session(output, partial=True)
            (output / "partial-results.json").write_text(json_text(partial))
            (output / "partial-results.md").write_text(markdown(partial))
        except (OSError, ValueError, KeyError, TypeError) as replay_error:
            (output / "partial-replay-error.txt").write_text(str(replay_error))
        raise
    return analysis


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--role", choices=ROLES, required=True)
    parser.add_argument(
        "--icd", type=Path, help="one explicitly selected ICD; required for Linux roles"
    )
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build-benchmark")
    parser.add_argument(
        "--conditions",
        type=Path,
        required=True,
        help="reviewed operator conditions JSON",
    )
    parser.add_argument("--output-root", type=Path, default=ROOT / "build-evidence")
    args = parser.parse_args()
    output = None
    try:
        require(
            sys.platform in ("linux", "darwin") and os.geteuid() != 0,
            "use a normal desktop user on Linux/macOS",
        )
        require(
            (args.role == "mac-kosmickrisp") == (sys.platform == "darwin"),
            "role/platform mismatch",
        )
        build = args.build_dir.resolve(strict=True)
        icd = args.icd.resolve(strict=True) if args.icd else None
        require(
            icd is not None or args.role == "mac-kosmickrisp",
            "select the session's ICD explicitly",
        )
        if icd is not None:
            require(
                icd.is_file() and ":" not in str(icd),
                "--icd must name one manifest file",
            )
        conditions = load_conditions(args.conditions, args.role)
        inherited = os.environ.copy()
        environment = measurement_environment(inherited, icd)
        args.output_root.mkdir(parents=True, exist_ok=True)
        output = Path(
            tempfile.mkdtemp(prefix=f"matrix-{args.role}-", dir=args.output_root)
        ).resolve()
        print(f"Evidence: {output}", flush=True)
        files, metadata = provenance(
            build, icd, conditions, inherited, environment, output
        )
        metadata["build_provenance_sha256"] = digest(output / "build-provenance.json")
        # Optional, read-only display context, outside the timed acquisition.
        if (
            sys.platform == "linux"
            and environment.get("DISPLAY")
            and shutil.which("xrandr")
        ):
            metadata["xrandr_providers"] = run_check(
                "xrandr-providers",
                [shutil.which("xrandr"), "--listproviders"],
                output,
                environment,
                10,
            )
        else:
            metadata["xrandr_providers"] = {
                "unavailable": "not an available X11 provider query"
            }
        if sys.platform == "darwin":
            metadata["mac_cpu"] = run_check(
                "cpu-brand",
                ["/usr/sbin/sysctl", "-n", "machdep.cpu.brand_string"],
                output,
                environment,
                10,
            )
        session = {
            "schema": SCHEMA,
            "session_id": str(uuid.uuid4()),
            "started_utc": timestamp(),
            "role": args.role,
            "status": "running",
            "provenance": metadata,
            "arms": [],
        }
        analysis = acquire(
            output,
            session,
            files["executable"],
            environment,
            lambda: fingerprints(files),
        )
        print(
            f"Acquisition complete: {len(session['arms']) - 2} matrix arms plus 2 automatic qualification arms."
        )
        for workload, cell in analysis["cells"].items():
            print(
                f"{workload}: primary {cell['selected_comparisons']['A_phase']['classification']}; selected {', '.join(cell['selected_arms'])}"
            )
        print(
            "Review results.md and raw evidence. No automatic policy or Stage 7 verdict."
        )
        return 0
    except (Exception, KeyboardInterrupt) as error:
        print(f"NOT COMPLETE: {error or type(error).__name__}", file=sys.stderr)
        if output is not None:
            (output / "failure.txt").write_text(str(error) or type(error).__name__)
            print(f"Keep partial evidence: {output}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
