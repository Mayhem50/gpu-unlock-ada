import copy
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from selector_analysis import OFFSETS,analyze_selectors


def fixture(ss0):
    values={'SS0':ss0,'SS1':0,'FEAT_READOUT_1':0x410080}
    return dict(register_snapshot=dict(samples=[dict(name=n,offset=f'0x{o:08x}',value=f'0x{values[n]:08x}',status='0x0000000000000000') for n,o in OFFSETS.items()]))


class SelectorTests(unittest.TestCase):
    def test_capture_four_to_five_exact_delta(self):
        r=analyze_selectors(fixture(0x04003065),fixture(0x04203025))
        self.assertEqual(r['ss0_xor'],'0x00200040');self.assertEqual(r['changed_bits'],[6,21])
        self.assertEqual(r['changed_fields'],[dict(index=1,bits=[4,7],left=6,right=2),dict(index=5,bits=[20,23],left=0,right=2)])
        self.assertTrue(r['readout_unchanged']);self.assertTrue(r['ss1_unchanged'])
        self.assertEqual(r['readout_fields_right'],[0,0,2,0,0,2,0,2,0])
        for key in ('full_readout_left','full_readout_right','cause_established','field_unit_mapping_established','admission_widened','unlock_ready'):
            self.assertFalse(r[key])

    def test_missing_duplicate_badf_or_transport_error_is_not_zero(self):
        for mode in ('missing','duplicate','badf','transport','offset'):
            a=fixture(0x04003065);b=copy.deepcopy(a);s=b['register_snapshot']['samples']
            if mode=='missing':s.pop()
            if mode=='duplicate':s.append(copy.deepcopy(s[0]))
            if mode=='badf':s[0]['value']='0xbadf5620'
            if mode=='transport':s[0]['status']='0x8000000000000007'
            if mode=='offset':s[0]['offset']='0x00823820'
            with self.subTest(mode=mode),self.assertRaises(ValueError):analyze_selectors(a,b)

    def test_private_pair_matches_report_without_modifying_captures(self):
        paths=[ROOT.parent/'captures'/f'{date}-run{n}'/f'CAP000{n}.JSON' for n,date in ((4,'2026-10-27'),(5,'2026-10-28'))]
        if not all(p.exists() for p in paths):self.skipTest('Private capture 4/5 pair unavailable')
        raw=[p.read_bytes() for p in paths]
        r=analyze_selectors(*(json.loads(b.decode('utf-8-sig')) for b in raw))
        self.assertEqual(r['ss0_xor'],'0x00200040');self.assertTrue(r['readout_unchanged'])
        self.assertEqual([p.read_bytes() for p in paths],raw)
