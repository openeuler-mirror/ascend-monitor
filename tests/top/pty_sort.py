#!/usr/bin/env python3
"""Header mouse/keyboard regression; works without a driver or NPU hardware."""
import argparse
import fcntl
import os
import pty
from pathlib import Path
import re
import select
import signal
import struct
import subprocess
import sys
import termios
import time
from pty_smoke import Screen


def run(binary, ascii_only, output=None):
    master, slave = pty.openpty()
    rows, cols = 55, 120
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
    original = termios.tcgetattr(slave)
    process = subprocess.Popen([binary] + (['--ascii'] if ascii_only else []),
                               stdin=slave, stdout=slave, stderr=slave,
                               env={**os.environ, 'TERM': 'xterm-256color', 'LC_ALL': 'en_US.UTF-8' if sys.platform == 'darwin' else 'C.UTF-8'},
                               start_new_session=True)
    screen = Screen(rows, cols)
    raw = bytearray()

    def drain(seconds=.35):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if select.select([master], [], [], min(.05, max(0, end - time.monotonic())))[0]:
                data = os.read(master, 65536)
                raw.extend(data)
                screen.feed(data)

    def send(data):
        os.write(master, data)
        drain()

    def header():
        lines = screen.text().splitlines()
        return next((y, line) for y, line in enumerate(lines) if 'NPU-MEM' in line and '%CPU' in line)

    def expect(label, down):
        y, line = header()
        marker = ('v' if down else '^') if ascii_only else ('▼' if down else '▲')
        assert label + marker in line, (label, marker, screen.text())
        start, width = {'NPU-MEM': (24, 12), '%CPU': (36, 7), '%MEM': (43, 7)}[label]
        values = []
        for body in screen.text().splitlines()[y + 1:]:
            if not body[8:17].strip().isdigit():
                continue
            value = body[3 + start:3 + start + width].strip()
            if value == 'N/A':
                values.append(None)
            elif label == 'NPU-MEM':
                match = re.fullmatch(r'([0-9.]+)([KMGT]?i?B)', value)
                assert match, (value, body)
                values.append(float(match[1]) * {'B': 1, 'KiB': 1024, 'MiB': 1024**2, 'GiB': 1024**3, 'TiB': 1024**4}[match[2]])
            else:
                values.append(float(value))
        if output:
            Path(output).mkdir(parents=True, exist_ok=True)
            Path(output, 'sort-session.ansi').write_bytes(raw)
        valid = [v for v in values if v is not None]
        assert valid == sorted(valid, reverse=down), (label, down, values, screen.text())
        assert values == valid + [None] * (len(values) - len(valid)), ('missing must be last', values)
        if output:
            mode = 'ascii' if ascii_only else 'unicode'
            Path(output).mkdir(parents=True, exist_ok=True)
            Path(output, f'sort-{mode}-{cols}x{rows}-{label}-{"desc" if down else "asc"}.txt').write_text(screen.text())

    def click(x, y):
        # Use the terminal protocol requested by the platform's curses library.
        # One press/release must cause exactly one toggle.
        if sys.platform == 'darwin':
            send(b'\x1b[M' + bytes((32, x + 33, y + 33))
                 + b'\x1b[M' + bytes((35, x + 33, y + 33)))
        else:
            send(f'\x1b[<0;{x + 1};{y + 1}M\x1b[<0;{x + 1};{y + 1}m'.encode())

    try:
        deadline = time.monotonic() + 15
        while 'NPU-MEM' not in screen.text() and time.monotonic() < deadline:
            drain(.2)
        assert 'NPU-MEM' in screen.text(), ('initial snapshot timeout', screen.text())
        expect('NPU-MEM', True)
        y, line = header()
        click(line.index('%CPU'), y)
        expect('%CPU', True)
        click(line.index('%CPU'), y)
        expect('%CPU', False)
        click(line.index('%MEM') + 5, y)  # Column padding also belongs to the target.
        expect('%MEM', True)
        click(line.index('%MEM'), y)
        expect('%MEM', False)
        click(line.index('PID'), y)
        expect('%MEM', False)
        click(line.index('%CPU'), y + 2)
        expect('%MEM', False)
        send(b's')
        expect('%CPU', True)
        send(b'/')
        expect('%CPU', False)
        send(b'S')
        expect('NPU-MEM', True)
        send(b'h')
        assert 'SORTING' in screen.text() and 'Click sort header' in screen.text()
        click(line.index('%CPU'), y)
        send(b'h')
        expect('NPU-MEM', True)
        for rows, cols in [(24, 90), (55, 200), (55, 120)]:
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
            screen = Screen(rows, cols)
            os.kill(process.pid, signal.SIGWINCH)
            drain()
            y, line = header()
            click(line.index('%CPU'), y)
            expect('%CPU', True)
            click(line.index('%CPU'), y)
            expect('%CPU', False)
            send(b'S')
            expect('NPU-MEM', True)
        send(b'q')
        process.wait(timeout=3)
        assert process.returncode == 0
        restored = termios.tcgetattr(slave)
        assert restored[3] & (termios.ECHO | termios.ICANON) == original[3] & (termios.ECHO | termios.ICANON), 'terminal echo/canonical mode not restored'
        print('PTY header sorting passed:', 'ASCII' if ascii_only else 'Unicode')
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)
        os.close(slave)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('binary')
    parser.add_argument('--output')
    args = parser.parse_args()
    run(args.binary, True, args.output)
    run(args.binary, False, args.output)
