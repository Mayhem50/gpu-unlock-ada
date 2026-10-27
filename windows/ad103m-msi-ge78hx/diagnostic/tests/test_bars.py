import copy
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from analyze import validate_bar_layout


class BarTests(unittest.TestCase):
    def setUp(self):
        self.display=json.loads((ROOT/'build/bar-layout.json').read_text())

    def test_msi_layout_uses_four_logical_indices(self):
        validate_bar_layout(self.display)
        bars=self.display['bars']
        self.assertEqual([b['config_slot'] for b in bars],[0,1,3,5])
        self.assertEqual([b['uefi_index'] for b in bars],[0,1,2,3])
        self.assertEqual([b['base'] for b in bars],['0x000000005e000000','0x0000006000000000','0x0000006400000000','0x0000000000005000'])
        self.assertTrue(all(b['resource']['matches_config'] for b in bars))
        self.assertEqual([b['resource']['type'] for b in bars],[0,0,0,1])

    def test_slot_index_cannot_replace_protocol_index(self):
        for key,value in (('uefi_index',3),('config_slot',2),('config_offset','0x18'),
                          ('width64',False),('base','0x000000000000000c')):
            changed=copy.deepcopy(self.display);changed['bars'][2][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):
                validate_bar_layout(changed)

    def test_missing_or_extra_high_dword_entry_is_rejected(self):
        for bars in (self.display['bars'][:-1],self.display['bars']+[self.display['bars'][1]]):
            changed=copy.deepcopy(self.display);changed['bars']=bars
            with self.assertRaises(ValueError):
                validate_bar_layout(changed)
