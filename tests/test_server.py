#!/usr/bin/env python3
"""Integration checks against a separately launched local service."""
import json, sys, unittest, urllib.request, urllib.error
BASE='http://127.0.0.1:'+str(sys.argv.pop(1) if len(sys.argv)>1 else 8765)
def call(path,body=None,headers=None):
    h={'X-Sandbox-Client':'1','Content-Type':'application/json',**(headers or {})}
    req=urllib.request.Request(BASE+'/api/'+path,data=None if body is None else json.dumps(body).encode(),headers=h)
    try:
        with urllib.request.urlopen(req,timeout=10) as r:return r.status,json.load(r)
    except urllib.error.HTTPError as e:return e.code,json.load(e)
class API(unittest.TestCase):
    def setUp(self):call('reset',{})
    def test_meta(self):
        status,data=call('meta');self.assertEqual(status,200);self.assertFalse(data['official'])
    def test_step_and_replay(self):
        for _ in range(10):status,state=call('step',{'count':6,'mode':1,'action':[0,0,5000,3000,0]});self.assertEqual(status,200)
        _,replay=call('replay');self.assertEqual(len(replay['frames']),60)
        call('reset',{});status,result=call('import',replay)
        self.assertEqual(status,200);self.assertEqual(result['hash'],state['hash']);self.assertEqual(result['verified_frames'],60)
    def test_invalid_action_atomic(self):
        _,before=call('state');status,_=call('step',{'count':1,'mode':0,'action':[1001,0,0,0,0]});self.assertEqual(status,400)
        _,after=call('state');self.assertEqual(before['hash'],after['hash'])
    def test_wrong_origin(self):
        status,_=call('reset',{}, {'Origin':'https://example.org'});self.assertEqual(status,403)
    def test_corrupt_replay_keeps_match(self):
        call('step',{'count':6,'mode':0,'action':[1000,0,5000,3000,0]});_,before=call('state');_,replay=call('replay')
        replay['frames'][2][11]^=1;status,_=call('import',replay);self.assertEqual(status,400)
        _,after=call('state');self.assertEqual(before['hash'],after['hash'])
    def test_wrong_build_rejected(self):
        _,replay=call('replay');replay['build']='other-build';self.assertEqual(call('import',replay)[0],400)
    def test_bool_not_accepted_as_int(self):
        self.assertEqual(call('step',{'count':True,'mode':0,'action':[0,0,0,0,0]})[0],400)
    def test_wrong_schema_rejected(self):
        self.assertEqual(call('import',{'schema':999})[0],400)
if __name__=='__main__':unittest.main()
