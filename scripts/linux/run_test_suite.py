"""Run unittest with machine-readable per-case outcomes and optional per-child audit."""
import argparse
import json
import os
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tests'))
from process_audit import RecordingResult


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--pattern',default='test*.py')
    args=parser.parse_args()
    audit=os.environ.get('TEST_PROCESS_AUDIT')
    if audit:
        Path(audit).touch(exist_ok=False)
    suite=unittest.defaultTestLoader.discover(str(ROOT/'tests'),pattern=args.pattern)
    result=unittest.TextTestRunner(stream=sys.stdout,verbosity=2,resultclass=RecordingResult).run(suite)
    report=result.report()
    if os.environ.get('TEST_SUITE_REPORT'):
        with Path(os.environ['TEST_SUITE_REPORT']).open('x') as stream:
            stream.write(json.dumps(report,indent=2)+'\n')
    return 0 if report['passed'] and result.testsRun else 1


if __name__=='__main__':
    sys.exit(main())
