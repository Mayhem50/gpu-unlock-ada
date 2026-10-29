"""Keep the exact GPU active briefly for a targeted register read; no tuning."""
import argparse
import datetime
import json
import os
from pathlib import Path
import time

p = argparse.ArgumentParser()
p.add_argument('directory', type=Path)
args = p.parse_args()
os.environ['CUDA_VISIBLE_DEVICES'] = 'GPU-ea71890f-a456-ef46-4715-eb6166e50012'
os.environ['CUDA_DEVICE_ORDER'] = 'PCI_BUS_ID'
import torch

assert torch.cuda.device_count() == 1
assert torch.cuda.get_device_name() == 'NVIDIA GeForce RTX 4090 Laptop GPU'
x = torch.zeros(32, device='cuda')
torch.cuda.synchronize()
(args.directory / 'awake-ready.json').write_text(json.dumps(dict(pid=os.getpid(), utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), purpose='tiny CUDA work to keep driver-mediated D0 during read; no power/clock settings changed')))
deadline = time.monotonic() + 90
while not (args.directory / 'awake-stop.txt').exists() and time.monotonic() < deadline:
    x.add_(1)
    torch.cuda.synchronize()
    time.sleep(.025)
del x
torch.cuda.empty_cache()
