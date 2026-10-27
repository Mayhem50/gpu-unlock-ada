import copy
import hashlib
import json
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from compare import compare


def fixture():
    data=json.loads((ROOT/'build/host-capture.json').read_text())
    target=data['display_devices'][0]['path']['hex']
    gop=data['gops'][0]['path']['hex']
    handles=[{'id':i,'path':None,'active_opens':[]} for i in range(8)]
    handles[0]['path']=target;handles[1]['path']=gop;handles[7]['path']='aabb'
    def edge(agent,controller,protocol,attributes):
        return dict(agent=agent,controller=controller,protocol=protocol*16,attributes=attributes,count=1)
    handles[0]['active_opens']=[edge(3,1,'11','0x00000008'),edge(3,0,'22','0x00000010')]
    handles[1]['active_opens']=[edge(4,1,'33','0x00000010'),edge(4,2,'33','0x00000008'),
                               edge(5,1,'44','0x00000010')]
    handles[7]['active_opens']=[edge(6,2,'55','0x00000008')]
    agents=[]
    for i in (3,4,5):
        raw=bytes([i])
        agents.append(dict(agent=i,image_handle=i,source_handle=0,
            file_path=f'0404{i:04x}',version='0x00000010',image_bytes=512,code_type=3,
            stop_rva='0x00000100',binding_status='0x0000000000000000',
            image_status='0x0000000000000000',binding_handle_matches=True,
            code=dict(rva='0x00000100',length=1,relocations_in_code=0,
                      loaded_sha256=hashlib.sha256(raw).hexdigest(),hex=raw.hex())))
    data['platform']=dict(available=True,complete=True,atomic_snapshot=False,release_authorized=False,
        target=0,conout=2,conin=2,stderr=2,handles=handles,handle_count=8,edge_count=6,gpu_agents=agents)
    return data


def permuted(data):
    data=copy.deepcopy(data);p=data['platform'];n=len(p['handles'])
    remap={i:n-1-i for i in range(n)}
    p['handles'].reverse()
    for h in p['handles']:
        h['id']=remap[h['id']]
        for e in h['active_opens']:
            for field in ('agent','controller'):e[field]=remap[e[field]]
    for field in ('target','conout','conin','stderr'):p[field]=remap[p[field]]
    for a in p['gpu_agents']:
        for field in ('agent','image_handle','source_handle'):a[field]=remap[a[field]]
    return data


class OwnershipTests(unittest.TestCase):
    def test_gpu_chain_resolves_without_driver_or_console_device_paths(self):
        data=fixture();r=compare(data,data)
        self.assertTrue(r['gpu_ownership_matches'])
        self.assertIsNone(r['boundary_ownership_matches'])
        self.assertIsNone(r['graph_matches'])
        diag=r['ownership_diagnostics']['left']
        self.assertEqual(diag['scope_counts']['gpu'],dict(edges=5,resolved_edges=5,unresolved_edges=0))
        self.assertEqual(diag['roots']['conout']['method'],'rooted_console_roles')
        self.assertEqual(diag['remaining_unidentified_handles'],[
            {'handle':6,'reason':'no_path_or_verified_driver_identity'}])
        self.assertFalse(r['unlock_ready'])
        self.assertFalse(r['ownership_diagnostics']['release_authorized'])

    def test_renumbering_handles_does_not_change_gpu_identity(self):
        data=fixture();self.assertTrue(compare(data,permuted(data))['gpu_ownership_matches'])

    def test_reassigning_console_role_is_not_hidden_by_role_anchors(self):
        data=fixture();other=copy.deepcopy(data);other['platform']['conin']=0
        r=compare(data,other)
        self.assertFalse(r['console_role_aliases_match'])
        self.assertFalse(r['gpu_ownership_matches'])

    def test_reassigning_driver_changes_gpu_graph_even_when_code_set_matches(self):
        data=fixture();other=copy.deepcopy(data)
        other['platform']['handles'][1]['active_opens'][0]['agent']=5
        r=compare(data,other)
        self.assertTrue(r['driver_code_matches'])
        self.assertFalse(r['gpu_ownership_matches'])

    def test_missing_driver_code_keeps_gpu_graph_unknown(self):
        data=fixture();data['platform']['gpu_agents'][0]['code']['hex']=None
        r=compare(data,data)
        self.assertIsNone(r['gpu_ownership_matches'])
        self.assertEqual(r['ownership_diagnostics']['left']['driver_rejections'][0]['handle'],3)

    def test_relocated_or_unbound_driver_cannot_be_an_identity_anchor(self):
        for field,value in (('relocations_in_code',1),('binding_handle_matches',False)):
            data=fixture();a=data['platform']['gpu_agents'][0]
            if field=='relocations_in_code':a['code'][field]=value
            else:a[field]=value
            with self.subTest(field=field):
                self.assertIsNone(compare(data,data)['gpu_ownership_matches'])

    def test_duplicate_driver_identities_are_ambiguous(self):
        data=fixture();p=data['platform']
        p['handles'].append(dict(id=8,path=None,active_opens=[]));p['handle_count']=9
        duplicate=copy.deepcopy(p['gpu_agents'][0]);duplicate.update(agent=8,image_handle=8)
        p['gpu_agents'].append(duplicate)
        self.assertIsNone(compare(data,data)['gpu_ownership_matches'])

    def test_duplicate_paths_are_not_unique_hardware_anchors(self):
        data=fixture();p=data['platform']
        p['handles'][6]['path']=p['handles'][0]['path']
        r=compare(data,data)
        self.assertIsNone(r['gpu_ownership_matches'])
        self.assertEqual(r['ownership_diagnostics']['left']['roots']['target']['reason'],'duplicate_device_path')

    def test_identifying_boundary_owner_makes_full_graph_comparable(self):
        data=fixture();data['platform']['handles'][6]['path']='ccdd'
        r=compare(data,permuted(data))
        self.assertTrue(r['graph_matches'])
        self.assertTrue(r['boundary_ownership_matches'])

    def test_changed_edge_multiplicity_is_detected(self):
        data=fixture();other=copy.deepcopy(data)
        other['platform']['handles'][1]['active_opens'][0]['count']=2
        self.assertFalse(compare(data,other)['gpu_ownership_matches'])

    def test_zero_open_count_is_rejected(self):
        data=fixture();data['platform']['handles'][1]['active_opens'][0]['count']=0
        with self.assertRaises(ValueError):compare(data,data)

    def test_cli_uses_independent_manifests_and_refuses_report_overwrite(self):
        with tempfile.TemporaryDirectory() as folder:
            folder=Path(folder);left=fixture();right=permuted(left);right['build_id']='other-reviewed-build'
            for name,data in (('left.json',left),('right.json',right),
                              ('left-manifest.json',{'build_id':left['build_id']}),
                              ('right-manifest.json',{'build_id':right['build_id']})):
                (folder/name).write_text(json.dumps(data),encoding='utf-8')
            command=[sys.executable,str(ROOT/'tools/compare.py'),str(folder/'left.json'),str(folder/'right.json'),
                     '--left-manifest',str(folder/'left-manifest.json'),'--right-manifest',str(folder/'right-manifest.json'),
                     '--output',str(folder/'report.json')]
            first=subprocess.run(command,capture_output=True,text=True)
            self.assertEqual(first.returncode,0,first.stderr)
            original=(folder/'report.json').read_bytes()
            self.assertTrue(json.loads(original)['gpu_ownership_matches'])
            second=subprocess.run(command,capture_output=True,text=True)
            self.assertNotEqual(second.returncode,0)
            self.assertEqual((folder/'report.json').read_bytes(),original)

    def test_cli_rejects_wrong_capture_manifest(self):
        with tempfile.TemporaryDirectory() as folder:
            folder=Path(folder);data=fixture()
            (folder/'capture.json').write_text(json.dumps(data),encoding='utf-8')
            (folder/'manifest.json').write_text(json.dumps({'build_id':'wrong-build'}),encoding='utf-8')
            run=subprocess.run([sys.executable,str(ROOT/'tools/compare.py'),str(folder/'capture.json'),
                                str(folder/'capture.json'),'--manifest',str(folder/'manifest.json')],
                               capture_output=True,text=True)
            self.assertNotEqual(run.returncode,0)
            self.assertIn('build_id',run.stderr)
