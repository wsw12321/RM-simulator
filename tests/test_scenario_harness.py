"""End-to-end tests: JSON -> real C++ rule module -> audited/replayed JSON.

No dependencies, network, browser, official referee or timing scheduler involved.
"""
import copy
import importlib.util
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("scenario_cli", ROOT / "scenario_harness/cli.py")
cli = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cli)
DRIVER = pathlib.Path(os.environ.get("SCENARIO_TEST_DRIVER", ROOT / "build/rmuc_scenario_driver"))
SCENARIOS = ROOT / "scenario_harness/scenarios"


def fixture(name="timeout_control_priority"):
    return cli.load(SCENARIOS / (name + ".json"), cli.MAX_SCENARIO_BYTES)


def execute(s):
    return cli.run_scenario(s, DRIVER)


def credit(sequence=1):
    return copy.deepcopy(fixture("terminal_immutability")["events"][0]["credits"][0]) | {"sequence": sequence}


def refresh_digest(log):
    log["log_sha256"] = cli.digest({k: v for k, v in log.items() if k != "log_sha256"})


class ScenarioHarnessTests(unittest.TestCase):
    def test_all_reviewable_examples(self):
        expected = {
            "timeout_control_priority": ("blue_wins", "timeout_control", []),
            "timeout_attack_priority": ("red_wins", "timeout_attack_damage", []),
            "timeout_surviving_hp": ("blue_wins", "timeout_surviving_robot_hp", []),
            "timeout_draw": ("draw", "timeout_equal", []),
            "early_base_ending": ("red_wins", "base_hp", []),
            "missing_inputs": ("draw", "timeout_equal", ["MissingControlScore", "MissingAttackDamage", "MissingSurvivingRobotHp"]),
            "unsupported_ruling": ("red_wins", "timeout_control", ["UnsupportedRefereeRuling"] * 2),
            "terminal_immutability": ("red_wins", "base_hp", ["AlreadyFinished"] * 2),
            "unresolved_simultaneous_bases": ("draw", "resolved_both_bases_zero", ["UnresolvedSimultaneousDestruction"]),
        }
        self.assertEqual({p.stem for p in SCENARIOS.glob("*.json")}, set(expected))
        for name, (winner, reason, errors) in expected.items():
            with self.subTest(name=name):
                log = execute(fixture(name))
                self.assertEqual(log["final_state"]["final_decision"]["outcome"], winner)
                self.assertEqual(log["final_state"]["final_decision"]["reason"], reason)
                self.assertEqual([r["error"] for r in log["records"] if r["status"] == "rejected"], errors)
                self.assertEqual(log["summary"]["rejected_boundaries"], len(errors))
                for r in log["records"]:
                    if r["status"] == "rejected":
                        self.assertEqual(r["before"], r["after"])
                self.assertEqual(cli.canonical(cli.verify_log(log, DRIVER)), cli.canonical(log))

    def test_timeout_boundary_and_base_priority(self):
        s = fixture()
        log = execute(s)
        self.assertEqual(log["records"][0]["decision"]["outcome"], "ongoing")
        self.assertFalse(log["records"][0]["after"]["terminal"])
        s["events"][1]["snapshot"]["blue"]["base_hp"] = 0
        last = execute(s)["final_state"]
        self.assertEqual(last["final_decision"]["reason"], "base_hp")
        self.assertEqual(last["final_decision"]["outcome"], "red_wins")

    def test_early_base_ending_keeps_unknowns(self):
        log = execute(fixture("early_base_ending"))
        snap = log["final_state"]["final_snapshot"]
        self.assertEqual(snap["elapsed_ms"], 12001)
        for team in ("red", "blue"):
            for k in ("control_score", "attack_damage", "surviving_robot_hp"):
                self.assertIsNone(snap[team][k])
        self.assertIsNone(log["final_state"]["ledger"])

    def test_irrelevant_unknown_tier_not_forced(self):
        s = fixture()
        for e in s["events"]:
            for team in ("red", "blue"):
                e["snapshot"][team]["attack_damage"] = None
                e["snapshot"][team]["surviving_robot_hp"] = None
        self.assertEqual(execute(s)["final_state"]["final_decision"]["reason"], "timeout_control")

    def test_terminal_immutability_and_atomic_seal(self):
        log = execute(fixture("terminal_immutability"))
        final = log["records"][1]["after"]
        self.assertEqual(final["ledger"], {"totals": [5, 3], "last_credit_sequence": 2, "sealed": True})
        self.assertEqual(final["final_snapshot"]["red"]["control_score"], 5)
        self.assertEqual(final["final_snapshot"]["blue"]["control_score"], 3)
        for r in log["records"][2:]:
            self.assertEqual(r["before"], final)
            self.assertEqual(r["after"], final)
            self.assertEqual(r["credit_attempts"], [])
            self.assertIsNone(r["resolved_snapshot"])
        self.assertEqual(log["final_state"], final)

    def test_referee_errors_roll_back_credit_and_clock(self):
        log = execute(fixture("unsupported_ruling"))
        for r in log["records"][:2]:
            self.assertEqual(r["credit_attempts"][0]["candidate_totals"], [4, 0])
            self.assertEqual(r["resolved_snapshot"]["red"]["control_score"], 4)
            self.assertEqual(r["after"]["ledger"]["totals"], [0, 0])
            self.assertEqual(r["after"]["last_elapsed_ms"], -1)
        self.assertEqual(log["final_state"]["ledger"]["totals"], [4, 0])
        self.assertEqual(log["final_state"]["ledger"]["last_credit_sequence"], 1)

    def test_missing_required_tier_rolls_back_otherwise_valid_credit(self):
        s = fixture("unsupported_ruling")
        s["events"] = s["events"][:1]
        e = s["events"][0]
        e["credits"][0]["control"]["blue_outpost"]["controller"] = "blue"
        e["credits"][0]["control"]["blue_base"]["controller"] = "blue"
        e["snapshot"]["referee_ruling"] = "none"
        e["snapshot"]["red"]["attack_damage"] = None
        r = execute(s)["records"][0]
        self.assertEqual(r["error"], "MissingAttackDamage")
        self.assertEqual(r["before"], r["after"])
        self.assertEqual(r["after"]["ledger"]["last_credit_sequence"], 0)

    def test_duplicate_or_gapped_credit_rejects_whole_boundary(self):
        for bad in (1, 3, cli.U64_MAX):
            with self.subTest(sequence=bad):
                s = fixture("terminal_immutability")
                s["events"] = s["events"][:2]
                e = s["events"][0]
                e["credits"] = [credit(1), credit(bad)]
                s["events"][1]["credits"] = [credit(1)]
                log = execute(s)
                r = log["records"][0]
                self.assertEqual(r["error"], "InvalidCreditSequence")
                self.assertEqual(len(r["credit_attempts"]), 2)
                self.assertEqual(r["before"], r["after"])
                self.assertEqual(log["final_state"]["ledger"]["totals"], [4, 0])

    def test_illegal_full_enemy_base_control_rejects_transaction(self):
        s = fixture("terminal_immutability")
        s["events"] = s["events"][:1]
        s["events"][0]["credits"][0]["control"]["blue_base"]["degree"] = "full"
        r = execute(s)["records"][0]
        self.assertEqual(r["error"], "InvalidEnemyFullBaseControl")
        self.assertEqual(r["before"], r["after"])

    def test_manual_ledger_has_no_implicit_clock_accrual(self):
        s = fixture("terminal_immutability")
        s["events"] = s["events"][:2]
        s["events"][1]["elapsed_ms"] = 420000
        s["events"][1]["credits"] = []
        s["events"][1]["snapshot"]["blue"]["base_hp"] = 1000
        state = execute(s)["final_state"]
        self.assertEqual(state["ledger"]["totals"], [4, 0])
        self.assertEqual(state["ledger"]["last_credit_sequence"], 1)

    def test_caller_order_not_sorted_at_equal_time(self):
        s = fixture("terminal_immutability")
        s["events"] = s["events"][:2]
        s["events"][0]["elapsed_ms"] = 2000
        log = execute(s)
        self.assertEqual([r["supplied"] for r in log["records"]], s["events"])
        self.assertEqual(log["records"][0]["after"]["ledger"]["last_credit_sequence"], 1)
        self.assertEqual(log["records"][1]["after"]["ledger"]["last_credit_sequence"], 2)
        s["events"].reverse()
        for i, e in enumerate(s["events"], 1):
            e["sequence"] = i
        self.assertEqual(execute(s)["records"][0]["error"], "InvalidCreditSequence")

    def test_ongoing_is_not_reported_terminal(self):
        s = fixture()
        s["events"] = s["events"][:1]
        log = execute(s)
        self.assertFalse(log["summary"]["terminal"])
        self.assertIsNone(log["final_state"]["final_decision"])
        self.assertIsNone(log["final_state"]["final_snapshot"])

    def test_int64_maximum_is_exact(self):
        s = fixture("timeout_draw")
        for team in ("red", "blue"):
            for k in s["events"][0]["snapshot"][team]:
                s["events"][0]["snapshot"][team][k] = cli.I64_MAX
        state = execute(s)["final_state"]
        self.assertEqual(state["final_decision"]["outcome"], "draw")
        self.assertEqual(state["final_snapshot"]["red"]["attack_damage"], cli.I64_MAX)

    def test_deterministic_log_bytes_and_roundtrip(self):
        for path in SCENARIOS.glob("*.json"):
            with self.subTest(path=path.name):
                s = cli.load(path, cli.MAX_SCENARIO_BYTES)
                a = execute(s)
                b = execute(cli.decode(cli.canonical(s), cli.MAX_SCENARIO_BYTES))
                self.assertEqual(cli.canonical(a), cli.canonical(b))
                self.assertEqual(cli.decode(cli.canonical(a), cli.MAX_LOG_BYTES), a)
                self.assertEqual(a["scenario_sha256"], cli.digest(s))
                self.assertNotIn(str(ROOT), cli.canonical(a).decode())

    def test_log_tampering_and_recomputed_hash(self):
        original = execute(fixture())
        changed = copy.deepcopy(original)
        changed["records"][1]["decision"]["outcome"] = "red_wins"
        with self.assertRaisesRegex(cli.HarnessError, "LogDigestMismatch"):
            cli.verify_log(changed, DRIVER)
        refresh_digest(changed)
        with self.assertRaisesRegex(cli.HarnessError, "ReplayMismatch"):
            cli.verify_log(changed, DRIVER)
        changed = copy.deepcopy(original)
        changed["scenario_sha256"] = "0" * 64
        refresh_digest(changed)
        with self.assertRaisesRegex(cli.HarnessError, "ScenarioDigestMismatch"):
            cli.verify_log(changed, DRIVER)

    def test_identity_changes_are_rejected(self):
        changed = execute(fixture())
        changed["identity"]["driver"]["module_version"] = "different-module"
        refresh_digest(changed)
        with self.assertRaisesRegex(cli.HarnessError, "ReplayMismatch"):
            cli.verify_log(changed, DRIVER)

    def test_stale_binary_checked_before_execution(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            for name in cli.SOURCE_FILES:
                dest = root / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / name, dest)
            with (root / "scenario_harness/driver.cpp").open("a") as out:
                out.write("\n// source changed after build\n")
            with mock.patch.object(cli, "ROOT", root), mock.patch.object(cli, "invoke", wraps=cli.invoke) as calls:
                with self.assertRaisesRegex(cli.HarnessError, "BuildIdentityMismatch"):
                    execute(fixture())
                self.assertEqual(calls.call_count, 1)
                self.assertEqual(calls.call_args.args[1], ("--identity",))

    def test_schema_validation_precedes_native_process(self):
        s = fixture()
        s["events"][1]["snapshot"]["red"]["base_hp"] = -1
        with mock.patch.object(cli, "invoke") as invoke:
            with self.assertRaises(cli.HarnessError):
                execute(s)
            invoke.assert_not_called()

    def test_schema_rejects_invalid_values(self):
        modifications = [
            (lambda s: s.update(schema="unknown")),
            (lambda s: s.update(fact_status="physical occupancy")),
            (lambda s: s.update(ordering_policy="sort automatically")),
            (lambda s: s.update(timing_policy="one second by wall clock")),
            (lambda s: s.update(control_source="auto")),
            (lambda s: s.update(unexpected=True)),
            (lambda s: s.update(scenario_id="../path")),
            (lambda s: s.update(description="")),
            (lambda s: s.update(description="\ud800")),
            (lambda s: s.update(events=[])),
            (lambda s: s["events"][0].update(sequence=2)),
            (lambda s: s["events"][0].update(sequence=True)),
            (lambda s: s["events"][1].update(elapsed_ms=419998)),
            (lambda s: s["events"][0].update(elapsed_ms=-1)),
            (lambda s: s["events"][1].update(elapsed_ms=420001)),
            (lambda s: s["events"][0].update(elapsed_ms=1.5)),
            (lambda s: s["events"][0].update(elapsed_ms=True)),
            (lambda s: s["events"][0].update(source_note=None)),
            (lambda s: s["events"][0].update(credits={})),
            (lambda s: s["events"][0]["snapshot"].update(referee_ruling="unknown")),
            (lambda s: s["events"][0]["snapshot"].update(simultaneous_destruction_resolved=1)),
            (lambda s: s["events"][0]["snapshot"]["red"].pop("attack_damage")),
        ]
        for field in ("base_hp", "control_score", "attack_damage", "surviving_robot_hp"):
            for bad in (-1, cli.I64_MAX+1, True, 1.0, "12", []):
                modifications.append(lambda s, field=field, bad=bad: s["events"][0]["snapshot"]["red"].update({field: bad}))
        modifications.append(lambda s: s["events"][0]["snapshot"]["red"].update(base_hp=None))
        for i, change in enumerate(modifications):
            with self.subTest(case=i):
                s = fixture(); change(s)
                with self.assertRaises(cli.HarnessError):
                    cli.validate_scenario(s)

    def test_control_source_conflicts_fail_closed(self):
        s = fixture()
        s["events"][0]["credits"] = [credit()]
        with self.assertRaisesRegex(cli.HarnessError, "external_totals prohibits"):
            cli.validate_scenario(s)
        for value in (None, 0, 4):
            s = fixture("terminal_immutability")
            s["events"][0]["snapshot"]["red"]["control_score"] = value
            with self.assertRaisesRegex(cli.HarnessError, "unknown keys"):
                cli.validate_scenario(s)

    def test_credit_schema_limits(self):
        changes = [
            lambda c: c.update(sequence=0), lambda c: c.update(sequence=-1),
            lambda c: c.update(sequence=cli.U64_MAX+1), lambda c: c.update(sequence=True),
            lambda c: c.update(extra=0), lambda c: c["control"].pop("red_base"),
            lambda c: c["control"]["red_base"].update(controller="green"),
            lambda c: c["control"]["red_base"].update(degree="occupied"),
            lambda c: c["control"]["red_base"].update(extra=True),
        ]
        for i, change in enumerate(changes):
            with self.subTest(case=i):
                s = fixture("terminal_immutability"); change(s["events"][0]["credits"][0])
                with self.assertRaises(cli.HarnessError):
                    cli.validate_scenario(s)

    def test_resource_limits(self):
        s = fixture()
        s["events"] = [s["events"][0]] * (cli.MAX_EVENTS + 1)
        with self.assertRaises(cli.HarnessError):
            cli.validate_scenario(s)
        s = fixture("terminal_immutability")
        s["events"][0]["credits"] = [credit()] * (cli.MAX_CREDITS + 1)
        with self.assertRaises(cli.HarnessError):
            cli.validate_scenario(s)
        with self.assertRaisesRegex(cli.HarnessError, "byte resource limit"):
            cli.decode(b" " * (cli.MAX_SCENARIO_BYTES + 1), cli.MAX_SCENARIO_BYTES)

    def test_malformed_json_rejected(self):
        for raw in (b'{"x":1,"x":2}', b'{"outer":{"x":1,"x":2}}', b'{',
                    b'[] trailing', b'NaN', b'Infinity', b'-Infinity', b'1.5', b'1e2',
                    b'1' * 100, b'"\xff"', b'[' * 1100 + b']' * 1100):
            with self.subTest(raw=raw[:30]), self.assertRaises(cli.HarnessError):
                cli.decode(raw, cli.MAX_SCENARIO_BYTES)

    def test_unicode_output_size_limits_and_roundtrip(self):
        s = fixture("timeout_draw")
        s["description"] = "人工裁定🧪" * 100
        s["events"][0]["source_note"] = "🧪" * 500
        log = execute(s)
        with tempfile.TemporaryDirectory() as tmp:
            for value, limit, name in ((s, cli.MAX_SCENARIO_BYTES, "scenario"),
                                       (log, cli.MAX_LOG_BYTES, "log")):
                exact = (json.dumps(value, ensure_ascii=True, sort_keys=True, indent=2,
                                    allow_nan=False) + "\n").encode("ascii")
                out = pathlib.Path(tmp) / (name + ".json")
                with self.assertRaisesRegex(cli.HarnessError, "OutputResourceLimit"):
                    cli.write_json(value, out, len(exact)-1)
                self.assertFalse(out.exists())
                with mock.patch("sys.stdout") as stdout:
                    with self.assertRaisesRegex(cli.HarnessError, "OutputResourceLimit"):
                        cli.write_json(value, "-", len(exact)-1)
                    stdout.write.assert_not_called()
                # Inclusive exact-byte boundary, then the real schema-sized input read.
                cli.write_json(value, out, len(exact))
                self.assertEqual(out.read_bytes(), exact)
                self.assertEqual(cli.load(out, limit), value)
            self.assertEqual(cli.canonical(execute(cli.load(pathlib.Path(tmp) / "scenario.json",
                                                          cli.MAX_SCENARIO_BYTES))), cli.canonical(log))
            self.assertEqual(cli.verify_log(cli.load(pathlib.Path(tmp) / "log.json", cli.MAX_LOG_BYTES), DRIVER), log)

    def test_real_unicode_expansion_exceeding_file_limits(self):
        # Reproduces the reviewed valid-UTF8-input -> oversized-ASCII-output case.
        s = fixture("timeout_draw")
        first = s["events"][0]
        first["elapsed_ms"] = 1000
        first["source_note"] = "🧪" * 500
        s["events"] = [copy.deepcopy(first) for _ in range(3000)]
        for index, event in enumerate(s["events"], 1):
            event["sequence"] = index
        raw = json.dumps(s, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self.assertLess(len(raw), cli.MAX_SCENARIO_BYTES)
        cli.validate_scenario(cli.decode(raw, cli.MAX_SCENARIO_BYTES))
        log = execute(s)
        with tempfile.TemporaryDirectory() as tmp:
            out = pathlib.Path(tmp) / "large.json"
            with self.assertRaisesRegex(cli.HarnessError, "OutputResourceLimit"):
                cli.write_json(log, out, cli.MAX_LOG_BYTES)
            self.assertFalse(out.exists())
            # A smaller scenario can fit its log budget while extracted pretty
            # ASCII JSON still exceeds the scenario budget.
            s["events"] = s["events"][:1500]
            log = execute(s)
            log_out = pathlib.Path(tmp) / "fits-log.json"
            cli.write_json(log, log_out, cli.MAX_LOG_BYTES)
            self.assertEqual(cli.load(log_out, cli.MAX_LOG_BYTES), log)
            with self.assertRaisesRegex(cli.HarnessError, "OutputResourceLimit"):
                cli.write_json(log["scenario"], out, cli.MAX_SCENARIO_BYTES)
            self.assertFalse(out.exists())

    def test_private_native_protocol_validation(self):
        good = cli.protocol(fixture())
        invalid = [
            b"", good + b"\n", good.replace(b"END\n", b"END junk\n"),
            good.replace(b"external_totals 2", b"external_totals 4097"),
            good.replace(b"BOUNDARY 1 419999", b"BOUNDARY 1 420001"),
            good.replace(b"BOUNDARY 1 419999", b"BOUNDARY 1 -1"),
            good.replace(b"BOUNDARY 1 419999", b"BOUNDARY 1 1.5"),
            good.replace(b"BOUNDARY 1 419999", b"BOUNDARY 2 419999"),
            good.replace(b"BOUNDARY 2 420000", b"BOUNDARY 2 419998"),
            good.replace(b" none 0 ", b" none 2 "),
            good.replace(b" none 0 ", b" invalid 0 "),
            good.replace(b" 9000 ", b" 9223372036854775808 "),
            good.replace(b" 9000 ", b" true "),
            good.replace(b" 9000 ", b" 9000junk "),
            good.replace(b" 9000 ", b" -9000 "),
            good.replace(b"external_totals", b"manual_one_second_credits"),
            b"x" * (1024 * 1024 + 1) + b"\n",
        ]
        for data in invalid:
            with self.subTest(data=data[:100]):
                result = subprocess.run([str(DRIVER)], input=data, capture_output=True, timeout=15)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, b"")
                self.assertIn(b"ProtocolInputError", result.stderr)
        manual = cli.protocol(fixture("terminal_immutability"))
        for old, new in ((b"CREDIT 1 ", b"CREDIT -1 "), (b"red temporary", b"green temporary"),
                         (b"red temporary", b"red invalid"), (b"CREDIT 1 ", b"CREDIT 18446744073709551616 ")):
            result = subprocess.run([str(DRIVER)], input=manual.replace(old, new), capture_output=True, timeout=15)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, b"")

    def test_cli_run_verify_extract_and_exit_codes(self):
        # Public CLI uses the normal build, while alternate binaries are exercised
        # through the module API in optimization/sanitizer passes.
        if DRIVER != ROOT / "build/rmuc_scenario_driver":
            self.skipTest("public CLI intentionally pins the normal build path")
        def command(*args):
            return subprocess.run([sys.executable, str(ROOT / "scenario_harness/cli.py"), *map(str, args)],
                                  capture_output=True, timeout=20, cwd="/tmp")
        with tempfile.TemporaryDirectory() as tmp:
            out, extracted = pathlib.Path(tmp) / "log.json", pathlib.Path(tmp) / "scenario.json"
            result = command("run", SCENARIOS / "terminal_immutability.json", "--output", out)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertEqual(result.stdout, b"")
            self.assertEqual(command("verify", out).returncode, 0)
            self.assertEqual(command("extract", out, "--output", extracted).returncode, 0)
            self.assertEqual(cli.load(extracted, cli.MAX_SCENARIO_BYTES), fixture("terminal_immutability"))
            rerun = command("run", extracted)
            self.assertEqual(rerun.returncode, 1)
            self.assertEqual(rerun.stdout, out.read_bytes())
            before = out.read_bytes()
            self.assertEqual(command("run", extracted, "--output", out).returncode, 2)
            self.assertEqual(out.read_bytes(), before)
            result = command("run", SCENARIOS / "timeout_draw.json")
            self.assertEqual(result.returncode, 0, result.stderr)
            bad = pathlib.Path(tmp) / "bad.json"; bad.write_text('{"schema":null}')
            result = command("run", bad)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, b"")
            self.assertIn(b"HarnessError", result.stderr)


if __name__ == "__main__":
    unittest.main()
