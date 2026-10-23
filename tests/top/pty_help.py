#!/usr/bin/env python3
"""Structured help: aligned columns, tabs, wrapping, scrolling and modal return."""
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
import termios
import time
from pty_smoke import Screen


def run(binary, ascii_only, output=None):
    master, slave = pty.openpty()
    rows, cols = 55, 120
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
    process = subprocess.Popen([binary] + (['--ascii'] if ascii_only else []), stdin=slave, stdout=slave, stderr=slave,
                               env={**os.environ, 'TERM': 'xterm-256color',
                                    'LC_ALL': 'en_US.UTF-8' if sys.platform == 'darwin' else 'C.UTF-8'}, start_new_session=True)
    screen = Screen(rows, cols)

    def drain(seconds=.25):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if select.select([master], [], [], min(.05, max(0, end - time.monotonic())))[0]:
                screen.feed(os.read(master, 65536))

    def send(data):
        os.write(master, data)
        drain()

    def resize(height, width):
        nonlocal screen, rows, cols
        rows, cols = height, width
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
        screen = Screen(rows, cols)
        process.send_signal(signal.SIGWINCH)
        drain(.4)

    def save(name):
        if output:
            Path(output).mkdir(parents=True, exist_ok=True)
            Path(output, f'help-{"ascii" if ascii_only else "unicode"}-{name}.txt').write_text(screen.text())

    def page(number):
        assert f'Page {number}/2' in screen.text(), screen.text()
        label = f'{number} ' + ('Shortcuts' if number == 1 else 'Metrics')
        x = ''.join(screen.cells[3]).index(label)
        assert screen.attributes[3][x] & 4, ('active tab must stand out', screen.text())
        assert 'Esc/h/q: back' in ''.join(screen.cells[-2])

    def click_tab(number):
        label = f'{number} ' + ('Shortcuts' if number == 1 else 'Metrics')
        x, y = ''.join(screen.cells[3]).index(label), 3
        if sys.platform == 'darwin':
            send(b'\x1b[M' + bytes((32, x + 33, y + 33)) + b'\x1b[M' + bytes((35, x + 33, y + 33)))
        else:
            send(f'\x1b[<0;{x+1};{y+1}M\x1b[<0;{x+1};{y+1}m'.encode())

    try:
        drain(.5)
        send(b'h')
        page(1)
        assert all(s in screen.text() for s in ('NAVIGATION', 'PROCESS SELECTION', 'SORTING', 'BACK & EXIT'))
        left = (cols - min(108, cols - 4)) // 2
        assert screen.attributes[6][left] & 1, 'section heading should be bold'
        assert screen.attributes[7][left] & 1 and not screen.attributes[7][left+24] & 1, 'key/description columns need separate emphasis'
        save('shortcuts-wide')
        send(b'2')
        page(2)
        assert 'DEVICE ID & STATUS' in screen.text() and 'One logical CPU = 100%' in screen.text()
        assert 'Y-axis' in screen.text() and 'STALE' in screen.text(), screen.text()
        save('metrics-wide')
        send(b'\t'); page(1)
        send(b'\x1b[Z'); page(2)
        send(b'\x1bOD'); page(1)
        click_tab(2); page(2)
        resize(24, 90)
        page(2)
        assert 'DEVICE ID & STATUS' in screen.text()
        send(b'\x1bOF')
        page(2)
        assert 'STALE' in screen.text() and 'DEVICE ID & STATUS' not in screen.text()
        save('metrics-small-end')
        send(b'\x1bOH')
        assert 'DEVICE ID & STATUS' in screen.text()
        before = ''.join(screen.cells[-1])
        send(b'\x1b[6~')
        assert ''.join(screen.cells[-1]) != before, 'page down must scroll'
        send(b'1'); page(1)
        send(b'\x1bOF')
        assert 'BACK & EXIT' in screen.text(), screen.text()
        send(b'\x1bOH')
        assert 'NAVIGATION' in screen.text()
        save('shortcuts-small')
        resize(55, 200)
        page(1)
        left = (cols - 108) // 2
        assert ''.join(screen.cells[1]).index('ascend-monitor-tui') == left
        assert all(not any(c != ' ' for c in row[left+108:]) for row in screen.cells), 'help must stay within readable content width'
        save('shortcuts-centered')
        resize(9, 39)
        assert 'Help: resize' in screen.text()
        resize(24, 90)
        page(1)
        send(b'q')
        assert process.poll() is None and 'Page 1/2' not in screen.text(), 'q closes help, not the app'
        send(b'h'); page(1)
        send(b'\x1b')
        assert 'Page 1/2' not in screen.text()
        send(b'q')
        process.wait(timeout=10)
        assert process.returncode == 0
        print('Structured help PTY passed:', 'ASCII' if ascii_only else 'Unicode')
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
    args = parser.parse_args()
    run(args.binary, True, args.output)
    run(args.binary, False, args.output)
