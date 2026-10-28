import json
from pathlib import Path
import unittest
from release_dependencies import storage_scope,audit


class ScopeTests(unittest.TestCase):
    def fixture(self):
        root='02010c00d041030a00000000';usb='030506000000';end='7fff0400'
        e=lambda a,c:dict(agent=a,controller=c,protocol='00'*16,attributes='0x00000008',count=1)
        return dict(handles=[dict(id=0,path=root+end,active_opens=[e(4,1),e(4,2)]),
            dict(id=1,path=root+usb+end,active_opens=[]),dict(id=2,path=root+'010106000001'+end,active_opens=[]),
            dict(id=4,path=None,active_opens=[])])

    def test_shared_parent_does_not_protect_all_siblings(self):
        nodes,edges=storage_scope(self.fixture(),1)
        self.assertEqual(nodes,{0,1});self.assertEqual([e['controller'] for e in edges],[1])

    def test_hidden_parent_is_added_without_forward_expansion(self):
        p=self.fixture();p['handles'][0]['path']=None
        nodes,_=storage_scope(p,1);self.assertEqual(nodes,{0,1})

    def test_unknown_agent_refuses(self):
        p=self.fixture();p['handles'][0]['active_opens'][0]['agent']=99
        with self.assertRaises(ValueError):storage_scope(p,1)

    def test_duplicate_handle_refuses(self):
        p=self.fixture();p['handles'].append(p['handles'][0].copy())
        with self.assertRaises(ValueError):storage_scope(p,1)

    def test_private_capture_six(self):
        file=Path(__file__).resolve().parents[1]/'captures/2026-10-28-run6/CAP0006.JSON'
        if not file.exists():self.skipTest('Private capture unavailable')
        r=audit(json.loads(file.read_text(encoding='utf-8-sig')))
        self.assertEqual(r['storage_controllers'],[139,384,414,415])
        self.assertEqual(r['storage_agents_without_driver_binding_code'],[90,92,94,327,328,354])
        self.assertEqual(r['shared_release_preserved_images'],[320])
        self.assertEqual(r['agent_overlap'],[]);self.assertFalse(r['release_authorized'])


if __name__=='__main__':unittest.main()
