import importlib.util
import unittest
from unittest.mock import patch
from pathlib import Path
import threading, json, urllib.request, urllib.error
spec=importlib.util.spec_from_file_location('bridge',Path(__file__).resolve().parents[1]/'server.py')
bridge=importlib.util.module_from_spec(spec);spec.loader.exec_module(bridge)
class Bridge(unittest.TestCase):
    def test_candidate_build_checked_after_rebuild(self):
        real=bridge.EngineProcess();replay=real.command('EXPORT');initial=real.command('STATE')
        class Rebuilt:
            closed=False
            def command(self,command):return {**initial,'build':'a-different-build'}
            def close(self):self.closed=True
        candidate=Rebuilt()
        server=bridge.ThreadingHTTPServer(('127.0.0.1',0),bridge.Handler);server.engine=real
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        try:
            with patch.object(bridge,'EngineProcess',return_value=candidate):
                req=urllib.request.Request(f'http://127.0.0.1:{server.server_port}/api/import',data=json.dumps(replay).encode(),headers={'X-Sandbox-Client':'1','Content-Type':'application/json'})
                with self.assertRaises(urllib.error.HTTPError) as caught:urllib.request.urlopen(req)
                self.assertEqual(caught.exception.code,400)
                self.assertIn('New engine binary differs',caught.exception.read().decode())
            self.assertIs(server.engine,real);self.assertTrue(candidate.closed)
        finally:server.shutdown();server.server_close();real.close();thread.join()
if __name__=='__main__':unittest.main()
