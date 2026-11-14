"""TSan acceptance with ASLR disabled only in private test-process families."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

SOURCE=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(SOURCE/'tests'))
from process_audit import classify_process


def run_validation(build, output, diagnostics=False):
    output.mkdir()
    built=json.loads((build/'SOURCE.json').read_text())
    evaluator=json.loads((SOURCE/'SOURCE.json').read_text())
    for name,digest in built['files'].items():
        if name.startswith(('src/','include/','tests/')) or name=='Makefile':
            if hashlib.sha256((SOURCE/name).read_bytes()).hexdigest()!=digest:
                raise ValueError('build provenance does not match current test/compiler input: '+name)
    (output/'SOURCE.json').write_bytes((build/'SOURCE.json').read_bytes())
    (output/'EVALUATOR_SOURCE.json').write_bytes((SOURCE/'SOURCE.json').read_bytes())
    commands=[];processes=[]
    environment=dict(os.environ,TSAN_OPTIONS='halt_on_error=1:exitcode=66',GATEWAY_TSAN_MODE='1')

    def run(command,name,expected=0,env=None):
        p=subprocess.run(list(map(str,command)),cwd=SOURCE,env=env or environment,capture_output=True,text=True)
        (output/(name+'.stdout.txt')).write_text(p.stdout)
        (output/(name+'.stderr.txt')).write_text(p.stderr)
        item=dict(name=name,command=list(map(str,command)),exit_code=p.returncode,expected_exit=expected,
                  kind=classify_process(p.returncode,p.stderr,expected),stderr=p.stderr)
        commands.append(item)
        (output/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        return item

    # These are read-only probes of global policy. Never write sysctl or change production launch.
    before=subprocess.check_output(['sudo','-n','cat','/proc/sys/vm/mmap_rnd_bits'],text=True).strip()
    randomization=Path('/proc/sys/kernel/randomize_va_space').read_text().strip()
    run(['make',f'BUILD={build}','tsan-build'],'build')
    prefix=['setarch','x86_64','-R']
    for binary in ('gateway','gateway-fault'):
        for index in range(20):
            processes.append(run(prefix+[build/'tsan'/binary,'--help'],f'{binary}-startup-{index:02}',expected=2))
    queue=run(prefix+[build/'tsan/test_queue_threads'],'queue-stress')
    environment.update(GATEWAY=str(build/'tsan/gateway'),GATEWAY_FAULT=str(build/'tsan/gateway-fault'),
                       STAGE3_EVIDENCE=str(output/'faults'),TEST_PROCESS_AUDIT=str(output/'processes.jsonl'),
                       TEST_SUITE_REPORT=str(output/'suite.json'))
    suite_command=run(prefix+[sys.executable,SOURCE/'scripts/linux/run_test_suite.py','--pattern','test_storage*.py'],
                      'storage-suite',env=environment)
    suite=json.loads((output/'suite.json').read_text()) if (output/'suite.json').exists() else None
    children=[json.loads(line) for line in (output/'processes.jsonl').read_text().splitlines()] if (output/'processes.jsonl').exists() else []
    environment.update(MQTT_EVIDENCE=str(output/'mqtt'),TEST_PROCESS_AUDIT=str(output/'mqtt-processes.jsonl'),
                       TEST_SUITE_REPORT=str(output/'mqtt-suite.json'))
    mqtt_command=run(prefix+[sys.executable,SOURCE/'scripts/linux/run_test_suite.py','--pattern','test_mqtt.py'],
                     'mqtt-suite',env=environment)
    mqtt_suite=json.loads((output/'mqtt-suite.json').read_text()) if (output/'mqtt-suite.json').exists() else None
    mqtt_children=[json.loads(line) for line in (output/'mqtt-processes.jsonl').read_text().splitlines()] if (output/'mqtt-processes.jsonl').exists() else []
    environment.update(TEST_PROCESS_AUDIT=str(output/'ipc-processes.jsonl'),TEST_SUITE_REPORT=str(output/'ipc-suite.json'))
    ipc_command=run(prefix+[sys.executable,SOURCE/'scripts/linux/run_test_suite.py','--pattern','test_ipc.py'],'ipc-suite',env=environment)
    ipc_suite=json.loads((output/'ipc-suite.json').read_text()) if (output/'ipc-suite.json').exists() else None
    ipc_children=[json.loads(line) for line in (output/'ipc-processes.jsonl').read_text().splitlines()] if (output/'ipc-processes.jsonl').exists() else []
    diagnostic_suite=None;diagnostic_children=[];diagnostic_ok=True
    if diagnostics:
        environment.update(STAGE6_EVIDENCE=str(output/'diagnostic-cases'),
                           TEST_PROCESS_AUDIT=str(output/'diagnostic-processes.jsonl'),
                           TEST_SUITE_REPORT=str(output/'diagnostic-suite.json'))
        diagnostic_command=run(prefix+[sys.executable,SOURCE/'scripts/linux/run_test_suite.py','--pattern','test_diagnostic.py'],
                               'diagnostic-suite',env=environment)
        diagnostic_suite=json.loads((output/'diagnostic-suite.json').read_text()) if (output/'diagnostic-suite.json').exists() else None
        diagnostic_children=[json.loads(line) for line in (output/'diagnostic-processes.jsonl').read_text().splitlines()] if (output/'diagnostic-processes.jsonl').exists() else []
        diagnostic_ok=bool(diagnostic_command['kind']=='expected_exit' and diagnostic_suite and diagnostic_suite['passed'] and
                           all(p['kind']=='expected_exit' for p in diagnostic_children))
    after=subprocess.check_output(['sudo','-n','cat','/proc/sys/vm/mmap_rnd_bits'],text=True).strip()
    unchanged=before==after and randomization==Path('/proc/sys/kernel/randomize_va_space').read_text().strip()
    passed=bool(unchanged and diagnostic_ok and all(p['kind']=='expected_exit' for p in processes) and
                queue['kind']=='expected_exit' and suite_command['kind']=='expected_exit' and
                suite and suite['passed'] and suite['tests_run']>=20 and
                all(p['kind']=='expected_exit' for p in children) and mqtt_command['kind']=='expected_exit' and
                mqtt_suite and mqtt_suite['passed'] and mqtt_suite['tests_run']>=9 and
                all(p['kind']=='expected_exit' for p in mqtt_children) and ipc_command['kind']=='expected_exit' and
                ipc_suite and ipc_suite['passed'] and ipc_suite['tests_run']>=10 and all(p['kind']=='expected_exit' for p in ipc_children))
    report=dict(passed=passed,source_snapshot=built['snapshot'],evaluator_snapshot=evaluator['snapshot'],launch_prefix=prefix,environment={'TSAN_OPTIONS':environment['TSAN_OPTIONS'],
        'GATEWAY_TSAN_MODE':'1'},startup_checks=processes,queue=queue,suite=suite,processes=children,
        mqtt_suite=mqtt_suite,mqtt_processes=mqtt_children,
        ipc_suite=ipc_suite,ipc_processes=ipc_children,
        diagnostic_suite=diagnostic_suite,diagnostic_processes=diagnostic_children,
        global_policy=dict(mmap_rnd_bits_before=before,mmap_rnd_bits_after=after,randomize_va_space=randomization,unchanged=unchanged),
        binaries={name:hashlib.sha256((build/'tsan'/name).read_bytes()).hexdigest() for name in ('gateway','gateway-fault','test_queue_threads')})
    (output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True);parser.add_argument('--diagnostics',action='store_true');args=parser.parse_args()
    result=run_validation(args.build,args.output,args.diagnostics)
    print(json.dumps(dict(passed=result['passed'],tests=result['suite']['tests_run'] if result['suite'] else 0,
                         startup_checks=len(result['startup_checks']),global_policy=result['global_policy']),indent=2))
    sys.exit(0 if result['passed'] else 1)
