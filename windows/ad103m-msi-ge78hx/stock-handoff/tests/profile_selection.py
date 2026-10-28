"""Windows-only negative profile tests; no EFI execution or USB writes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--confirmation', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = root / 'build'
    profile = build / 'profile.json'
    before = hashlib.sha256(profile.read_bytes()).hexdigest()
    data = json.loads(profile.read_text(encoding='utf-8-sig'))
    bad = build / 'negative-profile-confirmation.json'
    bad.write_text(json.dumps({'TailleOctets': data['image_bytes'], 'SHA256': '0' * 64}))
    servicing = str(Path(os.environ['WINDIR']) / 'Boot/EFI/bootmgfw.efi')
    cases = [
        (['-EspImage', str(args.image)], 'requires the actual ESP fingerprint'),
        (['-EspImage', servicing, '-EspConfirmation', str(args.confirmation)], 'existing Windows component-store'),
        (['-EspImage', str(args.image), '-EspConfirmation', str(bad)], 'fingerprint does not match'),
    ]
    if data['servicing_copy_sha256'] != data['image_sha256']:
        cases.append((['-EspConfirmation', str(args.confirmation)], 'fingerprint does not match'))
    for flags, expected in cases:
        result = subprocess.run(['pwsh', '-NoProfile', '-File', str(root / 'Prepare-Profile.ps1'), *flags], capture_output=True, text=True)
        assert result.returncode and expected in result.stdout + result.stderr, (flags, result.stdout, result.stderr)
        assert hashlib.sha256(profile.read_bytes()).hexdigest() == before, 'Rejected selection overwrote approved profile'
    print(f'PASS: {len(cases)} invalid image/ESP-confirmation combinations refused; approved profile unchanged')

if __name__ == '__main__':
    main()
