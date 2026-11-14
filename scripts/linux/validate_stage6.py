"""Offline stage 6 acceptance. Never opens physical UART or invokes service deployment."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tests'))
from diagnostic_vectors import VECTORS
from analyze_window import analyze
from check_stage6_evidence import check


def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True)
    p.add_argument('--mcu-reference',type=Path,required=True);a=p.parse_args();a.output.mkdir()
    manifest=json.loads((ROOT/'SOURCE.json').read_text())
    for name,digest in manifest['files'].items():assert hashlib.sha256((ROOT/name).read_bytes()).hexdigest()==digest,name
    (a.output/'SOURCE.json').write_bytes((ROOT/'SOURCE.json').read_bytes())
    build=Path.home()/'project/build'/manifest['snapshot'];commands=[]
    def run(command,name,env=None):
        with (a.output/(name+'.txt')).open('wb') as log:
            proc=subprocess.run(list(map(str,command)),cwd=ROOT,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=900)
        commands.append(dict(name=name,command=list(map(str,command)),exit_code=proc.returncode))
        (a.output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        print(name,proc.returncode,flush=True);return proc.returncode==0
    mcu=json.loads((a.mcu_reference/'host-result.json').read_text())
    for name,digest in mcu['sources'].items():assert hashlib.sha256((a.mcu_reference/name).read_bytes()).hexdigest()==digest,name
    (a.output/'MCU_SOURCE.json').write_text(json.dumps(mcu,indent=2)+'\n')
    mcu_ok=True
    for sanitize in (False,True):
        for enabled in (0,1):
            name=f'mcu-{int(sanitize)}-{enabled}';exe=build/name
            command=['gcc','-std=c11','-Wall','-Wextra','-Werror','-pedantic','-O1','-g',f'-DAPP_DIAGNOSTICS_ENABLED={enabled}']
            if sanitize:command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie','-no-pie']
            for inc in ('Application/Inc','BSP/Inc','Components/SHT30/Inc'):command+=['-I',a.mcu_reference/inc]
            command += [a.mcu_reference/name for name in mcu['sources']]+['-o',exe]
            good=run(command,name+'-build') and run([exe],name+'-test')
            if good:
                vector=subprocess.check_output([exe,'--vectors'])
                (a.output/(name+'-vectors.bin')).write_bytes(vector)
                good=vector==b''.join(VECTORS)
            mcu_ok &= good
    suites={}
    for name,target in (('normal','test'),('asan','sanitize')):
        env=dict(os.environ,STAGE3_EVIDENCE=str(a.output/(name+'-storage')),
                 MQTT_EVIDENCE=str(a.output/(name+'-mqtt')),STAGE6_EVIDENCE=str(a.output/(name+'-diagnostics')),
                 TEST_SUITE_REPORT=str(a.output/(name+'-suite.json')),TEST_PROCESS_AUDIT=str(a.output/(name+'-processes.jsonl')))
        good=run(['make',f'BUILD={build}',target],name,env)
        suites[name]=json.loads((a.output/(name+'-suite.json')).read_text()) if (a.output/(name+'-suite.json')).exists() else {}
        if not good:break
    tsan_ok=run([sys.executable,ROOT/'scripts/linux/validate_tsan.py','--build',build,'--output',a.output/'tsan','--diagnostics'],'tsan')
    analysis=[];reconciled=[]
    for summary in a.output.glob('*-diagnostics/**/summary.json'):
        result=analyze(summary.parent);analysis.append(result)
        if result['summary'].get('storage',{}).get('run_id'):
            reconciled.append(check(summary.parent,summary.parent.parent/'history.sqlite'))
    (a.output/'diagnostic-reconciled.json').write_text(json.dumps(reconciled,indent=2)+'\n')
    (a.output/'diagnostic-analysis.json').write_text(json.dumps(analysis,indent=2)+'\n')
    passed=(mcu_ok and len(suites)==2 and all(s.get('passed') for s in suites.values()) and tsan_ok and
            all(not r['diagnostic_mismatches'] and r['summary_counts_match'] for r in analysis))
    report=dict(passed=passed,scope='offline only; no board acceptance',mcu_host=bool(mcu_ok),suites=suites,
                commands=commands,build=str(build),completed_utc=datetime.now(timezone.utc).isoformat(),
                binaries={str(f.relative_to(build)):hashlib.sha256(f.read_bytes()).hexdigest() for f in build.rglob('*') if f.is_file() and f.name in ('gateway','gatewayctl','gateway-fault')})
    (a.output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(passed=passed,suites={n:s.get('tests_run') for n,s in suites.items()},mcu_host=mcu_ok,tsan=tsan_ok)))
    return 0 if passed else 1


if __name__=='__main__':raise SystemExit(main())
