#!/usr/bin/env python3
"""Offline, deterministic adapter for MANUALLY SUPPLIED ADJUDICATED facts.

This is not the browser sandbox or an official/full match simulation. No capture,
clock sampling, damage attribution or referee implementation lives here.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
HARNESS_VERSION = "rmuc2027-adjudicated-harness.1"
SCENARIO_SCHEMA = "rmuc2027-adjudicated-scenario-v1"
LOG_SCHEMA = "rmuc2027-adjudicated-event-log-v1"
PROTOCOL = "rmuc2027-scenario-driver-v1"
FACT_STATUS = "MANUALLY_SUPPLIED_ADJUDICATED_FACTS"
ORDERING = "listed_atomic_boundaries_credits_before_snapshot"
TIMING = "explicit_milliseconds_no_sampling_or_accrual"
MAX_EVENTS = 4096
MAX_CREDITS = 8192
MAX_SCENARIO_BYTES = 8 * 1024 * 1024
MAX_LOG_BYTES = 32 * 1024 * 1024
I64_MAX = 2**63 - 1
U64_MAX = 2**64 - 1
POSITIONS = ("red_outpost", "blue_outpost", "red_base", "blue_base")
SOURCE_FILES = (
    "rules/rmuc2027_v1/match_rules.hpp",
    "rules/rmuc2027_v1/match_rules.cpp",
    "rules/rmuc2027_v1/provenance.json",
    "scenario_harness/driver.cpp",
    "scenario_harness/cli.py",
    "scenario_harness/FORMAT.md",
)


class HarnessError(ValueError):
    pass


def canonical(value) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True,
                      allow_nan=False).encode("ascii")


def digest(value) -> str:
    return hashlib.sha256(canonical(value)).hexdigest()


def _pairs(pairs):
    obj = {}
    for key, value in pairs:
        if key in obj:
            raise HarnessError(f"duplicate JSON key: {key}")
        obj[key] = value
    return obj


def _integer(token):
    # Reject pathological numeric strings before Python's arbitrary-size conversion.
    if len(token.lstrip("-")) > 20:
        raise HarnessError("JSON integer has more than 20 digits")
    return int(token)


def _non_integer(token):
    raise HarnessError(f"non-integer JSON number is unsupported: {token[:40]}")


def decode(data: bytes, limit: int):
    if len(data) > limit:
        raise HarnessError(f"JSON exceeds {limit} byte resource limit")
    try:
        value = json.loads(data.decode("utf-8"), object_pairs_hook=_pairs,
                           parse_int=_integer, parse_float=_non_integer,
                           parse_constant=_non_integer)
        pending = [(value, 1)]
        while pending:
            node, depth = pending.pop()
            if isinstance(node, (dict, list)):
                if depth > 32:
                    raise HarnessError("JSON nesting exceeds 32 levels")
                children = node.values() if isinstance(node, dict) else node
                pending.extend((child, depth+1) for child in children if isinstance(child, (dict, list)))
        return value
    except (UnicodeError, json.JSONDecodeError, RecursionError) as exc:
        raise HarnessError(f"invalid JSON: {exc}") from exc


def load(path, limit):
    with pathlib.Path(path).open("rb") as stream:
        return decode(stream.read(limit + 1), limit)


def keys(obj, required, where):
    if not isinstance(obj, dict):
        raise HarnessError(f"{where}: expected object")
    if set(obj) != set(required):
        missing, extra = set(required) - set(obj), set(obj) - set(required)
        raise HarnessError(f"{where}: missing keys {sorted(missing)}, unknown keys {sorted(extra)}")


def text_value(value, where, maximum=2048):
    if not isinstance(value, str) or not value.strip() or len(value) > maximum:
        raise HarnessError(f"{where}: expected nonempty string, at most {maximum} characters")
    try:
        value.encode("utf-8")
    except UnicodeError as exc:
        raise HarnessError(f"{where}: invalid Unicode") from exc


def integer(value, where, minimum=0, maximum=I64_MAX):
    if type(value) is not int or not minimum <= value <= maximum:
        raise HarnessError(f"{where}: expected integer in [{minimum}, {maximum}], not boolean")


def enum(value, choices, where):
    if not isinstance(value, str) or value not in choices:
        raise HarnessError(f"{where}: expected one of {list(choices)}")


def validate_scenario(s):
    keys(s, ("schema", "scenario_id", "description", "fact_status", "ordering_policy",
             "timing_policy", "control_source", "events"), "scenario")
    enum(s["schema"], (SCENARIO_SCHEMA,), "schema")
    text_value(s["scenario_id"], "scenario_id", 80)
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", s["scenario_id"]):
        raise HarnessError("scenario_id: use ASCII letters, digits, dot, underscore or hyphen")
    text_value(s["description"], "description")
    enum(s["fact_status"], (FACT_STATUS,), "fact_status")
    enum(s["ordering_policy"], (ORDERING,), "ordering_policy")
    enum(s["timing_policy"], (TIMING,), "timing_policy")
    enum(s["control_source"], ("external_totals", "manual_one_second_credits"), "control_source")
    manual = s["control_source"] == "manual_one_second_credits"
    if not isinstance(s["events"], list) or not 1 <= len(s["events"]) <= MAX_EVENTS:
        raise HarnessError(f"events: expected 1..{MAX_EVENTS} boundaries")
    previous_time, credit_count = -1, 0
    for index, e in enumerate(s["events"], 1):
        where = f"event {index}"
        keys(e, ("sequence", "elapsed_ms", "source_note", "credits", "snapshot"), where)
        integer(e["sequence"], f"{where}.sequence", index, index)
        integer(e["elapsed_ms"], f"{where}.elapsed_ms", 0, 420000)
        if e["elapsed_ms"] < previous_time:
            raise HarnessError(f"{where}: boundary time regression")
        previous_time = e["elapsed_ms"]
        text_value(e["source_note"], f"{where}.source_note")
        if not isinstance(e["credits"], list):
            raise HarnessError(f"{where}.credits: expected list")
        credit_count += len(e["credits"])
        if credit_count > MAX_CREDITS:
            raise HarnessError(f"credits: exceeds {MAX_CREDITS} resource limit")
        if not manual and e["credits"]:
            raise HarnessError(f"{where}: external_totals prohibits manual credits")
        for ci, c in enumerate(e["credits"], 1):
            cw = f"{where}.credit {ci}"
            keys(c, ("sequence", "control"), cw)
            integer(c["sequence"], f"{cw}.sequence", 1, U64_MAX)
            keys(c["control"], POSITIONS, f"{cw}.control")
            for pos in POSITIONS:
                pc = c["control"][pos]
                keys(pc, ("controller", "degree"), f"{cw}.{pos}")
                enum(pc["controller"], ("red", "blue"), f"{cw}.{pos}.controller")
                enum(pc["degree"], ("temporary", "full"), f"{cw}.{pos}.degree")
        snap = e["snapshot"]
        keys(snap, ("red", "blue", "referee_ruling", "simultaneous_destruction_resolved"), f"{where}.snapshot")
        enum(snap["referee_ruling"], ("none", "round_forfeit", "other"), f"{where}.referee_ruling")
        if type(snap["simultaneous_destruction_resolved"]) is not bool:
            raise HarnessError(f"{where}.simultaneous_destruction_resolved: expected boolean")
        for team in ("red", "blue"):
            required = ["base_hp", "attack_damage", "surviving_robot_hp"]
            if not manual:
                required.append("control_score")
            keys(snap[team], required, f"{where}.{team}")
            integer(snap[team]["base_hp"], f"{where}.{team}.base_hp")
            for name in required:
                value = snap[team][name]
                if name != "base_hp" and value is not None:
                    integer(value, f"{where}.{team}.{name}")
    return s


def source_identity():
    files = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in SOURCE_FILES}
    provenance = load(ROOT / "rules/rmuc2027_v1/provenance.json", MAX_SCENARIO_BYTES)
    return {
        "harness_version": HARNESS_VERSION,
        "scenario_schema": SCENARIO_SCHEMA,
        "log_schema": LOG_SCHEMA,
        "implementation_sha256": digest(files),
        "source_files_sha256": files,
        "module_provenance": provenance,
    }


def expected_driver(identity):
    p = identity["module_provenance"]
    return {"protocol": PROTOCOL, "implementation_sha256": identity["implementation_sha256"],
            "module_version": p["module_version"], "source_version": "RMUC2027_CN_V1.0.0_20261015",
            "source_sha256": p["source"]["sha256"]}


def protocol(s):
    def val(value):
        return "?" if value is None else str(value)
    lines = [f"BEGIN {PROTOCOL} {s['control_source']} {len(s['events'])}"]
    for e in s["events"]:
        snap = e["snapshot"]
        tokens = ["BOUNDARY", str(e["sequence"]), str(e["elapsed_ms"]), str(len(e["credits"])),
                  snap["referee_ruling"], str(int(snap["simultaneous_destruction_resolved"]))]
        for team in ("red", "blue"):
            t = snap[team]
            tokens.extend(val(t.get(k)) for k in ("base_hp", "control_score", "attack_damage", "surviving_robot_hp"))
        lines.append(" ".join(tokens))
        for c in e["credits"]:
            tokens = ["CREDIT", str(c["sequence"])]
            for name in POSITIONS:
                tokens.extend((c["control"][name]["controller"], c["control"][name]["degree"]))
            lines.append(" ".join(tokens))
    return ("\n".join(lines) + "\nEND\n").encode("ascii")


def invoke(driver, args=(), data=None):
    try:
        result = subprocess.run([str(driver), *args], input=data, capture_output=True,
                                check=False, timeout=15)
    except subprocess.TimeoutExpired as exc:
        raise HarnessError("native driver timed out; scenario is invalid, not a match loss") from exc
    except OSError as exc:
        raise HarnessError(f"cannot start native driver; run bash tools/build.sh: {exc}") from exc
    if result.returncode:
        raise HarnessError(f"native driver rejected protocol (exit {result.returncode}): "
                           + result.stderr.decode("utf-8", errors="replace").strip())
    return decode(result.stdout, MAX_LOG_BYTES)


def run_scenario(s, driver=None):
    validate_scenario(s)
    identity = source_identity()
    driver = pathlib.Path(driver) if driver is not None else ROOT / "build/rmuc_scenario_driver"
    native_identity = invoke(driver, ("--identity",))
    if native_identity != expected_driver(identity):
        raise HarnessError("BuildIdentityMismatch: native driver and current sources/provenance differ; rebuild first")
    result = invoke(driver, data=protocol(s))
    if result.get("driver") != native_identity or len(result.get("records", [])) != len(s["events"]):
        raise HarnessError("DriverResponseMismatch: driver identity or boundary count changed")
    identity["driver"] = native_identity
    records = []
    for e, r in zip(s["events"], result["records"]):
        if r["sequence"] != e["sequence"]:
            raise HarnessError("DriverResponseMismatch: boundary sequence changed")
        records.append({"supplied": e, **r})
    rejected = sum(r["status"] == "rejected" for r in records)
    log = {
        "schema": LOG_SCHEMA,
        "identity": identity,
        "scenario": s,
        "scenario_sha256": digest(s),
        "records": records,
        "final_state": result["final_state"],
        "summary": {"accepted_boundaries": len(records)-rejected, "rejected_boundaries": rejected,
                    "terminal": result["final_state"]["terminal"]},
    }
    log["log_sha256"] = digest(log)
    return log


def validate_log(log):
    keys(log, ("schema", "identity", "scenario", "scenario_sha256", "records", "final_state", "summary", "log_sha256"), "log")
    enum(log["schema"], (LOG_SCHEMA,), "log.schema")
    body = {k: v for k, v in log.items() if k != "log_sha256"}
    if log["log_sha256"] != digest(body):
        raise HarnessError("LogDigestMismatch: log has changed")
    validate_scenario(log["scenario"])
    if log["scenario_sha256"] != digest(log["scenario"]):
        raise HarnessError("ScenarioDigestMismatch: scenario has changed")
    return log


def verify_log(log, driver=None):
    validate_log(log)
    reproduced = run_scenario(log["scenario"], driver)
    if canonical(reproduced) != canonical(log):
        raise HarnessError("ReplayMismatch: recorded identity, decisions or state differ from this build")
    return reproduced


def write_json(value, output, limit):
    data = json.dumps(value, ensure_ascii=True, sort_keys=True, indent=2, allow_nan=False) + "\n"
    # ASCII escaping/indentation can expand valid UTF-8 input substantially.
    # Measure the exact emitted representation before opening/writing anything.
    if len(data.encode("ascii")) > limit:
        raise HarnessError(f"OutputResourceLimit: serialized JSON exceeds {limit} bytes; no output written")
    if output == "-":
        sys.stdout.write(data)
    else:
        # Never silently overwrite the scenario, a previous log, or another file.
        with pathlib.Path(output).open("x", encoding="utf-8") as stream:
            stream.write(data)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("build-id", help="print current source identity for compiling the native driver")
    for name in ("run", "verify", "extract"):
        cmd = commands.add_parser(name)
        cmd.add_argument("input", help="scenario JSON for run; event-log JSON otherwise")
        if name != "verify":
            cmd.add_argument("--output", default="-", help="new output file (never overwritten), or - for stdout")
    args = parser.parse_args(argv)
    try:
        if args.command == "build-id":
            print(source_identity()["implementation_sha256"])
            return 0
        if args.command == "run":
            log = run_scenario(load(args.input, MAX_SCENARIO_BYTES))
            write_json(log, args.output, MAX_LOG_BYTES)
            return 1 if log["summary"]["rejected_boundaries"] else 0
        log = load(args.input, MAX_LOG_BYTES)
        if args.command == "verify":
            verify_log(log)
            print(f"VERIFIED {log['scenario']['scenario_id']}: {len(log['records'])} boundaries; log {log['log_sha256']}")
        else:
            # Extraction verifies the recorded execution too, not just its checksum.
            verify_log(log)
            write_json(log["scenario"], args.output, MAX_SCENARIO_BYTES)
        return 0
    except (HarnessError, OSError, RecursionError) as exc:
        print(json.dumps({"error": type(exc).__name__, "message": str(exc)}, ensure_ascii=True), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
