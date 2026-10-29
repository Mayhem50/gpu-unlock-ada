from pathlib import Path
import datetime
import hashlib
import json
import sys

HERE = Path(__file__).resolve().parent
BUILD = HERE / 'build/hybrid'
DIAG = HERE.parent / 'diagnostic'
sys.path.insert(0, str(DIAG / 'tools'))
from verify import validate_pe, validate_source

headers = HERE.parents[1] / 'ad104-msi/vendor/edk2'
for name, digest in json.loads((headers / 'headers.json').read_text())['files'].items():
    assert hashlib.sha256((headers / name).read_bytes()).hexdigest() == digest, name
binary = BUILD / 'MSI2717-HYBRID-D2.EFI'
validate_pe(binary.read_bytes())
for path in (BUILD / 'src').iterdir():
    if path.suffix in ('.c', '.inc'):
        source = path.read_text().replace('->Mem.Read', '->ApprovedRead')
        if path.name == 'hybrid_decode.inc':
            assert source.count('->Pci.Write(') == 2
            assert source.count('EfiPciIoWidthUint16,4,1,') == 2
            source = source.replace('->Pci.Write', '->ApprovedCommandWrite')
        validate_source(source)
compile_text = (BUILD / 'compile.cmd').read_text()
assert all(word not in compile_text for word in ['unlock_610', 'port.c', 'msi_release.c'])
capture = json.loads((BUILD / 'build/hybrid-capture.json').read_text())
assert capture['probe_version'] == 'hybrid-D2' and not capture['gpu_writes'] and not capture['unlock_attempted']
assert capture['hybrid_entry']['nvidia_mmio_read_allowed']
assert not capture['hybrid_entry']['detach_allowed'] and not capture['hybrid_entry']['unlock_enabled']
assert capture['hybrid_entry']['pmcsr'] == '0x0000'
assert len(capture['hybrid_entry']['nvidia_config256']) == 512
assert not capture['intel_preservation']['unlock_authorized']
assert capture['intel_preservation']['observed_unchanged']
assert len(capture['hybrid_entry']['intel_config256']) == 512
assert capture['register_snapshot']['complete'] and capture['physical_prom']['complete']
decoded = json.loads((BUILD / 'build/decode-capture.json').read_text())
assert decoded['hybrid_entry']['pci_command'] == '0x0000'
assert decoded['decode_test_entry']['requested'] and decoded['decode_test_entry']['memory_decode_enabled']
assert not decoded['decode_test_entry']['gpu_mmio_writes'] and not decoded['decode_test_entry']['unlock_attempted']
assert decoded['decode_test_cleanup']['original_config_restored'] and decoded['decode_test_cleanup']['exclusive_claim_closed']
assert decoded['decode_test_cleanup']['safe_to_return'] and decoded['intel_preservation']['observed_unchanged']
assert decoded['register_snapshot']['complete'] and decoded['physical_prom']['complete']
for name in ['host-tests.log', 'entry-tests.log']:
    assert 'PASS:' in (BUILD / 'build' / name).read_text()
provenance = json.loads((BUILD / 'source-provenance.json').read_text())
manifest = dict(complete=True, binary=binary.name, sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                bytes=binary.stat().st_size, mode='hybrid-explicit-decode-test', target='10de:2717/1462:13cd/A1',
                intel='8086:a788/1462:13cd @ 0:0:2:0', gpu_write_capability=True,
                pci_config_write_capability=True, gpu_mmio_write_capability=False,
                unlock_capability=False, firmware_or_dma_linked=False, requires_key='D or Enter: read-only; T: reversible PCI memory decode test',
                log_directory='MSI2717-HYBRID', hardware_tested=False,
                source_provenance=provenance, utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
(BUILD / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print('PASS: hybrid EFI D2; x64 PE, header pins, read-only D and explicit T decode/restore paths; no unlock library')
