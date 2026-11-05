#!/usr/bin/env python3
"""Offline logical-capture/control/credit/judge experiment, not full or official RMUC."""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
ADAPTER_VERSION = "rmuc2027-integrated-adapter.1"
SCENARIO_SCHEMA = "rmuc2027-integrated-scenario-v1"
LOG_SCHEMA = "rmuc2027-integrated-event-log-v1"
CHECKPOINT_SCHEMA = "rmuc2027-integrated-prefix-checkpoint-v1"
CONTINUATION_SCHEMA = "rmuc2027-integrated-continuation-v1"
PROTOCOL = "rmuc2027-integrated-driver-v1"
FACT_STATUS = "MANUALLY_SUPPLIED_ADJUDICATED_FACTS"
PHASE_POLICY = "ENGINEERING-NOT-OFFICIAL-capture-continuation-credit-judge-v1"
CAPTURE_POLICY = "strict-reject-unadjudicated-v1"
TIMING_POLICY = "explicit_milliseconds_external_credits_no_sampling-v1"
WORLD_CONTRACT = "EXTERNAL_FACTS_COMPLETE_NO_UNLISTED_CONTROL_TRANSITIONS"
CONTINUATION_ASSERTION = "TEMPORARY_CONTROL_UNCHANGED_NO_RESTORATION_OR_OUTPOST_LIFECYCLE_TRANSITION"
MAX_EVENTS, MAX_CREDITS = 512, 4096
MAX_SCENARIO_BYTES, MAX_LOG_BYTES = 8*1024*1024, 32*1024*1024
MAX_CHECKPOINT_BYTES = MAX_LOG_BYTES
I64_MAX, U64_MAX = 2**63-1, 2**64-1
POSITIONS = ("red_outpost", "blue_outpost", "red_base", "blue_base")
SOURCE_FILES = (
    "rules/rmuc2027_capture_v1/capture_rules.hpp", "rules/rmuc2027_capture_v1/capture_rules.cpp",
    "rules/rmuc2027_capture_v1/provenance.json", "rules/rmuc2027_v1/match_rules.hpp",
    "rules/rmuc2027_v1/match_rules.cpp", "rules/rmuc2027_v1/provenance.json",
    "integrated_scenario/driver.cpp", "integrated_scenario/cli.py",
    "integrated_scenario/FORMAT.md", "integrated_scenario/provenance.json",
)

class IntegrationError(ValueError):
    pass


def canonical(value):
    try:
        return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True,
                          allow_nan=False).encode("ascii")
    except (TypeError, ValueError, RecursionError, UnicodeError) as exc:
        raise IntegrationError(f"invalid JSON value: {exc}") from exc


def digest(value):
    return hashlib.sha256(canonical(value)).hexdigest()


def _pairs(pairs):
    result = {}
    for k, v in pairs:
        if k in result:
            raise IntegrationError(f"duplicate JSON key: {k}")
        result[k] = v
    return result


def _integer(token):
    if len(token.lstrip("-")) > 20:
        raise IntegrationError("JSON integer has more than 20 digits")
    return int(token)


def _noninteger(token):
    raise IntegrationError(f"non-integer JSON number is unsupported: {token[:40]}")


def decode(data, limit):
    if len(data) > limit:
        raise IntegrationError(f"JSON exceeds {limit} byte resource limit")
    try:
        value = json.loads(data.decode("utf-8"), object_pairs_hook=_pairs,
                           parse_int=_integer, parse_float=_noninteger, parse_constant=_noninteger)
        pending = [(value, 1)]
        while pending:
            node, depth = pending.pop()
            if isinstance(node, (dict, list)):
                if depth > 32:
                    raise IntegrationError("JSON nesting exceeds 32 levels")
                children = node.values() if isinstance(node, dict) else node
                pending.extend((child, depth+1) for child in children if isinstance(child, (dict, list)))
        return value
    except (UnicodeError, json.JSONDecodeError, RecursionError) as exc:
        raise IntegrationError(f"invalid JSON: {exc}") from exc


def load(path, limit):
    with pathlib.Path(path).open("rb") as stream:
        return decode(stream.read(limit+1), limit)


def detached(value, limit):
    """API has the same bounded JSON boundary as files; never retain caller aliases."""
    try:
        data = json.dumps(value, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode("utf-8")
    except (ValueError, TypeError, RecursionError, UnicodeError) as exc:
        raise IntegrationError(f"invalid JSON API value: {exc}") from exc
    return decode(data, limit)


def keys(obj, required, where):
    if not isinstance(obj, dict) or any(not isinstance(k, str) for k in obj):
        raise IntegrationError(f"{where}: expected object with string keys")
    if set(obj) != set(required):
        raise IntegrationError(f"{where}: missing keys {sorted(set(required)-set(obj))}, unknown keys {sorted(set(obj)-set(required))}")


def text_value(v, where, maximum=2048):
    if not isinstance(v, str) or not v.strip() or len(v) > maximum:
        raise IntegrationError(f"{where}: expected nonempty string, at most {maximum} characters")
    try:
        v.encode("utf-8")
    except UnicodeError as exc:
        raise IntegrationError(f"{where}: invalid Unicode") from exc


def integer(v, where, minimum=0, maximum=I64_MAX):
    if type(v) is not int or not minimum <= v <= maximum:
        raise IntegrationError(f"{where}: expected integer in [{minimum}, {maximum}], not boolean")


def enum(v, choices, where):
    if not isinstance(v, str) or v not in choices:
        raise IntegrationError(f"{where}: expected one of {list(choices)}")


def boolean(v, where):
    if type(v) is not bool:
        raise IntegrationError(f"{where}: expected boolean")


def logical(v, where):
    keys(v, ("red", "blue"), where)
    for team in ("red", "blue"):
        boolean(v[team], f"{where}.{team}")


def _validate_scenario(s):
    keys(s, ("schema", "scenario_id", "description", "fact_status", "phase_policy", "capture_policy",
             "timing_policy", "world_contract", "initial_logical", "events"), "scenario")
    for field, expected in (("schema", SCENARIO_SCHEMA), ("fact_status", FACT_STATUS),
                            ("phase_policy", PHASE_POLICY), ("capture_policy", CAPTURE_POLICY),
                            ("timing_policy", TIMING_POLICY), ("world_contract", WORLD_CONTRACT)):
        enum(s[field], (expected,), field)
    text_value(s["scenario_id"], "scenario_id", 80)
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", s["scenario_id"]):
        raise IntegrationError("scenario_id: expected ASCII letters/digits/dot/underscore/hyphen")
    text_value(s["description"], "description")
    keys(s["initial_logical"], POSITIONS, "initial_logical")
    for p in POSITIONS:
        logical(s["initial_logical"][p], f"initial_logical.{p}")
    if not isinstance(s["events"], list) or not 1 <= len(s["events"]) <= MAX_EVENTS:
        raise IntegrationError(f"events: expected 1..{MAX_EVENTS} boundaries")
    previous, total_credits = 0, 0
    for index, e in enumerate(s["events"], 1):
        w = f"event {index}"
        keys(e, ("sequence", "elapsed_ms", "source_note", "logical_inputs", "continuations", "credits", "snapshot"), w)
        integer(e["sequence"], w+".sequence", index, index)
        integer(e["elapsed_ms"], w+".elapsed_ms", 1, 420000)
        if e["elapsed_ms"] <= previous:
            raise IntegrationError(f"{w}: time must be strictly increasing across all attempts")
        previous = e["elapsed_ms"]
        text_value(e["source_note"], w+".source_note")
        keys(e["logical_inputs"], POSITIONS, w+".logical_inputs")
        keys(e["continuations"], POSITIONS, w+".continuations")
        for p in POSITIONS:
            if e["logical_inputs"][p] is not None:
                logical(e["logical_inputs"][p], w+".logical_inputs."+p)
            continuation = e["continuations"][p]
            if continuation is not None:
                cw = w+".continuations."+p
                keys(continuation, ("from_ms", "through_ms", "assertion", "source_note"), cw)
                integer(continuation["from_ms"], cw+".from_ms", 0, 420000)
                integer(continuation["through_ms"], cw+".through_ms", 0, 420000)
                enum(continuation["assertion"], (CONTINUATION_ASSERTION,), cw+".assertion")
                text_value(continuation["source_note"], cw+".source_note")
        if not isinstance(e["credits"], list):
            raise IntegrationError(w+".credits: expected array")
        total_credits += len(e["credits"])
        if total_credits > MAX_CREDITS:
            raise IntegrationError(f"credits exceeds {MAX_CREDITS} resource limit")
        for cr in e["credits"]:
            keys(cr, ("sequence", "source_note"), w+".credit")
            integer(cr["sequence"], w+".credit.sequence", 1, U64_MAX)
            text_value(cr["source_note"], w+".credit.source_note")
        snap = e["snapshot"]
        keys(snap, ("red", "blue", "referee_ruling", "simultaneous_destruction_resolved"), w+".snapshot")
        enum(snap["referee_ruling"], ("none", "round_forfeit", "other"), w+".referee_ruling")
        boolean(snap["simultaneous_destruction_resolved"], w+".simultaneous_destruction_resolved")
        for team in ("red", "blue"):
            t = snap[team]
            keys(t, ("base_hp", "attack_damage", "surviving_robot_hp"), w+"."+team)
            integer(t["base_hp"], w+"."+team+".base_hp")
            for field in ("attack_damage", "surviving_robot_hp"):
                if t[field] is not None:
                    integer(t[field], w+"."+team+"."+field)
    return s


def validate_scenario(s):
    return _validate_scenario(detached(s, MAX_SCENARIO_BYTES))


def source_identity():
    files = {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in SOURCE_FILES}
    return {"adapter_version": ADAPTER_VERSION, "scenario_schema": SCENARIO_SCHEMA,
            "log_schema": LOG_SCHEMA, "checkpoint_schema": CHECKPOINT_SCHEMA,
            "continuation_schema": CONTINUATION_SCHEMA, "source_files_sha256": files,
            "implementation_sha256": digest(files),
            "integration_provenance": load(ROOT/"integrated_scenario/provenance.json", MAX_SCENARIO_BYTES),
            "capture_provenance": load(ROOT/"rules/rmuc2027_capture_v1/provenance.json", MAX_SCENARIO_BYTES),
            "match_provenance": load(ROOT/"rules/rmuc2027_v1/provenance.json", MAX_SCENARIO_BYTES)}


def expected_driver(identity):
    return {"protocol": PROTOCOL, "implementation_sha256": identity["implementation_sha256"],
            "capture_module_version": identity["capture_provenance"]["module_version"],
            "match_module_version": identity["match_provenance"]["module_version"],
            "capture_source_sha256": identity["capture_provenance"]["source"]["sha256"],
            "match_source_sha256": identity["match_provenance"]["source"]["sha256"]}


def protocol(s):
    def value(v):
        return "?" if v is None else str(v)
    lines = [f"BEGIN {PROTOCOL} {PHASE_POLICY} {len(s['events'])}"]
    lines.append("INITIAL "+" ".join(str(int(s["initial_logical"][p][team])) for p in POSITIONS for team in ("red", "blue")))
    for e in s["events"]:
        snap = e["snapshot"]
        parts = ["BOUNDARY", str(e["sequence"]), str(e["elapsed_ms"]), str(len(e["credits"])),
                 snap["referee_ruling"], str(int(snap["simultaneous_destruction_resolved"]))]
        for team in ("red", "blue"):
            parts.extend(value(snap[team][field]) for field in ("base_hp", "attack_damage", "surviving_robot_hp"))
        lines.append(" ".join(parts))
        for p in POSITIONS:
            parts = ["POSITION", p]
            logical_input, continuation = e["logical_inputs"][p], e["continuations"][p]
            parts.extend(["none"] if logical_input is None else ["logical", str(int(logical_input["red"])), str(int(logical_input["blue"]))])
            parts.extend(["none"] if continuation is None else ["continuation", continuation["assertion"], str(continuation["from_ms"]), str(continuation["through_ms"])])
            lines.append(" ".join(parts))
        lines.extend(f"CREDIT {cr['sequence']}" for cr in e["credits"])
    return ("\n".join(lines)+"\nEND\n").encode("ascii")


def invoke(driver, args=(), data=None):
    try:
        result = subprocess.run([str(driver), *args], input=data, capture_output=True, check=False, timeout=15)
    except subprocess.TimeoutExpired as exc:
        raise IntegrationError("native driver timeout; invalid experiment, not a match loss") from exc
    except OSError as exc:
        raise IntegrationError(f"cannot start driver; run bash tools/build.sh: {exc}") from exc
    if result.returncode:
        raise IntegrationError(f"native driver rejected protocol (exit {result.returncode}): "+result.stderr.decode("utf-8", errors="replace").strip())
    return decode(result.stdout, MAX_LOG_BYTES)


def run_scenario(s, driver=None):
    s = validate_scenario(s)
    identity = source_identity()
    driver = pathlib.Path(driver) if driver is not None else ROOT/"build/rmuc_integrated_driver"
    native = invoke(driver, ("--identity",))
    if native != expected_driver(identity):
        raise IntegrationError("BuildIdentityMismatch: compiled driver differs from current source/provenance; rebuild first")
    result = invoke(driver, data=protocol(s))
    if result.get("driver") != native or len(result.get("records", [])) != len(s["events"]):
        raise IntegrationError("DriverResponseMismatch: identity or boundary count changed")
    identity["driver"] = native
    records = []
    for e, r in zip(s["events"], result["records"]):
        if r["sequence"] != e["sequence"]:
            raise IntegrationError("DriverResponseMismatch: boundary sequence changed")
        records.append({"supplied": e, **r})
    rejected = sum(r["status"] == "rejected" for r in records)
    log = {"schema": LOG_SCHEMA, "identity": identity, "scenario": s, "scenario_sha256": digest(s),
           "initial_state": result["initial_state"], "records": records, "final_state": result["final_state"],
           "summary": {"accepted_boundaries": len(records)-rejected, "rejected_boundaries": rejected,
                       "terminal": result["final_state"]["terminal"]}}
    log["log_sha256"] = digest(log)
    return log


def validate_log(log):
    keys(log, ("schema", "identity", "scenario", "scenario_sha256", "initial_state", "records", "final_state", "summary", "log_sha256"), "log")
    enum(log["schema"], (LOG_SCHEMA,), "log.schema")
    if log["log_sha256"] != digest({k: v for k, v in log.items() if k != "log_sha256"}):
        raise IntegrationError("LogDigestMismatch: log has changed")
    validate_scenario(log["scenario"])
    if log["scenario_sha256"] != digest(log["scenario"]):
        raise IntegrationError("ScenarioDigestMismatch: scenario has changed")
    return log


def verify_log(log, driver=None):
    log = validate_log(detached(log, MAX_LOG_BYTES))
    reproduced = run_scenario(log["scenario"], driver)
    if canonical(reproduced) != canonical(log):
        raise IntegrationError("ReplayMismatch: identity, decisions, traces or state differ")
    return reproduced


def make_checkpoint(log, prefix_count, driver=None):
    log = verify_log(log, driver)
    integer(prefix_count, "prefix_count", 1, len(log["records"]))
    prefix = detached(log["scenario"], MAX_SCENARIO_BYTES)
    prefix["events"] = prefix["events"][:prefix_count]
    replay = run_scenario(prefix, driver)
    cp = {"schema": CHECKPOINT_SCHEMA, "prefix_scenario": prefix, "identity": replay["identity"],
          "prefix_log_sha256": replay["log_sha256"], "state": replay["final_state"],
          "state_sha256": digest(replay["final_state"])}
    cp["checkpoint_sha256"] = digest(cp)
    return cp


def restore_checkpoint(checkpoint, driver=None):
    cp = detached(checkpoint, MAX_CHECKPOINT_BYTES)
    keys(cp, ("schema", "prefix_scenario", "identity", "prefix_log_sha256", "state", "state_sha256", "checkpoint_sha256"), "checkpoint")
    enum(cp["schema"], (CHECKPOINT_SCHEMA,), "checkpoint.schema")
    if cp["checkpoint_sha256"] != digest({k: v for k, v in cp.items() if k != "checkpoint_sha256"}):
        raise IntegrationError("CheckpointDigestMismatch: checkpoint has changed")
    if cp["state_sha256"] != digest(cp["state"]):
        raise IntegrationError("CheckpointStateMismatch: state digest changed")
    replay = run_scenario(cp["prefix_scenario"], driver)
    if (canonical(cp["identity"]) != canonical(replay["identity"]) or cp["prefix_log_sha256"] != replay["log_sha256"]
            or canonical(cp["state"]) != canonical(replay["final_state"])):
        raise IntegrationError("CheckpointReplayMismatch: incompatible identity, prefix log or state")
    return replay


def resume_checkpoint(checkpoint, continuation, driver=None):
    prefix = restore_checkpoint(checkpoint, driver)
    extension = detached(continuation, MAX_SCENARIO_BYTES)
    keys(extension, ("schema", "checkpoint_sha256", "events"), "continuation")
    enum(extension["schema"], (CONTINUATION_SCHEMA,), "continuation.schema")
    if extension["checkpoint_sha256"] != checkpoint["checkpoint_sha256"]:
        raise IntegrationError("CheckpointBindingMismatch: continuation belongs to another checkpoint")
    if not isinstance(extension["events"], list) or not extension["events"]:
        raise IntegrationError("continuation.events: expected nonempty array")
    full = detached(prefix["scenario"], MAX_SCENARIO_BYTES)
    count = len(full["events"])
    full["events"].extend(extension["events"])
    replay = run_scenario(full, driver)
    if canonical(replay["records"][:count]) != canonical(prefix["records"]):
        raise IntegrationError("CheckpointPrefixMismatch: full replay prefix differs")
    return replay


def write_json(value, output, limit):
    try:
        data = (json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2, allow_nan=False)+"\n").encode("utf-8")
    except (TypeError, ValueError, UnicodeError, RecursionError) as exc:
        raise IntegrationError(f"invalid output JSON: {exc}") from exc
    if len(data) > limit:
        raise IntegrationError(f"OutputResourceLimit: serialized JSON exceeds {limit} bytes; no output written")
    if output == "-":
        sys.stdout.buffer.write(data)
    else:
        with pathlib.Path(output).open("xb") as stream:
            stream.write(data)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("build-id")
    for name in ("run", "verify", "extract", "checkpoint", "restore", "resume"):
        p = commands.add_parser(name)
        p.add_argument("input")
        if name == "resume":
            p.add_argument("continuation")
        if name == "checkpoint":
            p.add_argument("--through", type=int, required=True)
        if name != "verify":
            p.add_argument("--output", default="-")
    args = parser.parse_args(argv)
    try:
        if args.command == "build-id":
            sys.stdout.buffer.write((source_identity()["implementation_sha256"]+"\n").encode("ascii"))
            return 0
        if args.command == "run":
            log = run_scenario(load(args.input, MAX_SCENARIO_BYTES))
        elif args.command == "resume":
            log = resume_checkpoint(load(args.input, MAX_CHECKPOINT_BYTES), load(args.continuation, MAX_SCENARIO_BYTES))
        elif args.command == "restore":
            log = restore_checkpoint(load(args.input, MAX_CHECKPOINT_BYTES))
        else:
            original = load(args.input, MAX_LOG_BYTES)
            if args.command == "checkpoint":
                cp = make_checkpoint(original, args.through)
                write_json(cp, args.output, MAX_CHECKPOINT_BYTES)
                return 0
            log = verify_log(original)
            if args.command == "verify":
                sys.stdout.buffer.write((f"VERIFIED {log['scenario']['scenario_id']}: {len(log['records'])} boundaries; log {log['log_sha256']}\n").encode("utf-8"))
                return 0
            if args.command == "extract":
                write_json(log["scenario"], args.output, MAX_SCENARIO_BYTES)
                return 0
        write_json(log, args.output, MAX_LOG_BYTES)
        return 1 if args.command in ("run", "resume") and log["summary"]["rejected_boundaries"] else 0
    except (IntegrationError, OSError, RecursionError) as exc:
        sys.stderr.buffer.write((json.dumps({"error": type(exc).__name__, "message": str(exc)}, ensure_ascii=True)+"\n").encode("utf-8"))
        return 2


if __name__ == "__main__":
    sys.exit(main())
