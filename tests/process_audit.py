"""Per-child diagnostics and unittest outcomes; never infer a whole suite from one stderr."""
import json
import os
from pathlib import Path
import unittest


def classify_process(code, stderr, expected):
    if isinstance(stderr,bytes):
        stderr=stderr.decode(errors='replace')
    if 'WARNING: ThreadSanitizer:' in stderr or 'SUMMARY: ThreadSanitizer:' in stderr:
        return 'tsan_report'
    if code==66 and 'FATAL: ThreadSanitizer: unexpected memory mapping' in stderr:
        return 'runtime_mapping_failure'
    accepted=expected if isinstance(expected,(list,tuple)) else [expected]
    return 'expected_exit' if code in accepted else 'other_runtime_failure'


class ChildRuntimeError(Exception):
    def __init__(self, record):
        self.kind=record['kind'];self.record=record
        super().__init__(json.dumps(record))


def audit_process(command, code, stderr, expected, case, phase, pid=None):
    if isinstance(stderr,bytes):
        stderr=stderr.decode(errors='replace')
    record=dict(case=case,phase=phase,pid=pid,command=list(map(str,command)),
                exit_code=code,expected_exit=expected,stderr=stderr,
                kind=classify_process(code,stderr,expected))
    path=os.environ.get('TEST_PROCESS_AUDIT')
    if path:
        with Path(path).open('a') as stream:
            stream.write(json.dumps(record)+'\n')
    return record


def stderr_once(proc):
    if not hasattr(proc,'_audit_stderr'):
        proc._audit_stderr=proc.stderr.read()
    return proc._audit_stderr


def audit_child(proc, case, phase='exit', startup=False):
    if getattr(proc,'_audit_recorded',False):
        return
    expected='running' if startup else getattr(proc,'_expected_exit',143)
    record=audit_process(proc.args,proc.returncode,stderr_once(proc),expected,case,phase,proc.pid)
    proc._audit_recorded=True
    if record['kind']!='expected_exit':
        raise ChildRuntimeError(record)


def audit_file_child(proc, stderr_path, case, startup=False):
    if getattr(proc,'_audit_recorded',False):
        return
    stderr=Path(stderr_path).read_text(errors='replace')
    expected='running' if startup else getattr(proc,'_expected_exit',143)
    record=audit_process(proc.args,proc.returncode,stderr,expected,case,
                         'startup' if startup else 'exit',proc.pid)
    proc._audit_recorded=True
    if record['kind']!='expected_exit':
        raise ChildRuntimeError(record)


class RecordingResult(unittest.TextTestResult):
    def __init__(self,*args,**kwargs):
        super().__init__(*args,**kwargs);self.outcomes=[]

    def record(self,test,kind,error=None,subtest=False):
        item=dict(test=str(test),kind=kind,subtest=subtest)
        if error:
            item['error_type']=error[0].__name__;item['message']=str(error[1])
        self.outcomes.append(item)

    def addSuccess(self,test):
        super().addSuccess(test);self.record(test,'passed')

    def addFailure(self,test,error):
        super().addFailure(test,error);self.record(test,'test_assertion_failure',error)

    def addError(self,test,error):
        super().addError(test,error)
        self.record(test,getattr(error[1],'kind','test_error'),error)

    def addSkip(self,test,reason):
        super().addSkip(test,reason);self.record(test,'skipped')

    def addSubTest(self,test,subtest,error):
        super().addSubTest(test,subtest,error)
        if error:
            kind=getattr(error[1],'kind','test_assertion_failure' if issubclass(error[0],test.failureException) else 'test_error')
            self.record(subtest,kind,error,subtest=True)

    def report(self):
        return dict(tests_run=self.testsRun,failures=len(self.failures),errors=len(self.errors),
                    skipped=len(self.skipped),passed=self.wasSuccessful() and not self.skipped,
                    outcomes=self.outcomes)
