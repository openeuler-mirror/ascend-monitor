#!/usr/bin/env python3
"""Linux PTY integration checks; Python is a test dependency, not a runtime one."""
import argparse
import codecs
import fcntl
import json
import os
import pty
import re
import select
import signal
import struct
import subprocess
import termios
import time
from pathlib import Path

class Screen:
    """Decode the VT cursor/erase operations emitted by ncurses for assertions."""
    def __init__(self, rows, cols):
        self.rows, self.cols = rows, cols
        self.cells = [[' '] * cols for _ in range(rows)]
        self.attributes = [[0] * cols for _ in range(rows)]
        self.style = 0  # bold=1, dim=2, reverse=4; foreground index stored above bit 7.
        self.foreground = -1
        self.y = self.x = 0
        self.pending = ''
        self.acs = False
        self.last = ' '
        self.decoder = codecs.getincrementaldecoder('utf-8')('replace')
        self.scroll_top, self.scroll_bottom = 0, rows - 1

    def scroll(self, top, bottom, count, down=False):
        count = min(count, bottom - top + 1)
        blank = [[' '] * self.cols for _ in range(count)]
        region = self.cells[top:bottom + 1]
        self.cells[top:bottom + 1] = blank + region[:len(region) - count] if down else region[count:] + blank
        blank_style = [[0] * self.cols for _ in range(count)]
        styles = self.attributes[top:bottom + 1]
        self.attributes[top:bottom + 1] = blank_style + styles[:len(styles) - count] if down else styles[count:] + blank_style

    def put(self, char):
        if self.acs:
            char = {'q': '─', 'x': '│', 'a': '▒', 'l': '┌', 'k': '┐', 'm': '└', 'j': '┘', 'n': '┼', 't': '├', 'u': '┤', 'v': '┴', 'w': '┬'}.get(char, char)
        if 0 <= self.y < self.rows and 0 <= self.x < self.cols:
            self.cells[self.y][self.x] = char
            self.attributes[self.y][self.x] = self.style | ((self.foreground + 1) << 8)
        self.x = min(self.cols - 1, self.x + 1)
        self.last = char

    def feed(self, data):
        s = self.pending + self.decoder.decode(data)
        i = 0
        while i < len(s):
            c = s[i]
            if c == '\x1b':
                if i + 1 >= len(s): break
                if s[i + 1] == '[':
                    m = re.match(r'\x1b\[([0-9;? >!]*)([@-~])', s[i:])
                    if not m: break
                    raw, op = m.groups()
                    args = [int(v) if v else 0 for v in raw.lstrip('? >!').split(';')]
                    n = args[0] or 1
                    if op in ('H', 'f'):
                        self.y = min(self.rows - 1, max(0, args[0] - 1))
                        self.x = min(self.cols - 1, max(0, (args[1] if len(args) > 1 else 1) - 1))
                    elif op == 'G': self.x = min(self.cols - 1, n - 1)
                    elif op == 'd': self.y = min(self.rows - 1, n - 1)
                    elif op == 'A': self.y = max(0, self.y - n)
                    elif op == 'B': self.y = min(self.rows - 1, self.y + n)
                    elif op == 'C': self.x = min(self.cols - 1, self.x + n)
                    elif op == 'D': self.x = max(0, self.x - n)
                    elif op == 'J':
                        if args[0] == 2: self.cells = [[' '] * self.cols for _ in range(self.rows)]
                        elif args[0] == 0:
                            self.cells[self.y][self.x:] = [' '] * (self.cols - self.x)
                            for row in range(self.y + 1, self.rows): self.cells[row] = [' '] * self.cols
                    elif op == 'K':
                        start, end = (0, self.cols) if args[0] == 2 else ((0, self.x + 1) if args[0] == 1 else (self.x, self.cols))
                        self.cells[self.y][start:end] = [' '] * (end - start)
                    elif op == 'X': self.cells[self.y][self.x:self.x + n] = [' '] * min(n, self.cols - self.x)
                    elif op == 'b':
                        for _ in range(n): self.put(self.last)
                    elif op == 'r':
                        self.scroll_top = max(0, args[0] - 1)
                        self.scroll_bottom = min(self.rows - 1, (args[1] if len(args) > 1 and args[1] else self.rows) - 1)
                        self.y = self.x = 0
                    elif op in ('S', 'T'):
                        self.scroll(self.scroll_top, self.scroll_bottom, n, down=op == 'T')
                    elif op in ('L', 'M') and self.scroll_top <= self.y <= self.scroll_bottom:
                        self.scroll(self.y, self.scroll_bottom, n, down=op == 'L')
                    elif op == 'm':
                        k = 0
                        while k < len(args):
                            value = args[k]
                            if value == 0: self.style = 0; self.foreground = -1
                            elif value == 1: self.style |= 1
                            elif value == 2: self.style |= 2
                            elif value == 7: self.style |= 4
                            elif value == 22: self.style &= ~3
                            elif value == 27: self.style &= ~4
                            elif 30 <= value <= 37: self.foreground = value - 30
                            elif 90 <= value <= 97: self.foreground = value - 90 + 8
                            elif value == 39: self.foreground = -1
                            elif value in (38, 48) and k + 2 < len(args):
                                if args[k + 1] == 5:
                                    if value == 38: self.foreground = args[k + 2]
                                    k += 2
                                elif args[k + 1] == 2 and k + 4 < len(args):
                                    if value == 38: self.foreground = (1 << 24) | (args[k + 2] << 16) | (args[k + 3] << 8) | args[k + 4]
                                    k += 4
                            k += 1
                    i += m.end(); continue
                if s[i + 1] in '()':
                    if i + 2 >= len(s): break
                    if s[i + 1] == '(': self.acs = s[i + 2] == '0'
                    i += 3; continue
                if s[i + 1] == 'M':
                    if self.y == self.scroll_top: self.scroll(self.scroll_top, self.scroll_bottom, 1, down=True)
                    else: self.y = max(0, self.y - 1)
                i += 2; continue
            if c == '\r': self.x = 0
            elif c == '\n':
                if self.y == self.scroll_bottom: self.scroll(self.scroll_top, self.scroll_bottom, 1)
                else: self.y = min(self.rows - 1, self.y + 1)
            elif c == '\b': self.x = max(0, self.x - 1)
            elif c == '\t': self.x = min(self.cols - 1, (self.x // 8 + 1) * 8)
            elif c >= ' ': self.put(c)
            i += 1
        self.pending = s[i:]

    def text(self):
        return '\n'.join(''.join(row).rstrip() for row in self.cells)


def run(binary, output):
    master, slave = pty.openpty()
    def size(rows, cols):
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
    size(55, 200)
    original = termios.tcgetattr(slave)
    process = subprocess.Popen([binary], stdin=slave, stdout=slave, stderr=slave,
                               env={**os.environ, 'TERM': 'xterm-256color', 'LC_ALL': 'C.UTF-8'}, start_new_session=True)
    screen = Screen(55, 200)
    raw = bytearray()
    def drain(seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if select.select([master], [], [], min(.1, max(0, end - time.monotonic())))[0]:
                try: data = os.read(master, 65536)
                except OSError: break
                raw.extend(data); screen.feed(data)
    def send(keys, seconds=.3):
        os.write(master, keys); drain(seconds)
    try:
        drain(4)
        initial = screen.text()
        assert 'ascend-monitor-tui 0.1.0' in initial and 'ascend-top' not in initial, initial
        assert 'Driver: 25.5.1' in initial and 'CANN: 8.5.2' in initial, initial
        assert initial.count('310P3') == 8, initial
        assert len(re.findall(r'Share (?:On|Off|N/A)', initial)) == 8, initial
        assert len(re.findall(r'Logic \d+', initial)) == 8, initial
        assert 'NPU MEM:' in initial and 'NPU UTL:' in initial and 'VLLM' in initial, initial
        assert 'root@' in initial and 'N/A' in initial, initial
        assert any('\u2800' <= c <= '\u28ff' for c in initial) and '*' not in initial, initial
        assert '│' in initial and '┼' in initial and '┌' in initial, initial
        if output:
            Path(output).mkdir(parents=True, exist_ok=True)
            Path(output, 'screen-wide.txt').write_text(initial)
            Path(output, 'screen-wide.ansi').write_bytes(raw)
        # Match the user's side-by-side terminal width. Bars must stay beside data.
        size(55, 120); screen = Screen(55, 120); process.send_signal(signal.SIGWINCH); drain(1)
        medium = screen.text()
        assert medium.count('310P3') == 8, medium
        assert all('MEM:' in row and row.count('│') == 5 for row in medium.splitlines() if '310P3' in row), medium
        assert '43.24GiB' in medium and 'Card 6 / Chip 0' in medium, medium
        assert len(re.findall(r'Share (?:On|Off|N/A)', medium)) == 8, medium
        if output: Path(output, 'screen-medium.txt').write_text(medium)
        send(b'h')
        assert 'NAVIGATION' in screen.text(), screen.text()
        send(b'2')
        assert 'one logical cpu = 100%' in screen.text().lower(), screen.text()
        assert 'container sharing enabled' in screen.text().lower(), screen.text()
        send(b'h')
        size(24, 90); screen = Screen(24, 90); process.send_signal(signal.SIGWINCH); drain(1)
        assert 'NPU MEM:' in screen.text() and 'Processes:' in screen.text(), screen.text()
        assert re.search(r'\d+/\d+  Share (?:On|Off|N/A)', screen.text()), screen.text()
        assert re.search(r'Logic \d+', screen.text()) and 'MEM:' in screen.text(), screen.text()
        send(b'\t\x1bOF')  # device focus and End, application cursor mode
        assert '7    310P3' in screen.text(), screen.text()
        send(b'\t\x1bOF\x1bOC')
        assert 'Processes' in screen.text(), screen.text()
        if output: Path(output, 'screen-small.txt').write_text(screen.text())
        size(20, 70); screen = Screen(20, 70); process.send_signal(signal.SIGWINCH); drain(.3)
        assert 'Terminal too small' in screen.text(), screen.text()
        size(55, 200); screen = Screen(55, 200); process.send_signal(signal.SIGWINCH); drain(1)
        assert screen.text().count('310P3') == 8, screen.text()
        send(b'q'); process.wait(timeout=10)
        assert process.returncode == 0, process.returncode
        assert termios.tcgetattr(slave) == original, 'terminal mode not restored'
        print(json.dumps({'pty': 'passed', 'checks': ['8 chips', 'versions', 'processes', 'help', '120-column grid and bars', '24x90 resize', 'device scroll', 'process scroll', 'command pan', 'too small', 'restore', 'q', 'termios restoration']}))
    finally:
        if process.poll() is None: process.kill(); process.wait()
        os.close(master); os.close(slave)

def check_graphics_modes(binary, output):
    for label, args, locale, expect_braille in [
        ('unicode', [], 'C.UTF-8', True),
        ('forced-ascii', ['--ascii'], 'C.UTF-8', False),
        ('c-locale', [], 'C', False),
        ('invalid-locale', [], 'ascend_top_missing_locale', False),
    ]:
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 55, 120, 0, 0))
        process = subprocess.Popen([binary] + args, stdin=slave, stdout=slave, stderr=slave,
            env={**os.environ, 'TERM': 'xterm-256color', 'LC_ALL': locale}, start_new_session=True)
        screen = Screen(55, 120)
        try:
            end = time.monotonic() + 4
            while time.monotonic() < end:
                if select.select([master], [], [], .1)[0]: screen.feed(os.read(master, 65536))
            frame = screen.text()
            assert frame.count('310P3') == 8 and '*' not in frame, (label, frame)
            has_braille = any('\u2800' <= c <= '\u28ff' for c in frame)
            assert has_braille == expect_braille, (label, frame)
            if expect_braille:
                axis = next(i for i, row in enumerate(screen.cells) if ''.join(row).count('now') == 2)
                assert all(t in ''.join(screen.cells[axis]) for t in ('30s', '60s', '120s')), frame
                assert '41s' not in ''.join(screen.cells[axis]) and '81s' not in ''.join(screen.cells[axis]), frame
                swap_row = next(row for row in screen.cells if 'SWP:' in ''.join(row))
                assert any('\u2800' <= c <= '\u28ff' for c in swap_row[40:64]), ('Swap baseline must align with SWP label', frame)
                def occupied(cells):
                    dots = []
                    for char in cells:
                        mask = ord(char) - 0x2800 if '\u2800' <= char <= '\u28ff' else 0
                        dots.extend([bool(mask & 0x47), bool(mask & 0xb8)])
                    return dots
                host = occupied(screen.cells[axis + 1][3:64])
                npu = occupied(screen.cells[axis + 1][67:117])
                for dots in (host, npu):
                    assert any(dots) and all(dots[dots.index(True):]), ('false history gaps', frame)
                assert sum(host) == sum(npu), ('host/NPU time scale mismatch', frame)

            if not expect_braille:
                assert '|' in frame and '+' in frame, (label, frame)
                assert all(ord(c) < 128 for c in frame), (label, frame)
                assert '.' in frame, (label, frame)
            if output: Path(output, 'graphics-' + label + '.txt').write_text(frame)
            os.write(master, b'q'); process.wait(timeout=10)
            assert process.returncode == 0
        finally:
            if process.poll() is None: process.kill(); process.wait()
            os.close(master); os.close(slave)
    print('Graphics modes passed: Unicode, --ascii, C locale, invalid locale; no star markers')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('binary')
    parser.add_argument('--output')
    args = parser.parse_args()
    run(args.binary, args.output)
    check_graphics_modes(args.binary, args.output)
