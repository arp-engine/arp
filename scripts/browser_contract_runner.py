"""Reusable headless browser contract session with bounded completion wait."""
import functools
import http.server
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import threading
import time


# Fetch standard, port blocking: https://fetch.spec.whatwg.org/#port-blocking
# An OS-assigned ephemeral port can still be on this list (notably Windows).
_FETCH_BAD_PORTS = frozenset((
    0, 1, 7, 9, 11, 13, 15, 17, 19, 20, 21, 22, 23, 25, 37, 42, 43,
    53, 69, 77, 79, 87, 95, 101, 102, 103, 104, 109, 110, 111, 113,
    115, 117, 119, 123, 135, 137, 139, 143, 161, 179, 389, 427, 465,
    512, 513, 514, 515, 526, 530, 531, 532, 540, 548, 554, 556, 563,
    587, 601, 636, 989, 990, 993, 995, 1719, 1720, 1723, 2049,
    3659, 4045, 4190, 5060, 5061, 6000, 6566, 6665, 6666, 6667,
    6668, 6669, 6697, 10080,
))


def create_browser_http_server(handler):
    """Bind a browser-usable loopback port without disabling browser policy.

    Hold rejected ports until selection ends so the OS cannot immediately
    return the same one. This bounds selection by the finite blocked set;
    workload execution and its deadline start only after the server is ready.
    """
    rejected = []
    try:
        for unused in range(len(_FETCH_BAD_PORTS) + 1):
            server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
            if server.server_port not in _FETCH_BAD_PORTS:
                return server
            rejected.append(server)
        raise RuntimeError("BROWSER-CONTRACT-HTTP-PORT-CAPACITY")
    finally:
        for server in rejected:
            server.server_close()


def wait_for_browser_result(ready, browser, profile, timeout_seconds, *, crash_output=None):
    """Wait for the actual result; a crashed child must never become a timeout.

    Only the dedicated temporary profile is inspected. A Crashpad report is
    failure evidence, never a substitute for the page's successful result.
    """
    if timeout_seconds <= 0:
        raise ValueError("BROWSER-CONTRACT-TIMEOUT-BUDGET")
    deadline = time.monotonic() + timeout_seconds
    reports = pathlib.Path(profile) / "Crashpad" / "reports"
    while True:
        if ready.is_set():
            return True
        crashes = sorted(reports.glob("*.dmp"))
        if crashes:
            # Preserve only this owned session's crash reports before profile
            # retirement removes them. Evidence never changes the failure.
            if crash_output is not None:
                destination = pathlib.Path(crash_output)
                destination.mkdir(parents=True, exist_ok=True)
                # Crashpad creates the report before writing its payload. Do
                # not kill its owned process tree while the dump is still
                # empty/growing; this diagnostic wait consumes the original
                # workload deadline and is itself bounded to ten seconds.
                crash_deadline = min(deadline, time.monotonic() + 10.)
                previous_sizes = None
                while True:
                    sizes = [crash.stat().st_size for crash in crashes]
                    if all(sizes) and sizes == previous_sizes:
                        break
                    remaining = crash_deadline - time.monotonic()
                    if remaining <= 0:
                        break
                    previous_sizes = sizes
                    time.sleep(min(.1, remaining))
                for crash in crashes:
                    shutil.copy2(crash, destination / crash.name)
            raise RuntimeError("BROWSER-CONTRACT-CHILD-CRASH: " + crashes[0].name)
        if browser.poll() is not None:
            raise RuntimeError("BROWSER-CONTRACT-PROCESS-EXIT: " + str(browser.returncode))
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return False
        if ready.wait(min(1.0, remaining)):
            return True


def cleanup_browser_profile(profile, timeout_seconds=10, *, deadline=None):
    """Release an owned profile within the unchanged Windows teardown budget."""
    if deadline is None:
        deadline = time.monotonic() + timeout_seconds
    while True:
        try:
            profile.cleanup()
            return
        except OSError as error:
            # Windows may report access denied while browser database handles
            # finish closing. Python 3.9 TemporaryDirectory can wrap that same
            # file's unlink failure in NotADirectoryError during its fallback.
            # A genuine directory/type error or another file is not retryable.
            denied = error
            if isinstance(error, NotADirectoryError) and getattr(error, 'winerror', None) == 267:
                denied = error.__context__
                if not error.filename or getattr(denied, 'filename', None) != error.filename:
                    raise
            if (sys.platform != 'win32' or not isinstance(denied, PermissionError)
                    or getattr(denied, 'winerror', None) not in (5, 32)):
                raise
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError('BROWSER-CONTRACT-RETIRE-DEADLINE') from error
            time.sleep(min(0.05, remaining))


def retire_browser_process(browser, profile, timeout_seconds=10):
    """Retire a browser tree and its profile within one existing teardown budget.

    Chrome may release its cache file handles after the parent exits. Retry only
    Windows sharing/access-denied errors after wait confirms parent retirement;
    preserve every other error and fail at the same teardown deadline. This does
    not retry a workload or change its GPU completion/report deadline.
    """
    deadline=time.monotonic()+timeout_seconds
    def remaining():
        available=deadline-time.monotonic()
        if available<=0:
            raise TimeoutError('BROWSER-CONTRACT-RETIRE-DEADLINE')
        return available
    if sys.platform=='win32':
        subprocess.run(['taskkill','/PID',str(browser.pid),'/T','/F'],
                       capture_output=True,timeout=remaining())
        # Tree traversal can return with the original parent still live. Use
        # only its owned handle; keep the same total retirement deadline.
        if browser.poll() is None:
            browser.kill()
    else:
        browser.terminate()
    browser.wait(timeout=remaining())
    cleanup_browser_profile(profile, deadline=deadline)


class BrowserProgressJournal:
    """ProgressBatch/1: exact ordered bounded diagnostics, without GPU authority."""
    limits = {"contractVersion": 1, "batchMessages": 128, "maxMessages": 16384,
              "maxMessageCodeUnits": 65536, "maxCodeUnits": 4 * 1024 * 1024}

    def __init__(self):
        self.messages = []
        self.observations = []
        self.code_units = 0

    @staticmethod
    def code_units_of(message):
        return len(message.encode("utf-16-le", "surrogatepass")) // 2

    def accept(self, batch, elapsed):
        if (type(batch.get("contractVersion")) is not int or batch.get("contractVersion") != 1
                or type(batch.get("first")) is not int
                or batch["first"] != len(self.messages)):
            raise ValueError("BROWSER-PROGRESS-ORDER")
        messages = batch.get("messages")
        if (not isinstance(messages, list) or not messages
                or len(messages) > self.limits["batchMessages"]
                or len(messages) > self.limits["maxMessages"] - len(self.messages)
                or any(not isinstance(m, str) for m in messages)):
            raise ValueError("BROWSER-PROGRESS-CAPACITY")
        units = [self.code_units_of(m) for m in messages]
        if (any(n > self.limits["maxMessageCodeUnits"] for n in units)
                or sum(units) > self.limits["maxCodeUnits"] - self.code_units):
            raise ValueError("BROWSER-PROGRESS-CAPACITY")
        self.messages.extend(messages)
        self.observations.extend({"message": m, "elapsedSeconds": elapsed} for m in messages)
        self.code_units += sum(units)
        return {"contractVersion": 1, "acceptedThrough": len(self.messages)}

    def matches(self, messages):
        return isinstance(messages, list) and self.messages == messages


class BrowserContractRunner:
    def __init__(self, browser, directory, entry, output, timeout_seconds, capture_progress=False,
                 disable_background_timer_throttling=False):
        if timeout_seconds <= 0:
            raise ValueError("BROWSER-CONTRACT-TIMEOUT-BUDGET")
        self.browser = str(browser)
        self.directory = pathlib.Path(directory).resolve()
        self.entry = entry
        self.output = pathlib.Path(output)
        self.timeout_seconds = timeout_seconds
        self.capture_progress = capture_progress
        if type(disable_background_timer_throttling) is not bool:
            raise ValueError("BROWSER-CONTRACT-TIMER-POLICY")
        self.disable_background_timer_throttling = disable_background_timer_throttling

    def browser_command(self, profile, url):
        # Launch syntax only; backend capability still comes from the real page.
        if pathlib.Path(self.browser).stem.lower() == 'firefox':
            if self.disable_background_timer_throttling:
                raise ValueError('BROWSER-CONTRACT-FIREFOX-TIMER-POLICY-UNSUPPORTED')
            return [self.browser, '--headless', '--no-remote', '--profile', str(profile), url]
        command = [self.browser, '--headless=new', '--no-first-run', '--no-default-browser-check',
                   '--disable-background-networking', '--user-data-dir=' + str(profile), url]
        if self.disable_background_timer_throttling:
            command.insert(1, '--disable-background-timer-throttling')
        return command

    def run(self, initial_result=None):
        ready = threading.Event()
        result = dict(initial_result) if initial_result is not None else {}
        journal = BrowserProgressJournal()
        journal_lock = threading.Lock()
        started = time.monotonic()
        output = self.output
        capture_progress = self.capture_progress
        class Handler(http.server.SimpleHTTPRequestHandler):
            def log_message(self, *unused):
                pass
            def do_POST(self):
                if self.path == "/progress" and capture_progress:
                    with journal_lock:
                        try:
                            batch = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                            receipt = journal.accept(batch, time.monotonic() - started)
                            (output / "progress.json").write_text(json.dumps(journal.observations, indent=2), encoding="utf-8")
                        except (ValueError, TypeError, KeyError) as error:
                            self.send_error(400, str(error)); return
                    self.send_response(200); self.end_headers()
                    self.wfile.write(json.dumps(receipt).encode("utf-8")); return
                if self.path != "/result":
                    self.send_error(404); return
                result.update(json.loads(self.rfile.read(int(self.headers["Content-Length"]))))
                if capture_progress and not journal.matches(result.get("messages")):
                    result["errors"] = list(result.get("errors", [])) + ["BROWSER-PROGRESS-FINAL-MISMATCH"]
                self.send_response(200); self.end_headers(); self.wfile.write(b"ok")
                ready.set()
        self.output.mkdir(parents=True, exist_ok=True)
        server = create_browser_http_server(
            functools.partial(Handler, directory=str(self.directory)))
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            profile_directory=tempfile.TemporaryDirectory(prefix="arp-browser-contract-")
            with profile_directory as profile:
                command = self.browser_command(profile, f"http://127.0.0.1:{server.server_port}/" + self.entry)
                with (self.output / "browser.log").open("w", encoding="utf-8") as log:
                    browser = subprocess.Popen(command, stdout=log, stderr=log,
                        creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0)
                    try:
                        if not wait_for_browser_result(ready, browser, profile, self.timeout_seconds,
                                                       crash_output=self.output / "crashes"):
                            raise RuntimeError("BROWSER-CONTRACT-TIMEOUT; see browser.log")
                    finally:
                        retire_browser_process(browser,profile_directory,10)
        finally:
            server.shutdown(); server.server_close()
            (self.output / "report.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        return result
