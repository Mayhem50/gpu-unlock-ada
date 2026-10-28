"""Compare stable inventory content; never admit a transaction or normalize relocations."""
import json
from analyze import analyze
from graph import ownership, compare_ownership
from dependencies import compare_dependencies


def normalized(data):
    result = analyze(data, data['build_id'])
    if not result['identity_observed']:
        raise ValueError('No unique exact identity')
    target = next(d for d in data['display_devices'] if d['exact_expected_identity'])
    platform = data.get('platform', {})
    graph = ownership(data)
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
        'graph_resolved': graph['scopes']['global']['resolved'],
        'edges': graph['scopes']['global']['edges'],
        'roots': graph['roots'],
        'topology':topology,
        'physical_gop_complete':result['topology_complete'] and bool(gops) and all(
            g['parent'] is not None and g['path'] and g['path'].get('valid') is True and
            g['ambiguous'] is False and g['status']=='0x0000000000000000' and g['mode'] is not None for g in gops),
        'console_variables_complete':all(
            data.get(k,{}).get('status')=='0x0000000000000000' and
            data.get(k,{}).get('path',{}).get('valid') is True for k in ('conout_variable','conin_variable')),
        'topology_complete':result['topology_complete'] and bool(gops) and all(
            data.get(key,{}).get('valid') is True for key in ('console_out_handle_path','console_in_handle_path')),
        'driver_code_comparable': bool(codes) and code_stable,
        'driver_code': sorted_rows(codes),
    }


def compare(left, right):
    a, b = normalized(left), normalized(right)
    fields = ('identity','cached_rom_sha256','physical_prom_sha256','bars')
    graph = compare_ownership(left,right)
    gops_equal=a['topology']['gops']==b['topology']['gops']
    variables_equal=all(a['topology'][k]==b['topology'][k] for k in ('conout_variable','conin_variable'))
    topology_equal=a['topology']==b['topology']
    topology_match=(True if a['topology_complete'] and b['topology_complete'] else None) if topology_equal else False
    return {
        'inventory_matches': all(a[k] is not None and a[k] == b[k] for k in fields),
        'topology_matches':topology_match,
        'graph_matches': graph['matches']['global'],
        'gpu_ownership_matches': graph['matches']['gpu'],
        'boundary_ownership_matches': graph['matches']['boundary'],
        'physical_gop_matches':(True if a['physical_gop_complete'] and b['physical_gop_complete'] else None) if gops_equal else False,
        'console_variables_match':(True if a['console_variables_complete'] and b['console_variables_complete'] else None) if variables_equal else False,
        'console_role_aliases_match':graph['console_role_aliases_match'],
        'ownership_diagnostics':graph,
        'dependency_diagnostics':compare_dependencies(left,right),
        'topology_unknown_reasons':{
            side:[k for k in ('console_out_handle_path','console_in_handle_path') if data.get(k,{}).get('valid') is not True]
            for side,data in (('left',left),('right',right))},
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
    p.add_argument('--left-manifest', type=Path)
    p.add_argument('--right-manifest', type=Path)
    p.add_argument('--output', type=Path, help='New private JSON report; existing files are never overwritten')
    p.add_argument('--manifest', type=Path, default=Path(__file__).resolve().parents[1]/'build/manifest.json')
    args = p.parse_args()
    captures = []
    for path, manifest_path in ((args.left,args.left_manifest or args.manifest),(args.right,args.right_manifest or args.manifest)):
        manifest = json.loads(manifest_path.read_text(encoding='utf-8-sig'))
        if path.stat().st_size > 64*1024*1024:
            p.error('Oversized capture')
        data = json.loads(path.read_text(encoding='utf-8-sig'))
        analyze(data, manifest['build_id'])
        captures.append(data)
    report=compare(*captures)
    if args.output:
        with args.output.open('x',encoding='utf-8') as stream:
            json.dump(report,stream,indent=2)
    print(json.dumps(report, indent=2))
