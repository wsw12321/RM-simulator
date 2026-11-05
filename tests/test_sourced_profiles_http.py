#!/usr/bin/env python3
"""Additive sourced wrappers with live unchanged native roster authority."""
import copy
import hashlib
import json
import sys
import unittest
import urllib.error
import urllib.request

BASE='http://127.0.0.1:'+str(sys.argv.pop(1) if len(sys.argv)>1 else 8765)
P='/api/roster/profiles/'

def call(path,data=None,raw=None):
    body=raw.encode() if raw is not None else None if data is None else json.dumps(data).encode()
    request=urllib.request.Request(BASE+path,data=body,headers={'Content-Type':'application/json','X-Sandbox-Client':'1'} if body is not None else {})
    try:
        with urllib.request.urlopen(request,timeout=90) as r:return r.status,json.load(r)
    except urllib.error.HTTPError as e:return e.code,json.load(e)

def manual(rid,**kwargs):
    return {'id':rid,'kind':'manual','action':dict(sdk_version=1,dx=0,dy=0,aim_x=10000,aim_y=2900,fire=0,**kwargs)}

class SourcedHTTP(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        status,cls.catalog=call(P+'catalog');assert status==200,cls.catalog
        status,meta=call('/api/roster/meta');assert status==200,meta
        cls.default=meta['default_config']
        cls.first=cls.catalog['presets'][0]['id']

    def ok(self,path,data=None,raw=None):
        code,result=call(path,data,raw);self.assertEqual(code,200,result);return result
    def packet(self):return self.ok(P+'session/state?observer=red-1')
    def mutation(self,path,data,packet=None):
        packet=packet or self.packet()
        return self.ok(P+path,{'expected_revision':packet['run_revision'],'expected_tick':packet['state']['tick'],'request':data})
    def prepare(self,config=None,preset=None):
        config=copy.deepcopy(config or self.default)
        return self.ok(P+'prepare',{'config':config,'presets':{r['id']:preset or self.first for r in config['robots']}})
    def apply(self,envelope=None):
        envelope=envelope or self.prepare()
        return self.mutation('reset',{'observer':'red-1','configuration':envelope})
    def setUp(self):self.ok('/api/roster/reset',{'observer':'red-1','config':self.default})

    def test_catalog_and_partial_roles(self):
        self.assertEqual(len(self.catalog['presets']),46)
        self.assertFalse(self.catalog['official']);self.assertFalse(self.catalog['complete'])
        for role in ('infantry','heavy','sentry'):
            preset=next(p for p in self.catalog['presets'] if p['role']==role)
            envelope=self.prepare(preset=preset['id']);packet=self.apply(envelope)
            self.assertEqual(packet['provenance_status'],'sourced-partial')
            own=next(r for r in envelope['resolved_config']['robots'] if r['id']=='red-1')
            self.assertEqual(packet['state']['observation']['self']['hp'],own['profile']['max_hp'])
            self.assertEqual(packet['state']['observation']['self_heat']['limit_tenths'],own['profile']['heat_limit_tenths'])
            self.assertEqual(set(preset['overrides']),{'max_hp'} if role!='infantry' else {'max_hp','heat_limit_tenths','cooling_per_second'})
            self.assertIn('initialization_note',envelope['provenance']['robots'][0])

    def test_combat_capture_wrapped_replay_and_raw_identity(self):
        config=copy.deepcopy(self.default);config['robots']= [r for r in config['robots'] if r['id'].endswith('-1')]
        red=next(r for r in config['robots'] if r['id']=='red-1');blue=next(r for r in config['robots'] if r['id']=='blue-1')
        red['spawn']={'x':6500,'y':6100,'yaw':0};blue['spawn']={'x':7200,'y':6100,'yaw':128}
        config['heat_threshold_policy']='ENGINEERING-flowchart-greater-equal-v1'
        envelope=self.prepare(config);packet=self.apply(envelope)
        controllers=[manual(r['id']) for r in envelope['resolved_config']['robots']]
        controllers[1]['action'].update(fire=1,aim_x=10000,aim_y=6100)
        packet=self.mutation('session/step',{'observer':'red-1','count':480,'controllers':controllers},packet)
        obs=packet['state']['observation'];self.assertGreater(obs['self_heat']['heat_tenths'],0)
        self.assertLess(obs['self']['ammo'],red['profile']['ammo'])
        self.assertEqual(obs['zones'][1]['controller'],0)
        self.assertEqual(self.ok('/api/roster/state?observer=blue-1')['observation']['self']['hp'],0)
        self.assertNotIn('hash',packet['state']);self.assertNotIn('damage_admin',packet['state'])
        wrapper=self.ok(P+'replay');raw=self.ok('/api/roster/replay');self.assertEqual(wrapper['native_replay'],raw)
        imported=self.mutation('import',{'observer':'red-1','replay':wrapper});self.assertEqual(imported['state']['verified_frames'],480)
        self.assertEqual(self.ok('/api/roster/replay'),raw)
        self.ok('/api/roster/import',{'observer':'red-1','replay':raw});self.assertEqual(self.packet()['provenance_status'],'synthetic')
        code,_=call(P+'replay');self.assertEqual(code,400)
        self.assertEqual(self.ok('/api/roster/replay'),raw)

    def test_raw_reset_import_clear_even_identical_config_and_step_carries(self):
        envelope=self.prepare();self.apply(envelope)
        controllers=[manual(r['id']) for r in envelope['resolved_config']['robots']]
        self.ok('/api/roster/step',{'observer':'red-1','count':1,'controllers':controllers})
        self.assertEqual(self.packet()['active_config'],envelope)
        raw=self.ok('/api/roster/replay');self.ok('/api/roster/import',{'observer':'red-1','replay':raw})
        self.assertIsNone(self.packet()['active_config'])
        self.apply(envelope);self.ok('/api/roster/reset',{'observer':'red-1','config':envelope['resolved_config']})
        self.assertIsNone(self.packet()['active_config'])

    def test_rejections_preserve_state_and_context_and_duplicate_keys(self):
        envelope=self.prepare();before=self.apply(envelope);raw=self.ok('/api/roster/replay')
        changes=[lambda e:e.update(official=True),lambda e:e.update(complete=True),lambda e:e.update(extra_claim='official hero'),lambda e:e['provenance']['catalog'].update(sha256='0'*64),lambda e:e['provenance']['robots'][0]['fields']['max_hp']['source'].update(printed_page=1),lambda e:e['resolved_config']['robots'][0]['profile'].update(max_hp=999),lambda e:e['selection']['robots'][0]['conditions'].update(dynamic_progression='enabled')]
        for change in changes:
            bad=copy.deepcopy(envelope);change(bad)
            body={'expected_revision':before['run_revision'],'expected_tick':0,'request':{'observer':'red-1','configuration':bad}}
            code,_=call(P+'reset',body);self.assertEqual(code,400);self.assertEqual(self.packet(),before);self.assertEqual(self.ok('/api/roster/replay'),raw)
        text=json.dumps({'expected_revision':before['run_revision'],'expected_tick':0,'request':{'observer':'red-1','configuration':envelope}}).replace('"official": false','"official": true, "official": false',1)
        code,result=call(P+'reset',raw=text);self.assertEqual(code,400);self.assertIn('Duplicate',result['error']);self.assertEqual(self.packet(),before)
        for invalid in [True,1.0,'1',None]:
            body={'expected_revision':invalid,'expected_tick':0,'request':{'observer':'red-1','configuration':envelope}}
            code,_=call(P+'reset',body);self.assertEqual(code,400);self.assertEqual(self.packet(),before)

    def test_stale_revision_and_tick_reject_before_mutation(self):
        envelope=self.prepare();old=self.apply(envelope)
        self.ok('/api/roster/reset',{'observer':'red-1','config':envelope['resolved_config']})
        controllers=[manual(r['id']) for r in envelope['resolved_config']['robots']]
        for action,request in [('session/step',{'observer':'red-1','count':1,'controllers':controllers}),('reset',{'observer':'red-1','configuration':envelope})]:
            code,result=call(P+action,{'expected_revision':old['run_revision'],'expected_tick':0,'request':request})
            self.assertEqual(code,409,result);self.assertTrue(result['resync_required']);self.assertEqual(self.packet()['state']['tick'],0);self.assertIsNone(self.packet()['active_config'])
        now=self.packet();code,_=call(P+'reset',{'expected_revision':now['run_revision'],'expected_tick':1,'request':{'observer':'red-1','configuration':envelope}});self.assertEqual(code,409)

    def test_failed_native_import_and_unknown_source_options(self):
        envelope=self.prepare();before=self.apply(envelope);wrapper=self.ok(P+'replay')
        wrapper['native_replay']['initial_hash']^=1;wrapper['native_replay']['final_hash']^=1
        wrapper['native_replay_sha256']=hashlib.sha256(json.dumps(wrapper['native_replay'],sort_keys=True,separators=(',',':'),ensure_ascii=True).encode()).hexdigest()
        code,_=call(P+'import',{'expected_revision':before['run_revision'],'expected_tick':0,'request':{'observer':'red-1','replay':wrapper}});self.assertEqual(code,400);self.assertEqual(self.packet(),before)
        for role in ('infantry','heavy','sentry'):
            p=next(p for p in self.catalog['presets'] if p['role']==role);selection=self.prepare(preset=p['id'])['selection']
            for value in [True,2.0,'official-all-options',None]:
                bad=copy.deepcopy(selection);bad['robots'][0]['preset_id']=value;code,_=call(P+'resolve',bad);self.assertEqual(code,400)
            bad=copy.deepcopy(selection);bad['robots'][0]['conditions']['respawn']='official';code,_=call(P+'resolve',bad);self.assertEqual(code,400)

if __name__=='__main__':unittest.main()
