"""Classify targeted raw observations; event cookies never establish quiescence."""
import re

SLOTS=[('mode_raw',0x5344,4),('attribute_raw',0x5348,4),('column_raw',0x534c,4),('row_raw',0x5350,4),('cursor_flag_raw',0x5358,1),
 ('pointer_gate_raw',0x55c0,1),('input_gate_raw',0x55c1,1),('pointer_stop_raw',0x55c2,1),('input_depth_raw',0x55d8,1),('buffer_flag_raw',0x55d9,1),('input_stop_raw',0x55ec,1),
 ('expected_child_pointer',0x55f0,8),('buffer_limit_pointer',0x5638,8),('columns_raw',0x5640,4),('text_buffer_pointer',0x5648,8),('mode_table_pointer',0x5650,8),('attribute_buffer_pointer',0x5658,8),
 ('input_list_pointer',0x5728,8),('output_anchor_pointer',0x5760,8),('output_list_pointer',0x5768,8),('pointer_anchor_pointer',0x5780,8),('pointer_list_pointer',0x5788,8)]
PIN='c14e5170fda8bd2529028c8248d7e6cf9518cbac498af7f69fd6cdc8e9929766'


def assess(data):
    result=dict(console_fields_observed=False,reason='not_captured',public_wait_tokens={},
                pointer_protocols_raw=[],event_contexts_known=False,pending_events_known=False,
                release_authorized=False,quiescence_proved=False)
    state=data.get('platform',{}).get('console_state')
    if state is None:return result
    try:
        if state.get('profile')!='msi-capture6-console-v1' or state.get('profile_code_sha256')!=PIN or state.get('image_bytes')!=24256:
            raise ValueError('console profile not verified')
        for name in ('image_pinned','active_console_binding_verified','slots_complete','memory_maps_equal','post_service_state_equal'):
            if state.get(name) is not True:raise ValueError('incomplete '+name)
        for name in ('single_use_violation','heap_lists_followed','event_objects_dereferenced','event_contexts_known',
                     'pending_events_known','callbacks_invoked','atomic_snapshot','release_authorized'):
            if state.get(name) is not False:raise ValueError('incompatible '+name)
        slots=state['slots']
        if len(slots)!=len(SLOTS):raise ValueError('missing/extra state slots')
        for slot,(name,rva,size) in zip(slots,SLOTS):
            if (slot.get('name')!=name or slot.get('rva')!=f'0x{rva:08x}' or type(slot.get('bytes')) is not int or slot['bytes']!=size or
                not isinstance(slot.get('first'),str) or not re.fullmatch('[0-9a-f]{'+str(size*2)+'}',slot['first']) or slot['first']!=slot.get('second')):
                raise ValueError('missing/changed/unverified state slot')
        interfaces=state['input_interfaces'];kinds=['simple_input','extended_input','simple_pointer_raw','absolute_pointer_raw']
        if [i.get('kind') for i in interfaces]!=kinds:raise ValueError('missing/duplicate input interfaces')
        for index,interface in enumerate(interfaces):
            if interface.get('ram_read') is not True or interface.get('two_reads_equal') is not True:continue
            words=interface['raw_words'];count=3 if index==0 else 6 if index==1 else 4
            if (interface.get('status')!='0x0000000000000000' or len(words)!=count or words!=interface.get('second_raw_words') or
                any(not isinstance(w,str) or not re.fullmatch('0x[0-9a-f]{16}',w) for w in words)):
                raise ValueError('unverified interface words')
            if index<2:
                if interface.get('public_wait_token')!=words[2]:raise ValueError('wait-token mismatch')
                result['public_wait_tokens'][kinds[index]]=words[2]
            else:
                if interface.get('pointer_layout_decoded') is not False or interface.get('public_wait_token') is not None:
                    raise ValueError('opaque pointer interface was interpreted')
                result['pointer_protocols_raw'].append(kinds[index])
        result.update(console_fields_observed=True,reason='raw_observation_only')
    except (KeyError,TypeError,ValueError) as error:
        result.update(console_fields_observed=False,reason=str(error),public_wait_tokens={},pointer_protocols_raw=[])
    return result
