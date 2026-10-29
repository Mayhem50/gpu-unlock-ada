"""Generate an isolated read-only hybrid backend from the full diagnostic.

The transaction-enabled dedicated backend is not overwritten or linked.
"""
from pathlib import Path
import hashlib
import json
import shutil
import sys

HERE = Path(__file__).resolve().parent
DIAG = HERE.parent / 'diagnostic'
OUT = HERE / 'build/hybrid'
SRC = OUT / 'src'
SRC.mkdir(parents=True, exist_ok=True)
(OUT / 'tests').mkdir(exist_ok=True)
(OUT / 'build').mkdir(exist_ok=True)
for path in (DIAG / 'src').iterdir():
    if path.suffix in ('.c', '.h'):
        shutil.copy2(path, SRC / path.name)
for name in ['hybrid_policy.h', 'hybrid_policy.c', 'hybrid_probe.inc', 'hybrid_decode.inc', 'hybrid_entry.c']:
    shutil.copy2(HERE / name, SRC / name)

def change(path, old, new):
    text = path.read_text(encoding='utf-8-sig')
    if text.count(old) != 1:
        raise ValueError(f'{path.name}: expected one source anchor: {old[:70]}')
    path.write_text(text.replace(old, new), encoding='utf-8')

probe = SRC / 'probe.c'
change(probe, 'EFI_STATUS EFIAPI UefiMain(', '#include "hybrid_probe.inc"\n#include "hybrid_decode.inc"\nEFI_STATUS EFIAPI HybridCaptureMain(')
change(probe, 'EnumerateDisplays(st->BootServices,&t);Gops(st,&t);', 'EnumerateDisplays(st->BootServices,&t);Gops(st,&t);HybridBegin(st,&t);HybridDecodeBegin(st,image,&t);')
change(probe, 'if(target) mmio_attempts=SampleRegisters', 'if(target&&HybridMmioAllowed) mmio_attempts=SampleRegisters')
change(probe, 'else TextPut(&t,"{\\"guard_passed\\":false,\\"reason\\":\\"no_unique_target\\",\\"read_attempts\\":0,\\"complete\\":false}");', 'else TextPut(&t,"{\\"guard_passed\\":false,\\"reason\\":\\"hybrid_pci_read_policy_refused\\",\\"read_attempts\\":0,\\"complete\\":false}");')
change(probe, 'TextPut(&t,",\\"limitations\\":[', 'HybridEnd(st,&t);\n    TextPut(&t,",\\"limitations\\":[')
change(probe, 'L"AD103M-PROBE"', 'L"MSI2717-HYBRID"')
change(probe, 'Saved AD103M-PROBE', 'Saved MSI2717-HYBRID')
change(probe, 'MSI GE78HX AD103M diagnostic v0.4.4 - no GPU writes or unlock.', 'MSI 2717 HYBRID D2 - D observation / T decode PCI temporaire; aucun unlock.')
change(probe, 'TextPut(&t,",\\"platform\\":");', 'HybridDecodeEnd(st,&t);\n    TextPut(&t,",\\"platform\\":");')
change(probe, 'if(t.failed) {log->Close(log);', 'if(t.failed) {log->Close(log);HybridDecodeContain(st);')
change(probe, 'if(st->BootServices->Stall) st->BootServices->Stall(8000000);', 'HybridDecodeContain(st);\n    if(st->BootServices->Stall) st->BootServices->Stall(8000000);')
change(SRC / 'core.h', '#define PROBE_VERSION "0.4.4"', '#define PROBE_VERSION "hybrid-D2"')
captured = HERE / 'build/hybrid/hardware/CAP0002/CAP0002.JSON'
captured_bytes = captured.read_bytes()
assert hashlib.sha256(captured_bytes).hexdigest() == '997b4747928c6d34219e76ed5b81e05f0b5a2831f89d4fd0fd96271e9be6e6f3'
captured_data = json.loads(captured_bytes.decode('utf-8-sig'))
header = bytes.fromhex(captured_data['hybrid_entry']['nvidia_config256'])
(SRC / 'hybrid_capture_pin.h').write_text('static const UINT8 HybridCapturedHeader[64]={' + ','.join(hex(b) for b in header[:64]) + '};\nstatic const UINT8 HybridCapturedConfig[256]={' + ','.join(hex(b) for b in header) + '};\n', encoding='ascii')
# Re-read the current power state before each independently guarded MMIO/PROM
# sequence, rather than relying on HybridBegin's earlier observation.
change(SRC / 'sample.c', '#include "sample.h"', '#include "sample.h"\n#include "hybrid_policy.h"')
change(SRC / 'sample.c', '/* The diagnostic never enables memory decoding or bus mastering itself. */', '''UINT32 full_config[64];
    status=pci->Pci.Read(pci,EfiPciIoWidthUint32,0,64,full_config);
    if(EFI_ERROR(status)||!BytesEqual(words,full_config,64))return "hybrid_config_changed";
    HYBRID_PCI_STATE state=HybridPciState((UINT8 *)full_config);
    if(!state.mmio_read_allowed)return state.reason;
    /* The diagnostic never enables memory decoding or bus mastering itself. */''')
tests = (DIAG / 'tests/host_tests.c').read_text(encoding='utf-8-sig').replace('UefiMain(', 'HybridCaptureMain(')
tests = tests.replace('Config[0][4]=3;Config[0][8]', 'Config[0][6]=0x10;Config[0][0x34]=0x40;Config[0][0x40]=1;Config[0][0x42]=3;Config[0][4]=3;Config[0][8]')
tests = tests.replace('Config[1][2]=1;Config[1][11]=3;', 'Config[1][2]=0x88;Config[1][3]=0xa7;Config[1][11]=3;Config[1][44]=0x62;Config[1][45]=0x14;Config[1][46]=0xcd;Config[1][47]=0x13;')
tests = tests.replace('int main(void) {TestCore();', '#include "hybrid_tests.inc"\nint main(void) {TestHybrid();TestCore();')
tests = tests.replace("name[0]==L'A'", "name[0]==L'M'")
tests = tests.replace('Scenario=scenario;ConfigReads=', 'HybridDecodeRequested=FALSE;Scenario=scenario;ConfigReads=')
tests = tests.replace('#include "hybrid_tests.inc"', '#include "hybrid_tests.inc"\n#include "hybrid_decode_tests.inc"')
tests = tests.replace('TestHybrid();TestCore();', 'TestHybridDecode();TestHybrid();TestCore();')
(OUT / 'tests/host_tests.c').write_text(tests, encoding='utf-8')
shutil.copy2(HERE / 'hybrid_tests.inc', OUT / 'tests/hybrid_tests.inc')
shutil.copy2(HERE / 'hybrid_entry_tests.c', OUT / 'tests/hybrid_entry_tests.c')
shutil.copy2(HERE / 'hybrid_decode_tests.inc', OUT / 'tests/hybrid_decode_tests.inc')
pins = {}
for path in [*(DIAG / 'src').glob('*'), *(HERE / n for n in ['hybrid_policy.h', 'hybrid_policy.c', 'hybrid_probe.inc', 'hybrid_decode.inc', 'hybrid_decode_tests.inc', 'hybrid_entry.c', 'hybrid_tests.inc', 'hybrid_entry_tests.c', 'prepare_hybrid.py', 'Build-Hybrid.ps1'])]:
    if path.is_file():
        pins[str(path.relative_to(HERE.parent))] = hashlib.sha256(path.read_bytes()).hexdigest()
build_id = hashlib.sha256(json.dumps(pins, sort_keys=True).encode()).hexdigest()
(SRC / 'build_id.h').write_text(f'#define PROBE_BUILD_ID "{build_id}"\n', encoding='ascii')
(OUT / 'source-provenance.json').write_text(json.dumps({'source_hashes': pins, 'build_id': build_id, 'mode': 'hybrid-explicit-decode-test', 'unlock_linked': False, 'capture_pin_sha256': hashlib.sha256(captured_bytes).hexdigest()}, indent=2), encoding='utf-8')
sys.path.insert(0, str(DIAG / 'tools'))
from verify import validate_source
for path in SRC.glob('*'):
    if path.suffix not in ('.c', '.inc'):
        continue
    source = path.read_text().replace('->Mem.Read', '->ApprovedRead')
    if path.name == 'hybrid_decode.inc':
        assert source.count('->Pci.Write(') == 2
        assert source.count('EfiPciIoWidthUint16,4,1,') == 2
        source = source.replace('->Pci.Write', '->ApprovedCommandWrite')
    validate_source(source)
print('Prepared hybrid diagnostic backend: Intel preservation snapshots, PCI power policy, no transaction library.')
