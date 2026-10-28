"""A Windows crash is a nonzero signed exit code, not necessarily ERRORLEVEL >= 1."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


@unittest.skipUnless(os.name=='nt','Windows command error-code semantics')
class RunnerTests(unittest.TestCase):
    def test_nonzero_including_access_violation_cannot_reach_success(self):
        with tempfile.TemporaryDirectory() as folder:
            file=Path(folder)/'exit-test.cmd'
            for code in (0,1,-1,-1073741819):
                file.write_text('@echo off\ncmd /d /c exit /b '+str(code)+'\n'
                                'if not "%errorlevel%"=="0" exit /b 1\n'
                                'echo REACHED_SUCCESS\nexit /b 0\n',encoding='ascii')
                run=subprocess.run([os.environ['ComSpec'],'/d','/c',str(file)],capture_output=True,text=True)
                self.assertEqual(run.returncode==0,code==0)
                self.assertEqual('REACHED_SUCCESS' in run.stdout,code==0)

    def test_diagnostic_runners_use_nonzero_checks(self):
        for name in ('Build.ps1','Test-Host.ps1'):
            text=(ROOT/name).read_text()
            self.assertNotIn('if errorlevel 1 exit /b 1',text)
            self.assertIn('if not "%errorlevel%"=="0" exit /b 1',text)
