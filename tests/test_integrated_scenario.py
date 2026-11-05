"""Cold, real-native integration checks for manually adjudicated logical facts.

These tests do not turn detector samples into logical occupancy, invent credits,
or claim the adapter's phase order is an official referee scheduler.
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
spec = importlib.util.spec_from_file_location('integrated_cli', ROOT / 'integrated_scenario/cli.py')
cli = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cli)
DRIVER = pathlib.Path(os.environ.get('INTEGRATED_TEST_DRIVER', ROOT / 'build/rmuc_integrated_driver'))
SCENARIOS = ROOT / 'integrated_scenario/scenarios'
POSITIONS = ('red_outpost', 'blue_outpost', 'red_base', 'blue_base')


def fixture(name='compact_capture_credit_timeout'):
    return cli.load(SCENARIOS / (name + '.json'), cli.MAX_SCENARIO_BYTES)


def execute(value):
    return cli.run_scenario(value, DRIVER)


def team(base_hp=1000, attack_damage=0, surviving_robot_hp=0):
    return dict(base_hp=base_hp, attack_damage=attack_damage, surviving_robot_hp=surviving_robot_hp)


def credit(sequence=1):
    return dict(sequence=sequence, source_note='Externally supplied settled credit.')


def event(sequence, elapsed_ms):
    result = copy.deepcopy(fixture()['events'][0])
    result.update(sequence=sequence, elapsed_ms=elapsed_ms, credits=[])
    return result


def continuation(start, end):
    result = copy.deepcopy(fixture()['events'][1]['continuations']['red_outpost'])
    result.update(from_ms=start, through_ms=end)
    return result


def refresh(value, field):
    value[field] = cli.digest({k: v for k, v in value.items() if k != field})


class IntegratedScenarioTests(unittest.TestCase):
    def test_all_reviewable_fixtures_replay_and_rollback(self):
        expected = {
            'capture_credit_timeout': [],
            'compact_capture_credit_timeout': [],
            'external_handoff': ['ExternalContinuationRequired'] * 2,
            'strict_five_second_boundary': ['UnadjudicatedCoincidence'],
            'transaction_rollback': ['InvalidCreditSequence', 'ContinuationIntervalMismatch', 'UnsupportedRefereeRuling'],
            'contested_to_none': ['UnadjudicatedContestExitToNone'],
            'terminal_immutability': ['AlreadyFinished'],
        }
        self.assertEqual({p.stem for p in SCENARIOS.glob('*.json')}, set(expected))
        for name, errors in expected.items():
            with self.subTest(name=name):
                log = execute(fixture(name))
                self.assertEqual([r['error'] for r in log['records'] if r['status'] == 'rejected'], errors)
                for record in log['records']:
                    if record['status'] == 'rejected':
                        self.assertEqual(record['before'], record['after'])
                self.assertEqual(cli.verify_log(log, DRIVER), log)
                self.assertEqual(execute(fixture(name)), log)

    def test_complete_score_changing_interval_example(self):
        log = execute(fixture('capture_credit_timeout'))
        self.assertEqual(len(log['records']), 32)
        self.assertEqual(log['records'][1]['after']['captures']['red_outpost']['time_ms'], 390000)
        self.assertEqual(log['records'][1]['after']['ledger']['totals'], [0, 0])
        self.assertEqual(log['final_state']['ledger']['totals'], [0, 30])
        self.assertEqual(log['final_state']['final_decision']['reason'], 'timeout_control')
        self.assertEqual(log['final_state']['final_decision']['outcome'], 'blue_wins')
        self.assertEqual(log['final_state']['final_snapshot']['red']['base_hp'], 9000)
        self.assertEqual(log['final_state']['final_snapshot']['blue']['base_hp'], 1000)

    def test_capture_derived_credit_beats_unequal_intact_base_hp(self):
        log = execute(fixture())
        first, final = log['records'][0], log['final_state']
        self.assertEqual(first['decision']['outcome'], 'ongoing')
        self.assertEqual(first['after']['ledger']['totals'], [0, 1])
        self.assertEqual(final['ledger']['totals'], [0, 2])
        self.assertEqual(final['ledger']['last_credit_sequence'], 2)
        self.assertTrue(final['ledger']['sealed'])
        self.assertEqual(final['final_decision']['outcome'], 'blue_wins')
        self.assertEqual(final['final_decision']['reason'], 'timeout_control')
        self.assertEqual(final['final_snapshot']['red']['base_hp'], 9000)
        self.assertEqual(final['final_snapshot']['blue']['base_hp'], 1000)

    def test_exact_endpoint_and_external_handoff_freeze_capture(self):
        log = execute(fixture())
        first, last = log['records']
        captured = first['after']['captures']['red_outpost']
        self.assertTrue(captured['completed'])
        self.assertEqual(captured['time_ms'], 5000)
        self.assertEqual(captured['progress_ms'], 5000)
        self.assertEqual(captured['control']['controller'], 'blue')
        self.assertEqual(captured['control']['degree'], 'temporary')
        self.assertEqual(last['after']['captures']['red_outpost'], captured)
        self.assertEqual(last['after']['last_elapsed_ms'], 420000)
        self.assertEqual(first['continuation_checks'], [])
        self.assertEqual(last['continuation_checks'][0]['from_ms'], 5000)
        self.assertEqual(last['continuation_checks'][0]['through_ms'], 420000)

    def test_residual_interval_is_audited_not_consumed(self):
        log = execute(fixture('external_handoff'))
        rejected, accepted, missing, continued = log['records']
        attempt = rejected['capture_attempts'][0]
        self.assertEqual(attempt['actual_endpoint_ms'], 5000)
        self.assertEqual(attempt['requested_time_ms'], 6000)
        self.assertEqual(attempt['unconsumed_ms'], 1000)
        self.assertEqual(rejected['after'], log['initial_state'])
        frozen = accepted['after']['captures']['red_outpost']
        self.assertEqual(frozen['time_ms'], 5000)
        self.assertEqual(continued['after']['captures']['red_outpost'], frozen)
        self.assertEqual(continued['continuation_checks'][0]['from_ms'], 7000)
        self.assertEqual(continued['after']['ledger']['totals'], [0, 2])

    def test_strict_five_second_input_has_no_hidden_grace(self):
        log = execute(fixture('strict_five_second_boundary'))
        early, rejected, accepted = log['records']
        self.assertEqual(early['after']['captures']['red_outpost']['progress_ms'], 3000)
        self.assertFalse(early['after']['captures']['red_outpost']['completed'])
        self.assertEqual(rejected['error'], 'UnadjudicatedCoincidence')
        self.assertEqual(rejected['after'], early['after'])
        captured = accepted['after']['captures']['red_outpost']
        self.assertEqual(captured['time_ms'], 5000)
        self.assertTrue(captured['active']['blue'])
        self.assertEqual(accepted['after']['ledger']['totals'], [0, 1])

    def test_no_implicit_credit_accrual(self):
        scenario = fixture()
        for boundary in scenario['events']:
            boundary['credits'] = []
        state = execute(scenario)['final_state']
        self.assertEqual(state['ledger']['totals'], [0, 0])
        self.assertEqual(state['ledger']['last_credit_sequence'], 0)
        self.assertEqual(state['final_decision']['outcome'], 'draw')
        self.assertEqual(state['final_decision']['reason'], 'timeout_equal')

    def test_terminal_rejects_before_candidate_work(self):
        log = execute(fixture('terminal_immutability'))
        first, second = log['records']
        self.assertEqual(first['decision']['reason'], 'base_hp')
        self.assertEqual(second['before'], first['after'])
        self.assertEqual(second['after'], first['after'])
        self.assertEqual(second['credit_attempts'], [])
        self.assertIsNone(second['resolved_snapshot'])

    def test_missing_tiebreak_rolls_back_capture(self):
        s = fixture()
        s['events'] = [event(1, 420000)]
        s['events'][0]['continuations']['red_outpost'] = continuation(5000, 420000)
        s['events'][0]['snapshot']['red']['attack_damage'] = None
        r = execute(s)['records'][0]
        self.assertEqual(r['error'], 'MissingAttackDamage')
        self.assertEqual(r['before'], r['after'])

    def test_missing_surviving_hp_tier_is_atomic(self):
        s = fixture()
        s['events'] = [event(1, 420000)]
        s['events'][0]['continuations']['red_outpost'] = continuation(5000, 420000)
        s['events'][0]['snapshot']['blue']['surviving_robot_hp'] = None
        r = execute(s)['records'][0]
        self.assertEqual(r['error'], 'MissingSurvivingRobotHp')
        self.assertEqual(r['before'], r['after'])

    def test_phase_failures_show_candidates_but_never_commit(self):
        log = execute(fixture('transaction_rollback'))
        credit_error, continuation_error, judge_error, accepted = log['records']
        self.assertEqual(credit_error['error_source'], 'credit')
        self.assertEqual(len(credit_error['credit_attempts']), 2)
        self.assertEqual(credit_error['credit_attempts'][0]['candidate_totals'], [0, 1])
        self.assertIsNone(credit_error['resolved_snapshot'])
        self.assertEqual(continuation_error['error_source'], 'continuation')
        self.assertEqual(continuation_error['credit_attempts'], [])
        self.assertIsNone(continuation_error['derived_control'])
        self.assertEqual(judge_error['error_source'], 'judge')
        self.assertEqual(judge_error['resolved_snapshot']['blue']['control_score'], 1)
        for rejected in (credit_error, continuation_error, judge_error):
            self.assertEqual(rejected['after'], log['initial_state'])
            self.assertEqual(rejected['capture_attempts'][0]['candidate_state']['time_ms'], 5000)
        self.assertEqual(accepted['after']['ledger']['totals'], [0, 1])
        self.assertEqual(accepted['after']['ledger']['last_credit_sequence'], 1)

    def test_capture_failure_rolls_back_prior_positions(self):
        s = fixture(); s['events'] = [event(1, 5000)]
        # First position reaches capture; next position's redundant input rejects all.
        s['events'][0]['logical_inputs']['blue_outpost'] = {'red': False, 'blue': False}
        r = execute(s)['records'][0]
        self.assertEqual(r['error'], 'RedundantInput')
        self.assertEqual(r['before'], r['after'])

    def test_handoff_exact_coverage_and_no_zero_interval(self):
        for value, error in ((continuation(0, 5000), 'UnexpectedContinuation'),
                             (continuation(5000, 5000), 'UnexpectedContinuation')):
            s = fixture(); s['events'] = s['events'][:1]
            s['events'][0]['continuations']['red_outpost'] = value
            r = execute(s)['records'][0]
            self.assertEqual(r['error'], error)
            self.assertEqual(r['before'], r['after'])
        s = fixture()
        s['events'][1]['logical_inputs']['red_outpost'] = {'red': False, 'blue': False}
        self.assertEqual(execute(s)['records'][1]['error'], 'UnsupportedTemporaryLogicalInput')

    def test_input_after_capture_endpoint_rejected(self):
        s = fixture(); s['events'] = [event(1, 5001)]
        s['events'][0]['logical_inputs']['red_outpost'] = {'red': False, 'blue': False}
        r = execute(s)['records'][0]
        self.assertEqual(r['error'], 'InputBeyondCaptureEndpoint')
        self.assertEqual(r['before'], r['after'])

    def test_closed_timestamps_and_global_attempt_order(self):
        for timestamp in (0, 5000, 4999):
            s = fixture(); s['events'][1]['elapsed_ms'] = timestamp
            with self.assertRaises(cli.IntegrationError):
                cli.validate_scenario(s)
        s = fixture('external_handoff'); s['events'][1]['elapsed_ms'] = 6000
        with self.assertRaises(cli.IntegrationError):
            cli.validate_scenario(s)

    def test_prefix_restore_and_continuation_equal_full_run(self):
        for name in ('compact_capture_credit_timeout', 'external_handoff', 'transaction_rollback', 'terminal_immutability'):
            full = execute(fixture(name))
            for count in range(1, len(full['records'])):
                with self.subTest(name=name, count=count):
                    checkpoint = cli.make_checkpoint(full, count, DRIVER)
                    prefix = cli.restore_checkpoint(checkpoint, DRIVER)
                    self.assertEqual(prefix['final_state'], full['records'][count - 1]['after'])
                    suffix = dict(schema='rmuc2027-integrated-continuation-v1',
                                  checkpoint_sha256=checkpoint['checkpoint_sha256'],
                                  events=copy.deepcopy(full['scenario']['events'][count:]))
                    self.assertEqual(cli.resume_checkpoint(checkpoint, suffix, DRIVER), full)

    def test_checkpoint_tampering_and_binding(self):
        log = execute(fixture()); checkpoint = cli.make_checkpoint(log, 1, DRIVER)
        changed = copy.deepcopy(checkpoint)
        changed['state']['ledger']['totals'][0] += 1
        with self.assertRaises(cli.IntegrationError):
            cli.restore_checkpoint(changed, DRIVER)
        changed['state_sha256'] = cli.digest(changed['state'])
        refresh(changed, 'checkpoint_sha256')
        with self.assertRaises(cli.IntegrationError):
            cli.restore_checkpoint(changed, DRIVER)
        suffix = dict(schema='rmuc2027-integrated-continuation-v1', checkpoint_sha256='0'*64,
                      events=log['scenario']['events'][1:])
        with self.assertRaises(cli.IntegrationError):
            cli.resume_checkpoint(checkpoint, suffix, DRIVER)

    def test_canonical_identity_distinguishes_false_from_zero(self):
        log = execute(fixture())
        checkpoint = cli.make_checkpoint(log, 1, DRIVER)
        self.assertIs(checkpoint['identity']['integration_provenance']['complete_rule_pack'], False)
        checkpoint['identity']['integration_provenance']['complete_rule_pack'] = 0
        refresh(checkpoint, 'checkpoint_sha256')
        with self.assertRaisesRegex(cli.IntegrationError, 'CheckpointReplayMismatch'):
            cli.restore_checkpoint(checkpoint, DRIVER)
        changed = copy.deepcopy(log)
        changed['identity']['integration_provenance']['complete_rule_pack'] = 0
        refresh(changed, 'log_sha256')
        with self.assertRaises(cli.IntegrationError):
            cli.verify_log(changed, DRIVER)

    def test_log_tampering_even_with_recomputed_digest(self):
        original = execute(fixture())
        for mutate in (lambda x: x['records'][1]['decision'].update(outcome='red_wins'),
                       lambda x: x.update(scenario_sha256='0'*64),
                       lambda x: x['identity'].update(unexpected_version='future')):
            value = copy.deepcopy(original); mutate(value)
            with self.assertRaises(cli.IntegrationError):
                cli.verify_log(value, DRIVER)
            refresh(value, 'log_sha256')
            with self.assertRaises(cli.IntegrationError):
                cli.verify_log(value, DRIVER)

    def test_public_validation_enforces_detached_api_byte_bound(self):
        s = fixture()
        s['events'] = s['events'][:1]
        s['events'][0]['credits'] = [
            {'sequence': n, 'source_note': 'a' * 2048}
            for n in range(1, 4097)
        ]
        # Every field and credit count is individually valid, but serialized
        # API input still must pass the same 8 MiB limit as a JSON file.
        encoded = json.dumps(s, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
        self.assertGreater(len(encoded), cli.MAX_SCENARIO_BYTES)
        with mock.patch.object(cli, 'invoke') as invoke:
            for api in (cli.validate_scenario, execute):
                with self.subTest(api=api.__name__), self.assertRaises(cli.IntegrationError):
                    api(s)
            invoke.assert_not_called()

    def test_validation_returns_independent_copy_and_rejects_nonstring_keys(self):
        original = fixture()
        saved = copy.deepcopy(original)
        validated = cli.validate_scenario(original)
        self.assertEqual(validated, original)
        validated['events'][0]['source_note'] = 'mutated returned value'
        validated['initial_logical']['red_outpost']['blue'] = False
        self.assertEqual(original, saved)
        for key in (1, None, (1, 2)):
            with self.subTest(key=key):
                malformed = fixture()
                malformed[key] = 'not a valid schema key'
                with self.assertRaises(cli.IntegrationError):
                    cli.validate_scenario(malformed)

    def test_api_results_are_detached(self):
        s = fixture(); log = execute(s); original = copy.deepcopy(log)
        s['events'][0]['source_note'] = 'caller changed input'
        self.assertEqual(log, original)
        checkpoint = cli.make_checkpoint(log, 1, DRIVER)
        checkpoint['prefix_scenario']['description'] = 'caller changed checkpoint'
        self.assertEqual(log, original)

    def test_stale_binary_identity_fails_before_run(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            for name in cli.SOURCE_FILES:
                dest = root / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / name, dest)
            with (root / 'integrated_scenario/driver.cpp').open('a') as stream:
                stream.write('\n// changed after native build\n')
            with mock.patch.object(cli, 'ROOT', root), mock.patch.object(cli, 'invoke', wraps=cli.invoke) as calls:
                with self.assertRaisesRegex(cli.IntegrationError, 'BuildIdentityMismatch'):
                    execute(fixture())
                self.assertEqual(calls.call_count, 1)
                self.assertEqual(calls.call_args.args[1], ('--identity',))

    def test_cli_cold_run_verify_extract_checkpoint_resume(self):
        if DRIVER != ROOT / 'build/rmuc_integrated_driver':
            self.skipTest('CLI intentionally uses normal native build')
        def command(*args):
            return subprocess.run([sys.executable, str(ROOT / 'integrated_scenario/cli.py'), *map(str, args)],
                                  capture_output=True, timeout=20, cwd='/tmp')
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            log, extracted, checkpoint = tmp/'log.json', tmp/'scenario.json', tmp/'checkpoint.json'
            result = command('run', SCENARIOS/'compact_capture_credit_timeout.json', '--output', log)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, b'')
            self.assertEqual(command('verify', log).returncode, 0)
            self.assertEqual(command('extract', log, '--output', extracted).returncode, 0)
            self.assertEqual(cli.load(extracted, cli.MAX_SCENARIO_BYTES), fixture())
            rerun = command('run', extracted)
            self.assertEqual(rerun.returncode, 0, rerun.stderr)
            self.assertEqual(rerun.stdout, log.read_bytes())
            result = command('checkpoint', log, '--through', 1, '--output', checkpoint)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = command('restore', checkpoint)
            self.assertEqual(result.returncode, 0, result.stderr)
            cp = cli.load(checkpoint, cli.MAX_LOG_BYTES)
            suffix = dict(schema='rmuc2027-integrated-continuation-v1',
                          checkpoint_sha256=cp['checkpoint_sha256'], events=fixture()['events'][1:])
            suffix_path = tmp/'continuation.json'
            cli.write_json(suffix, suffix_path, cli.MAX_SCENARIO_BYTES)
            resumed = command('resume', checkpoint, suffix_path)
            self.assertEqual(resumed.returncode, 0, resumed.stderr)
            self.assertEqual(resumed.stdout, log.read_bytes())
            before = log.read_bytes()
            self.assertEqual(command('run', extracted, '--output', log).returncode, 2)
            self.assertEqual(log.read_bytes(), before)
            self.assertEqual(command('run', SCENARIOS/'external_handoff.json').returncode, 1)
            bad = tmp/'bad.json'; bad.write_text('{"schema":null}')
            result = command('run', bad)
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, b'')

    def test_cli_utf8_stdout_ignores_python_io_encoding(self):
        if DRIVER != ROOT / 'build/rmuc_integrated_driver':
            self.skipTest('CLI intentionally uses normal native build')
        def command(*args, encoding):
            env = dict(os.environ, PYTHONIOENCODING=encoding)
            return subprocess.run([sys.executable, str(ROOT / 'integrated_scenario/cli.py'), *map(str, args)],
                                  capture_output=True, timeout=20, cwd='/tmp', env=env)
        fixture_path = SCENARIOS / 'compact_capture_credit_timeout.json'
        normal = command('run', fixture_path, encoding='utf-8')
        alternate = command('run', fixture_path, encoding='utf-16')
        self.assertEqual(normal.returncode, 0, normal.stderr)
        self.assertEqual(alternate.returncode, 0, alternate.stderr)
        self.assertEqual(alternate.stdout, normal.stdout)
        self.assertEqual(json.loads(alternate.stdout.decode('utf-8')), execute(fixture()))
        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            path = tmp / 'log.json'; path.write_bytes(normal.stdout)
            verified = command('verify', path, encoding='utf-16')
            self.assertEqual(verified.returncode, 0, verified.stderr)
            self.assertTrue(verified.stdout.decode('utf-8').startswith('VERIFIED '))
            self.assertEqual(verified.stdout, command('verify', path, encoding='utf-8').stdout)
            cp = command('checkpoint', path, '--through', 1, encoding='utf-16')
            self.assertEqual(cp.returncode, 0, cp.stderr)
            json.loads(cp.stdout.decode('utf-8'))
            cp_path = tmp / 'checkpoint.json'; cp_path.write_bytes(cp.stdout)
            restored = command('restore', cp_path, encoding='utf-16')
            self.assertEqual(restored.returncode, 0, restored.stderr)
            json.loads(restored.stdout.decode('utf-8'))
            bad = tmp / 'bad.json'; bad.write_bytes(b'{"schema":null}')
            failed = command('run', bad, encoding='utf-16')
            self.assertEqual(failed.returncode, 2)
            self.assertEqual(failed.stdout, b'')
            self.assertEqual(json.loads(failed.stderr.decode('utf-8'))['error'], 'IntegrationError')
            bad.write_bytes(b'{"\\ud800":1,"\\ud800":2}')
            failed = command('run', bad, encoding='utf-16')
            self.assertEqual(failed.returncode, 2)
            self.assertEqual(failed.stdout, b'')
            self.assertNotIn(b'Traceback', failed.stderr)
            self.assertEqual(json.loads(failed.stderr.decode('utf-8'))['error'], 'IntegrationError')


    def test_validation_before_native_execution(self):
        s = fixture(); s['events'][1]['snapshot']['red']['base_hp'] = -1
        with mock.patch.object(cli, 'invoke') as invoke:
            with self.assertRaises(cli.IntegrationError):
                execute(s)
            invoke.assert_not_called()

    def test_schema_unknowns_types_bounds_and_versions(self):
        mutations = [lambda s: s.update(schema='v2'), lambda s: s.update(capture_policy='timer-first'),
                     lambda s: s.update(phase_policy='official'), lambda s: s.update(timing_policy='sampled'),
                     lambda s: s.update(world_contract='unknown'), lambda s: s.update(fact_status='detected'),
                     lambda s: s.update(scenario_id='../unsafe'), lambda s: s.update(description=''),
                     lambda s: s.update(description='\ud800'), lambda s: s.update(unexpected=1),
                     lambda s: s.update(events=[]), lambda s: s['events'][0].update(sequence=True),
                     lambda s: s['events'][0].update(elapsed_ms=420001),
                     lambda s: s['events'][0]['logical_inputs'].pop('red_base'),
                     lambda s: s['initial_logical']['red_outpost'].update(blue=1),
                     lambda s: s['events'][0].update(detector_samples=[]),
                     lambda s: s['events'][0]['credits'][0].update(control={}),
                     lambda s: s['events'][0]['snapshot']['red'].update(control_score=0),
                     lambda s: s['events'][0]['snapshot'].update(simultaneous_destruction_resolved=1),
                     lambda s: s['events'][1]['continuations']['red_outpost'].update(assertion='assumed')]
        for field in ('base_hp', 'attack_damage', 'surviving_robot_hp'):
            for bad in (-1, 2**63, True, 1.0, '1', []):
                mutations.append(lambda s, f=field, b=bad: s['events'][0]['snapshot']['red'].update({f: b}))
        for bad in (0, -1, 2**64, True, 1.0):
            mutations.append(lambda s, b=bad: s['events'][0]['credits'][0].update(sequence=b))
        for i, mutate in enumerate(mutations):
            with self.subTest(case=i):
                s = fixture(); mutate(s)
                with self.assertRaises(cli.IntegrationError):
                    cli.validate_scenario(s)

    def test_malformed_json_and_resource_limits(self):
        for raw in (b'{"x":1,"x":2}', b'{"a":{"x":1,"x":2}}', b'{', b'[] trailing',
                    b'NaN', b'Infinity', b'1.5', b'1e2', b'1'*21, b'"\xff"', b'['*33+b']'*33):
            with self.subTest(raw=raw[:40]), self.assertRaises(cli.IntegrationError):
                cli.decode(raw, cli.MAX_SCENARIO_BYTES)
        with self.assertRaises(cli.IntegrationError):
            cli.decode(b' '*(cli.MAX_SCENARIO_BYTES+1), cli.MAX_SCENARIO_BYTES)
        s = fixture(); s['events'] = [event(n, n) for n in range(1, 514)]
        with self.assertRaises(cli.IntegrationError): cli.validate_scenario(s)
        s = fixture(); s['events'][0]['credits'] = [credit(n) for n in range(1, 4098)]
        with self.assertRaises(cli.IntegrationError): cli.validate_scenario(s)

    def test_readable_utf8_exact_byte_limits_and_no_overwrite(self):
        s = fixture(); s['description'] = '人工裁定🧪' * 100
        log = execute(s)
        with tempfile.TemporaryDirectory() as tmp:
            for name, value, limit in (('scenario', s, cli.MAX_SCENARIO_BYTES), ('log', log, cli.MAX_LOG_BYTES)):
                exact = (json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2, allow_nan=False)+'\n').encode('utf-8')
                out = pathlib.Path(tmp) / (name+'.json')
                with self.assertRaises(cli.IntegrationError): cli.write_json(value, out, len(exact)-1)
                self.assertFalse(out.exists())
                with mock.patch('sys.stdout') as stdout:
                    with self.assertRaises(cli.IntegrationError): cli.write_json(value, '-', len(exact)-1)
                    stdout.write.assert_not_called(); stdout.buffer.write.assert_not_called()
                cli.write_json(value, out, len(exact))
                self.assertEqual(out.read_bytes(), exact)
                self.assertEqual(cli.load(out, limit), value)
                with self.assertRaises((cli.IntegrationError, FileExistsError)):
                    cli.write_json(value, out, len(exact))
                self.assertEqual(out.read_bytes(), exact)

    def test_native_protocol_strict_tokens(self):
        good = cli.protocol(fixture())
        invalid = [b'', good+b'junk\n', good+b'\n', good.replace(b'END\n', b'END junk\n'), b'x'*16385+b'\n']
        for old, new in ((b'5000', b'5000junk'), (b'5000', b'-5000'), (b'9000', b'9223372036854775808')):
            self.assertIn(old, good)
            invalid.append(good.replace(old, new, 1))
        replacements = ((b'BOUNDARY 1 5000', b'BOUNDARY 2 5000'),
                        (b'BOUNDARY 2 420000', b'BOUNDARY 2 5000'),
                        (b' none 0 ', b' none 2 '),
                        (b'CREDIT 1\n', b'CREDIT -1\n'),
                        (b'CREDIT 1\n', b'CREDIT 18446744073709551616\n'),
                        (b'POSITION red_outpost none none', b'POSITION red_outpost logical 1 2 none'))
        for old, new in replacements:
            self.assertIn(old, good)
            invalid.append(good.replace(old, new, 1))
        for raw in invalid:
            with self.subTest(raw=raw[:80]):
                result = subprocess.run([str(DRIVER)], input=raw, capture_output=True, timeout=15)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, b'')
                self.assertIn(b'ProtocolInputError', result.stderr)


if __name__ == '__main__':
    unittest.main()
