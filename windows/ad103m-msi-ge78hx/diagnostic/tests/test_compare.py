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
            'handle_count':2,'edge_count':1,'gpu_agents':[],
            'handles':[
                {'id':0,'path':'0101','active_opens':[{'agent':1,'controller':0,'protocol':'00'*16,'attributes':'0x00000010','count':1}]},
                {'id':1,'path':'0202','active_opens':[]}]}
        changed=copy.deepcopy(data)
        changed['platform']['handles']=[
            {'id':0,'path':'0202','active_opens':[]},
            {'id':1,'path':'0101','active_opens':[{'agent':0,'controller':1,'protocol':'00'*16,'attributes':'0x00000010','count':1}]}]
        self.assertTrue(compare.compare(data,changed)['graph_matches'])
