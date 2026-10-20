"""Sample instruction pointers of a child Windows x64 process; no instrumentation.

Only the explicitly launched child's threads are suspended, and each successful
suspend is paired with a resume in finally. Samples include startup and shutdown.
Symbols are resolved from the same PE with MinGW nm, accounting for ASLR. This
reports self samples, not inclusive stacks or wall-clock benchmark timings.
"""
from __future__ import annotations

import argparse
import bisect
import collections
import ctypes
import json
import os
from pathlib import Path
import struct
import subprocess
import time


class PeSymbolTable:
    def __init__(self, image_base: int, image_size: int, symbols: list[tuple[int, str]]):
        self.image_base = image_base
        self.image_size = image_size
        self.symbols = sorted(symbols)
        self.addresses = [address for address, _ in self.symbols]

    @classmethod
    def from_executable(cls, executable: Path, nm: str):
        data = executable.read_bytes()
        pe = struct.unpack_from('<I', data, 0x3C)[0]
        if data[pe:pe + 4] != b'PE\0\0' or struct.unpack_from('<H', data, pe + 4)[0] != 0x8664:
            raise ValueError('sampling requires an AMD64 PE executable')
        optional = pe + 24
        if struct.unpack_from('<H', data, optional)[0] != 0x20B:
            raise ValueError('sampling requires PE32+')
        base = struct.unpack_from('<Q', data, optional + 24)[0]
        size = struct.unpack_from('<I', data, optional + 56)[0]
        result = subprocess.run([nm, '-n', '-C', '--defined-only', str(executable)],
                                check=True, capture_output=True, text=True,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        symbols = []
        for line in result.stdout.splitlines():
            columns = line.split(maxsplit=2)
            if len(columns) == 3 and columns[1] in ('t', 'T', 'w', 'W'):
                symbols.append((int(columns[0], 16), columns[2]))
        if not symbols:
            raise ValueError('no text symbols; retain an unstripped executable')
        return cls(base, size, symbols)

    def resolve(self, instruction_pointer: int, loaded_base: int) -> str | None:
        rva = instruction_pointer - loaded_base
        if not 0 <= rva < self.image_size:
            return None
        address = self.image_base + rva
        index = bisect.bisect_right(self.addresses, address) - 1
        return self.symbols[index][1] if index >= 0 else '<before first text symbol>'


class WindowsProcessSampler:
    # WinNT.h AMD64 CONTEXT: ContextFlags at 48, Rip at 248; sizeof=1232,
    # alignment=16. Only CONTEXT_CONTROL is requested, never SIMD/debug state.
    CONTEXT_SIZE = 1232
    CONTEXT_FLAGS = 48
    CONTEXT_RIP = 248
    ERROR_BAD_LENGTH = 24
    ERROR_PARTIAL_COPY = 299

    @classmethod
    def retry_module_snapshot(cls, error_code: int, image_known: bool) -> bool:
        # The initial child loader may not expose a complete module list yet.
        # Partial copies after locating the image remain hard failures. The
        # caller's existing timeout bounds every retry, and errors are recorded.
        return error_code == cls.ERROR_BAD_LENGTH or (
            error_code == cls.ERROR_PARTIAL_COPY and not image_known)

    def __init__(self, interval_ms: float, cpu: int | None):
        if os.name != 'nt' or ctypes.sizeof(ctypes.c_void_p) != 8:
            raise RuntimeError('sampling requires 64-bit Python on Windows')
        from ctypes import wintypes as w
        self.w = w
        self.interval = interval_ms / 1000
        self.cpu = cpu
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        signatures = {
            'CreateToolhelp32Snapshot': (w.HANDLE, [w.DWORD, w.DWORD]),
            'CloseHandle': (w.BOOL, [w.HANDLE]),
            'OpenThread': (w.HANDLE, [w.DWORD, w.BOOL, w.DWORD]),
            'SuspendThread': (w.DWORD, [w.HANDLE]),
            'ResumeThread': (w.DWORD, [w.HANDLE]),
            'GetThreadContext': (w.BOOL, [w.HANDLE, ctypes.c_void_p]),
            'GetThreadTimes': (w.BOOL, [w.HANDLE, *([ctypes.POINTER(w.FILETIME)] * 4)]),
            'SetProcessAffinityMask': (w.BOOL, [w.HANDLE, ctypes.c_size_t]),
            'GetProcessAffinityMask': (w.BOOL, [w.HANDLE, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t)]),
        }
        for name, (result, arguments) in signatures.items():
            function = getattr(self.kernel, name)
            function.restype, function.argtypes = result, arguments

        class ThreadEntry(ctypes.Structure):
            _fields_ = [('size', w.DWORD), ('usage', w.DWORD), ('id', w.DWORD), ('process', w.DWORD),
                        ('priority', w.LONG), ('delta', w.LONG), ('flags', w.DWORD)]

        class ModuleEntry(ctypes.Structure):
            _fields_ = [('size', w.DWORD), ('id', w.DWORD), ('process', w.DWORD),
                        ('globalUsage', w.DWORD), ('processUsage', w.DWORD),
                        ('base', ctypes.c_void_p), ('bytes', w.DWORD), ('module', w.HMODULE),
                        ('name', w.WCHAR * 256), ('path', w.WCHAR * 260)]

        self.ThreadEntry, self.ModuleEntry = ThreadEntry, ModuleEntry
        for prefix, structure in [('Thread32', ThreadEntry), ('Module32', ModuleEntry)]:
            for suffix in ('First', 'Next'):
                function = getattr(self.kernel, prefix + suffix + ('W' if prefix == 'Module32' else ''))
                function.restype = w.BOOL
                function.argtypes = [w.HANDLE, ctypes.POINTER(structure)]

    def _entries(self, flags: int, process: int, structure, prefix: str):
        snapshot = self.kernel.CreateToolhelp32Snapshot(flags, process)
        if snapshot == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            entry = structure()
            entry.size = ctypes.sizeof(entry)
            first = getattr(self.kernel, prefix + 'First' + ('W' if prefix == 'Module32' else ''))
            next_entry = getattr(self.kernel, prefix + 'Next' + ('W' if prefix == 'Module32' else ''))
            if not first(snapshot, ctypes.byref(entry)):
                error = ctypes.get_last_error()
                if error != 18:  # ERROR_NO_MORE_FILES is a completed enumeration.
                    raise ctypes.WinError(error)
                return
            while True:
                yield entry
                if not next_entry(snapshot, ctypes.byref(entry)):
                    error = ctypes.get_last_error()
                    if error != 18:
                        raise ctypes.WinError(error)
                    break
        finally:
            self.kernel.CloseHandle(snapshot)

    def sample(self, command: list[str], output: Path, symbols: PeSymbolTable, timeout: float):
        output.mkdir(parents=True, exist_ok=True)
        addresses = collections.Counter()
        errors = collections.Counter()
        threads = {}
        thread_times = {}
        modules = []
        base = None
        started = time.perf_counter()
        storage = ctypes.create_string_buffer(self.CONTEXT_SIZE + 15)
        context = (ctypes.addressof(storage) + 15) & ~15
        with (output / 'stdout.txt').open('w', encoding='utf-8') as stdout, \
                (output / 'stderr.txt').open('w', encoding='utf-8') as stderr:
            child = subprocess.Popen(command, stdout=stdout, stderr=stderr, creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                if self.cpu is not None:
                    process_mask, system_mask = ctypes.c_size_t(), ctypes.c_size_t()
                    if not self.kernel.GetProcessAffinityMask(int(child._handle), ctypes.byref(process_mask), ctypes.byref(system_mask)):
                        raise ctypes.WinError(ctypes.get_last_error())
                    mask = 1 << self.cpu
                    if not mask & system_mask.value or not self.kernel.SetProcessAffinityMask(int(child._handle), mask):
                        raise RuntimeError('requested processor is unavailable')
                next_scan = started
                while child.poll() is None:
                    now = time.perf_counter()
                    if now - started > timeout:
                        raise TimeoutError('profiled child exceeded --timeout')
                    if now >= next_scan:
                        for entry in self._entries(0x4, 0, self.ThreadEntry, 'Thread32'):
                            if entry.process != child.pid or entry.id in threads:
                                continue
                            handle = self.kernel.OpenThread(0x0002 | 0x0008 | 0x0800, False, entry.id)
                            if handle: threads[entry.id] = handle
                            else: errors['OpenThread/' + str(ctypes.get_last_error())] += 1
                        try:
                            loaded = []
                            for entry in self._entries(0x8, child.pid, self.ModuleEntry, 'Module32'):
                                loaded.append(dict(base=entry.base, bytes=entry.bytes, path=entry.path))
                                if base is None:
                                    if Path(entry.path).resolve() == Path(command[0]).resolve():
                                        base = entry.base
                            modules = loaded
                        except OSError as error:
                            # Enumeration can race the child's normal exit.
                            # Keep the last complete map; a missing image still
                            # fails the final attribution checks below.
                            if error.winerror in (self.ERROR_BAD_LENGTH, self.ERROR_PARTIAL_COPY) \
                                    and child.poll() is not None:
                                errors['module-process-exit/' + str(error.winerror)] += 1
                                break
                            if not self.retry_module_snapshot(error.winerror, base is not None): raise
                            errors['module-loader-race/' + str(error.winerror)] += 1
                        next_scan = now + 0.1
                    for thread_id, handle in list(threads.items()):
                        times = [self.w.FILETIME() for _ in range(4)]
                        if self.kernel.GetThreadTimes(handle, *(ctypes.byref(value) for value in times)):
                            ticks = lambda value: (value.dwHighDateTime << 32) | value.dwLowDateTime
                            thread_times[thread_id] = dict(kernel_seconds=ticks(times[2]) / 1e7, user_seconds=ticks(times[3]) / 1e7)
                        else: errors['GetThreadTimes/' + str(ctypes.get_last_error())] += 1
                        previous = self.kernel.SuspendThread(handle)
                        if previous == 0xFFFFFFFF:
                            errors['SuspendThread/' + str(ctypes.get_last_error())] += 1
                            self.kernel.CloseHandle(handle)
                            del threads[thread_id]
                            continue
                        try:
                            ctypes.c_uint32.from_address(context + self.CONTEXT_FLAGS).value = 0x100001
                            if not self.kernel.GetThreadContext(handle, context):
                                errors['GetThreadContext/' + str(ctypes.get_last_error())] += 1
                            else:
                                ip = ctypes.c_uint64.from_address(context + self.CONTEXT_RIP).value
                                addresses[(thread_id, ip)] += 1
                        finally:
                            if self.kernel.ResumeThread(handle) == 0xFFFFFFFF:
                                raise ctypes.WinError(ctypes.get_last_error())
                    time.sleep(self.interval)
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait()
                for handle in threads.values(): self.kernel.CloseHandle(handle)
        if child.returncode:
            raise RuntimeError(f'profiled child failed: {child.returncode}; see {output / "stderr.txt"}')
        if base is None or not addresses:
            raise RuntimeError('no executable module or instruction samples; profile is invalid')
        if not thread_times:
            raise RuntimeError('no thread CPU times; cannot identify the active sampled thread')
        sampled_threads = {thread_id for thread_id, _ in addresses}
        primary = max(sampled_threads, key=lambda tid: sum(thread_times.get(tid, {}).values()))
        counts = collections.Counter()
        raw = []
        for (thread_id, ip), count in addresses.items():
            symbol = symbols.resolve(ip, base)
            if symbol is None:
                owner = next((item for item in modules if item['base'] <= ip < item['base'] + item['bytes']), None)
                symbol = '<external ' + Path(owner['path']).name + '>' if owner else '<unknown module>'
            if thread_id == primary: counts[symbol] += count
            raw.append(dict(thread_id=thread_id, instruction_pointer=hex(ip), samples=count, symbol=symbol))
        total = sum(counts.values())
        report = dict(command=command, exit_code=child.returncode, cpu=self.cpu,
                      elapsed_seconds=time.perf_counter() - started, interval_ms=self.interval * 1000,
                      loaded_image_base=hex(base), preferred_image_base=hex(symbols.image_base),
                      total_samples=total, errors=dict(errors), raw=raw,
                      all_thread_samples=sum(addresses.values()), primary_thread_id=primary,
                      thread_cpu_times=thread_times, modules=modules,
                      hotspots=[dict(symbol=name, samples=count, percent=100 * count / total)
                                for name, count in counts.most_common()],
                      scope='Self IP samples of explicitly launched child threads, including startup/shutdown; '
                            'hotspots show the thread with the most observed CPU time, not blocked worker threads. '
                            'Percentages are sample shares, not CPU-time weights. External DLLs are grouped by module. '
                            'Suspension perturbs timing: do not use as a benchmark.')
        (output / 'profile.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
        for item in report['hotspots'][:20]:
            print(f'{item["percent"]:5.1f}% {item["samples"]:6d} {item["symbol"]}')
        return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nm', default='C:/mingw64/bin/nm.exe')
    parser.add_argument('--interval-ms', type=float, default=1)
    parser.add_argument('--timeout', type=float, default=60)
    parser.add_argument('--cpu', type=int)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.command and args.command[0] == '--': args.command.pop(0)
    if not args.command or args.interval_ms <= 0 or args.timeout <= 0 or (args.cpu is not None and not 0 <= args.cpu < 64):
        parser.error('provide a child command, positive timing values, and a CPU in [0, 63]')
    args.command[0] = str(Path(args.command[0]).resolve())
    symbols = PeSymbolTable.from_executable(Path(args.command[0]), args.nm)
    WindowsProcessSampler(args.interval_ms, args.cpu).sample(args.command, args.output, symbols, args.timeout)


if __name__ == '__main__':
    main()
