"""Reuse a completed core run only when all native core inputs are identical."""
from pathlib import Path
import argparse
import hashlib
import json

HERE = Path(__file__).resolve().parent
BUILD = HERE / 'build/hybrid-unlock'
parser = argparse.ArgumentParser()
parser.add_argument('--prior', required=True, type=Path)
args = parser.parse_args()
prior_raw = args.prior.read_bytes()
prior = json.loads(prior_raw.decode('utf-8-sig'))
current = json.loads((BUILD / 'source-provenance.json').read_text())
scenarios = prior['native_core_scenarios']
assert prior['complete'] and scenarios in (150, 180, 240)
log_sha = hashlib.sha256((BUILD / 'core-tests.log').read_bytes()).hexdigest()
assert log_sha == prior['core_validation_sha256']

def inputs(provenance):
    roots = {'port.c', 'port.h', 'compat.c', 'checks.c', 'checks.h', 'profile.c', 'profile.h', 'sha256.c', 'sha256.h', 'tests/test_efi_transaction.c', 'tests/captured_config.h'}
    return {name.replace('\\', '/'): sha for name, sha in provenance['source_hashes'].items()
            if name.replace('\\', '/') in roots or name.replace('\\', '/').startswith(('vendor/', 'compat/'))}

before = inputs(prior['source_provenance'])
after = inputs(current)
assert before == after and len(after) > 20, 'Core, fixture or dependency source changed: run full validation'
for name, sha in after.items():
    assert hashlib.sha256((BUILD / 'source' / name).read_bytes()).hexdigest() == sha, name
report = dict(reused=True, scenarios=scenarios, unchanged_input_files=len(after),
              prior_manifest_sha256=hashlib.sha256(prior_raw).hexdigest(),
              core_log_sha256=log_sha, prior_validation_utc=prior['utc'])
(BUILD / 'core-reuse.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(f'Core validation reused: {scenarios} passed cases, {len(after)} identical core/fixture/dependency files; new ownership/app tests still execute.')
