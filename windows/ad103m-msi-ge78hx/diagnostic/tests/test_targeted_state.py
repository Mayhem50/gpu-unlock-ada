import copy
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from targeted_state import assess,SLOTS,PIN


def fixture():
    s=dict(schema=1,profile='msi-capture6-console-v1',profile_code_sha256=PIN,image_bytes=24256,
           image_pinned=True,active_console_binding_verified=True,slots_complete=True,memory_maps_equal=True,post_service_state_equal=True,
           slots=[dict(name=n,rva=f'0x{r:08x}',bytes=b,first='00'*b,second='00'*b) for n,r,b in SLOTS],input_interfaces=[])
    for key in ('single_use_violation','heap_lists_followed','event_objects_dereferenced','event_contexts_known','pending_events_known','callbacks_invoked','atomic_snapshot','release_authorized'):s[key]=False
    for i,kind in enumerate(('simple_input','extended_input','simple_pointer_raw','absolute_pointer_raw')):
        words=['0x0000000000000001']*(3 if i==0 else 6 if i==1 else 4)
        s['input_interfaces'].append(dict(kind=kind,status='0x0000000000000000',ram_read=True,two_reads_equal=True,raw_words=words,second_raw_words=words.copy(),
                                        public_wait_token=words[2] if i<2 else None,pointer_layout_decoded=False))
    return dict(platform=dict(console_state=s))


class TargetedTests(unittest.TestCase):
    def test_empty_old_capture_is_unknown(self):self.assertFalse(assess({})['console_fields_observed'])

    def test_raw_zero_slots_and_opaque_tokens_are_observations_only(self):
        r=assess(fixture());self.assertTrue(r['console_fields_observed'])
        self.assertEqual(len(r['public_wait_tokens']),2)
        for key in ('event_contexts_known','pending_events_known','release_authorized','quiescence_proved'):self.assertFalse(r[key])

    def test_every_slot_mutation_missing_or_partial_is_refused(self):
        for i in range(len(SLOTS)):
            for mode in ('missing','partial','changed','rva'):
                d=fixture();s=d['platform']['console_state']['slots']
                if mode=='missing':s.pop(i)
                if mode=='partial':s[i]['first']=None
                if mode=='changed':s[i]['second']='ff'*s[i]['bytes']
                if mode=='rva':s[i]['rva']='0x00000000'
                with self.subTest(index=i,mode=mode):self.assertFalse(assess(d)['console_fields_observed'])

    def test_bad_profile_drift_reentry_and_event_claims_are_refused(self):
        for key in ('image_pinned','active_console_binding_verified','slots_complete','memory_maps_equal','post_service_state_equal','single_use_violation',
                    'event_objects_dereferenced','callbacks_invoked','event_contexts_known','pending_events_known','release_authorized'):
            d=fixture();s=d['platform']['console_state'];s[key]=not s[key]
            with self.subTest(key=key):self.assertFalse(assess(d)['console_fields_observed'])
        d=fixture();d['platform']['console_state']['profile']='synthetic-test-only';self.assertFalse(assess(d)['console_fields_observed'])

    def test_wait_cookie_cannot_be_changed_or_inferred_from_pointer_words(self):
        for i in range(4):
            d=fixture();d['platform']['console_state']['input_interfaces'][i]['public_wait_token']='0x0000000000000002'
            self.assertFalse(assess(d)['console_fields_observed'])

    def test_actual_c_synthetic_capture_has_no_production_qualification(self):
        file=ROOT/'build/console-state-host.json'
        if not file.exists():self.skipTest('Run CPU console tests first')
        s=json.loads(file.read_text());self.assertEqual(len(s['slots']),len(SLOTS))
        self.assertFalse(assess(dict(platform=dict(console_state=s)))['console_fields_observed'])
