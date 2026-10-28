"""Offline raw selector deltas, without assigning undocumented unit/rate meanings."""
import re

OFFSETS={'SS0':0x82381c,'SS1':0x823820,'FEAT_READOUT_1':0x823818}


def samples(data):
    result={}
    for s in data['register_snapshot']['samples']:
        name=s['name']
        if name not in OFFSETS:continue
        value=s.get('value')
        if (name in result or s.get('status')!='0x0000000000000000' or
            int(s.get('offset',''),16)!=OFFSETS[name] or not isinstance(value,str) or
            not re.fullmatch('0x[0-9a-f]{8}',value)):
            raise ValueError('Unverified selector sample: '+name)
        v=int(value,16)
        if v==0xffffffff or v&0xffff0000==0xbadf0000:raise ValueError('Inaccessible selector: '+name)
        result[name]=v
    if set(result)!=set(OFFSETS):raise ValueError('Missing selector/readout sample')
    return result


def analyze_selectors(left,right):
    a,b=samples(left),samples(right);x=a['SS0']^b['SS0']
    def fields(v,width,count):return [(v>>(width*i))&((1<<width)-1) for i in range(count)]
    return dict(ss0_left=f"0x{a['SS0']:08x}",ss0_right=f"0x{b['SS0']:08x}",
        ss0_xor=f'0x{x:08x}',changed_bits=[i for i in range(32) if x&(1<<i)],
        changed_fields=[dict(index=i,bits=[4*i,4*i+3],left=(a['SS0']>>(4*i))&15,right=(b['SS0']>>(4*i))&15)
                        for i in range(8) if x&(15<<(4*i))],
        ss1_left=f"0x{a['SS1']:08x}",ss1_right=f"0x{b['SS1']:08x}",ss1_unchanged=a['SS1']==b['SS1'],
        readout_left=f"0x{a['FEAT_READOUT_1']:08x}",readout_right=f"0x{b['FEAT_READOUT_1']:08x}",
        readout_fields_left=fields(a['FEAT_READOUT_1'],3,9),readout_fields_right=fields(b['FEAT_READOUT_1'],3,9),
        readout_unchanged=a['FEAT_READOUT_1']==b['FEAT_READOUT_1'],
        full_readout_left=(a['FEAT_READOUT_1']&0x07ffffff)==0,
        full_readout_right=(b['FEAT_READOUT_1']&0x07ffffff)==0,
        cause_established=False,field_unit_mapping_established=False,admission_widened=False,unlock_ready=False)
