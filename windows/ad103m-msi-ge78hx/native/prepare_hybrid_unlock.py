"""Generate the native Hybrid U build, isolated from dedicated and D/T EFIs."""
from pathlib import Path
import hashlib
import json
import shutil
import struct
import subprocess
import sys

HERE = Path(__file__).resolve().parent
OUT = HERE / 'build/hybrid-unlock'
SRC = OUT / 'source'
DIAG = HERE.parent / 'diagnostic'
subprocess.run([sys.executable, str(HERE / 'prepare.py')], check=True)
SRC.mkdir(parents=True, exist_ok=True)
shutil.copytree(HERE / 'build/source', SRC, dirs_exist_ok=True, ignore=shutil.ignore_patterns('build', '__pycache__'))
for name, expected in [('CAP0002', '997b4747928c6d34219e76ed5b81e05f0b5a2831f89d4fd0fd96271e9be6e6f3'), ('CAP0003', '4471cee433b461e07fc78afa4d93b205092458c0f6388526853d4bf40d89820c')]:
    path = HERE / f'build/hybrid/hardware/{name}/{name}.JSON'
    assert hashlib.sha256(path.read_bytes()).hexdigest() == expected
    capture = json.loads(path.read_text(encoding='utf-8-sig'))
    if name == 'CAP0003':
        assert capture['decode_test_cleanup']['safe_to_return'] and capture['intel_preservation']['observed_unchanged']
        assert capture['physical_prom']['sha256'] == '24b6d455544615dd3185e6c42512c104add2322b8c10053f6384566729ce7076'
config = bytes.fromhex(capture['hybrid_entry']['nvidia_config256'])
(SRC / 'hybrid_capture_pin.h').write_text('static const UINT8 HybridCapturedHeader[64]={' + ','.join(hex(x) for x in config[:64]) + '};\n', encoding='ascii')
for name in ['core.h', 'sample.h', 'platform.h']:
    shutil.copy2(DIAG / 'src' / name, SRC / name)
shutil.copy2(DIAG / 'src/core.c', SRC / 'hybrid_core.c')
resource_check = (DIAG / 'src/sample.c').read_text().split('BOOLEAN ResourceMatches(', 1)[1].split('static void FailureCount(', 1)[0]
with (SRC / 'hybrid_core.c').open('a', encoding='utf-8') as out:
    out.write('\nBOOLEAN ResourceMatches(' + resource_check)
for name in ['hybrid_probe.inc', 'hybrid_policy.h', 'hybrid_policy.c', 'hybrid_native.h', 'hybrid_native_backend.inc']:
    shutil.copy2(HERE / name, SRC / name)
shutil.copy2(HERE / 'hybrid_unlock_app.c', SRC / 'app.c')
# Reuse the diagnostic's PCI/path/GOP inventory, not its application, sampler,
# private-console reader, T write path, or D/T entrypoint.
prefix = (DIAG / 'src/probe.c').read_text(encoding='utf-8-sig').split('static EFI_STATUS OpenLog(', 1)[0]
prefix = prefix.replace('#ifndef PROBE_BUILD_ID\n#error Build with Build.ps1 to bind captures to the source manifest\n#endif', '')
for line in ['static EFI_GUID LoadedGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID;', 'static EFI_GUID FsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;', 'static EFI_GUID GlobalGuid={0x8be4df61,0x93ca,0x11d2,{0xaa,0x0d,0x00,0xe0,0x98,0x03,0x2b,0x8c}};', 'static CHAR8 * volatile RelocationAnchor=Report;']:
    assert line in prefix
    prefix = prefix.replace(line, '')
prefix = prefix.replace('#define REPORT_CAPACITY (64u*1024u*1024u)', '#define REPORT_CAPACITY (16u*1024u)')
prefix = prefix[:prefix.index('static void VariableByte(')] + prefix[prefix.index('static void BarJson('):]
prefix = prefix.replace('static void Message(EFI_SYSTEM_TABLE *st,CHAR16 *s) {\n    if(st->ConOut && st->ConOut->OutputString) st->ConOut->OutputString(st->ConOut,s);\n}\n', '')
release = '#include "port.h"\n' + prefix + '\n#include "hybrid_probe.inc"\n#include "hybrid_native_backend.inc"\n'
(SRC / 'msi_release.c').write_text(release, encoding='utf-8')

def change(path, old, new):
    text = path.read_text(encoding='utf-8-sig')
    assert old in text, (path.name, old[:60])
    path.write_text(text.replace(old, new), encoding='utf-8')

change(SRC / 'Build-Experimental.ps1', "@('app','boot_io','msi_release')", "@('app','boot_io','msi_release','hybrid_core','hybrid_policy')")
change(SRC / 'Build-Experimental.ps1', "@('app','boot_io','msi_release','port'", "@('app','boot_io','msi_release','hybrid_core','hybrid_policy','port'")
change(SRC / 'boot_io.c', 'MSI2717-UNLOCK', 'MSI2717-HYBRID-U')
change(SRC / 'boot_io.c', 'MSI 2717 EXPERIMENTAL v0.1', 'MSI 2717 HYBRID U6')
change(SRC / 'boot_io.c', 'D : preflight sans detachement ni firmware GPU. U : essai unlock, puis Windows.', 'D : controle hybride sans ecriture. U : UNLOCK NVIDIA, affichage Intel conserve, puis Windows.')
change(SRC / 'boot_io.c', "L'image peut rester FIGEE sur ce message pendant la liberation du GPU.", 'Intel reste connecte. U lance le firmware NVIDIA et un reset de cette seule fonction PCI.')
change(SRC / 'tests/test_boot_io.c', 'MSI2717-UNLOCK', 'MSI2717-HYBRID-U')
# Exercise the same transaction library with the observed Hybrid configuration
# after managed decode enable, instead of silently keeping dedicated command=3.
enabled = bytearray(config)
struct.pack_into('<H', enabled, 4, 2)
(SRC / 'tests/captured_config.h').write_text('static const UINT8 CapturedConfig[256]={' + ','.join(hex(x) for x in enabled) + '};\n', encoding='ascii')
change(SRC / 'tests/test_efi_transaction.c', 'fake.pci_attributes=EFI_PCI_IO_ATTRIBUTE_IO|EFI_PCI_IO_ATTRIBUTE_MEMORY;', 'fake.pci_attributes=EFI_PCI_IO_ATTRIBUTE_MEMORY;')
# U4 reached native cold admission and was refused only by our historical
# selector list. The user requested removal of that list: accept a valid
# stock read, latch its entire value, and retain every later drift check.
selector_evidence = HERE / 'build/hybrid-unlock/hardware/RUN0004/RUN0004.TXT'
selector_hash = hashlib.sha256(selector_evidence.read_bytes()).hexdigest()
assert selector_hash == '2feb590d7d46e0dbf2413497d3e1b6c6d9ce25ffe99bf10cf687e0b23d90fa09'
assert 'SS0=04003025 SS1=00000000 fault=0' in selector_evidence.read_text()
old_selector_gate = '(*Ss0!=0x88888888u && (GuProfileObservationOnly() || *Ss0==0x04003065u || *Ss0==0x04013065u || *Ss0==0x04203065u || *Ss0==0x04203025u || *Ss0==0x05213065u))'
change(SRC / 'profile.c', old_selector_gate, '*Ss0!=0x88888888u')
change(SRC / 'tests/test_efi_transaction.c', 'permutation<5', 'permutation<8')
change(SRC / 'tests/test_efi_transaction.c', '0x04203025u,0x05213065u}', '0x04203025u,0x05213065u,0x04003025u,0x12345678u,0x00000000u}')
change(SRC / 'tests/test_efi_transaction.c', 'SCENARIO_COUNT*5', 'SCENARIO_COUNT*8')
(SRC / 'tests/fixture.h').write_bytes((HERE / 'build/fixture.h').read_bytes())
profile_tests = (HERE / 'profile_tests.c').read_text().replace('"build/source/profile.h"', '"../profile.h"').replace('"build/source/compat/compat.h"', '"../compat/compat.h"').replace('"build/fixture.h"', '"fixture.h"')
profile_tests = profile_tests.replace('CHECK(GuAd104ColdEntry()); /* Exact captured value, not a bitmask. */', 'config[1]=(config[1]&~7u)|2u;CHECK(GuAd104ColdEntry()); /* Hybrid managed memory decode, BME off. */')
profile_tests = profile_tests.replace('reg_values[12]=0x04203025u;CHECK(GuAd104ColdEntry());', '''reg_values[12]=0x04203025u;CHECK(GuAd104ColdEntry());
 reg_values[12]=0x04003025u;CHECK(GuAd104ColdEntry());
reg_values[12]=0x04003024u;CHECK(GuAd104ColdEntry());
 reg_values[12]=0x04003026u;CHECK(GuAd104ColdEntry());
 reg_values[12]=0x04003035u;CHECK(GuAd104ColdEntry());
 reg_values[12]=0x12345678u;CHECK(GuAd104ColdEntry());
 reg_values[12]=0u;CHECK(GuAd104ColdEntry());''')
for admitted in ['0x04203064u', '0x05213064u', '0x05213075u']:
    profile_tests = profile_tests.replace(f'reg_values[12]={admitted};CHECK(!GuAd104ColdEntry());', f'reg_values[12]={admitted};CHECK(GuAd104ColdEntry());')
profile_tests = profile_tests.replace('exact captured SS0 admitted; unknown values and AD104 identity refused', 'SS0 valid entry accepted without historical list; invalid reads, unlocked marker and AD104 identity refused')
(SRC / 'tests/test_hybrid_profile.c').write_text(profile_tests, encoding='utf-8')
(SRC / 'tests/hybrid_config.h').write_text('static const UINT8 HybridConfig[256]={' + ','.join(hex(x) for x in config) + '};\n', encoding='ascii')
# Native backend harness: retain existing EFI mocks, replace the application
# under test with the actual ownership backend, then add managed-attribute tests.
host = (DIAG / 'tests/host_tests.c').read_text(encoding='utf-8-sig').split('static void TestCore(void)', 1)[0]
host = host.replace('#include "../src/probe.c"', '#include "../msi_release.c"\nstatic EFI_GUID LoadedGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID,FsGuid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;\nstatic EFI_GUID GlobalGuid={0x8be4df61,0x93ca,0x11d2,{0xaa,0x0d,0,0xe0,0x98,0x03,0x2b,0x8c}};')
host = host.replace('Config[1][2]=1;Config[1][11]=3;', 'Config[1][2]=0x88;Config[1][3]=0xa7;Config[1][11]=3;Config[1][44]=0x62;Config[1][45]=0x14;Config[1][46]=0xcd;Config[1][47]=0x13;')
host += '\n#include "hybrid_native_tests.inc"\n'
(SRC / 'tests/test_hybrid_native.c').write_text(host, encoding='utf-8')
shutil.copy2(HERE / 'hybrid_native_tests.inc', SRC / 'tests/hybrid_native_tests.inc')
shutil.copy2(HERE / 'hybrid_unlock_app_tests.c', SRC / 'tests/test_hybrid_app.c')
# Pin every generated source, rather than claiming the dedicated build's
# provenance covers the new backend or application.
pins = {str(p.relative_to(SRC)): hashlib.sha256(p.read_bytes()).hexdigest() for p in SRC.rglob('*') if p.is_file() and p.suffix in ('.c', '.h', '.inc', '.py', '.ps1')}
(OUT / 'source-provenance.json').write_text(json.dumps({'source_hashes': pins, 'hardware_hybrid_decode_capture': '4471cee433b461e07fc78afa4d93b205092458c0f6388526853d4bf40d89820c', 'hardware_cold_selector_capture_sha256': selector_hash, 'hardware_unlock_validated': False}, indent=2), encoding='utf-8')
print('Prepared isolated native Hybrid U6: full SS0 entry latch, no historical selector whitelist; Intel preserved.')
