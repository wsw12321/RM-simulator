"""NativePipe: deadlines, bounds and line handling with scripted child processes.

Pure Python children, so this suite also runs where no native build exists
(the Windows release job runs it before packaging).
"""
import sys
import threading
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from native_pipe import NativePipe  # noqa: E402

MESSAGES = dict(write_timeout='write timed out', read_timeout='read timed out', stopped='stopped', oversize='oversize')


def child(code):
    return [sys.executable, '-u', '-c', code]


ECHO = 'import sys\nfor line in sys.stdin: sys.stdout.write(line); sys.stdout.flush()'


class NativePipeTests(unittest.TestCase):
    def open(self, code, max_bytes=1 << 20):
        pipe = NativePipe(child(code), max_bytes)
        self.addCleanup(pipe.close)
        return pipe

    def test_round_trips_in_order_and_strips_newline(self):
        pipe = self.open(ECHO)
        for i in range(50):
            self.assertEqual(pipe.exchange(f'{{"n":{i}}}\n'.encode(), 5, **MESSAGES), f'{{"n":{i}}}'.encode())

    def test_windows_text_mode_carriage_return_is_removed_once(self):
        pipe = self.open('import sys\nfor line in sys.stdin: sys.stdout.buffer.write(b"ok\\r\\r\\n"); sys.stdout.flush()')
        self.assertEqual(pipe.exchange(b'x\n', 5, **MESSAGES), b'ok\r')

    def test_line_split_across_writes_is_assembled(self):
        pipe = self.open('import sys, time\nsys.stdin.readline()\nfor part in (b"ab", b"cd", b"ef\\n"):\n'
                         '    sys.stdout.buffer.write(part); sys.stdout.flush(); time.sleep(0.05)')
        self.assertEqual(pipe.exchange(b'go\n', 5, **MESSAGES), b'abcdef')

    def test_silent_child_hits_the_read_deadline(self):
        pipe = self.open('import sys, time\nsys.stdin.readline()\ntime.sleep(30)')
        start = time.monotonic()
        with self.assertRaisesRegex(RuntimeError, '^read timed out$'):
            pipe.exchange(b'x\n', 0.5, **MESSAGES)
        self.assertLess(time.monotonic() - start, 3)

    def test_child_that_never_reads_hits_the_write_deadline(self):
        pipe = self.open('import time\ntime.sleep(30)')
        start = time.monotonic()
        with self.assertRaisesRegex(RuntimeError, '^write timed out$'):
            pipe.exchange(b'x' * (16 << 20) + b'\n', 0.5, **MESSAGES)  # far beyond any pipe buffer
        self.assertLess(time.monotonic() - start, 3)

    def test_exit_without_answer_is_reported_now_and_later(self):
        pipe = self.open('import sys\nsys.stdin.readline()')
        with self.assertRaisesRegex(RuntimeError, '^stopped$'):
            pipe.exchange(b'x\n', 5, **MESSAGES)
        pipe.proc.wait(5)
        start = time.monotonic()
        with self.assertRaises((RuntimeError, OSError)):  # stopped, or the write already fails on the closed pipe
            pipe.exchange(b'x\n', 5, **MESSAGES)
        self.assertLess(time.monotonic() - start, 3)

    def test_unterminated_flood_is_bounded(self):
        pipe = self.open('import sys\nsys.stdin.readline()\nsys.stdout.write("x" * 300000); sys.stdout.flush()\n'
                         'import time; time.sleep(30)', max_bytes=100000)
        with self.assertRaisesRegex(RuntimeError, '^oversize$'):
            pipe.exchange(b'x\n', 5, **MESSAGES)

    def test_long_terminated_line_is_bounded(self):
        pipe = self.open('import sys\nsys.stdin.readline()\nsys.stdout.write("x" * 150000 + "\\n"); sys.stdout.flush()\n'
                         'import time; time.sleep(30)', max_bytes=100000)
        with self.assertRaisesRegex(RuntimeError, '^oversize$'):
            pipe.exchange(b'x\n', 5, **MESSAGES)

    def test_line_at_the_bound_is_accepted(self):
        pipe = self.open('import sys\nsys.stdin.readline()\nsys.stdout.write("y" * 100000 + "\\n"); sys.stdout.flush()',
                         max_bytes=100000)
        self.assertEqual(len(pipe.exchange(b'x\n', 5, **MESSAGES)), 100000)

    def test_bound_excludes_the_windows_carriage_return(self):
        # Windows text mode turns "\n" into "\r\n": the bound is the same as on POSIX.
        for body, ending, accepted in ((100000, b'\r\n', True), (100001, b'\r\n', False), (100001, b'\n', False)):
            with self.subTest(body=body, ending=ending):
                pipe = self.open(f'import sys\nsys.stdin.readline()\nsys.stdout.buffer.write(b"y" * {body} + {ending!r}); '
                                 'sys.stdout.flush()\nimport time; time.sleep(30)', max_bytes=100000)
                if accepted:
                    self.assertEqual(pipe.exchange(b'x\n', 5, **MESSAGES), b'y' * body)
                else:
                    with self.assertRaisesRegex(RuntimeError, '^oversize$'):
                        pipe.exchange(b'x\n', 5, **MESSAGES)

    def test_extra_output_does_not_grow_without_bound(self):
        # A child that prints endless lines: the reader holds one unconsumed line and then stops reading.
        pipe = self.open('import sys\nwhile True: sys.stdout.write("z" * 1000 + "\\n")', max_bytes=4096)
        self.assertEqual(pipe.exchange(b'x\n', 5, **MESSAGES), b'z' * 1000)
        self.assertLessEqual(pipe._lines.qsize(), 1)

    def test_write_error_reaches_the_caller(self):
        pipe = self.open('pass')
        pipe.proc.wait(5)
        with self.assertRaises((OSError, RuntimeError)):
            pipe.exchange(b'x' * (1 << 20) + b'\n', 5, **MESSAGES)

    def test_close_ends_the_helper_threads(self):
        before = threading.active_count()
        for code in (ECHO, 'import time\ntime.sleep(30)', 'import sys\nwhile True: sys.stdout.write("z\\n")'):
            pipe = NativePipe(child(code), 4096)
            try:
                pipe.exchange(b'x' * (4 << 20) + b'\n', 0.3, **MESSAGES)
            except (RuntimeError, OSError):
                pass
            pipe.close()
        deadline = time.monotonic() + 5
        while threading.active_count() > before and time.monotonic() < deadline:
            time.sleep(0.05)
        self.assertEqual(threading.active_count(), before)


if __name__ == '__main__':
    unittest.main()
