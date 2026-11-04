#!/usr/bin/env python3
"""Live process search PTY checks with deterministic, test-only telemetry."""
import argparse
import fcntl
import os
import pty
import select
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time
from pathlib import Path
from pty_smoke import Screen


def run(binary, ascii_only):
    master, slave = pty.openpty()
    rows, cols = 55, 120
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
    original = termios.tcgetattr(slave)
    temp = tempfile.TemporaryDirectory(prefix='tui-search-')
    phase = Path(temp.name, 'phase')
    process = subprocess.Popen([binary] + (['--ascii'] if ascii_only else []),
                               stdin=slave, stdout=slave, stderr=slave, start_new_session=True,
                               env={**os.environ, 'TERM': 'xterm-256color',
                                    'ASCEND_TEST_PHASE_FILE': str(phase),
                                    'LC_ALL': 'en_US.UTF-8' if sys.platform == 'darwin' else 'C.UTF-8'})
    screen = Screen(rows, cols)

    def drain(seconds=.25):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if select.select([master], [], [], min(.05, max(0, end - time.monotonic())))[0]:
                screen.feed(os.read(master, 65536))

    def send(data):
        os.write(master, data)
        drain()

    def entries():
        return [(y, int(line[8:17].strip())) for y, line in enumerate(screen.text().splitlines())
                if line[3:8].strip().isdigit() and line[8:17].strip().isdigit()]

    def expect(pids, count=None):
        actual = [pid for y, pid in entries() if screen.attributes[y][8] & 1]
        assert sorted(actual) == sorted(pids), (pids, actual, screen.text())
        for y, pid in entries():
            if pid in pids:
                assert (screen.attributes[y][8] >> 8) - 1 == 3, 'matching rows should be yellow'
        if count is not None:
            assert f'{count} matching rows' in screen.text(), screen.text()

    def resize(height, width):
        nonlocal screen, rows, cols
        rows, cols = height, width
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
        screen = Screen(rows, cols)
        process.send_signal(signal.SIGWINCH)
        drain(.4)

    try:
        deadline = time.monotonic() + 10
        while len(entries()) < 3 and time.monotonic() < deadline:
            drain(.2)
        assert len(entries()) == 3, screen.text()
        expect([])
        send(b'fCHOS')
        expect([42, 42], 2)  # Highlights appear before Enter, including both chips.
        assert len(entries()) == 3, 'search must retain unmatched rows'
        send(b'EN')
        expect([42, 42], 2)
        send(b'X')
        expect([], 0)
        assert process.poll() is None
        send(b'\x7f')
        expect([42, 42], 2)
        phase.write_text('renamed')
        drain(.8)
        expect([], 0)  # New samples recalculate matches while the prompt stays open.
        phase.write_text('normal')
        drain(.8)
        expect([42, 42], 2)
        send(b'\r')
        expect([42, 42], 2)
        assert 'Process details' not in screen.text(), 'Enter accepts the query'
        drain(.8)
        expect([42, 42], 2)  # Refresh preserves highlights.
        send(b's/')
        expect([42, 42], 2)
        send(b'h')
        assert 'PROCESS SEARCH' in screen.text(), screen.text()
        send(b'h')
        expect([42, 42], 2)
        send(b'f\x15tester')
        expect([42, 84, 42], 3)
        send(b'\x15qsh/f?')
        expect([], 0)
        assert 'Find: qsh/f?_' in screen.text() and process.poll() is None
        assert 'Help' not in screen.text(), 'shortcuts must be literal text during input'
        # Multi-byte input must be removed as one character by Backspace.
        send(b'\x15' + 'é'.encode() + b'\x7f84')
        expect([84], 1)
        send(b'\r')
        resize(24, 90)
        expect([84], 1)  # First matching row remains visible in the smallest supported size.
        assert entries(), screen.text()
        resize(55, 120)
        expect([84], 1)
        send(b'\x1b')
        expect([])
        assert 'Find:' not in screen.text() and process.poll() is None
        send(b'fworker')
        expect([42, 84, 42], 3)
        send(b'\x1b')
        expect([])
        assert 'Find:' not in screen.text() and process.poll() is None
        send(b'f' + b'x' * 300)
        expect([], 0)
        assert 'Enter: keep' in screen.text(), 'long input should preserve the status'
        send(b'\x15')
        expect([], 0)
        send(b'\r')
        assert 'Find:' not in screen.text(), screen.text()
        send(b'q')
        process.wait(timeout=10)
        assert process.returncode == 0
        restored = termios.tcgetattr(slave)
        assert restored[3] & (termios.ECHO | termios.ICANON) == original[3] & (termios.ECHO | termios.ICANON)
        print('Process search PTY passed:', 'ASCII' if ascii_only else 'Unicode')
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)
        os.close(slave)
        temp.cleanup()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('binary')
    args = parser.parse_args()
    run(args.binary, True)
    run(args.binary, False)
