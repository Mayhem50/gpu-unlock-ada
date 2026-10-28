"""Seal and verify a local diagnostic packet. No USB, firmware or GPU access."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import sys
from verify import validate_pe

ROOT=Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inputs(root):
    vendor=root.parent.parent/'ad104-msi/vendor/edk2'
    pins=json.loads((vendor/'headers.json').read_text())['files']
    files=[]
    for folder in ('src','tests','tools'):
        files.extend(p for p in (root/folder).rglob('*') if p.is_file() and p.suffix in ('.c','.h','.py','.ps1') and '__pycache__' not in p.parts)
    files.extend(root/n for n in ('Build.ps1','Test-Host.ps1','README.md','COLD-CAPTURE.txt','VALIDATION.md'))
    files.append(vendor/'headers.json')
    for name,expected in pins.items():
        p=(vendor/name).resolve()
        if not p.is_relative_to(vendor.resolve()) or digest(p)!=expected:
            raise ValueError('Invalid pinned header: '+name)
        files.append(p)
    headers={p.resolve() for p in (vendor/'Include').rglob('*.h')}
    if headers!={(vendor/n).resolve() for n in pins if n.endswith('.h')}:
        raise ValueError('Unexpected or missing vendor headers')
    import os
    return dict(sorted((Path(os.path.relpath(p,root)).as_posix(),digest(p)) for p in files))


def verify(root,manifest):
    if (manifest.get('version')!='0.4.4' or manifest.get('binary')!='AD103M-PROBE.EFI' or
        manifest.get('tests_passed') is not True or manifest.get('packet_ready') is not True or
        manifest.get('gpu_write_capability') is not False or manifest.get('unlock_capability') is not False or
        manifest.get('execution_approved') is not False):
        raise ValueError('Incomplete or incompatible diagnostic manifest')
    if manifest.get('inputs_sha256')!=inputs(root):raise ValueError('Stale or missing build inputs')
    captured=manifest['inputs_sha256']
    sources={k[4:]:v for k,v in captured.items() if k.startswith('src/')}
    identity='\n'.join(k+'='+v for k,v in sorted(sources.items()))
    identity+='\nheaders='+captured['../../ad104-msi/vendor/edk2/headers.json'].upper()
    identity+='\nbuild='+captured['Build.ps1'].upper()
    if manifest.get('source_sha256')!=sources or manifest.get('build_id')!=hashlib.sha256(identity.encode()).hexdigest():
        raise ValueError('Build identity does not match source inputs')
    if not re.fullmatch('[0-9a-f]{64}',manifest.get('sha256','')):raise ValueError('Invalid binary digest')
    binary=root/'build/AD103M-PROBE.EFI'
    if digest(binary)!=manifest['sha256']:raise ValueError('Binary digest mismatch')
    raw=binary.read_bytes();validate_pe(raw)
    if manifest['build_id'].encode() not in raw or b'0.4.4' not in raw:
        raise ValueError('Compiled build/version identity missing')
    if set(manifest.get('build_files_sha256',{}))!={'build/build_id.h','build/compile.cmd'}:
        raise ValueError('Missing generated build provenance')
    if set(manifest.get('toolchain',{}))!={'compiler','linker','vcvars','python'}:
        raise ValueError('Missing toolchain provenance')
    for name,expected in manifest['build_files_sha256'].items():
        p=(root/name).resolve()
        if not p.is_relative_to((root/'build').resolve()) or digest(p)!=expected:
            raise ValueError('Generated build input mismatch')
    for tool in manifest['toolchain'].values():
        if digest(Path(tool['path']))!=tool['sha256']:raise ValueError('Toolchain changed')
    return True


def seal(root):
    build=root/'build';manifest_path=build/'manifest.json'
    manifest=json.loads(manifest_path.read_text(encoding='utf-8-sig'))
    before=json.loads((build/'inputs-before.json').read_text())
    if inputs(root)!=before:raise ValueError('Inputs changed during compilation or validation')
    manifest.update(inputs_sha256=before,packet_ready=True,unlock_capability=False,
                    execution_approved=False,signed=False,
                    build_files_sha256={f'build/{n}':digest(build/n) for n in ('build_id.h','compile.cmd')})
    manifest['toolchain']['python']={'path':sys.executable,'sha256':digest(Path(sys.executable)),'version':sys.version}
    verify(root,manifest)
    with manifest_path.open('w',encoding='utf-8') as stream:json.dump(manifest,stream,indent=2)
    return manifest


def make_packet(root,manifest):
    verify(root,manifest)
    folder=root/'build/packets'/('diagnostic-0.4.4-'+manifest['sha256'][:16])
    folder.mkdir(parents=True,exist_ok=False)
    for name in ('AD103M-PROBE.EFI','manifest.json'):
        with (folder/name).open('xb') as stream:stream.write((root/'build'/name).read_bytes())
    if digest(folder/'AD103M-PROBE.EFI')!=manifest['sha256']:raise ValueError('Packet copy mismatch')
    text=('DIAGNOSTIC MSI GE78HX v0.4.4 — PAQUET LOCAL NON EXECUTE\n'
          'SHA256 : '+manifest['sha256']+'\nBuild ID : '+manifest['build_id']+'\n'
          'Ce paquet ne contient aucun unlock, reset, detachement GOP ou changement des protections.\n'
          'Il contient les lectures PCI/MMIO/PROM bornees de la sonde precedente et la collecte passive des dependances.\n'
          'La sonde ecrit un nouveau CAPxxxx.JSON sur son propre volume USB.\n'
          'Aucune installation USB ni execution n est autorisee par ce fichier ou le manifeste.\n'
          'Avant un essai distinct : verifier les sources/empreintes, les sauvegardes, la cle de recuperation accessible,\n'
          'l etat courant Secure Boot/BitLocker, le travail sauvegarde et l alimentation secteur.\n'
          'Ne pas coller de cle de recuperation dans le rapport. Conserver les captures 3/4 et leurs manifestes.\n'
          'Une future mise sur USB doit sauvegarder et verifier le Stage A actuel, sans formater ni modifier le boot interne.\n'
          'Au futur essai approuve, verifier la banniere diagnostic v0.4.4 et recuperer le nouveau CAP avec CE manifeste.\n'
          'Un rapport incomplet reste indetermine ; ne pas tenter de lecture forcee, reset ou unlock.\n')
    with (folder/'LIRE-AVANT-TEST.txt').open('x',encoding='utf-8') as stream:stream.write(text)
    return folder


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action',choices=('preflight','seal','verify','create'))
    args=p.parse_args()
    if args.action=='preflight':
        snapshot=inputs(ROOT)
        (ROOT/'build/inputs-before.json').write_text(json.dumps(snapshot,indent=2),encoding='utf-8')
        print('Pinned inputs preflight passed');return
    if args.action=='seal':
        seal(ROOT);print('Diagnostic manifest sealed; not executed');return
    manifest=json.loads((ROOT/'build/manifest.json').read_text(encoding='utf-8-sig'))
    if args.action=='create':print(make_packet(ROOT,manifest))
    else:verify(ROOT,manifest);print('Exact local diagnostic packet inputs verified; no execution approval')


if __name__=='__main__':main()
