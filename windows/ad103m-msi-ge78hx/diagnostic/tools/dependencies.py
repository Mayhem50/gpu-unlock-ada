"""Compare passive callback provenance; never grant release or quiescence."""
import hashlib
import re

OUTPUT=('Reset','OutputString','TestString','QueryMode','SetMode','SetAttribute',
        'ClearScreen','SetCursorPosition','EnableCursor')
EXPECTED={(r,n) for r in ('conout','stderr') for n in OUTPUT}|{
    ('conin','Reset'),('conin','ReadKeyStroke'),('filesystem','OpenVolume'),
    ('log','Write'),('log','Flush'),('log','Close')}
ZERO='0x0000000000000000'


def address(s):
    if not isinstance(s,str) or not re.fullmatch(r'0x[0-9a-f]{16}',s):
        raise ValueError('invalid address')
    n=int(s,16)
    if not n:raise ValueError('null address')
    return n


def path(s):
    if not isinstance(s,str) or not re.fullmatch(r'(?:[0-9a-f]{2}){4,4096}',s):
        raise ValueError('invalid device path')
    raw=bytes.fromhex(s);at=0;usb=False
    while at+4<=len(raw):
        kind,sub=raw[at:at+2];n=int.from_bytes(raw[at+2:at+4],'little')
        if n<4 or at+n>len(raw):break
        if kind==0x7f:
            if sub==0xff and n==4 and at+4==len(raw):return raw,usb
            break
        usb|=kind==3 and ((sub==5 and n==6) or (sub==15 and n==11) or (sub==16 and n>=10))
        at+=n
    raise ValueError('unterminated device path')


def observation(data):
    platform=data.get('platform',{});d=platform.get('dependencies')
    result={'resolved':False,'reason':'not_captured','release_authorized':False,
            'storage_ownership_proved':False,'quiescence_proved':False}
    if d is None:return result
    try:
        if (d.get('observation_complete') is not True or d.get('callbacks_invoked') is not False or
            d.get('atomic_snapshot') is not False or d.get('release_authorized') is not False):
            raise ValueError('incomplete or incompatible observation')
        if not 0<len(platform['handles'])<=4096 or any(type(h.get('id')) is not int for h in platform['handles']):
            raise ValueError('invalid handle inventory')
        nodes={h['id']:h for h in platform['handles']}
        if len(nodes)!=len(platform['handles']):raise ValueError('duplicate handle')
        bindings=d['bindings']
        if len(bindings)!=3 or {b['role'] for b in bindings}!={'conout','conin','stderr'}:
            raise ValueError('missing or duplicate console binding')
        interfaces={}
        for b in bindings:
            if (b['status']!=ZERO or b['matches_system_table'] is not True or
                type(b['handle']) is not int or b['handle'] not in nodes or b['handle']!=platform[b['role']]):
                raise ValueError('console binding not verified')
            interfaces[b['role']]=address(b['interface'])
        source=d['source'];volume=source['volume'];self=source['image']
        if (source['status']!=ZERO or source['filesystem_status']!=ZERO or source['usb'] is not True or
            type(volume) is not int or volume not in nodes or type(self) is not int or self not in nodes or self==volume):
            raise ValueError('source binding not verified')
        source_path,usb=path(source['path'])
        if not usb or source['path']!=nodes[volume].get('path'):raise ValueError('USB path not verified')
        if (type(source.get('log_volume')) is not int or source['log_volume']!=volume or
            source.get('matches_opened_volume') is not True or
            source.get('opened_path_sha256')!=hashlib.sha256(source_path).hexdigest()):
            raise ValueError('opened log source not verified')
        callbacks=d['callbacks'];keys=[(c['role'],c['name']) for c in callbacks]
        if len(keys)!=len(EXPECTED) or set(keys)!=EXPECTED:raise ValueError('callback set incomplete or duplicated')
        images=d['images'];ranges=[];seen=set()
        if len(images)>4096:raise ValueError('image inventory too large')
        for image in images:
            h=image['handle'];base=address(image['base']);size=image['image_bytes'];code=image.get('code') or {}
            if type(h) is not int or h not in nodes or h in seen:raise ValueError('image handle ambiguous')
            seen.add(h)
            if type(size) is not int or not 64<=size<=16*1024*1024 or base+size>2**64-1:
                raise ValueError('invalid image range')
            ranges.append((base,base+size,image))
        rows=[];addresses={};owners={}
        for c in callbacks:
            a=address(c['address']);k=(c['role'],c['name']);addresses[k]=a
            matches=[(base,image) for base,end,image in ranges if base<=a<end]
            if len(matches)!=1:raise ValueError('callback image missing or ambiguous')
            base,image=matches[0];code=image.get('code') or {};payload=bytes.fromhex(code.get('hex') or '')
            owners[k]=image['handle']
            rva=int(code.get('rva',''),16);length=code.get('length');offset=a-base
            path(image['file_path'])
            if (image['code_type']!=3 or code.get('relocations_in_code')!=0 or type(length) is not int or
                not 0<length<=512*1024 or len(payload)!=length or
                hashlib.sha256(payload).hexdigest()!=code.get('loaded_sha256') or
                not 0<=rva<=offset<rva+length<=image['image_bytes']):
                raise ValueError('callback executable code not verified')
            rows.append([*k,image['file_path'],image['image_bytes'],rva,length,code['loaded_sha256'],offset])
        # Only path ancestors are reported here. This is not a complete storage
        # driver closure, and equality cannot prove notification exclusion.
        ancestors=[]
        for h in nodes.values():
            if h.get('path'):
                try:raw,_=path(h['path'])
                except ValueError:continue
                if source_path.startswith(raw[:-4]):ancestors.append(h['id'])
        result.update(resolved=True,reason=None,normalized={
            'source_path':source['path'],'callbacks':sorted(rows),
            'interface_aliases':{a+'='+b:interfaces[a]==interfaces[b] for a in interfaces for b in interfaces if a<b},
            'image_instance_aliases':[[*a,*b] for a in sorted(owners) for b in sorted(owners) if a<b and owners[a]==owners[b]],
            'callback_aliases':[[*a,*b] for a in sorted(addresses) for b in sorted(addresses) if a<b and addresses[a]==addresses[b]]},
            storage_ancestor_handles=sorted(ancestors))
    except (KeyError,TypeError,ValueError,AttributeError) as error:
        result['reason']=str(error)
    return result


def compare_dependencies(left,right):
    a,b=observation(left),observation(right)
    return {'callbacks_match':a.get('normalized')==b.get('normalized') if a['resolved'] and b['resolved'] else None,
            'left':a,'right':b,'release_authorized':False}
