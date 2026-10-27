import copy
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from analyze import analyze


class ClassificationTests(unittest.TestCase):
    def setUp(self):
        self.data=json.loads((ROOT/'build/sec2-capture.json').read_text())

    def test_sec2_unavailable_retains_partial_status_but_prom_is_captured(self):
        result=analyze(self.data,self.data['build_id'])
        self.assertEqual(result['register_classification'],'sec2_cpuctl_unavailable')
        self.assertFalse(result['register_snapshot_complete'])
        self.assertTrue(result['physical_prom_complete'])
        self.assertFalse(result['unlock_ready'])

    def test_invalid_continuation_is_rejected(self):
        mutations=[
            lambda d:d['register_snapshot'].update(complete=True),
            lambda d:d['register_snapshot'].update(final_chip='0x194000a1'),
            lambda d:d['register_snapshot'].update(pci_snapshot_unchanged=False),
            lambda d:d['register_snapshot'].update(prom_read_allowed=False),
            lambda d:d['register_snapshot'].update(classification='ignore_all_errors'),
            lambda d:d['register_snapshot']['samples'][21].update(value='0x00000010'),
            lambda d:d['register_snapshot']['samples'][21].update(status='0x8000000000000007'),
            lambda d:d['register_snapshot']['samples'][1].update(value='0xbadf0000'),
        ]
        for i,mutate in enumerate(mutations):
            data=copy.deepcopy(self.data);mutate(data)
            with self.subTest(case=i),self.assertRaises(ValueError):
                analyze(data,data['build_id'])

    def test_historical_capture_remains_readable_with_its_own_build_id(self):
        data=copy.deepcopy(self.data)
        data['probe_version']='0.4.0'
        data['register_snapshot'].pop('classification')
        data['register_snapshot'].pop('prom_read_allowed')
        data['register_snapshot']['read_attempts']=22
        data['register_snapshot']['final_chip']=None
        data['physical_prom']={'guard_passed':False,'read_attempts':0,'complete':False}
        result=analyze(data,data['build_id'])
        self.assertFalse(result['physical_prom_complete'])
