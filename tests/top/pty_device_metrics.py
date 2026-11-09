#!/usr/bin/env python3
"""Check independent device metric colors, CORE labels, clipping and selection."""
import argparse
import fcntl
import os
from pathlib import Path
import pty
import select
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time
from pty_smoke import Screen


def run(binary, ascii_only, output=None, hardware=False):
    master, slave = pty.openpty()
    rows, cols = 55, 120
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
    original = termios.tcgetattr(slave)
    with tempfile.TemporaryDirectory(prefix='tui-device-metrics-') as temp:
        phase = Path(temp, 'phase')
        phase.write_text('metrics')
        process = subprocess.Popen(
            [binary] + (['--ascii'] if ascii_only else []),
            stdin=slave, stdout=slave, stderr=slave, start_new_session=True,
            env={**os.environ, 'TERM': 'xterm-256color',
                 'LC_ALL': 'en_US.UTF-8' if sys.platform == 'darwin' else 'C.UTF-8',
                 'ASCEND_TEST_PHASE_FILE': str(phase)},
        )
        screen = Screen(rows, cols)
        raw = bytearray()
        mode = 'ascii' if ascii_only else 'unicode'

        def drain(seconds=.4):
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                if select.select([master], [], [], min(.05, max(0, end - time.monotonic())))[0]:
                    data = os.read(master, 65536)
                    raw.extend(data)
                    screen.feed(data)

        def foreground(y, x):
            return (screen.attributes[y][x] >> 8) - 1

        def save(name):
            if output:
                Path(output).mkdir(parents=True, exist_ok=True)
                Path(output, f'metrics-{mode}-{name}.txt').write_text(screen.text())
                Path(output, f'metrics-{mode}-session.ansi').write_bytes(raw)

        def check(expected=None):
            lines = screen.text().splitlines()
            styles = {}
            compact = 'Auto/Compact' in screen.text() or '  Compact [c]' in screen.text()
            summary_row = next(y for y,line in enumerate(lines) if 'NPU MEM:' in line)
            for y, line in enumerate(lines):
                if compact:
                    if y >= summary_row or not line[3:8].strip().isdigit():
                        continue
                    npu = int(line[3:8].strip())
                    memory_color, core_color = foreground(y, 23), foreground(y, 53)
                    for x in (3, 8, 63, 75):
                        assert foreground(y, x) == -1, ('compact metadata inherits metric color', line)
                    assert screen.cells[y][cols - 2] in ('|', '│'), line
                    if expected is not None:
                        assert (memory_color, core_color) == expected[npu], (npu, memory_color, core_color, expected)
                    styles[npu] = (memory_color, core_color)
                    continue
                if 'MEM:' not in line or not line[3:8].strip().isdigit():
                    continue
                npu = int(line[3:8].strip())
                core_line = lines[y + 1]
                mem_x = line.index('MEM:')
                core_x = core_line.index('CORE:' if 'CORE:' in core_line else 'UTL:')
                # The two representations of each metric must agree.
                memory_color, core_color = foreground(y + 1, 23), foreground(y + 1, 48)
                assert core_color == foreground(y + 1, core_x), ('Core number/bar mismatch', npu, core_line)
                assert memory_color == foreground(y, mem_x), ('memory number/bar mismatch', npu, line)
                for row, x in ((y, mem_x), (y + 1, core_x)):
                    assert screen.cells[row][x + 5] in ('#', '█', ' '), (x, lines[row])
                assert all(screen.cells[row][cols - 2] in ('|', '│') for row in (y, y + 1)), screen.text()
                # Identity, temperature/power, PCI and sharing are neutral.
                for row, x in ((y, 3), (y, 8), (y, 23), (y, 48), (y + 1, 3),
                               (y + 1, core_line.index('Logic'))):
                    assert foreground(row, x) == -1, ('metadata inherits metric color', row, x, lines[row])
                if expected is not None:
                    assert (memory_color, core_color) == expected[npu], (npu, memory_color, core_color, expected)
                styles[npu] = (memory_color, core_color)
            assert styles, screen.text()
            assert 'NPU CORE:' in screen.text(), screen.text()
            assert 'UTL:' not in screen.text() and 'UTIL:' not in screen.text()
            return styles

        try:
            deadline = time.monotonic() + 15
            while 'MEM:' not in screen.text() and time.monotonic() < deadline:
                drain(.2)
            # ANSI colors: green=2, yellow=3, red=1, default=-1.
            expected = None if hardware else {0: (2, 1), 1: (3, 1), 2: (1, 2)}
            baseline = check(expected)
            save('normal')
            os.write(master, b'\x1bOB')
            drain(.8)
            selected = check(expected)
            if not hardware:
                assert selected == baseline
                assert any(style & 2 for row in screen.attributes for style in row), 'unlinked devices must dim'
            save('selected')
            # Clear selection before switching fixture phases or resizing.
            os.write(master, b'\x1b')
            drain()
            if not hardware:
                for name, expected in (
                    ('metrics-missing', {0: (-1, 1), 1: (3, -1), 2: (-1, -1)}),
                    ('metrics-thresholds', {0: (1, 2), 1: (3, 3), 2: (2, 1)}),
                ):
                    phase.write_text(name)
                    drain(.8)
                    check(expected)
                    save(name)
            for rows, cols in ((24, 90), (55, 100), (55, 200)):
                fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
                screen = Screen(rows, cols)
                process.send_signal(signal.SIGWINCH)
                drain(.8)
                check(expected)
                save(f'{cols}x{rows}')
            os.write(master, b'q')
            drain(.3)
            process.wait(timeout=10)
            assert process.returncode == 0
            restored = termios.tcgetattr(slave)
            if sys.platform == 'darwin':
                # macOS curses marks buffered input for reprocessing on exit.
                restored[3] &= ~termios.PENDIN
                original[3] &= ~termios.PENDIN
            assert restored == original, 'terminal settings not restored'
            print('Device metrics PTY passed:', mode, 'hardware' if hardware else 'fixture')
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
    run(args.binary, True, args.output, args.hardware)
    run(args.binary, False, args.output, args.hardware)
