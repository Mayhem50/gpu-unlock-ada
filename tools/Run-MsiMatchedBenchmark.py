"""Run the existing repository CUDA benchmark; no configuration mutations."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BENCH = ROOT / 'windows/ad104-msi/benchmark.py'
PYTHON = Path(sys.executable)
GPU = 'GPU-ea71890f-a456-ef46-4715-eb6166e50012'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--label', required=True)
options = parser.parse_args()
OUT = options.output.resolve()
OUT.mkdir(parents=True, exist_ok=False)
env = os.environ.copy()
env['CUDA_VISIBLE_DEVICES'] = GPU
env['CUDA_DEVICE_ORDER'] = 'PCI_BUS_ID'
identity = subprocess.run(['nvidia-smi', '--query-gpu=uuid,pci.bus_id,pci.device_id,driver_version', '--format=csv,noheader,nounits'], capture_output=True, text=True, check=True).stdout.strip()
assert len(identity.splitlines()) == 1 and identity.split(',')[0].strip() == GPU, identity
assert '0x271710DE' in identity, identity
metadata = dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), gpu_identity=identity,
               benchmark_sha256=hashlib.sha256(BENCH.read_bytes()).hexdigest(), python=str(PYTHON),
               git_commit=subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=ROOT, capture_output=True, text=True, check=True).stdout.strip(),
               configuration_changes=False, efi_or_unlock_executed=False,
               claim='Three matched GEMM processes on the exact MSI GPU; this benchmark does not read unlock registers; historical stock MUX mode was not recorded')
(OUT / 'manifest.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
print('Output:', OUT, flush=True)
fields = 'timestamp,uuid,pstate,utilization.gpu,memory.used,power.draw,power.limit,temperature.gpu,clocks.sm,clocks.mem,clocks_event_reasons.active'
with (OUT / 'telemetry.csv').open('w', encoding='utf-8') as stream, (OUT / 'telemetry-errors.txt').open('w', encoding='utf-8') as errors:
    sampler = subprocess.Popen(['nvidia-smi', '--query-gpu=' + fields, '--format=csv,nounits', '--loop-ms=250'], stdout=stream, stderr=errors)
    try:
        for repeat in range(1, 4):
            args = [str(PYTHON), str(BENCH), '--out', str(OUT / f'run-{repeat}.json'), '--label', f'{options.label}-{repeat}', '--size', '4096', '--samples', '15', '--batch', '5', '--warmup', '3']
            print(f'Run {repeat}/3', flush=True)
            with (OUT / f'run-{repeat}.log').open('w', encoding='utf-8') as log:
                result = subprocess.run(args, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=180)
            print((OUT / f'run-{repeat}.log').read_text(encoding='utf-8'), flush=True)
            if result.returncode:
                raise RuntimeError(f'Benchmark failed, exit {result.returncode}; see run-{repeat}.log')
            if sampler.poll() is not None:
                raise RuntimeError('Telemetry sampler stopped; inspect telemetry-errors.txt')
    finally:
        sampler.terminate()
        sampler.wait(timeout=15)
print('Completed matched post-unlock CUDA measurements; no configuration change.', flush=True)
