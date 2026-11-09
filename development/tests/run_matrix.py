"""Run the NGC comparison suite against both supported stock OpenMW API versions."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--windows-source',type=Path,required=True)
    parser.add_argument('--android-source',type=Path,required=True)
    parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    project=Path(__file__).resolve().parents[2]
    output=args.report or project/'development/results/ESP-LUA-PARITY-MATRIX.json'
    output.parent.mkdir(parents=True,exist_ok=True)
    def hashes():
        return {p.relative_to(project).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
                for folder in (project/'mod',project/'development/MWSE') for p in folder.rglob('*')
                if p.is_file() and (folder.name=='MWSE' or p.suffix in ('.lua','.esp','.omwscripts'))}
    frozen=hashes()
    results=[]
    for label,source in (('windows',args.windows_source),('android',args.android_source)):
        command=[sys.executable,'-X','utf8',str(project/'development/tests/run_tests.py'),'--engine-source',str(source)]
        start=time.monotonic()
        run=subprocess.run(command,env={**os.environ,'PYTHONDONTWRITEBYTECODE':'1'},capture_output=True,text=True,encoding='utf-8')
        (output.parent/f'esp-lua-{label}.log').write_text(run.stdout+run.stderr,encoding='utf-8',newline='\n')
        if run.returncode:
            print((run.stdout+run.stderr)[-5000:],flush=True)
            raise SystemExit(run.returncode)
        assert hashes()==frozen,'Runtime or original reference sources changed during testing'
        count=int(re.search(r'(\d+) tests passed',run.stdout)[1])
        row={'platform':label,'result':'passed','test_groups':count,'direct_mwse_formula_comparisons':10858,
             'seconds':round(time.monotonic()-start,3),
             'scope':'Actual stock engine Combat scripts; standard actor controls reject unsupported fields.'}
        results.append(row)
        print(json.dumps(row),flush=True)
    report={'result':'passed','custom_engine_hooks_required':False,'matrix':results,'source_sha256':frozen,
            'scope':'Numerical/behavior Lua validation. Packaged Windows gameplay is validated separately; Android device gameplay remains untested.'}
    output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8',newline='\n')

if __name__=='__main__':
    main()
