"""Rooted UEFI ownership observations; never authorize a driver release."""
from collections import Counter
import hashlib
import json
import re

ROLES=('target','conout','conin','stderr')


def key(value):
    return json.dumps(value,sort_keys=True,separators=(',',':'))


def ownership(data):
    platform=data.get('platform',{})
    handles=platform.get('handles',[])
    nodes={h['id']:h for h in handles}
    path_counts=Counter(h.get('path') for h in handles if h.get('path'))
    role_sets={i:[r for r in ROLES if platform.get(r)==i] for i in nodes}
    drivers={}
    driver_rejections=[]
    for agent in platform.get('gpu_agents',[])+platform.get('boundary_agents',[]):
        code=agent.get('code') or {}
        raw=code.get('hex')
        reason=None
        try:
            payload=bytes.fromhex(raw) if isinstance(raw,str) else None
            size=agent.get('image_bytes')
            rva=int(code.get('rva',''),16)
            stop=int(agent.get('stop_rva',''),16)
            if (agent.get('binding_status')!='0x0000000000000000' or
                agent.get('image_status')!='0x0000000000000000' or
                agent.get('binding_handle_matches') is not True or agent.get('code_type')!=3 or
                not isinstance(agent.get('file_path'),str) or not re.fullmatch(r'(?:[0-9a-f]{2})+',agent['file_path']) or
                not re.fullmatch(r'0x[0-9a-f]{8}',agent.get('version','')) or type(size) is not int or
                payload is None or not payload or len(payload)!=code.get('length') or
                hashlib.sha256(payload).hexdigest()!=code.get('loaded_sha256') or
                code.get('relocations_in_code')!=0 or not 0<=rva<=stop<rva+len(payload)<=size):
                reason='driver_identity_not_verified'
        except (TypeError,ValueError):
            reason='driver_identity_not_verified'
        if reason:
            driver_rejections.append({'handle':agent.get('agent'),'reason':reason})
            continue
        identity=['driver',agent['file_path'],agent.get('version'),size,agent['stop_rva'],
                  code['rva'],code['length'],code['loaded_sha256']]
        drivers.setdefault(agent.get('agent'),[]).append(key(identity))
    driver_counts=Counter(label for entries in drivers.values() for label in entries)
    labels={};methods={};reasons={}
    for i,h in nodes.items():
        path=h.get('path')
        identities=drivers.get(i,[])
        if path and path_counts[path]==1:
            labels[i]=key(['path',path]);methods[i]='unique_device_path'
        elif len(identities)==1 and driver_counts[identities[0]]==1:
            labels[i]=identities[0];methods[i]='verified_driver_code_and_file_path'
        elif role_sets[i] and any(r in role_sets[i] for r in ('conout','conin','stderr')):
            labels[i]=key(['console_roles',role_sets[i]]);methods[i]='rooted_console_roles'
        else:
            reasons[i]=('duplicate_device_path' if path else
                        'ambiguous_driver_identity' if identities else 'no_path_or_verified_driver_identity')
    target=platform.get('target')
    target_path=nodes.get(target,{}).get('path')
    def descendant(path):
        return bool(target_path and path and target_path.endswith('7fff0400') and
                    path.endswith('7fff0400') and path.startswith(target_path[:-8]))
    gpu_nodes={i for i,h in nodes.items() if i==target or descendant(h.get('path'))}
    role_nodes={platform.get(r) for r in ROLES}
    # Boundary identities can label endpoints without expanding the original
    # direct boundary into all unrelated neighbors of those external agents.
    gpu_drivers={a.get('agent') for a in platform.get('gpu_agents',[]) if a.get('agent') in drivers}
    related=gpu_nodes|role_nodes|gpu_drivers
    scopes={s:{'edges':[],'unresolved':[],'edge_count':0} for s in ('global','gpu','boundary')}
    for h in handles:
        for edge in h['active_opens']:
            identifiers=[h['id'],edge.get('agent'),edge.get('controller')]
            if (not re.fullmatch(r'0x[0-9a-f]{8}',edge.get('attributes','')) or
                type(edge.get('count')) is not int or edge['count']<=0):
                raise ValueError('Invalid ownership attributes/count')
            missing=[{'endpoint':name,'handle':identifier,'reason':reasons.get(identifier,'missing_handle_reference')}
                     for name,identifier in zip(('source','agent','controller'),identifiers) if identifier not in labels]
            row=[labels.get(i) for i in identifiers]+[edge['protocol'],edge['attributes'],edge['count']]
            selected=['global']
            if h['id'] in gpu_nodes:
                selected.append('gpu')
            elif any(i in related for i in identifiers):
                selected.append('boundary')
            for scope in selected:
                entry=scopes[scope];entry['edge_count']+=1
                if missing:
                    entry['unresolved'].append({'source':h['id'],'protocol':edge['protocol'],
                                                'attributes':edge['attributes'],'count':edge['count'],'missing':missing})
                else:entry['edges'].append(row)
    roots={r:labels.get(platform.get(r)) for r in ROLES}
    aliases={a+'='+b:platform.get(a)==platform.get(b) for a in ROLES for b in ROLES if a<b}
    complete=platform.get('available') is True and platform.get('complete') is True and bool(handles)
    for scope,entry in scopes.items():
        entry['edges']=sorted(entry['edges'],key=key)
        entry['resolved']=bool(complete and all(roots.values()) and not entry['unresolved'] and
                               (entry['edge_count']>0 or scope=='boundary'))
    return {'scopes':scopes,'roots':roots,'root_aliases':aliases,
            'diagnostics':{'capture_graph_complete':complete,'handle_count':len(handles),
                'pathless_handle_count':sum(not h.get('path') for h in handles),
                'identified_by':dict(Counter(methods.values())),
                'roots':{r:{'handle':platform.get(r),'method':methods.get(platform.get(r)),
                            'reason':None if roots[r] else reasons.get(platform.get(r),'missing_root')} for r in ROLES},
                'driver_rejections':driver_rejections,
                'remaining_unidentified_handles':[
                    {'handle':i,'reason':reasons[i]} for i in reasons
                    if any(i==m['handle'] for u in scopes['global']['unresolved'] for m in u['missing'])],
                'scope_counts':{s:{'edges':e['edge_count'],'resolved_edges':len(e['edges']),
                                  'unresolved_edges':len(e['unresolved'])} for s,e in scopes.items()},
                'unresolved_edges':{s:e['unresolved'] for s,e in scopes.items()},
                'release_authorized':False,
                'limitations':['console roles identify rooted relationships, not callback implementations',
                               'no virtual console function-pointer/code capture',
                               'snapshots are not atomic and do not validate DisconnectController/Stop or recovery']}}


def compare_ownership(left,right):
    a,b=ownership(left),ownership(right)
    result={}
    for scope in ('global','gpu','boundary'):
        x,y=a['scopes'][scope],b['scopes'][scope]
        result[scope]=(x['edges']==y['edges'] and a['roots']==b['roots'] and
                       a['root_aliases']==b['root_aliases']) if x['resolved'] and y['resolved'] else None
    return {'matches':result,'left':a['diagnostics'],'right':b['diagnostics'],
            'console_role_aliases_match':a['root_aliases']==b['root_aliases'],
            'release_authorized':False}
