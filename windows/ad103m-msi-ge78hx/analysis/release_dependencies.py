"""Read saved MSI captures only; never generate release permission or firmware code."""
import hashlib
import json
from pathlib import Path
import sys
import uuid

TOOLS=Path(__file__).resolve().parents[1]/'diagnostic/tools'
sys.path.insert(0,str(TOOLS))
from analyze import analyze
from dependencies import observation,path


def storage_scope(platform,volume):
    nodes={h['id']:h for h in platform['handles']}
    if len(nodes)!=len(platform['handles']) or volume not in nodes:raise ValueError('Invalid storage root')
    raw,_=path(nodes[volume]['path'])
    protected={volume}
    for h in nodes.values():
        if h.get('path'):
            parent,_=path(h['path'])
            if raw.startswith(parent[:-4]):protected.add(h['id'])
    # Reverse child-to-parent closure only. Do not pull every other device on
    # the shared PCI/USB bus into the storage dependency set.
    changed=True
    while changed:
        changed=False
        for h in nodes.values():
            for e in h['active_opens']:
                if int(e['attributes'],16)==8 and e['controller'] in protected and h['id'] not in protected:
                    protected.add(h['id']);changed=True
    edges=[]
    for h in nodes.values():
        if h['id'] not in protected:continue
        for e in h['active_opens']:
            attr=int(e['attributes'],16)
            if e['controller'] not in protected:
                if attr==8:continue  # unrelated child of a protected common ancestor
                raise ValueError('Unexpected external controller on protected source')
            if e['agent'] not in nodes or e['count']<=0 or attr not in (8,16):raise ValueError('Unresolved protected edge')
            edges.append(dict(source=h['id'],**e,protocol_guid=str(uuid.UUID(bytes_le=bytes.fromhex(e['protocol'])))))
    return protected,edges


def audit(data):
    p=data['platform'];observed=observation(data)
    if not observed['resolved']:raise ValueError('Callback observation is not resolved')
    protected,edges=storage_scope(p,p['dependencies']['source']['volume'])
    owners={e['agent'] for e in edges}
    drivers={a['agent']:a for a in p.get('gpu_agents',[])+p.get('boundary_agents',[])}
    gpu_nodes={h['id'] for h in p['handles'] if h['gpu_descendant']}
    release_agents={e['agent'] for h in p['handles'] if h['id'] in gpu_nodes for e in h['active_opens']}
    roots={p[k] for k in ('conout','conin','stderr')}
    inputs={e['agent'] for h in p['handles'] if h['id'] not in gpu_nodes
            for e in h['active_opens'] if e['controller'] in roots}
    released_images={drivers[a]['image_handle'] for a in release_agents if a in drivers}
    preserved_images={drivers[a]['image_handle'] for a in inputs if a in drivers}
    return dict(storage_controllers=sorted(protected),storage_edges=edges,storage_owner_agents=sorted(owners),
        storage_agents_without_driver_binding_code=sorted(owners-set(drivers)),
        candidate_release_agents=sorted(release_agents),protected_console_agents=sorted(inputs),
        agent_overlap=sorted(release_agents&owners),shared_release_preserved_images=sorted(released_images&preserved_images),
        callbacks=observed['normalized']['callbacks'],
        storage_ownership_proved=False,quiescence_proved=False,release_authorized=False,
        note='Snapshot-local IDs only; empty agent overlap is not proof of isolation or safe Stop behavior')


if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture',type=Path);parser.add_argument('manifest',type=Path)
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    if args.capture.stat().st_size>64*1024*1024:raise ValueError('Oversized capture')
    raw=args.capture.read_bytes();data=json.loads(raw.decode('utf-8-sig'))
    manifest=json.loads(args.manifest.read_text(encoding='utf-8-sig'))
    analyze(data,manifest['build_id']);report=audit(data)
    report.update(capture_sha256=hashlib.sha256(raw).hexdigest(),manifest_sha256=hashlib.sha256(args.manifest.read_bytes()).hexdigest())
    if args.output:
        with args.output.open('x',encoding='utf-8') as stream:json.dump(report,stream,indent=2)
    print(json.dumps({k:v for k,v in report.items() if k not in ('storage_edges','callbacks')},indent=2))
