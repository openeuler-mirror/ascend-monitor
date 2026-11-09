#!/usr/bin/env python3
"""Exercise layout budgets, density overrides and viewport interaction through ncurses."""
import argparse
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import signal
import struct
import subprocess
import sys
import termios
import time
from pty_smoke import Screen


def run(binary, ascii_only, count, output=None):
    master, slave = pty.openpty()
    rows, cols = 24, 90
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
    original = termios.tcgetattr(slave)
    env = {**os.environ, 'TERM': 'xterm-256color',
           'LC_ALL': 'en_US.UTF-8' if sys.platform == 'darwin' else 'C.UTF-8'}
    if count is not None:
        env['ASCEND_TEST_DEVICE_COUNT'] = str(count)
    process = subprocess.Popen([binary] + (['--ascii'] if ascii_only else []),
                               stdin=slave, stdout=slave, stderr=slave, env=env, start_new_session=True)
    screen = Screen(rows, cols)
    raw = bytearray()
    mode = 'ascii' if ascii_only else 'unicode'
    label = 'hardware' if count is None else str(count)

    def drain(seconds=.18):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if select.select([master], [], [], min(.03, max(0, end-time.monotonic())))[0]:
                data = os.read(master, 65536)
                raw.extend(data)
                screen.feed(data)

    def send(keys):
        os.write(master, keys)
        drain()

    def resize(height, width):
        nonlocal screen, rows, cols
        if (height, width) == (rows, cols):
            return
        rows, cols = height, width
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
        screen = Screen(rows, cols)
        process.send_signal(signal.SIGWINCH)
        drain(.3)

    def save(name):
        if output:
            Path(output).mkdir(parents=True, exist_ok=True)
            Path(output, f'compact-{label}-{mode}-{name}.txt').write_text(screen.text())
            Path(output, f'compact-{label}-{mode}.ansi').write_bytes(raw)

    def entries():
        return [(y, line) for y,line in enumerate(screen.text().splitlines())
                if line[3:8].strip().isdigit() and line[8:17].strip().isdigit()]

    def devices():
        header = next(i for i,line in enumerate(screen.text().splitlines()) if 'NPU  Name' in line)
        return [(i, int(line[3:8].strip())) for i,line in enumerate(screen.text().splitlines())
                if i > header and i < next(j for j,l in enumerate(screen.text().splitlines()) if 'NPU MEM:' in l)
                and line[3:8].strip().isdigit()]

    def check(compact):
        lines = screen.text().splitlines()
        header = next(i for i,line in enumerate(lines) if 'NPU-MEM' in line and '%CPU' in line)
        capacity = rows - (header + 2) - 2
        assert capacity >= 5, ('process capacity', capacity, screen.text())
        if count is not None:
            assert len(entries()) >= min(count, 5), screen.text()
        assert 'NPU CORE:' in screen.text() and 'NPU MEM:' in screen.text(), screen.text()
        assert '[c]' in screen.text(), screen.text()
        assert 'NPUs ' in screen.text(), screen.text()
        if count == 0:
            assert 'No NPU devices available' in screen.text(), screen.text()
            assert 'NPU process data unavailable' in screen.text(), screen.text()
        else:
            visible = devices()
            assert visible, screen.text()
            assert all(b[0] - a[0] == (1 if compact else 3) for a,b in zip(visible, visible[1:])), screen.text()
        save(f'{cols}x{rows}')

    try:
        deadline = time.monotonic() + 15
        while 'Processes:' not in screen.text() and time.monotonic() < deadline:
            drain(.15)
        check(True)  # This is the original 24-row starvation regression.
        assert 'Auto/Compact' in screen.text(), screen.text()
        total = count if count is not None else int(re.search(r'NPUs \d+(?:-\d+)?/(\d+)', screen.text())[1])
        for height in (24,28,36,55):
            for width in (90,120,200):
                resize(height,width)
                compact = 15 + (11 if height >= 45 else 7) + 3 * max(1,total) + 5 > height
                assert ('Auto/Compact' if compact else 'Auto/Detailed') in screen.text(), screen.text()
                check(compact)
        assert ' now ' in screen.text(), 'large windows must retain history curves'
        send(b'c')
        assert 'Auto/' not in screen.text() and 'Compact [c]' in screen.text(), screen.text()
        check(True)
        resize(24,90)
        check(True)
        if count is None or count >= 8:
            initial = devices()
            send(b'\t\x1b[6~')
            following = devices()
            assert following[0][1] == initial[-1][1] + 1, (initial,following,screen.text())
            send(b'\x1bOF')
            if count is not None:
                assert devices()[-1][1] == count - 1, screen.text()
            send(b'\x1bOH')
            assert devices()[0][1] == 0, screen.text()
            send(b'\t')
        if count != 0:
            send(b'\x1bOB')
            selected = [line[8:17].strip() for y,line in entries() if screen.attributes[y][8] & 4]
            assert selected, screen.text()
            send(b'c')
            assert 'Auto/Compact' in screen.text(), screen.text()
            assert [line[8:17].strip() for y,line in entries() if screen.attributes[y][8] & 4] == selected
            send(b' ')
            assert 'PAUSED' in screen.text()
            send(b'c')
            assert 'PAUSED' in screen.text() and 'Compact [c]' in screen.text()
            send(b'fc')
            assert 'Find: c_' in screen.text() and 'Compact [c]' in screen.text(), screen.text()
            send(b'\x1b')
            send(b'h')
            assert 'Toggle Auto / Compact' in screen.text(), screen.text()
            send(b'c')
            assert 'Help' in screen.text()
            send(b'h')
            assert 'Compact [c]' in screen.text() and 'PAUSED' in screen.text()
        resize(20,70)
        assert 'Terminal too small' in screen.text(), screen.text()
        resize(55,120)
        check(True)
        send(b'q')
        process.wait(timeout=5)
        assert process.returncode == 0
        restored = termios.tcgetattr(slave)
        if sys.platform == 'darwin':
            original[3] &= ~termios.PENDIN
            restored[3] &= ~termios.PENDIN
        assert original == restored, 'terminal mode not restored'
        print('Compact PTY passed:', label, mode, flush=True)
    finally:
        if process.poll() is None:
            save('failure')
            process.kill()
            process.wait()
        os.close(master)
        os.close(slave)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('binary')
    parser.add_argument('--output')
    parser.add_argument('--hardware', action='store_true')
    args = parser.parse_args()
    for ascii_only in (True,False):
        for count in ([None] if args.hardware else [8,16,0,1]):
            run(args.binary,ascii_only,count,args.output)
