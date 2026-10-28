"""Classify targeted raw observations; event cookies never establish quiescence."""
import re
from dependencies import observation, address

SLOTS=[('mode_raw',0x5344,4),('attribute_raw',0x5348,4),('column_raw',0x534c,4),('row_raw',0x5350,4),('cursor_flag_raw',0x5358,1),
 ('pointer_gate_raw',0x55c0,1),('input_gate_raw',0x55c1,1),('pointer_stop_raw',0x55c2,1),('input_depth_raw',0x55d8,1),('buffer_flag_raw',0x55d9,1),('input_stop_raw',0x55ec,1),
 ('expected_child_pointer',0x55f0,8),('buffer_limit_pointer',0x5638,8),('columns_raw',0x5640,4),('text_buffer_pointer',0x5648,8),('mode_table_pointer',0x5650,8),('attribute_buffer_pointer',0x5658,8),
 ('input_list_pointer',0x5728,8),('output_anchor_pointer',0x5760,8),('output_list_pointer',0x5768,8),('pointer_anchor_pointer',0x5780,8),('pointer_list_pointer',0x5788,8)]
PIN='c14e5170fda8bd2529028c8248d7e6cf9518cbac498af7f69fd6cdc8e9929766'


def assess(data):
    result=dict(console_fields_observed=False,reason='not_captured',public_wait_tokens={},
                pointer_protocols_raw=[],event_contexts_known=False,pending_events_known=False,
                interfaces_current_known=False,release_authorized=False,quiescence_proved=False)
    state=data.get('platform',{}).get('console_state')
    if state is None:return result
    try:
        if state.get('profile')!='msi-capture6-console-v1' or state.get('profile_code_sha256')!=PIN or state.get('image_bytes')!=24256:
            raise ValueError('console profile not verified')
        for name in ('image_pinned','active_console_binding_verified','input_interfaces_revalidated','slots_complete','memory_maps_equal','post_service_state_equal'):
            if state.get(name) is not True:raise ValueError('incomplete '+name)
        for name in ('single_use_violation','interfaces_current_known','heap_lists_followed','event_objects_dereferenced','event_contexts_known',
                     'pending_events_known','callbacks_invoked','atomic_snapshot','release_authorized'):
            if state.get(name) is not False:raise ValueError('incompatible '+name)
        maps=state['memory_maps']
        if len(maps)!=2 or maps[0]!=maps[1] or any(m.get('status')!='0x0000000000000000' or
            type(m.get('bytes')) is not int or not 0<m['bytes']<=65536 or
            type(m.get('descriptor_size')) is not int or not 40<=m['descriptor_size']<=256 or
            m['bytes']%m['descriptor_size'] or m.get('version')!=1 or
            not isinstance(m.get('key'),str) or not re.fullmatch(r'0x[0-9a-f]{16}',m['key']) or
            not isinstance(m.get('sha256'),str) or not re.fullmatch(r'[0-9a-f]{64}',m['sha256']) for m in maps):
            raise ValueError('inconsistent memory map diagnostics')
        diag=state['candidate_diagnostics']
        if (any(type(diag.get(k)) is not int for k in ('candidates','ram_readable','path_matches','code_matches')) or
            not 1<=diag['candidates']<=4 or not 1<=diag['ram_readable']<=diag['candidates'] or
            not 1<=diag['path_matches']<=diag['ram_readable'] or diag['code_matches']!=1):
            raise ValueError('inconsistent candidate diagnostics')
        dependency=observation(data)
        if not dependency['resolved']:raise ValueError('callback dependency evidence unresolved')
        base=address(state['image_base']);images=data['platform']['dependencies']['images']
        matching=[i for i in images if address(i['base'])==base and i['image_bytes']==24256 and
                  i.get('file_path')=='040614007d498a62f62b64428741069dbd3399d67fff0400' and (i.get('code') or {}).get('loaded_sha256')==PIN and
                  (i.get('code') or {}).get('rva')=='0x000002a0' and (i.get('code') or {}).get('length')==19648]
        if len(matching)!=1:raise ValueError('console image does not match dependency evidence')
        callback={(c['role'],c['name']):address(c['address']) for c in data['platform']['dependencies']['callbacks']}
        offsets={'Reset':0x4188,'OutputString':0x4218,'TestString':0x43dc,'QueryMode':0x444c,'SetMode':0x44a8,
                 'SetAttribute':0x4590,'ClearScreen':0x461c,'SetCursorPosition':0x4694,'EnableCursor':0x472c}
        if any(callback[(role,name)]!=base+rva for role in ('conout','stderr') for name,rva in offsets.items()):
            raise ValueError('output callbacks differ from pinned image')
        if callback[('conin','Reset')]!=base+0x3230 or callback[('conin','ReadKeyStroke')]!=base+0x32d4:
            raise ValueError('input callbacks differ from pinned image')
        slots=state['slots']
        if len(slots)!=len(SLOTS):raise ValueError('missing/extra state slots')
        for slot,(name,rva,size) in zip(slots,SLOTS):
            if (slot.get('name')!=name or slot.get('rva')!=f'0x{rva:08x}' or type(slot.get('bytes')) is not int or slot['bytes']!=size or
                not isinstance(slot.get('first'),str) or not re.fullmatch('[0-9a-f]{'+str(size*2)+'}',slot['first']) or slot['first']!=slot.get('second')):
                raise ValueError('missing/changed/unverified state slot')
        interfaces=state['input_interfaces'];kinds=['simple_input','extended_input','simple_pointer_raw','absolute_pointer_raw']
        if [i.get('kind') for i in interfaces]!=kinds:raise ValueError('missing/duplicate input interfaces')
        basic=interfaces[0]
        bindings={b['role']:b for b in data['platform']['dependencies']['bindings']}
        if (basic.get('ram_read') is not True or basic.get('two_reads_equal') is not True or
            basic.get('interface')!=bindings['conin']['interface'] or
            basic.get('raw_words',[])[:2]!=[f"0x{base+0x3230:016x}",f"0x{base+0x32d4:016x}"]):
            raise ValueError('input interface differs from callback evidence')
        for index,interface in enumerate(interfaces):
            if (not isinstance(interface.get('status'),str) or not re.fullmatch(r'0x[0-9a-f]{16}',interface['status']) or
                not isinstance(interface.get('interface'),str) or not re.fullmatch(r'0x[0-9a-f]{16}',interface['interface']) or
                interface.get('revalidated_status')!=interface.get('status') or
                interface.get('revalidated_interface')!=interface.get('interface')):
                raise ValueError('input interface revalidation differs')
            if interface['status']=='0x0000000000000000' and interface.get('interface')=='0x0000000000000000':
                raise ValueError('success with null input interface')
            if interface['status']!='0x0000000000000000' and interface['interface']!='0x0000000000000000':
                raise ValueError('failed query has non-null interface')
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
