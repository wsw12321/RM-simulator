"""Bounded request/response lines with a local native engine, on POSIX and Windows.

The bridges used to wait on the child's pipes with select() and write to a
non-blocking stdin, which keeps every command inside a deadline. Windows has
neither for pipes, so here one daemon thread reads the child's stdout and hands
over complete lines through a one-slot queue, and each write runs in a
short-lived thread; the caller waits on both with one deadline. As before, a
hung child cannot stall a command past its deadline, and a child that floods
its output is not read further: the reader stops at a line longer than the
bound and pauses while an unconsumed line is waiting.

A response is returned without its newline and without the carriage return a
Windows C++ runtime adds in text mode. On Windows an extensionless binary path
(build/rmuc_combat) runs build/rmuc_combat.exe. Nothing here closes the process on an
error: callers keep their own policy (the bridges close a candidate on any
failure, the legacy sandbox only on timeout or overflow) and call close().
"""
import os
from pathlib import Path
import queue
import subprocess
import threading
import time

_CHUNK = 65536
_EOF = object()
_OVERFLOW = object()
EXE = '.exe' if os.name == 'nt' else ''


def executable(path):
    """The file to start for a native binary path: adds .exe on Windows."""
    path = Path(path)
    return path.with_name(path.name + EXE) if EXE and not path.suffix else path


class NativePipe:
    def __init__(self, binary, max_bytes):
        args = [str(a) for a in binary] if isinstance(binary, (list, tuple)) else [str(executable(binary))]
        self.proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0)
        self._max = max_bytes
        self._lines = queue.Queue(1)
        self._closed = False
        threading.Thread(target=self._read, daemon=True).start()

    def _put(self, item):
        while True:
            try:
                self._lines.put(item, timeout=0.2)
                return True
            except queue.Full:
                if self._closed:
                    return False

    def _read(self):
        partial = bytearray()
        try:
            while True:
                chunk = self.proc.stdout.read(_CHUNK)
                if not chunk:
                    break
                start = 0
                while (end := chunk.find(b'\n', start)) >= 0:
                    if len(partial) + end - start > self._max:
                        self._put(_OVERFLOW)
                        return
                    partial += chunk[start:end]
                    line = bytes(partial)
                    partial.clear()
                    if not self._put(line):
                        return
                    start = end + 1
                partial += chunk[start:]
                if len(partial) > self._max:
                    self._put(_OVERFLOW)
                    return
        except (OSError, ValueError):  # stdout closed or broken while shutting down
            pass
        self._put(_EOF)

    def _write(self, data, deadline):
        done = queue.Queue(1)

        def write():
            try:
                view = memoryview(data)
                while view:
                    view = view[self.proc.stdin.write(view):]
                done.put(None)
            except BaseException as exc:  # handed to the waiting caller
                done.put(exc)

        threading.Thread(target=write, daemon=True).start()
        try:
            error = done.get(timeout=max(0.0, deadline - time.monotonic()))
        except queue.Empty:
            return False
        if error is not None:
            raise error
        return True

    def exchange(self, data, timeout, *, write_timeout, read_timeout, stopped, oversize):
        """Send data and return the next response line; each failure raises RuntimeError with its message."""
        deadline = time.monotonic() + timeout
        if not self._write(data, deadline):
            raise RuntimeError(write_timeout)
        try:
            line = self._lines.get(timeout=max(0.0, deadline - time.monotonic()))
        except queue.Empty:
            raise RuntimeError(read_timeout) from None
        if line is _EOF or line is _OVERFLOW:
            self._lines.put(line)  # the reader has stopped; later calls fail the same way at once
            raise RuntimeError(stopped if line is _EOF else oversize)
        return line[:-1] if line.endswith(b'\r') else line

    def close(self):
        self._closed = True
        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        self.proc.stdin.close()
        self.proc.stdout.close()
