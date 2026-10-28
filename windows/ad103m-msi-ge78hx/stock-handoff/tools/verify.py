"""Read-only source, header, profile and PE checks; never executes the EFI."""
from pathlib import Path
import hashlib
import importlib.util
import json
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
def require(ok, message):
    if not ok:
        raise ValueError(message)

def main():
    checker = ROOT.parent / 'diagnostic/tools/verify.py'
    spec = importlib.util.spec_from_file_location('probe_pe_checks', checker)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.validate_pe((ROOT / 'build/MSI-STOCK-A.EFI').read_bytes())
    sources = '\n'.join(p.read_text() for p in (ROOT / 'src').glob('*'))
    allowed = {'Close','FreePool','OutputString','HandleProtocol','LocateHandleBuffer',
               'OpenVolume','Open','GetInfo','AllocatePool','Read','LoadImage',
               'ReadKeyStroke','Stall','StartImage','UnloadImage'}
    calls = set(re.findall(r'->\s*(\w+)\s*\(', sources))
    require(calls == allowed, f'Unexpected/missing firmware calls: {calls ^ allowed}')
    for word in ('PciIo','GraphicsOutput','DisconnectController','ConnectController',
                 'SetVariable','ResetSystem','ExitBootServices','RaiseTPL','RestoreTPL',
                 'AllocateBuffer','FreeBuffer','SetWatchdogTimer'):
        require(word not in sources, 'Forbidden capability: ' + word)
    require('EFI_FILE_MODE_WRITE' not in sources and 'EFI_FILE_MODE_CREATE' not in sources, 'File writes forbidden')
    require(sources.count('->StartImage(') == 1 and sources.count('->LoadImage(') == 1, 'Single image route')
    profile = json.loads((ROOT / 'build/profile.json').read_text(encoding='utf-8-sig'))
    require(profile['authenticode_status'] == 'Valid', 'Unsigned/untrusted profile origin')
    require(not profile['gpu_access'] and not profile['esp_modified'], 'Profile collector side effects')
    image = Path(profile['hash_origin']).read_bytes()
    require(len(image) == profile['image_bytes'] and hashlib.sha256(image).hexdigest() == profile['image_sha256'], 'Stale image pin')
    require(image[:2] == b'MZ', 'Profile image is not PE')
    off = struct.unpack_from('<I', image, 0x3c)[0]
    require(image[off:off+4] == b'PE\0\0' and struct.unpack_from('<H', image, off+4)[0] == 0x8664, 'Wrong image architecture')
    require(struct.unpack_from('<H', image, off+24+68)[0] == 10, 'Profile image is not an EFI application')
    headers = ROOT.parent.parent / 'ad104-msi/vendor/edk2'
    for path, digest in json.loads((headers / 'headers.json').read_text())['files'].items():
        require(hashlib.sha256((headers / path).read_bytes()).hexdigest() == digest, 'Changed vendor header: ' + path)
    print('PASS: stock-only firmware call whitelist, pinned x64 Windows image, header pins and EFI PE; NOT executed.')

if __name__ == '__main__':
    main()
