#!/usr/bin/env python3
"""HTTP endpoint tests against an explicitly launched loopback server."""
import copy,json,sys,unittest,urllib.error,urllib.request
BASE='http://127.0.0.1:'+str(sys.argv.pop(1) if len(sys.argv)>1 else 8765)
CONFIG={'boundary_policy':'strict-reject-unadjudicated-v1','credit_strategy':'EXPERIMENTAL-end-of-second-v1'}
def call(path,body=None,headers=None,raw=None):
    data=raw if raw is not None else (None if body is None else json.dumps(body).encode())
    req=urllib.request.Request(BASE+'/api/'+path,data=data,headers={'X-Sandbox-Client':'1','Content-Type':'application/json',**(headers or {})})
    try:
        with urllib.request.urlopen(req,timeout=30) as response:return response.status,json.load(response)
    except urllib.error.HTTPError as error:return error.code,json.load(error)
class HTTP(unittest.TestCase):
    def setUp(self):self.assertEqual(call('objective/reset',CONFIG)[0],200)
    def test_meta_state_and_mode_isolation(self):
        self.assertEqual(call('objective/meta')[1]['profile'],'objective-experimental-v1')
        self.assertEqual(len(call('objective/meta')[1]['zones']),4)
        _,sandbox=call('state');_,objective=call('objective/state')
        for _ in range(2):
            status,state=call('objective/step',{'count':600,'mode':1,'action':[0,0,0,0,0]});self.assertEqual(status,200)
        self.assertEqual(state['objective']['score'],[13,33]);self.assertEqual(call('state')[1],sandbox)
        call('reset',{});self.assertEqual(call('objective/state')[1],state)
        _,replay=call('objective/replay');status,restored=call('objective/import',replay);self.assertEqual(status,200);self.assertEqual(restored['hash'],state['hash'])
        self.assertEqual(call('import',replay)[0],400)
        _,other=call('replay');self.assertEqual(call('objective/import',other)[0],400)
        self.assertEqual(call('objective/state')[1]['hash'],state['hash'])
        self.assertNotEqual(state['hash'],objective['hash'])
    def test_malformed_bodies_and_origin(self):
        _,before=call('objective/state')
        bodies=[b'{"\\ud800":1,"\\ud800":2}',b'{"count":1,"count":2}',b'{"frames":'+b'['*2000+b']'*2000+b'}',b'null',b'[]',b'{"count":NaN}',b'{}']
        for raw in bodies:
            status,response=call('objective/step',raw=raw);self.assertEqual(status,400,(raw[:50],response))
            self.assertIn('error',response);self.assertEqual(call('objective/state')[1],before)
        self.assertEqual(call('objective/reset',CONFIG,{'Origin':'https://example.com'})[0],403)
        self.assertEqual(call('objective/reset',CONFIG,{'X-Sandbox-Client':'0'})[0],403)
        self.assertEqual(call('objective/state',headers={'Host':'attacker.invalid'})[0],403)
    def test_config_replay_and_fire_rollback(self):
        call('objective/step',{'count':120,'mode':1,'action':[0,0,0,0,0]});_,before=call('objective/state');_,replay=call('objective/replay')
        tampered=copy.deepcopy(replay);tampered['frames'][60][11]^=1
        self.assertEqual(call('objective/import',tampered)[0],400)
        self.assertEqual(call('objective/step',{'count':1,'mode':0,'action':[0,0,0,0,1]})[0],400)
        self.assertEqual(call('objective/reset',{**CONFIG,'boundary_policy':'automatic'})[0],400)
        self.assertEqual(call('objective/state')[1],before)
if __name__=='__main__':unittest.main()
