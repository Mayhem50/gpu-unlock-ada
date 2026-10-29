"""Summarize a real hybrid capture without granting write authorization."""
import argparse
import hashlib
import json
from pathlib import Path


def analyze(path):
    raw = path.read_bytes()
    data = json.loads(raw.decode('utf-8-sig'))
    if data.get('probe_version') not in ('hybrid-D1', 'hybrid-D2') or data.get('gpu_writes') is not False or data.get('unlock_attempted') is not False:
        raise ValueError('Not a supported hybrid diagnostic capture')
    entry = data['hybrid_entry']
    preserved = data['intel_preservation']
    devices = data['display_devices']
    intel = [i for i, dev in enumerate(devices) if dev.get('vendor') == '0x8086' and dev.get('device_id') == '0xa788' and dev.get('subsystem_vendor') == '0x1462' and dev.get('subsystem_device') == '0x13cd']
    intel_gops = [g for g in data['gops'] if g.get('display_parent_index') in intel and g.get('current_mode')]
    complete = (data.get('pci_enumeration_complete') is True and data.get('gop_enumeration_complete') is True
                and entry.get('intel_matches') == 1 and entry.get('nvidia_matches') == 1
                and len(intel) == 1 and len(intel_gops) == 1 and preserved.get('observed_unchanged') is True)
    registers = {s['name']: s.get('value') for s in data.get('register_snapshot', {}).get('samples', [])}
    return dict(capture=str(path.resolve()), sha256=hashlib.sha256(raw).hexdigest(), build_id=data.get('build_id'),
                capture_usable_for_hybrid_review=complete, intel_preservation=preserved,
                intel_gops=intel_gops, nvidia_pci_command=entry.get('pci_command'),
                nvidia_pmcsr=entry.get('pmcsr'), nvidia_mmio_policy=entry.get('mmio_policy'),
                nvidia_mmio_read_allowed=(entry.get('nvidia_mmio_read_allowed') is True or data.get('decode_test_entry', {}).get('memory_decode_enabled') is True),
                decode_test_entry=data.get('decode_test_entry'), decode_test_cleanup=data.get('decode_test_cleanup'),
                registers=registers, physical_prom_complete=data.get('physical_prom', {}).get('complete') is True,
                platform_graph_complete=data.get('platform', {}).get('complete') is True,
                unlock_authorized=False,
                next_step='Review this hardware capture before implementing/admitting the hybrid transaction; do not loosen the dedicated profile')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('capture', type=Path)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    text = json.dumps(analyze(args.capture), indent=2)
    if args.out:
        args.out.write_text(text, encoding='utf-8')
    print(text)
