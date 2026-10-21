#!/usr/bin/env python3
"""Process selection/dimming/details PTY checks, with optional deterministic telemetry."""
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


def run(binary, ascii_only, output=None, fixture=False):
    master, slave = pty.openpty()
    rows, cols = 55, 120
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
    original = termios.tcgetattr(slave)
    with tempfile.TemporaryDirectory(prefix='tui-process-view-') as temp:
        phase = Path(temp, 'phase')
        env = {**os.environ, 'TERM': 'xterm-256color',
               'LC_ALL': 'en_US.UTF-8' if sys.platform == 'darwin' else 'C.UTF-8',
               'ASCEND_TEST_PHASE_FILE': str(phase)}
        process = subprocess.Popen([binary] + (['--ascii'] if ascii_only else []),
                                   stdin=slave, stdout=slave, stderr=slave, env=env, start_new_session=True)
        screen = Screen(rows, cols)
        raw = bytearray()

        def drain(seconds=.4):
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                if select.select([master], [], [], min(.05, max(0, end - time.monotonic())))[0]:
                    data = os.read(master, 65536)
                    raw.extend(data)
                    screen.feed(data)

        def send(data):
            os.write(master, data)
            drain()

        def click(x, y):
            if sys.platform == 'darwin':
                send(b'\x1b[M' + bytes((32, x + 33, y + 33)) + b'\x1b[M' + bytes((35, x + 33, y + 33)))
            else:
                send(f'\x1b[<0;{x + 1};{y + 1}M\x1b[<0;{x + 1};{y + 1}m'.encode())

        def resize(height, width):
            nonlocal screen, rows, cols
            rows, cols = height, width
            fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
            screen = Screen(rows, cols)
            process.send_signal(signal.SIGWINCH)
            drain(.6)

        def entries():
            return [(y, int(line[3:8].strip()), int(line[8:17].strip()))
                    for y, line in enumerate(screen.text().splitlines())
                    if line[3:8].strip().isdigit() and line[8:17].strip().isdigit()]

        def selected():
            return [(y, npu, pid) for y, npu, pid in entries() if screen.attributes[y][8] & 4]

        def device_styles():
            return [(int(line[3:8].strip()), bool(screen.attributes[y][3] & 2))
                    for y, line in enumerate(screen.text().splitlines())
                    if line[3:8].strip().isdigit() and 'MEM:' in line]

        baseline_colors = {}

        def colors():
            result = {}
            for y, line in enumerate(screen.text().splitlines()):
                if line[3:8].strip().isdigit() and 'MEM:' in line:
                    x = line.index('MEM:')
                    result[int(line[3:8].strip())] = tuple((screen.attributes[y][col] >> 8) - 1
                                                         for col in (3, x, x + 5))
            return result

        def check_selection(pid, linked=None):
            assert len(selected()) == 1 and selected()[0][2] == pid, (pid, selected(), screen.text())
            actual_colors = colors()
            for npu, color in actual_colors.items():
                assert color == baseline_colors[npu], ('selection replaced original metric colors', npu, baseline_colors[npu], color)
            for y, line in enumerate(screen.text().splitlines()):
                if line[3:8].strip().isdigit() and 'MEM:' in line and screen.attributes[y][3] & 2:
                    x = line.index('MEM:') + 5
                    assert not screen.attributes[y][x] & 4, 'muted bar must use foreground fill, not reversed background'
                    assert screen.cells[y][x] == ('#' if ascii_only else '█'), 'bar glyph must receive foreground dimming'
            styles = device_styles()
            if linked is not None:
                assert all(dim == (npu not in linked) for npu, dim in styles), (linked, styles)
            assert any(dim for _, dim in styles) and any(not dim for _, dim in styles), styles

        def save(name):
            if output:
                Path(output).mkdir(parents=True, exist_ok=True)
                mode = 'ascii' if ascii_only else 'unicode'
                Path(output, f'view-{mode}-{name}.txt').write_text(screen.text())
                Path(output, f'view-{mode}-session.ansi').write_bytes(raw)

        def set_phase(value):
            phase.write_text(value)
            drain(.8)

        try:
            deadline = time.monotonic() + 15
            while not entries() and time.monotonic() < deadline:
                drain(.2)
            assert len(entries()) >= 2, screen.text()
            assert not selected() and not any(dim for _, dim in device_styles())
            baseline_colors = colors()
            first_pid = entries()[0][2]
            send(b'\x1bOB')
            check_selection(first_pid, {0, 2} if fixture else None)
            send(b'\x1bOB')
            assert selected() and selected()[0][2] != first_pid, screen.text()
            y, _, pid = entries()[0]
            click(12, y)
            check_selection(pid, {0, 2} if fixture else None)
            save('selected')
            drain(2.5)
            send(b'\r')
            assert 'Process details' in screen.text() and f'PID {pid}' in screen.text()
            assert all(label in screen.text() for label in ('NPU Memory:', 'CPU Usage:', 'Host Memory (RSS):', 'Y-axis:'))
            assert any('.' == c if ascii_only else '\u2800' <= c <= '\u28ff'
                       for row in screen.cells[7:] for c in row), screen.text()
            if fixture:
                assert 'NPU Memory: 400MiB' in screen.text() and 'Host Memory (RSS): 512MiB (5.0%)' in screen.text()
                assert 'CPU Usage: 250.0%' in screen.text() and '0 - 300%' in screen.text(), screen.text()
            save('details')
            send(b'h')
            assert 'Help' in screen.text()
            send(b'\x1b')
            assert 'Process details' in screen.text(), screen.text()
            resize(24, 90)
            assert all(label in screen.text() for label in ('NPU Memory:', 'CPU Usage:', 'Host Memory (RSS):')), screen.text()
            save('details-small')
            resize(20, 70)
            assert 'Terminal too small' in screen.text()
            resize(55, 120)
            send(b'\x1b')
            check_selection(pid, {0, 2} if fixture else None)
            send(b's')
            check_selection(pid, {0, 2} if fixture else None)
            if fixture:
                set_phase('reorder')
                check_selection(pid, {0, 2})
                save('reordered')
                send(b'\r')
                set_phase('missing')
                assert 'NPU Memory: N/A' in screen.text() and 'CPU Usage: 250.0%' in screen.text(), screen.text()
                set_phase('unavailable')
                assert 'Process data unavailable' in screen.text() and 'CPU Usage: N/A' in screen.text()
                set_phase('reuse')
                assert 'Process exited or released all NPUs' in screen.text()
                assert 'NPU Memory: N/A' in screen.text() and 'Host Memory (RSS): N/A' in screen.text()
                save('pid-reused')
                send(b'\x1b')
                assert not selected() and not any(dim for _, dim in device_styles()), screen.text()
                set_phase('normal')
            click(5, 0)
            assert not selected() and not any(dim for _, dim in device_styles()), screen.text()
            send(b'\x1bOA')
            assert selected(), screen.text()
            send(b'\x1b')
            assert process.poll() is None and not selected()
            assert not any(dim for _, dim in device_styles())
            send(b'q')
            process.wait(timeout=10)
            assert process.returncode == 0
            restored = termios.tcgetattr(slave)
            assert restored[3] & (termios.ECHO | termios.ICANON) == original[3] & (termios.ECHO | termios.ICANON)
            print('Process selection/dimming/details PTY passed:', 'ASCII' if ascii_only else 'Unicode', 'fixture' if fixture else 'hardware')
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
    parser.add_argument('--fixture', action='store_true')
    args = parser.parse_args()
    run(args.binary, True, args.output, args.fixture)
    run(args.binary, False, args.output, args.fixture)
