"""Prepare base MSI transaction/profile sources for the Hybrid generator.

The AD104 reference is reused with explicit checked substitutions. The Hybrid
generator installs the active app, ownership backend and SS0 admission policy.
Generated private profiles and source live under build/.
"""
from pathlib import Path
import hashlib
import json
import shutil
import uuid
import struct

here = Path(__file__).resolve().parent
target = here / 'build' / 'source'
reference = here.parents[1] / 'ad104-msi'
capture = here.parent / 'captures/2026-10-28-run6/CAP0006.JSON'
raw = capture.read_bytes()
assert hashlib.sha256(raw).hexdigest() == '1ee966fb7f30b6009639d4629d975ebefd13c6c9be293e4f5c6980b0d133d6c7'
d = json.loads(raw.decode('utf-8-sig'))
target.mkdir(parents=True, exist_ok=True)
pins = {}
for folder in ['vendor', 'compat', 'tools']:
    shutil.copytree(reference / folder, target / folder, dirs_exist_ok=True)
for p in reference.iterdir():
    if p.is_file() and p.suffix in ['.c', '.h', '.ps1']:
        shutil.copy2(p, target / p.name)
for p in reference.rglob('*'):
    if p.is_file() and p.suffix in ['.c', '.h', '.ps1', '.py'] and 'build' not in p.relative_to(reference).parts:
        pins[str(p.relative_to(reference))] = hashlib.sha256(p.read_bytes()).hexdigest()

def edit(name, replacements):
    p = target / name
    s = p.read_text(encoding='utf-8-sig')
    for old, new in replacements:
        if old not in s:
            raise ValueError(f'{name}: reference changed: {old[:80]}')
        s = s.replace(old, new)
    p.write_text(s, encoding='utf-8')

edit('port.h', [('0x278210deU', '0x271710deU'), ('0x51321462U', '0x13cd1462U'),
                ('0x80000000ULL', '0x5e000000ULL'), ('0x2ffaU', '0x3ff8U')])
# Every native entry gate and every family-dependent transaction predicate use
# the same exact target. The other families are unreachable at the entry gates.
edit('vendor/unlock_610.c', [('0x2782', '0x2717'), ('ss1!=1', 'ss1!=0')])
edit('profile.c', [
 ('c81eb48fc6cb4f320d302e98f88db7101dfc1e4c3e21de82f57a6c8233ad3fb4', '24b6d455544615dd3185e6c42512c104add2322b8c10053f6384566729ce7076'),
 ('0x45488', '0x45788'), ('0x45188', '0x45488'),
 ('c[6]!=0x40', 'c[6]!=0x60'), ('c[8]!=0x44', 'c[8]!=0x64'), ('c[9]!=0x4001', 'c[9]!=0x5001'),
 ('0x194000a1', '0x193000a1'), ('0x2ffa', '0x3ff8'), ('0x2ef9ff0', '0x3cf7ff0'),
 ('*Ss0!=0x88888888 && *Ss1==1', '(*Ss0==0x04003065u || *Ss0==0x04013065u || *Ss0==0x04203065u) && *Ss1==0')])
# Log precise cold-profile evidence before the menu. Firmware may continue to
# Windows when an EFI application returns; the USB trace must explain refusal.
edit('profile.c', [
 ('if(!GuCheckConfig(c,s,b,d,f,&caps)', '''GuLog("MSI cold PCI: location=%zu:%zu:%zu:%zu id=%08x sub=%08x classrev=%08x bars=%08x,%08x,%08x,%08x,%08x,%08x\\n",(size_t)s,(size_t)b,(size_t)d,(size_t)f,c[0],c[11],c[2],c[4],c[5],c[6],c[7],c[8],c[9]);
  if(!GuCheckConfig(c,s,b,d,f,&caps)'''),
 ('return caps.pm==Gu.pm_cap', '''GuLog("MSI cold caps: pm=%x/%x pcie=%x/%x version=%x/%x msi=%x/%x msix=%x/%x\\n",caps.pm,Gu.pm_cap,caps.pcie,Gu.pcie_cap,caps.pcie_version,Gu.pcie_version,caps.msi,Gu.msi_cap,caps.msix,Gu.msix_cap);
  return caps.pm==Gu.pm_cap'''),
 ('if(GuRead32(offsets[i])!=values[i] || Gu.fault)return FALSE;', '''{UINT32 actual=GuRead32(offsets[i]);
     GuLog("MSI cold register: offset=%08x actual=%08x expected=%08x fault=%u\\n",offsets[i],actual,values[i],Gu.fault);
     if(actual!=values[i] || Gu.fault)return FALSE;}'''),
 ('*Ss0=GuRead32(0x82381c);*Ss1=GuRead32(0x823820);', '''*Ss0=GuRead32(0x82381c);*Ss1=GuRead32(0x823820);
  GuLog("MSI cold selectors: SS0=%08x SS1=%08x fault=%u\\n",*Ss0,*Ss1,Gu.fault);'''),
 ('if(Gu.started || Gu.fault || Gu.retained || Gu.allocations || Gu.profile_verified ||', '''GuLog("MSI prepare state: started=%u fault=%u retained=%u allocations=%zu profile=%u prom=%u BAR0=%llx bytes=%llx\\n",Gu.started,Gu.fault,Gu.retained,(size_t)Gu.allocations,Gu.profile_verified,Gu.prom_verified,(unsigned long long)Gu.bar_base,(unsigned long long)Gu.bar_size);
  if(Gu.started || Gu.fault || Gu.retained || Gu.allocations || Gu.profile_verified ||''')])

# Reuse the already confirmed Stage A ESP/image profile. Never infer an ESP
# confirmation from the servicing file or accept an unconfirmed profile.
profile = json.loads((here/'build/windows-profile.json').read_text(encoding='utf-8-sig'))
assert profile['operator_esp_confirmed'] or profile['esp_bytes_read_and_matched']
assert profile['authenticode_status'] == 'Valid'
assert hashlib.sha256(Path(profile['local_copy']).read_bytes()).hexdigest() == profile['image_sha256']
guid = uuid.UUID(profile['partition_guid'].strip('{}'))
b = guid.bytes_le
initializer = '{0x%x,0x%x,0x%x,{%s}}' % (int.from_bytes(b[:4], 'little'), int.from_bytes(b[4:6], 'little'), int.from_bytes(b[6:8], 'little'), ','.join(hex(x) for x in b[8:]))
(target / 'boot_profile.h').write_text('#pragma once\n#define GU_WINDOWS_PARTITION_INITIALIZER ' + initializer + '\n', encoding='ascii')
edit('boot_io.c', [('d->PartitionNumber==2&&d->PartitionStart==1085440', f"d->PartitionNumber=={profile['partition_number']}&&d->PartitionStart=={profile['start_lba']}"),
                   ('d->PartitionSize==202752', f"d->PartitionSize=={profile['sectors']}"),
                   ('3086728', str(profile['image_bytes'])),
                   ('b6f5ff8a5f605840796875259bba555d0642745a7ac8a5b38d16603768b22570', profile['image_sha256']),
                   ('AD104-ASSETS', 'MSI2717-ASSETS'), ('AD104-UNLOCK', 'MSI2717-UNLOCK'),
                   ('AD104 EXPERIMENTAL v0.5.4', 'MSI 2717 EXPERIMENTAL v0.1')])
edit('boot_io.c', [('static BOOLEAN Silent;', 'static BOOLEAN Silent,DryRun;\nBOOLEAN GuBootDryRunRequested(VOID){return DryRun;}'),
 ('if(key.UnicodeChar==\'u\'', "if(key.UnicodeChar=='d'||key.UnicodeChar=='D'){DryRun=TRUE;return EFI_SUCCESS;}\n      if(key.UnicodeChar=='u'"),
 ('U : un essai, puis Windows.', 'D : preflight sans detachement ni firmware GPU. U : essai unlock, puis Windows.')])
edit('boot_io.h', [('#pragma once', '#pragma once\nBOOLEAN GuBootDryRunRequested(VOID);')])
# boot_io.h's UEFI include must precede the new declaration.
edit('boot_io.h', [('BOOLEAN GuBootDryRunRequested(VOID);', ''), ('#include "port.h"', '#include "port.h"\nBOOLEAN GuBootDryRunRequested(VOID);')])
# The observation-only mode is irreversible for this application lifetime.
edit('profile.c', [('#include "profile.h"', '#include "profile.h"\nextern BOOLEAN GuProfileObservationOnly(VOID);'),
 ('(*Ss0==0x04003065u || *Ss0==0x04013065u || *Ss0==0x04203065u)', '(*Ss0!=0x88888888u && (GuProfileObservationOnly() || *Ss0==0x04003065u || *Ss0==0x04013065u || *Ss0==0x04203065u))')])
# Keep the archive self-contained: its mode defaults to strict and is only set
# once by the EFI root. It cannot be cleared to promote an observation to U.
edit('profile.c', [('extern BOOLEAN GuProfileObservationOnly(VOID);', '''static BOOLEAN ObservationOnly;
BOOLEAN GuProfileObservationOnly(VOID){return ObservationOnly;}
VOID GuSetProfileObservationOnly(VOID){ObservationOnly=TRUE;}''')])
edit('profile.h', [('#pragma once', '#pragma once\n/* Call only after selecting D; irreversible for this application lifetime. */')])
edit('profile.h', [('#include "port.h"', '#include "port.h"\nBOOLEAN GuProfileObservationOnly(VOID);')])
edit('vendor/unlock_610.c', [('if (!pdev || pdev->vendor!=0x10de', 'if (GuProfileObservationOnly() || !pdev || pdev->vendor!=0x10de')])
# Stock values observed on this exact MSI, including native RUN0002/3.
# Preserve an exact finite list, full selector latch, all other register pins
# and the separate irreversible observation-only mode.
edit('profile.c', [('*Ss0==0x04203065u))', '*Ss0==0x04203065u || *Ss0==0x04203025u || *Ss0==0x05213065u))')])
# Run the actual retained native core/compatibility library with the real
# reference assets against fake EFI transports for all five observed states.
(target/'tests').mkdir(exist_ok=True)
shutil.copy2(reference/'tests/test_efi_transaction.c',target/'tests/test_efi_transaction.c')
edit('tests/test_efi_transaction.c', [
 ('fw.image_off==0x45488', 'fw.image_off==0x45788'),
 ('0x194000a1', '0x193000a1'), ('0x2782', '0x2717'), ('0x2ffa', '0x3ff8'),
 ('0x2ef9ff0', '0x3cf7ff0'), ('0x2ff800000ULL', '0x3ff600000ULL'),
 ('0x7fa00000ULL', '0x17f800000ULL'), ('0x2ff8000', '0x3ff6000'), ('0x2ff8ff0', '0x3ff6ff0'),
 ('permutation<3', 'permutation<5'),
 ('test_entry_ss0=permutation==2?0x23603005U:permutation==1?0x23613005U:0x23703005U;test_entry_ss1=1;',
  'static const UINT32 states[]={0x04003065u,0x04013065u,0x04203065u,0x04203025u,0x05213065u};test_entry_ss0=states[permutation];test_entry_ss1=0;'),
 ('registers[SS1/4]==1', 'registers[SS1/4]==0'), ('SCENARIO_COUNT*3', 'SCENARIO_COUNT*5')])
(target/'tests/captured_config.h').write_text('static const UINT8 CapturedConfig[256]={'+','.join(hex(b) for b in bytes.fromhex(d['config256']))+'};\n',encoding='ascii')
(here/'build/capture-prom.bin').write_bytes(bytes.fromhex(d['physical_prom']['hex']))
shutil.copy2(reference/'tests/test_boot_io.c', target/'tests/test_boot_io.c')
edit('tests/test_boot_io.c', [
 ('3086728', str(profile['image_bytes'])),
 ('hd->PartitionNumber=2;hd->PartitionStart=1085440;hd->PartitionSize=202752;',
  f"hd->PartitionNumber={profile['partition_number']};hd->PartitionStart={profile['start_lba']};hd->PartitionSize={profile['sectors']};"),
 ('AD104-UNLOCK', 'MSI2717-UNLOCK'), ('AD104-ASSETS', 'MSI2717-ASSETS')])
(here / 'build/source-provenance.json').write_text(json.dumps({'reference_hashes': pins, 'capture_sha256': hashlib.sha256(raw).hexdigest(), 'hardware_tested': False, 'capture6_ss0_admitted': True}, indent=2), encoding='utf-8')
print('Prepared MSI native source: exact capture-6 SS0 admission and D preflight mode.')
words = struct.unpack('<64I', bytes.fromhex(d['config256']))
samples = d['register_snapshot']['samples']
(here / 'build/fixture.h').write_text('static UINT32 config[64]={' + ','.join(hex(x)+'u' for x in words) + '};\n' +
    'static const UINT32 reg_offsets[]={' + ','.join(str(int(s['offset'],16)) for s in samples) + '};\n' +
    'static UINT32 reg_values[]={' + ','.join(s['value']+'u' for s in samples) + '};\n', encoding='ascii')
