"""Optional integration with private operator captures; no blobs in the repository."""
import json
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from analyze import analyze
from compare import compare


class PrivateOwnershipTests(unittest.TestCase):
    def test_captures_three_and_four(self):
        captures=[]
        for n in (3,4):
            folder=ROOT.parent/'captures'/f'2026-10-27-run{n}'
            path=folder/f'CAP000{n}.JSON'
            if not path.exists():
                self.skipTest('Private capture pair is not available')
            data=json.loads(path.read_text(encoding='utf-8-sig'))
            manifest=json.loads((folder/'MANIFEST.json').read_text(encoding='utf-8-sig'))
            analyze(data,manifest['build_id']);captures.append(data)
            # The prepared collection predicate reaches exactly the missing
            # direct owners; it must not depend on their snapshot-local IDs.
            p=data['platform']
            roots={p[k] for k in ('target','conout','conin','stderr')}
            gpu_agents={a['agent'] for a in p['gpu_agents']}
            selected={e['agent'] for h in p['handles'] if not h['gpu_descendant']
                      for e in h['active_opens'] if e['controller'] is not None
                      and e['controller'] in roots and e['agent'] is not None}-gpu_agents
            self.assertEqual(len(selected),3)
            from graph import ownership
            unknown={m['handle'] for edge in ownership(data)['scopes']['boundary']['unresolved']
                     for m in edge['missing'] if m['endpoint']=='agent'}
            self.assertEqual(selected,unknown)
        r=compare(*captures)
        self.assertTrue(r['gpu_ownership_matches'])
        self.assertTrue(r['physical_gop_matches'])
        self.assertTrue(r['console_variables_match'])
        self.assertTrue(r['console_role_aliases_match'])
        self.assertTrue(r['driver_code_matches'])
        self.assertIsNone(r['graph_matches'])
        self.assertIsNone(r['boundary_ownership_matches'])
        self.assertFalse(r['unlock_ready'])
        for side in ('left','right'):
            diag=r['ownership_diagnostics'][side]
            self.assertEqual(diag['scope_counts']['gpu']['edges'],5)
            self.assertEqual(diag['scope_counts']['gpu']['unresolved_edges'],0)
            self.assertEqual(diag['scope_counts']['boundary']['unresolved_edges'],3)
            self.assertEqual(len(diag['driver_rejections']),0)
