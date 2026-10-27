"""Generate private host fixtures from captures 3/4. Never accesses hardware."""
import hashlib
import json
from pathlib import Path
import sys

PORT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(PORT.parent/'diagnostic/tools'))
from analyze import analyze
from compare import compare

def main():
    captures=[]
    for n in (3,4):
        folder=PORT.parent/'captures'/f'2026-10-27-run{n}'
        data=json.loads((folder/f'CAP000{n}.JSON').read_text(encoding='utf-8-sig'))
        manifest=json.loads((folder/'MANIFEST.json').read_text(encoding='utf-8-sig'))
        analyze(data,manifest['build_id']);captures.append(data)
    comparison=compare(*captures)
    if not all(comparison[k] is True for k in ('gpu_ownership_matches','physical_gop_matches','driver_code_matches')):
        raise ValueError('Captured GPU observations differ')
    out=PORT/'build';out.mkdir(exist_ok=True)
    lines=['/* PRIVATE generated host fixture; no firmware binary or execution. */']
    prom=None
    for i,c in enumerate(captures):
        data=bytes.fromhex(c['physical_prom']['hex'])
        if len(data)!=1048576 or hashlib.sha256(data).hexdigest()!='24b6d455544615dd3185e6c42512c104add2322b8c10053f6384566729ce7076':
            raise ValueError('PROM profile mismatch')
        if prom is not None and prom!=data:raise ValueError('PROM changed between captures')
        prom=data
        target=next(d for d in c['display_devices'] if d['exact_expected_identity'])
        config=bytes.fromhex(c['config256'])
        if len(config)!=256:raise ValueError('Full PCI config required')
        for name,values in (
            (f'CaptureConfig{i}',[int.from_bytes(config[j:j+4],'little') for j in range(0,256,4)]),
            (f'CaptureRegisters{i}',[int(s['value'],16) for s in c['register_snapshot']['samples']])):
            lines.append(f'static const uint32_t {name}[]={{'+','.join(hex(v)+'u' for v in values)+'};')
    p=captures[0]['platform'];target=p['target']
    gops=[h for h in p['handles'] if h.get('gpu_descendant') and h['id']!=target]
    if len(gops)!=1:raise ValueError('One physical GOP required')
    gop=gops[0]['id'];console=p['conout']
    if console!=p['conin'] or console!=p['stderr']:raise ValueError('Console alias changed')
    node={target:1,gop:2,console:3}
    protocol={'00b2f54cb868a54c9eecb23e3f50029a':1,'916e57093f6dd2118e3900a0c969723b':2,
              'c2777438c769d2118e3900a0c969723b':3,'dea94290dc23384a96fb7aded080516a':4}
    edges=[]
    for h in p['handles']:
        if h['id'] not in (target,gop):continue
        for e in h['active_opens']:
            proto=protocol[e['protocol']]
            driver=3 if h['id']==target else 1 if proto==3 else 2
            edges.append([node[h['id']],driver,node[e['controller']],proto,int(e['attributes'],16),e['count']])
    if len(edges)!=5:raise ValueError('Exactly five captured GPU edges required')
    lines.append('static const MsiEdge CaptureEdges[]={'+','.join('{'+','.join(map(str,e))+'}' for e in edges)+'};')
    (out/'captured_fixture.h').write_text('\n'.join(lines)+'\n',encoding='ascii')
    (out/'prom.bin').write_bytes(prom)
    candidate=dict(status='observed_profile_only',pci='10de:2717/1462:13cd/a1',
        vbios='95.03.2A.00.2A',prom_sha256=hashlib.sha256(prom).hexdigest(),
        source_captures=[dict(version=c['probe_version'],build_id=c['build_id']) for c in captures],
        gpu_relationships=5,boundary_relationships_unresolved=3,
        native_backend_implemented=False,bootable=False,hardware_qualified=False,
        qualification=dict(ownership_reviewed=False,console_callbacks_reviewed=False,
                           windows_image_reviewed=False,assets_verified=False))
    (out/'observed-profile.json').write_text(json.dumps(candidate,indent=2),encoding='utf-8')
    print('Private ROM/config/ownership fixtures prepared; candidate remains unqualified.')

if __name__=='__main__':main()
