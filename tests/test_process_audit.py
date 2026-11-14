import io
import unittest
from process_audit import classify_process, ChildRuntimeError, RecordingResult


class ProcessAuditTests(unittest.TestCase):
    def test_mapping_requires_matching_exit_and_exact_runtime_diagnostic(self):
        text='FATAL: ThreadSanitizer: unexpected memory mapping 0x123'
        self.assertEqual(classify_process(66,text,0),'runtime_mapping_failure')
        self.assertEqual(classify_process(1,text,0),'other_runtime_failure')

    def test_tsan_report_is_never_hidden_by_mapping_text_or_expected_exit(self):
        text='FATAL: ThreadSanitizer: unexpected memory mapping\nWARNING: ThreadSanitizer: data race'
        self.assertEqual(classify_process(66,text,66),'tsan_report')

    def test_unknown_exit_is_not_inferred_to_be_mapping(self):
        self.assertEqual(classify_process(66,'',0),'other_runtime_failure')

    def test_expected_usage_signal_and_crash_codes_are_context_specific(self):
        for code in (2,130,143,-9):
            self.assertEqual(classify_process(code,'',code),'expected_exit')
        self.assertEqual(classify_process(2,'',0),'other_runtime_failure')

    def test_mapping_and_independent_assertion_remain_separate_failures(self):
        class MappingCase(unittest.TestCase):
            def runTest(self):
                raise ChildRuntimeError(dict(kind='runtime_mapping_failure'))
        class AssertionCase(unittest.TestCase):
            def runTest(self):
                self.assertEqual(3,2)
        result=unittest.TextTestRunner(stream=io.StringIO(),verbosity=2,resultclass=RecordingResult).run(
            unittest.TestSuite([MappingCase(),AssertionCase()]))
        report=result.report()
        self.assertFalse(report['passed'])
        self.assertEqual([r['kind'] for r in report['outcomes']],['runtime_mapping_failure','test_assertion_failure'])
        self.assertEqual(report['failures'],1);self.assertEqual(report['errors'],1)

    def test_skips_do_not_count_as_full_pass(self):
        class SkipCase(unittest.TestCase):
            def runTest(self):
                self.skipTest('runtime unavailable')
        result=unittest.TextTestRunner(stream=io.StringIO(),verbosity=2,resultclass=RecordingResult).run(
            unittest.TestSuite([SkipCase()]))
        self.assertFalse(result.report()['passed'])


if __name__=='__main__':
    unittest.main()
