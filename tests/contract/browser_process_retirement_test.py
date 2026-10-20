"""Browser teardown ordering/budget tests; no GPU execution evidence."""
import pathlib
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest import mock

sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[2]/'scripts'))
import browser_contract_runner as runner

class BrowserProcessRetirementTest(unittest.TestCase):
    def test_firefox_transport_preserves_profile_url_and_does_not_override_capabilities(self):
        session = runner.BrowserContractRunner('C:/tools/firefox.exe', '.', 'pipeline.html', '.', 1)
        self.assertEqual(session.browser_command('profile', 'http://localhost/pipeline.html'),
            ['C:/tools/firefox.exe', '--headless', '--no-remote', '--profile', 'profile',
             'http://localhost/pipeline.html'])
        session.disable_background_timer_throttling = True
        with self.assertRaisesRegex(ValueError, 'FIREFOX-TIMER-POLICY-UNSUPPORTED'):
            session.browser_command('profile', 'http://localhost/pipeline.html')

    def test_report_is_required_and_completed_report_does_not_need_live_browser(self):
        ready = threading.Event()
        ready.set()
        with tempfile.TemporaryDirectory() as directory:
            browser = mock.Mock()
            self.assertTrue(runner.wait_for_browser_result(ready, browser, directory, 1))
            browser.poll.assert_not_called()

    def test_child_crash_fails_before_wait_even_when_parent_lives(self):
        with tempfile.TemporaryDirectory() as directory:
            reports = pathlib.Path(directory) / 'Crashpad' / 'reports'
            reports.mkdir(parents=True)
            (reports / 'fixture-crash.dmp').write_bytes(b'fixture')
            ready = mock.Mock()
            ready.is_set.return_value = False
            with self.assertRaisesRegex(RuntimeError, 'BROWSER-CONTRACT-CHILD-CRASH: fixture-crash.dmp'):
                runner.wait_for_browser_result(ready, mock.Mock(), directory, 900)
            ready.wait.assert_not_called()

    def test_parent_exit_is_not_a_successful_result(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, 'BROWSER-CONTRACT-PROCESS-EXIT: 9'):
                runner.wait_for_browser_result(threading.Event(), mock.Mock(returncode=9, poll=lambda: 9), directory, 900)

    def test_result_wait_uses_original_deadline(self):
        ready = mock.Mock()
        ready.is_set.return_value = False
        ready.wait.return_value = False
        with tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(runner.time, 'monotonic', side_effect=[0., 0.75, 1.]):
            self.assertFalse(runner.wait_for_browser_result(ready, mock.Mock(poll=lambda: None), directory, 1))
        ready.wait.assert_called_once_with(0.25)

    def sharing_violation(self):
        error=PermissionError('fixture-only file sharing violation')
        error.winerror=32
        return error

    def test_wait_precedes_profile_retry_and_uses_one_shared_deadline(self):
        events=[]
        browser=mock.Mock(pid=123)
        profile=mock.Mock()
        browser.wait.side_effect=lambda **unused:events.append('wait')
        failures=[self.sharing_violation(),self.sharing_violation(),None]
        def cleanup():
            events.append('cleanup')
            error=failures.pop(0)
            if error:raise error
        profile.cleanup.side_effect=cleanup
        with mock.patch.object(runner.sys,'platform','win32'), \
                mock.patch.object(runner.subprocess,'run') as kill, \
                mock.patch.object(runner.time,'monotonic',side_effect=[0.,1.,2.,3.,4.]), \
                mock.patch.object(runner.time,'sleep') as sleep:
            runner.retire_browser_process(browser,profile,10)
        self.assertEqual(events,['wait','cleanup','cleanup','cleanup'])
        kill.assert_called_once_with(['taskkill','/PID','123','/T','/F'],capture_output=True,timeout=9.)
        browser.wait.assert_called_once_with(timeout=8.)
        self.assertEqual(sleep.call_count,2)

    def test_permanent_sharing_violation_fails_at_original_teardown_deadline(self):
        browser,profile=mock.Mock(pid=123),mock.Mock()
        profile.cleanup.side_effect=self.sharing_violation()
        with mock.patch.object(runner.sys,'platform','win32'), \
                mock.patch.object(runner.subprocess,'run'), \
                mock.patch.object(runner.time,'monotonic',side_effect=[0.,1.,2.,10.]), \
                mock.patch.object(runner.time,'sleep') as sleep, \
                self.assertRaisesRegex(TimeoutError,'BROWSER-CONTRACT-RETIRE-DEADLINE'):
            runner.retire_browser_process(browser,profile,10)
        self.assertEqual(profile.cleanup.call_count,1)
        sleep.assert_not_called()

    def test_other_filesystem_errors_are_not_suppressed_or_retried(self):
        for error in (PermissionError('access denied'),OSError('disk failure')):
            browser,profile=mock.Mock(pid=123),mock.Mock()
            profile.cleanup.side_effect=error
            with mock.patch.object(runner.sys,'platform','win32'), \
                    mock.patch.object(runner.subprocess,'run'), \
                    mock.patch.object(runner.time,'sleep') as sleep, self.assertRaises(type(error)):
                runner.retire_browser_process(browser,profile,10)
            self.assertEqual(profile.cleanup.call_count,1)
            sleep.assert_not_called()

    def test_unretired_process_cannot_trigger_profile_cleanup(self):
        browser,profile=mock.Mock(pid=123),mock.Mock()
        browser.wait.side_effect=subprocess.TimeoutExpired('fixture-browser',10)
        with mock.patch.object(runner.sys,'platform','win32'), \
                mock.patch.object(runner.subprocess,'run'), self.assertRaises(subprocess.TimeoutExpired):
            runner.retire_browser_process(browser,profile,10)
        profile.cleanup.assert_not_called()

    def test_tree_command_cannot_leave_a_live_owned_parent_waiting(self):
        # taskkill can finish a child tree traversal while the owned parent is
        # still live. Its original Popen handle must be terminated before wait;
        # no global browser-name kill and no extended deadline is permitted.
        for tree_exit in (0, 1):
            browser, profile = mock.Mock(pid=123), mock.Mock()
            browser.poll.return_value = None
            events = []
            browser.kill.side_effect = lambda: events.append('kill-owned-parent')
            browser.wait.side_effect = lambda **unused: events.append('wait')
            profile.cleanup.side_effect = lambda: events.append('cleanup')
            with mock.patch.object(runner.sys, 'platform', 'win32'), \
                    mock.patch.object(runner.subprocess, 'run', return_value=mock.Mock(returncode=tree_exit)), \
                    mock.patch.object(runner.time, 'monotonic', return_value=0.):
                runner.retire_browser_process(browser, profile, 10)
            self.assertEqual(events, ['kill-owned-parent', 'wait', 'cleanup'])
            browser.kill.assert_called_once_with()
            browser.wait.assert_called_once_with(timeout=10.)

    def test_nonwindows_retirement_uses_native_process_wait_without_tree_command(self):
        browser,profile=mock.Mock(pid=123),mock.Mock()
        with mock.patch.object(runner.sys,'platform','linux'),mock.patch.object(runner.subprocess,'run') as kill:
            runner.retire_browser_process(browser,profile,10)
        browser.terminate.assert_called_once()
        browser.wait.assert_called_once()
        profile.cleanup.assert_called_once()
        kill.assert_not_called()

    def test_profile_only_cleanup_retries_with_its_own_bounded_deadline(self):
        profile = mock.Mock()
        profile.cleanup.side_effect = [self.sharing_violation(), None]
        with mock.patch.object(runner.sys, 'platform', 'win32'), \
                mock.patch.object(runner.time, 'monotonic', side_effect=[2., 3.]), \
                mock.patch.object(runner.time, 'sleep') as sleep:
            runner.cleanup_browser_profile(profile, 2)
        self.assertEqual(profile.cleanup.call_count, 2)
        sleep.assert_called_once_with(0.05)

    def test_profile_only_cleanup_does_not_reset_an_expired_shared_deadline(self):
        profile = mock.Mock()
        profile.cleanup.side_effect = self.sharing_violation()
        with mock.patch.object(runner.sys, 'platform', 'win32'), \
                mock.patch.object(runner.time, 'monotonic', return_value=7.), \
                self.assertRaisesRegex(TimeoutError, 'RETIRE-DEADLINE'):
            runner.cleanup_browser_profile(profile, deadline=7.)
        self.assertEqual(profile.cleanup.call_count, 1)

if __name__=='__main__':unittest.main()
