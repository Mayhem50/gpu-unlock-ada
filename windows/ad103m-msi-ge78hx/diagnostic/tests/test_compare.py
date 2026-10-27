import copy
import importlib.util
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import compare

class ComparisonTests(unittest.TestCase):
    def setUp(self):
        self.data = json.loads((ROOT/'build/host-capture.json').read_text())
    def test_unknown_graph_is_not_a_match(self):
        result = compare.compare(self.data,self.data)
        self.assertTrue(result['inventory_matches'])
        self.assertIsNone(result['graph_matches'])
        self.assertFalse(result['unlock_ready'])
    def test_volatile_registers_do_not_change_inventory(self):
        changed = copy.deepcopy(self.data)
        changed['register_snapshot']['samples'][12]['value']='0x12345678'
        self.assertTrue(compare.compare(self.data,changed)['inventory_matches'])
    def test_bar_change_is_reported(self):
        changed = copy.deepcopy(self.data)
        changed['display_devices'][0]['bars'][0]['base']='0x0000000090000000'
        self.assertFalse(compare.compare(self.data,changed)['inventory_matches'])
    def test_revision_mismatch_rejected(self):
        changed = copy.deepcopy(self.data)
        changed['display_devices'][0]['revision']=0xa2
        with self.assertRaises(ValueError):
            compare.compare(self.data,changed)
    def test_graph_ids_are_normalized(self):
        data=copy.deepcopy(self.data)
        data['platform']={
            'available':True,'complete':True,'atomic_snapshot':False,'release_authorized':False,
            'target':0,'conout':1,'conin':1,'stderr':1,
            'handle_count':2,'edge_count':1,'gpu_agents':[],
            'handles':[
                {'id':0,'path':data['display_devices'][0]['path']['hex'],'active_opens':[{'agent':1,'controller':0,'protocol':'00'*16,'attributes':'0x00000010','count':1}]},
                {'id':1,'path':'0202','active_opens':[]}]}
        changed=copy.deepcopy(data)
        changed['platform']['handles']=[
            {'id':0,'path':'0202','active_opens':[]},
            {'id':1,'path':data['display_devices'][0]['path']['hex'],'active_opens':[{'agent':0,'controller':1,'protocol':'00'*16,'attributes':'0x00000010','count':1}]}]
        changed['platform'].update(target=1,conout=0,conin=0,stderr=0)
        self.assertTrue(compare.compare(data,changed)['graph_matches'])
        changed['platform']['conout']=1
        self.assertFalse(compare.compare(data,changed)['graph_matches'])

    def test_gop_parent_change_is_detected(self):
        changed=copy.deepcopy(self.data)
        changed['gops'][0]['display_parent_index']=1
        self.assertFalse(compare.compare(self.data,changed)['topology_matches'])

    def test_console_change_is_detected(self):
        changed=copy.deepcopy(self.data)
        changed['console_out_handle_path']['hex']='changed-path'
        self.assertFalse(compare.compare(self.data,changed)['topology_matches'])

    def test_complete_topology_can_match_and_incomplete_is_unknown(self):
        self.assertIsNone(compare.compare(self.data,self.data)['topology_matches'])
        data=copy.deepcopy(self.data)
        data['console_in_handle_path']=copy.deepcopy(data['display_devices'][1]['path'])
        self.assertTrue(compare.compare(data,data)['topology_matches'])

    def test_console_variable_change_is_detected(self):
        changed=copy.deepcopy(self.data)
        changed['conout_variable']={'status':'0x0000000000000000','path':{'valid':True,'hex':'new-path'}}
        self.assertFalse(compare.compare(self.data,changed)['topology_matches'])

    def test_reordered_displays_keep_gop_associations(self):
        changed=copy.deepcopy(self.data)
        changed['display_devices'].reverse()
        for g in changed['gops']:
            g['display_parent_index']=1-g['display_parent_index']
        a=compare.normalized(self.data); b=compare.normalized(changed)
        self.assertEqual(a['topology'],b['topology'])

    def test_empty_complete_graph_is_rejected(self):
        changed=copy.deepcopy(self.data)
        changed['platform']={'available':True,'complete':True,'atomic_snapshot':False,'release_authorized':False,
                            'handle_count':0,'edge_count':0,'handles':[],'gpu_agents':[]}
        with self.assertRaises(ValueError):
            compare.compare(changed,changed)

    def test_complete_graph_target_references_are_required(self):
        for ref in (None,True,-1,1,99):
            changed=copy.deepcopy(self.data)
            changed['platform']={'available':True,'complete':True,'atomic_snapshot':False,'release_authorized':False,
                                'handle_count':1,'edge_count':0,'gpu_agents':[],'target':ref,'conout':0,'conin':0,'stderr':0,
                                'handles':[{'id':0,'path':changed['display_devices'][0]['path']['hex'],'active_opens':[]}]}
            with self.subTest(ref=ref),self.assertRaises(ValueError):
                compare.compare(changed,changed)
