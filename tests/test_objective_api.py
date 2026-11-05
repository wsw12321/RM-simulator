#!/usr/bin/env python3
"""Objective bridge contract tests. Real native subprocess; no browser-render claims."""
import copy
import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('objective_bridge',ROOT/'server.py')
bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
CONFIG={'boundary_policy':bridge.BOUNDARIES[0],'credit_strategy':bridge.CREDITS[1]}
class ObjectiveAPI(unittest.TestCase):
    def setUp(self):self.server=SimpleNamespace(engine=bridge.EngineProcess(),objective_engine=bridge.ObjectiveProcess())
    def tearDown(self):self.server.engine.close();self.server.objective_engine.close()
    def post(self,path,data):return bridge.objective_post(self.server,'/api/objective/'+path,data)
    def state(self):return self.server.objective_engine.command('STATE')
    def test_mode_isolation_and_replay_continuation(self):
        old=self.server.engine.command('ADVANCE 12 1 0 0 0 0 0');self.post('reset',CONFIG)
        for _ in range(2):self.post('step',{'count':600,'mode':1,'action':[0,0,0,0,0]})
        state=self.state();self.assertEqual(state['objective']['score'],[13,33])
        replay=self.server.objective_engine.command('EXPORT')
        self.assertEqual(self.post('import',replay)['hash'],state['hash'])
        continued=self.post('step',{'count':600,'mode':1,'action':[0,0,0,0,0]})
        self.post('import',replay)
        self.assertEqual(self.post('step',{'count':600,'mode':1,'action':[0,0,0,0,0]})['hash'],continued['hash'])
        self.assertEqual(self.server.engine.command('STATE'),old)
        self.server.engine.command('RESET');self.assertEqual(self.state()['hash'],continued['hash'])
        self.post('reset',{'boundary_policy':bridge.BOUNDARIES[1],'credit_strategy':'disabled'})
        self.assertEqual(self.state()['tick'],0);self.assertIsNone(self.state()['objective']['score'])
    def test_import_rollback_mode_and_config(self):
        self.post('reset',CONFIG);self.post('step',{'count':123,'mode':1,'action':[0,0,0,0,0]})
        replay=self.server.objective_engine.command('EXPORT');before=self.state()
        mutations=[lambda d:d.update(profile='sandbox-v1'),lambda d:d.update(build='bad'),lambda d:d.update(final_hash=0),lambda d:d['frames'][5].__setitem__(11,0),lambda d:d['config'].update(credit_strategy='disabled'),lambda d:d['frames'][0].__setitem__(5,1),lambda d:d.update(extra=1),lambda d:d.update(schema=True),lambda d:d['config'].update(clock_strategy='floor')]
        for mutate in mutations:
            bad=copy.deepcopy(replay);mutate(bad)
            with self.assertRaises(ValueError):self.post('import',bad)
            self.assertEqual(self.state(),before)
        with self.assertRaises(ValueError):bridge.validate_replay(replay)
        with self.assertRaises(ValueError):bridge.validate_objective_replay(self.server.engine.command('EXPORT'))
    def test_malformed_input_atomic(self):
        before=self.state();base={'count':1,'mode':0,'action':[0,0,0,0,0]}
        for key,value in [('count',True),('count',601),('count',0),('mode',3),('action',[0,0,0,0,1]),('action',[1001,0,0,0,0]),('action',[0,0,0.5,0,0]),('action',[0]*6),('unknown',0)]:
            bad=copy.deepcopy(base);bad[key]=value
            with self.assertRaises(ValueError):self.post('step',bad)
            self.assertEqual(self.state(),before)
        for config in ({},{**CONFIG,'boundary_policy':'auto'},{**CONFIG,'extra':1},{**CONFIG,'credit_strategy':True}):
            with self.assertRaises(ValueError):self.post('reset',config)
            self.assertEqual(self.state(),before)
        with self.assertRaises(ValueError):json.loads('{"count":1,"count":2}',object_pairs_hook=bridge.unique_object)
        for cmd in ('RESET 0 1 garbage','ADVANCE 1 0 0 0 0 0 0 garbage','FRAME 0 0 0 0 0 0 0 0 0 0 0 garbage'):
            with self.assertRaises(ValueError):self.server.objective_engine.command(cmd)
            self.assertEqual(self.state(),before)
    def test_full_duration_export_replay(self):
        self.post('reset',CONFIG)
        for _ in range(42):self.post('step',{'count':600,'mode':1,'action':[0,0,0,0,0]})
        result=self.state();self.assertEqual(result['tick'],25200)
        self.assertEqual(result['objective']['credits'],420);self.assertEqual(result['objective']['score'],[413,1233])
        self.assertEqual(result['objective']['result']['outcome'],'blue-wins')
        replay=self.server.objective_engine.command('EXPORT');self.assertLess(len(json.dumps(replay).encode()),4_000_000)
        self.assertEqual(self.post('import',replay)['hash'],result['hash'])
        with self.assertRaises(ValueError):self.post('step',{'count':1,'mode':1,'action':[0,0,0,0,0]})
        self.assertEqual(self.state()['hash'],result['hash'])
    def test_stale_binary_rejected(self):
        from unittest.mock import patch
        with patch.object(bridge,'objective_build_id',return_value='stale'):
            with self.assertRaisesRegex(ValueError,'stale'):bridge.ObjectiveProcess()
if __name__=='__main__':unittest.main()
