"""Summarize decoded Windows events without equating a D state to an SM read."""
import argparse
from collections import Counter
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('directory', type=Path)
args = parser.parse_args()
directory = args.directory
source = directory / 'dxg-native.jsonl'
if not source.exists():
    source = directory / 'dxg-power.jsonl'
events = [json.loads(line) for line in source.read_text(encoding='utf-8-sig').splitlines()]
registry_events = [e for e in events if e.get('provider') == 'Microsoft-Windows-Kernel-Registry']
events = [e for e in events if e.get('provider', 'Microsoft-Windows-DxgKrnl') == 'Microsoft-Windows-DxgKrnl']

def integer(value):
    if value is None:
        return None
    value = str(value).strip()
    return int(value, 16 if value.lower().startswith('0x') else 10)

adapters = {}
for event in events:
    data = event['data']
    if event['id'] == 110:
        vendor, device = integer(data.get('VendorID')), integer(data.get('DeviceID'))
        adapters[data.get('pDxgAdapter')] = dict(vendor=vendor, device=device, luid=data.get('AdapterLuid'), identity=f'{vendor:04x}:{device:04x}' if vendor is not None and device is not None else 'unknown')
contexts = {}
for event in events:
    data = event['data']
    if event['id'] in (24, 25) and data.get('pDxgAdapter') in adapters:
        contexts[data.get('pMiniportContext')] = data['pDxgAdapter']

transitions = []
for event in events:
    data = event['data']
    if 'DevicePowerState' not in data and event['id'] not in (138, 139, 457, 549, 75):
        continue
    pointer = data.get('AdapterHandle') or data.get('pDxgAdapter') or contexts.get(data.get('MiniportContext'))
    adapter = adapters.get(pointer)
    state = integer(data.get('DevicePowerState'))
    transitions.append(dict(utc=event['utc'], id=event['id'], task=event['task'], opcode=event['opcode'], adapter=adapter, device_power_state=state, device_power_label={1: 'D0', 2: 'D1', 3: 'D2', 4: 'D3'}.get(state), data=data))

report = dict(
    source_file=source.name,
    registry_events=registry_events,
    adapters=adapters,
    selected_dxg_events=len(events),
    event_counts=dict(Counter(str(e['id']) + ':' + str(e['task']) for e in events)),
    initialization_and_device_power_events=transitions,
    nvidia_device_power_events=[t for t in transitions if t['adapter'] and t['adapter']['identity'] == '10de:2717' and t['device_power_state'] is not None],
    limitations=[
        'ETW contains no SS0/SS1 register read; a power event alone cannot prove when selectors were lost.',
        'DEVICE_POWER_STATE 4 means D3; it does not alone distinguish D3hot from D3cold.',
        'Component F states and NVIDIA P states are not device D states.',
        'Events without a correlated adapter remain unattributed; they are not assumed to be NVIDIA.',
        'A live capture begins after driver initialization; boot capture is required for that interval.',
    ],
)
(directory / 'analysis.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps(report, indent=2))
