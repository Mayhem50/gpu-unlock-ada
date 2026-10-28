"""Disassemble saved, digest-checked code as COFF data; never execute firmware."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from release_dependencies import analyze,observation


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('capture',type=Path);p.add_argument('manifest',type=Path);p.add_argument('output',type=Path)
    args=p.parse_args();data=json.loads(args.capture.read_text(encoding='utf-8-sig'))
    manifest=json.loads(args.manifest.read_text(encoding='utf-8-sig'))
    analyze(data,manifest['build_id'])
    if not observation(data)['resolved']:raise ValueError('Unresolved passive callback origin')
    linker=Path(manifest['toolchain']['linker']['path'])
    if sha(linker)!=manifest['toolchain']['linker']['sha256']:raise ValueError('Local linker provenance changed')
    dumpbin=linker.with_name('dumpbin.exe')
    images={i['handle']:i for i in data['platform']['dependencies']['images']}
    for a in data['platform']['gpu_agents']+data['platform']['boundary_agents']:
        images.setdefault(a['image_handle'],dict(handle=a['image_handle'],code=a['code']))
    args.output.mkdir(parents=True,exist_ok=False)
    report=dict(capture_sha256=sha(args.capture),manifest_sha256=sha(args.manifest),
                dumpbin_sha256=sha(dumpbin),firmware_executed=False,gpu_access=False,images=[])
    for image in images.values():
        code=image['code'];raw=bytes.fromhex(code['hex'])
        if len(raw)!=code['length'] or hashlib.sha256(raw).hexdigest()!=code['loaded_sha256']:
            raise ValueError('Captured executable section digest mismatch')
        # COFF data object only: no PE/DOS header, EFI entry point or linker run.
        coff=struct.pack('<HHIIIHH',0x8664,1,0,0,0,0,0)+struct.pack(
            '<8sIIIIIIHHI',b'.text',len(raw),int(code['rva'],16),len(raw),60,0,0,0,0,0x60000020)+raw
        obj=args.output/f"image-{image['handle']}.obj";obj.write_bytes(coff)
        run=subprocess.run([str(dumpbin),'/disasm:nobytes',str(obj)],capture_output=True,text=True,check=True)
        disasm=args.output/f"image-{image['handle']}-disasm.txt";disasm.write_text(run.stdout,encoding='utf-8')
        report['images'].append(dict(snapshot_image_handle=image['handle'],code_sha256=code['loaded_sha256'],
                                     code_rva=code['rva'],code_bytes=len(raw),coff_sha256=sha(obj),disasm_sha256=sha(disasm)))
    (args.output/'manifest.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(f'{len(images)} saved image sections disassembled; no firmware/GPU execution')


if __name__=='__main__':main()
