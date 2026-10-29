from pathlib import Path
import datetime
import hashlib
import json
import sys

HERE = Path(__file__).resolve().parent
BUILD = HERE / 'build/hybrid-unlock'
SRC = BUILD / 'source'
sys.path.insert(0, str(SRC / 'tools'))
from pe_format import validate_pe

binary = BUILD / 'MSI2717-HYBRID-U6.EFI'
data = binary.read_bytes()
validate_pe(data)
provenance = json.loads((BUILD / 'source-provenance.json').read_text())
for name, expected in provenance['source_hashes'].items():
    assert hashlib.sha256((SRC / name).read_bytes()).hexdigest() == expected, name
for name in ['msi_release.c', 'hybrid_native_backend.inc', 'hybrid_probe.inc', 'app.c']:
    source = (SRC / name).read_text()
    assert all(call not in source for call in ['->DisconnectController(', '->ConnectController(', '->SetMode(', '->ResetSystem(', '->SetVariable(']), name
assert 'hybrid_decode.inc' not in (SRC / 'msi_release.c').read_text()
assert 'gpu_unlock_610_unlock(&dev,&shared)' in (SRC / 'app.c').read_text()
assert 'GuHybridReadyForWindows()' in (SRC / 'app.c').read_text()
assert 'GuHybridAbort()' in (SRC / 'app.c').read_text()
backend = (SRC / 'hybrid_native_backend.inc').read_text()
assert 'EFI_PCI_IO_ATTRIBUTE_MEMORY' in backend
finish = backend.split('EFI_STATUS GuHybridFinish(VOID)', 1)[1].split('BOOLEAN GuHybridReadyForWindows(VOID)', 1)[0]
assert 'NativeRestore()' not in finish and 'EfiPciIoAttributeOperationDisable' not in finish
assert 'if(!GuHybridReadyForWindows())return EFI_NOT_READY;' in finish
assert '30 scenarios' in (BUILD / 'backend-tests.log').read_text()
assert '12 flow scenarios' in (BUILD / 'app-tests.log').read_text()
pe = json.loads((SRC / 'build/experimental-pe.json').read_text())
assert pe['x64_efi_verified'] and not pe['imports_tls_clr'] and pe['max_function_stack'] <= 4096
core_log = (BUILD / 'core-tests.log').read_text()
assert core_log.splitlines()[-1].startswith('240 real core + compat + UEFI port scenarios passed;'), core_log.splitlines()[-1]
assert '*Ss0==0x' not in (SRC / 'profile.c').read_text()
assert 'GuEntrySelectorsMatch(ss0,ss1)' in (SRC / 'profile.c').read_text()
assert 'checks' in (BUILD / 'profile-tests.log').read_text()
assert '571 checks; 8 cases' in (BUILD / 'boot-tests.log').read_text()
windows = json.loads((HERE / 'build/windows-profile.json').read_text(encoding='utf-8-sig'))
assert hashlib.sha256(Path(windows['local_copy']).read_bytes()).hexdigest() == windows['image_sha256']
assert windows['image_sha256'].encode() in data
manifest = dict(complete=True, binary=binary.name, sha256=hashlib.sha256(data).hexdigest(), bytes=len(data),
                mode='hybrid-native-U6', target='10de:2717/1462:13cd/A1', intel='8086:a788/1462:13cd',
                gpu_write_capability=True, unlock_capability=True, automatic_unlock=False, requires_key='U',
                intel_driver_detach=False, controller_reconnect=False, original_decode_restored_before_windows=False,
                nvidia_memory_decode_enabled_before_windows=True, nvidia_bus_master_disabled_before_windows=True,
                native_backend_scenarios=30, application_flow_scenarios=12, native_core_scenarios=240,
                ss0_admission='valid stock read, full entry latch; no historical value whitelist',
                ss0_full_transaction_drift_checks=True,
                backend_validation_sha256=hashlib.sha256((BUILD / 'backend-tests.log').read_bytes()).hexdigest(),
                application_validation_sha256=hashlib.sha256((BUILD / 'app-tests.log').read_bytes()).hexdigest(),
                core_validation_sha256=hashlib.sha256((BUILD / 'core-tests.log').read_bytes()).hexdigest(),
                hardware_unlock_validated=False, hardware_decode_validated=True, windows_profile=windows,
                pe_validation=pe, source_provenance=provenance,
                utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
if (BUILD / 'core-reuse.json').exists():
    reuse = json.loads((BUILD / 'core-reuse.json').read_text())
    assert reuse['core_log_sha256'] == manifest['core_validation_sha256'] and reuse['scenarios'] == 240
    manifest['core_validation_reuse'] = reuse
(BUILD / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print(json.dumps({k: manifest[k] for k in ['complete', 'binary', 'sha256', 'mode', 'requires_key', 'native_core_scenarios', 'hardware_unlock_validated']}, indent=2))
