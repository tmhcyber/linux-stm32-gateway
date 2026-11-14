"""Repeatable stage 2 validation using a preserved firmware source reference."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--reference',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    source=Path(__file__).resolve().parents[2]
    manifest=json.loads((source/'SOURCE.json').read_text())
    for name,sha in manifest['files'].items():
        assert hashlib.sha256((source/name).read_bytes()).hexdigest()==sha,name
    ref=args.reference.resolve()
    provenance=json.loads((ref/'SOURCE.json').read_text())
    for name,sha in provenance['files'].items():
        assert hashlib.sha256((ref/name).read_bytes()).hexdigest()==sha,name
    out=args.output.resolve(); out.mkdir(exist_ok=False)
    (out/'SOURCE.json').write_bytes((source/'SOURCE.json').read_bytes())
    (out/'firmware-source.json').write_bytes((ref/'SOURCE.json').read_bytes())
    build=Path.home()/'project/build'/manifest['snapshot']
    commands=[]

    def run(command,name,expected=0):
        result=subprocess.run([str(x) for x in command],cwd=source,capture_output=True)
        (out/name).write_bytes(result.stdout+result.stderr)
        commands.append(dict(command=[str(x) for x in command],exit_code=result.returncode,log=name))
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if result.returncode!=expected:
            raise RuntimeError(f'{name}: {result.returncode} != {expected}')
        return result.stdout

    run(['make',f'BUILD={build}','test'],'tests.txt')
    run(['make',f'BUILD={build}','sanitize'],'sanitizers.txt')
    flags=['gcc','-std=c11','-g','-O0','-Wall','-Wextra','-Wpedantic','-Werror']
    run([*flags,'-I'+str(ref/'Application/Inc'),'tests/firmware_vectors.c',
         *[ref/'Application/Src'/name for name in ('app_data_service.c','app_snapshot_consumer.c','app_snapshot_text_formatter.c')],
         '-o',build/'firmware_vectors'],'firmware-build.txt')
    data=run([build/'firmware_vectors'],'firmware-vectors.bin')
    run([build/'gateway','--replay',out/'firmware-vectors.bin','--boundary','aligned','--output',out/'firmware-replay'],'firmware-replay.txt')
    summary=json.loads((out/'firmware-replay/summary.json').read_text())
    assert summary['valid_records']==20
    spec=importlib.util.spec_from_file_location('original_receiver',ref/'Tools/pc_uplink_receiver.py')
    original=importlib.util.module_from_spec(spec); sys.modules[spec.name]=original; spec.loader.exec_module(original)
    rejected=[]; accepted=0
    for line in data.split(b'\r\n')[:-1]:
        try:
            original.parse_uplink_line(line); accepted+=1
        except original.ProtocolError as exc:
            rejected.append(dict(body=line.decode(),code=exc.code.name,message=str(exc)))
    assert len(rejected)==7
    (out/'original-python-comparison.json').write_text(json.dumps(dict(accepted=accepted,rejected=rejected),indent=2)+'\n')
    error_data=b'v=1;v=1;\r\n'+data.split(b'\r\n')[0]+b'\r\n'
    (out/'gdb-input.bin').write_bytes(error_data)
    run([*flags,'-Iinclude','src/protocol.c','src/framer.c','src/serial.c','src/output.c','src/main.c','-o',build/'gateway-debug'],'debug-build.txt')
    run(['gdb','-q','-batch','-x','tests/protocol_error.gdb','--args',build/'gateway-debug',
         '--replay',out/'gdb-input.bin','--boundary','aligned','--output',out/'gdb-replay'],'gdb.txt')
    assert 'P_DUPLICATE' in (out/'gdb.txt').read_text()
    run(['gcc','--version'],'gcc.txt')
    hashes={str(p.relative_to(out)):hashlib.sha256(p.read_bytes()).hexdigest() for p in out.rglob('*') if p.is_file()}
    (out/'HASHES.json').write_text(json.dumps(hashes,indent=2)+'\n')
    print(f'PASS: {out}')


if __name__=='__main__':
    main()
