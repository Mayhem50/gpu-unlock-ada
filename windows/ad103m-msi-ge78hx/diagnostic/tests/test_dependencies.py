import copy
import hashlib
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from dependencies import EXPECTED,OUTPUT,ZERO,observation,compare_dependencies


def fixture():
    usb='0305060000007fff0400';file='0101060000017fff0400';raw=bytes(range(64));base=0x100000
    names={n:i for i,n in enumerate(OUTPUT)}
    offsets={('conin','Reset'):16,('conin','ReadKeyStroke'):17,('filesystem','OpenVolume'):20,
             ('log','Write'):21,('log','Flush'):22,('log','Close'):23}
    callbacks=[dict(role=r,name=n,address=f'0x{base+512+(names[n] if r in ("conout","stderr") else offsets[r,n]):016x}') for r,n in sorted(EXPECTED)]
    image=dict(handle=2,base=f'0x{base:016x}',image_bytes=4096,code_type=3,file_path=file,
               code=dict(rva='0x00000200',length=64,relocations_in_code=0,hex=raw.hex(),loaded_sha256=hashlib.sha256(raw).hexdigest()))
    deps=dict(bindings=[dict(role=r,handle=1,status=ZERO,matches_system_table=True,
                            interface=f'0x{0x20000 if r=="conin" else 0x10000:016x}') for r in ('conout','stderr','conin')],
        source=dict(image=4,volume=3,log_volume=3,matches_opened_volume=True,opened_path_sha256=hashlib.sha256(bytes.fromhex(usb)).hexdigest(),status=ZERO,filesystem_status=ZERO,path=usb,usb=True),
        callbacks=callbacks,images=[image],observation_complete=True,callbacks_invoked=False,atomic_snapshot=False,release_authorized=False)
    return dict(platform=dict(handles=[dict(id=i,path=usb if i==3 else None) for i in range(5)],
                              conout=1,conin=1,stderr=1,dependencies=deps))


class DependenciesTests(unittest.TestCase):
    def test_absent_old_capture_keeps_unknown(self):
        self.assertIsNone(compare_dependencies({}, {})['callbacks_match'])

    def test_complete_passive_provenance_never_qualifies_release(self):
        d=fixture();r=compare_dependencies(d,d)
        self.assertTrue(r['callbacks_match'])
        self.assertFalse(r['release_authorized'])
        self.assertFalse(r['left']['storage_ownership_proved'])
        self.assertFalse(r['left']['quiescence_proved'])
        self.assertEqual(r['left']['storage_ancestor_handles'],[3])

    def test_aslr_and_handle_permutation_preserve_identity(self):
        a=fixture();b=copy.deepcopy(a);p=b['platform'];d=p['dependencies']
        for h in p['handles']:h['id']=10-h['id']
        for k in ('conout','conin','stderr'):p[k]=10-p[k]
        for binding in d['bindings']:
            binding['handle']=10-binding['handle'];binding['interface']=f'0x{int(binding["interface"],16)+4096:016x}'
        for k in ('image','volume','log_volume'):d['source'][k]=10-d['source'][k]
        for image in d['images']:
            image['handle']=10-image['handle'];image['base']=f'0x{int(image["base"],16)+4096:016x}'
        for c in d['callbacks']:c['address']=f'0x{int(c["address"],16)+4096:016x}'
        self.assertTrue(compare_dependencies(a,b)['callbacks_match'])

    def test_every_callback_missing_duplicate_or_null_refuses(self):
        for index in range(24):
            for mode in ('missing','duplicate','null'):
                with self.subTest(index=index,mode=mode):
                    d=fixture();callbacks=d['platform']['dependencies']['callbacks']
                    if mode=='missing':callbacks.pop(index)
                    if mode=='duplicate':callbacks.append(copy.deepcopy(callbacks[index]))
                    if mode=='null':callbacks[index]['address']=ZERO
                    self.assertFalse(observation(d)['resolved'])

    def test_unmapped_ambiguous_nonexecutable_and_overflow_addresses_refuse(self):
        for mode in ('unmapped','ambiguous','outside_code','wrapped_image','duplicate_image','bad_image_handle'):
            d=fixture();dep=d['platform']['dependencies'];image=dep['images'][0]
            if mode=='unmapped':dep['callbacks'][0]['address']='0x0000000000000001'
            if mode=='ambiguous':
                other=copy.deepcopy(image);other['handle']=4;dep['images'].append(other)
            if mode=='outside_code':dep['callbacks'][0]['address']='0x0000000000100240'
            if mode=='wrapped_image':image['base']='0xfffffffffffffff0'
            if mode=='duplicate_image':dep['images'].append(copy.deepcopy(image))
            if mode=='bad_image_handle':image['handle']=None
            with self.subTest(mode=mode):self.assertFalse(observation(d)['resolved'])

    def test_unverified_code_or_file_path_refuses(self):
        for mode in ('missing','digest','relocation','size','type','path'):
            d=fixture();image=d['platform']['dependencies']['images'][0];code=image['code']
            if mode=='missing':code['hex']=None
            if mode=='digest':code['loaded_sha256']='0'*64
            if mode=='relocation':code['relocations_in_code']=1
            if mode=='size':code['length']+=1
            if mode=='type':image['code_type']=5
            if mode=='path':image['file_path']='7f010400'
            with self.subTest(mode=mode):self.assertFalse(observation(d)['resolved'])

    def test_source_and_console_binding_refusals(self):
        for mode in ('status','usb_flag','non_usb','path_mismatch','missing_self','missing_volume',
                     'same_image_volume','binding_status','interface_mismatch','null_interface','missing_binding',
                     'log_volume','log_path_digest','log_binding'):
            d=fixture();dep=d['platform']['dependencies'];src=dep['source'];b=dep['bindings'][0]
            if mode=='status':src['filesystem_status']='0x8000000000000003'
            if mode=='usb_flag':src['usb']=False
            if mode=='non_usb':src['path']='0101060000017fff0400'
            if mode=='path_mismatch':src['path']='0305060001007fff0400'
            if mode=='missing_self':src['image']=None
            if mode=='missing_volume':src['volume']=99
            if mode=='same_image_volume':src['image']=src['volume']
            if mode=='binding_status':b['status']='0x0000000000000001'
            if mode=='interface_mismatch':b['matches_system_table']=False
            if mode=='null_interface':b['interface']=ZERO
            if mode=='missing_binding':dep['bindings'].pop()
            if mode=='log_volume':src['log_volume']=0
            if mode=='log_path_digest':src['opened_path_sha256']='0'*64
            if mode=='log_binding':src['matches_opened_volume']=False
            with self.subTest(mode=mode):self.assertFalse(observation(d)['resolved'])

    def test_semantic_callback_or_source_change_differs(self):
        a=fixture()
        for mode in ('callback','code','source'):
            b=copy.deepcopy(a);dep=b['platform']['dependencies']
            if mode=='callback':dep['callbacks'][0]['address']='0x000000000010021f'
            if mode=='code':
                raw=b'x'*64;dep['images'][0]['code'].update(hex=raw.hex(),loaded_sha256=hashlib.sha256(raw).hexdigest())
            if mode=='source':
                dep['source']['path']='0305060001007fff0400';b['platform']['handles'][3]['path']=dep['source']['path']
                dep['source']['opened_path_sha256']=hashlib.sha256(bytes.fromhex(dep['source']['path'])).hexdigest()
            with self.subTest(mode=mode):self.assertFalse(compare_dependencies(a,b)['callbacks_match'])

    def test_flags_and_incomplete_observation_refuse(self):
        for flag,value in (('observation_complete',False),('callbacks_invoked',True),('atomic_snapshot',True),('release_authorized',True)):
            d=fixture();d['platform']['dependencies'][flag]=value
            self.assertFalse(observation(d)['resolved'])

    def test_identical_code_in_another_image_does_not_hide_instance_change(self):
        a=fixture();b=copy.deepcopy(a);dep=b['platform']['dependencies']
        duplicate=copy.deepcopy(dep['images'][0]);duplicate.update(handle=4,base='0x0000000000200000')
        dep['images'].append(duplicate)
        for c in dep['callbacks']:
            if c['role']=='log':c['address']=f'0x{int(c["address"],16)+0x100000:016x}'
        self.assertTrue(observation(b)['resolved'])
        self.assertFalse(compare_dependencies(a,b)['callbacks_match'])

    def test_actual_c_mock_json(self):
        file=ROOT/'build/platform-dependencies.json'
        if not file.exists():self.skipTest('Run the CPU platform suite first')
        d={'platform':json.loads(file.read_text())};r=observation(d)
        self.assertTrue(r['resolved'],r['reason'])
        self.assertEqual(len(r['normalized']['callbacks']),24)
