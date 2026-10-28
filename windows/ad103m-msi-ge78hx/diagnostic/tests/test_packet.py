import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import packet
from analyze import analyze


class PacketTests(unittest.TestCase):
    def fixture(self,folder):
        root=Path(folder);build=root/'build';build.mkdir()
        self.snapshot={'src/probe.c':'1'*64,'Build.ps1':'2'*64,
                       '../../ad104-msi/vendor/edk2/headers.json':'3'*64}
        identity='probe.c='+'1'*64+'\nheaders='+'3'*64+'\nbuild='+'2'*64
        build_id=hashlib.sha256(identity.encode()).hexdigest()
        (build/'AD103M-PROBE.EFI').write_bytes(b'FAKE UNIT TEST IMAGE 0.4.4 '+build_id.encode())
        for name in ('build_id.h','compile.cmd'):(build/name).write_text('unit test only')
        tool=build/'fake-tool';tool.write_bytes(b'not executable')
        m=dict(version='0.4.4',binary='AD103M-PROBE.EFI',tests_passed=True,packet_ready=True,
               gpu_write_capability=False,unlock_capability=False,execution_approved=False,
               sha256=packet.digest(build/'AD103M-PROBE.EFI'),build_id=build_id,
               inputs_sha256=self.snapshot,source_sha256={'probe.c':'1'*64},
               build_files_sha256={f'build/{n}':packet.digest(build/n) for n in ('build_id.h','compile.cmd')},
               toolchain={n:dict(path=str(tool),sha256=packet.digest(tool)) for n in ('compiler','linker','vcvars','python')})
        (build/'manifest.json').write_text(json.dumps(m))
        return root,m

    def test_exact_packet_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as folder:
            root,m=self.fixture(folder)
            with patch.object(packet,'inputs',return_value=self.snapshot),patch.object(packet,'validate_pe') as pe:
                self.assertTrue(packet.verify(root,m));pe.assert_called_once()
                out=packet.make_packet(root,m)
                self.assertEqual(packet.digest(out/'AD103M-PROBE.EFI'),m['sha256'])
                self.assertEqual(set(p.name for p in out.iterdir()),{'AD103M-PROBE.EFI','manifest.json','LIRE-AVANT-TEST.txt'})
                with self.assertRaises(FileExistsError):packet.make_packet(root,m)

    def test_each_missing_or_stale_provenance_refuses(self):
        with tempfile.TemporaryDirectory() as folder:
            root,m=self.fixture(folder)
            for name in ('tests_passed','packet_ready','execution_approved','gpu_write_capability','unlock_capability',
                         'sha256','build_id','source_sha256','inputs_sha256','build_files_sha256','toolchain'):
                other=copy.deepcopy(m)
                if type(other[name]) is bool:other[name]=not other[name]
                elif type(other[name]) is dict:other[name]={}
                else:other[name]='0'*64
                with self.subTest(field=name),patch.object(packet,'inputs',return_value=self.snapshot),patch.object(packet,'validate_pe'):
                    with self.assertRaises(ValueError):packet.verify(root,other)

    def test_binary_generated_file_and_tool_drift_refuse(self):
        for name in ('AD103M-PROBE.EFI','compile.cmd','fake-tool'):
            with tempfile.TemporaryDirectory() as folder:
                root,m=self.fixture(folder);(root/'build'/name).write_bytes(b'changed')
                with self.subTest(name=name),patch.object(packet,'inputs',return_value=self.snapshot),patch.object(packet,'validate_pe'):
                    with self.assertRaises(ValueError):packet.verify(root,m)

    def test_pe_failure_cannot_create_packet(self):
        with tempfile.TemporaryDirectory() as folder:
            root,m=self.fixture(folder)
            with patch.object(packet,'inputs',return_value=self.snapshot),patch.object(packet,'validate_pe',side_effect=ValueError('bad PE')):
                with self.assertRaises(ValueError):packet.make_packet(root,m)
                self.assertFalse((root/'build/packets').exists())

    def test_v044_retains_strict_bar_validation(self):
        d=json.loads((ROOT/'build/host-capture.json').read_text())
        d['probe_version']='0.4.4'
        analyze(d,d['build_id'])
        d['display_devices'][0]['bars'][0]['uefi_index']=99
        with self.assertRaises(ValueError):analyze(d,d['build_id'])
