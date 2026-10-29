from pathlib import Path
import hashlib
import json
import shutil
import zipfile

HERE = Path(__file__).resolve().parent
BUILD = HERE / 'build/hybrid-unlock'
manifest = json.loads((BUILD / 'manifest.json').read_text())
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
binary = BUILD / manifest['binary']
assert manifest['complete'] and manifest['native_core_scenarios'] == 240 and digest(binary) == manifest['sha256']
assets = HERE / 'build/assets'
pins = {
 'booter_load_ad102.bin': (57672, '6ee05b20e1dc244c08c052f36ef28469b8d09f4955d1fe33e48a179e11a7fd49'),
 'v67-ad102-610.43.03-four-plm.bin': (64000, '44c36b2d6b3424e53e18f3e55db85c833655de071ce66ce42749588a4a7dc8cb'),
 'gsp_ga10x.bin': (84277400, '572373620a37418f24dc16b5031c39338778c3257e48e8408de9a57291b24f3a'),
 'gsp_boot_ad102.bin': (36864, '45d6335a8185d6ff13e8d1484cea73f68d3d13aa1c19dc080ed1c3482dd5bac0'),
 'gsp_boot_desc_ad102.bin': (84, '83f8f3f1f5f9496e0ea8fc7103c2beb64d0b6de2f5ffe725f13514de5a95fa42'),
}
packet = BUILD / ('package-' + manifest['sha256'][:12])
for directory in ['EFI/BOOT', 'MSI2717-ASSETS', 'MSI2717-HYBRID-U', 'LICENSES']:
    (packet / directory).mkdir(parents=True, exist_ok=True)
shutil.copy2(binary, packet / 'EFI/BOOT/BOOTX64.EFI')
for name, (size, sha) in pins.items():
    assert (assets / name).stat().st_size == size and digest(assets / name) == sha, name
    shutil.copy2(assets / name, packet / 'MSI2717-ASSETS' / name)
shutil.copy2(HERE / 'TRIAL-HYBRID-U.txt', packet / 'MSI2717-HYBRID-U/PROCEDURE.txt')
shutil.copy2(BUILD / 'manifest.json', packet / 'MSI2717-HYBRID-U/efi-manifest.json')
shutil.copy2(assets / 'NVIDIA-LICENSE', packet / 'LICENSES/NVIDIA-LICENSE')
for name in ['firmware/NOTICE.md', 'licenses/GPL-2.0.txt', 'licenses/NVIDIA-MIT.txt']:
    shutil.copy2(HERE.parents[2] / name, packet / 'LICENSES' / Path(name).name)
files = {p.relative_to(packet).as_posix(): dict(bytes=p.stat().st_size, sha256=digest(p)) for p in packet.rglob('*') if p.is_file() and p.name != 'package-manifest.json'}
(packet / 'package-manifest.json').write_text(json.dumps(dict(mode=manifest['mode'], files=files), indent=2), encoding='utf-8')
archive = packet.with_suffix('.zip')
with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=1) as z:
    for p in packet.rglob('*'):
        if p.is_file(): z.write(p, p.relative_to(packet))
with zipfile.ZipFile(archive) as z:
    for name, entry in files.items(): assert hashlib.sha256(z.read(name)).hexdigest() == entry['sha256']
print(json.dumps(dict(package=str(packet), zip=str(archive), zip_sha256=digest(archive), firmware_files=5, zip_readback_verified=True), indent=2))
