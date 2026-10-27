"""Compare stable inventory content; never admit a transaction or normalize relocations."""
import json
from collections import Counter
from analyze import analyze


def normalized(data):
    result = analyze(data, data['build_id'])
    if not result['identity_observed']:
        raise ValueError('No unique exact identity')
    target = next(d for d in data['display_devices'] if d['exact_expected_identity'])
    platform = data.get('platform', {})
    handles = platform.get('handles', [])
    # Only paths, never enumeration IDs or memory addresses, label graph nodes.
    paths = {h['id']: h.get('path') for h in handles}
    counts = Counter(p for p in paths.values() if p)
    def label(identifier):
        path = paths.get(identifier)
        return path if path and counts[path] == 1 else None
    edges = []
    resolved = result['platform_graph_complete'] and bool(handles)
    roots = {key:label(platform.get(key)) for key in ('target','conout','conin','stderr')}
    if not all(roots.values()):
        resolved = False
    for h in handles:
        for edge in h['active_opens']:
            labels = [label(h['id']), label(edge['agent']), label(edge['controller'])]
            if not all(labels):
                resolved = False
            edges.append(labels + [edge['protocol'], edge['attributes'], edge['count']])
    codes = []
    code_stable = True
    for agent in platform.get('gpu_agents', []):
        code = agent.get('code')
        if not code or code.get('relocations_in_code') != 0:
            code_stable = False
            continue
        codes.append([agent.get('file_path'), agent.get('version'), agent.get('stop_rva'),
                      code.get('rva'), code.get('length'), code.get('loaded_sha256')])
    def sorted_rows(rows):
        return sorted(rows, key=lambda row: json.dumps(row, sort_keys=True))
    # Resolve parents to device identity/path rather than volatile array indices.
    displays=data['display_devices']
    def display_key(d):
        return {k:d.get(k) for k in ('vendor','device_id','subsystem_vendor','subsystem_device','revision','location','path')}
    gops=[]
    for g in data.get('gops',[]):
        parent=g.get('display_parent_index')
        gops.append({
            'path':g.get('path'), 'parent':display_key(displays[parent]) if isinstance(parent,int) and not isinstance(parent,bool) and 0<=parent<len(displays) else None,
            'ambiguous':g.get('ambiguous_parent'), 'console_out':g.get('same_handle_as_console_out'),
            'mode':g.get('current_mode'), 'status':g.get('protocol_status')})
    topology={
        'gops':sorted_rows(gops),
        **{key:data.get(key) for key in ('console_out_handle_path','console_in_handle_path','conout_variable','conin_variable')}}
    return {
        'identity': {k: target[k] for k in ('vendor','device_id','subsystem_vendor','subsystem_device','revision','location','path')},
        'cached_rom_sha256': result['rom_sha256'],
        'physical_prom_sha256': data.get('physical_prom', {}).get('sha256') if result['physical_prom_complete'] else None,
        'bars': target.get('bars'),
        'graph_resolved': resolved,
        'edges': sorted_rows(edges) if resolved else None,
        'roots': roots if resolved else None,
        'topology':topology,
        'topology_complete':result['topology_complete'] and bool(gops) and all(
            data.get(key,{}).get('valid') is True for key in ('console_out_handle_path','console_in_handle_path')),
        'driver_code_comparable': bool(codes) and code_stable,
        'driver_code': sorted_rows(codes),
    }


def compare(left, right):
    a, b = normalized(left), normalized(right)
    fields = ('identity','cached_rom_sha256','physical_prom_sha256','bars')
    topology_equal=a['topology']==b['topology']
    topology_match=(True if a['topology_complete'] and b['topology_complete'] else None) if topology_equal else False
    return {
        'inventory_matches': all(a[k] is not None and a[k] == b[k] for k in fields),
        'topology_matches':topology_match,
        'graph_matches': a['edges'] == b['edges'] and a['roots']==b['roots'] if a['graph_resolved'] and b['graph_resolved'] else None,
        'driver_code_matches': a['driver_code'] == b['driver_code'] if a['driver_code_comparable'] and b['driver_code_comparable'] else None,
        'unlock_ready': False,
        'note': 'Null means unresolved, not a match. Volatile registers and relocated code require separate review.',
    }


if __name__ == '__main__':
    import argparse
    from pathlib import Path
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('left', type=Path)
    p.add_argument('right', type=Path)
    p.add_argument('--manifest', type=Path, default=Path(__file__).resolve().parents[1]/'build/manifest.json')
    args = p.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding='utf-8-sig'))
    captures = []
    for path in (args.left,args.right):
        if path.stat().st_size > 64*1024*1024:
            p.error('Oversized capture')
        data = json.loads(path.read_text(encoding='utf-8-sig'))
        analyze(data, manifest['build_id'])
        captures.append(data)
    print(json.dumps(compare(*captures), indent=2))
