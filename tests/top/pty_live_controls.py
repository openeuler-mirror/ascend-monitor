#!/usr/bin/env python3
"""Runtime sampling wait and display-only pause, on fixture or real telemetry."""
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


class Session:
    def __init__(self, binary, ascii_only, interval, directory, hardware=False):
        self.master, self.slave = pty.openpty()
        self.rows, self.cols = 55, 120
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack('HHHH', self.rows, self.cols, 0, 0))
        self.original = termios.tcgetattr(self.slave)
        self.screen = Screen(self.rows, self.cols)
        self.raw = bytearray()
        self.log = Path(directory, 'samples.txt')
        self.phase = Path(directory, 'phase')
        env = {**os.environ, 'TERM': 'xterm-256color',
               'LC_ALL': 'en_US.UTF-8' if sys.platform == 'darwin' else 'C.UTF-8'}
        if not hardware:
            env.update(ASCEND_TEST_SAMPLE_LOG=str(self.log), ASCEND_TEST_PHASE_FILE=str(self.phase))
        self.process = subprocess.Popen([binary, '--interval', str(interval)] + (['--ascii'] if ascii_only else []),
                                        stdin=self.slave, stdout=self.slave, stderr=self.slave,
                                        env=env, start_new_session=True)

    def drain(self, seconds=.25):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if select.select([self.master], [], [], min(.05, max(0, end - time.monotonic())))[0]:
                data = os.read(self.master, 65536)
                self.raw.extend(data)
                self.screen.feed(data)

    def send(self, data, seconds=.25):
        os.write(self.master, data)
        self.drain(seconds)

    def expect(self, text, seconds=2):
        end = time.monotonic() + seconds
        while text not in self.screen.text() and time.monotonic() < end:
            self.drain(.1)
        assert text in self.screen.text(), (text, self.screen.text())

    def samples(self):
        return [float(line) for line in self.log.read_text().splitlines()] if self.log.exists() else []

    def body(self):
        return '\n'.join(self.screen.text().splitlines()[1:])

    def resize(self, rows, cols):
        self.rows, self.cols = rows, cols
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack('HHHH', rows, cols, 0, 0))
        self.screen = Screen(rows, cols)
        self.process.send_signal(signal.SIGWINCH)
        self.drain(.4)

    def save(self, output, name):
        if output:
            Path(output).mkdir(parents=True, exist_ok=True)
            Path(output, name + '.txt').write_text(self.screen.text())
            Path(output, name + '.ansi').write_bytes(self.raw)

    def quit(self):
        self.send(b'q')
        self.process.wait(timeout=10)
        assert self.process.returncode == 0
        restored = termios.tcgetattr(self.slave)
        original = self.original.copy()
        if sys.platform == 'darwin':
            original[3] &= ~termios.PENDIN
            restored[3] &= ~termios.PENDIN
        assert restored == original, 'terminal settings were not restored'

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait()
        os.close(self.master)
        os.close(self.slave)


def run(binary, ascii_only, output, hardware):
    mode = 'ascii' if ascii_only else 'unicode'
    with tempfile.TemporaryDirectory(prefix='tui-live-controls-') as temp:
        s = Session(binary, ascii_only, .2, temp, hardware)
        try:
            s.expect('NPU-MEM', 15)
            s.expect('wait 0.2s [min]')
            s.send(b'---')
            s.expect('wait 0.2s [min]')
            s.send(b'+')
            s.expect('wait 0.3s')
            s.drain(1.0)
            if not hardware:
                gaps = [b-a for a, b in zip(s.samples(), s.samples()[1:])][-2:]
                assert len(gaps) == 2 and all(.32 <= gap < .8 for gap in gaps), gaps
            s.send(b'=')
            s.expect('wait 0.4s')
            s.drain(1.2)
            if not hardware:
                samples = s.samples()
                gaps = [b-a for a, b in zip(samples, samples[1:])][-2:]
                assert len(gaps) == 2 and all(.42 <= gap < .9 for gap in gaps), gaps
            s.send(b'-')
            s.expect('wait 0.3s')
            if not hardware:
                s.send(b'fchosen\r')
            else:
                s.send(b'\x1bOB')  # Select an existing process without affecting it.
            s.send(b' ')
            s.expect('PAUSED @')
            frozen = s.body()
            count = len(s.samples())
            if not hardware:
                s.phase.write_text('renamed')
            s.send(b'+')
            s.expect('wait 0.4s')
            assert s.body() == frozen, 'changing wait must not zoom or alter frozen history'
            s.drain(3.3)
            assert s.body() == frozen and 'STALE' not in s.screen.text(), s.screen.text()
            if not hardware:
                assert len(s.samples()) >= count + 5, 'sampling stopped during display pause'
                assert 'renamed worker' not in s.screen.text()
            line = s.screen.text().splitlines()[0]
            x = line.index('PAUSED')
            assert s.screen.attributes[0][x] & 1, 'paused state should be bold'
            assert (s.screen.attributes[0][x] >> 8) - 1 == 3, 'paused state should be yellow'
            s.save(output, f'{mode}-paused-main')
            s.send(b'\r')
            s.expect('Process details')
            s.expect('NPU Memory:')
            if not hardware:
                s.expect('chosen worker')
                assert 'renamed worker' not in s.screen.text(), 'details mixed live data into a frozen frame'
            detail = s.body()
            s.send(b'-')
            s.expect('wait 0.3s')
            s.drain(.8)
            assert s.body() == detail, 'frozen detail metrics or plots changed'
            s.save(output, f'{mode}-paused-details')
            s.send(b'h')
            s.expect('Shortcuts')
            s.send(b'\x1bOF')
            s.expect('Space')
            s.send(b'+- ')
            s.send(b'h')
            s.expect('Process details')
            s.expect('wait 0.3s')
            s.expect('PAUSED @')
            s.send(b'q')
            s.expect('NPU-MEM')
            s.send(b'f\x15+- ')
            s.expect('Find: +- _')
            s.expect('wait 0.3s')
            s.expect('PAUSED @')
            s.send(b'\x1b')
            for rows, cols in ((24, 90), (20, 70), (55, 200), (55, 120)):
                s.resize(rows, cols)
                s.expect('PAUSED @')
                s.expect('wait 0.3s')
                if cols >= 90:
                    s.expect('Space resume')
                else:
                    s.expect('Terminal too small')
                s.save(output, f'{mode}-paused-{cols}x{rows}')
            s.send(b' ')
            s.expect('LIVE')
            assert 'PAUSED' not in s.screen.text().splitlines()[0]
            assert 'Space pause' in s.screen.text().splitlines()[0] and 'Space resume' not in s.screen.text().splitlines()[0]
            if not hardware:
                s.expect('renamed worker')
            s.save(output, f'{mode}-resumed')
            # Pause from details, return to the list, and resume there.
            s.send(b'\x1bOB\r')
            s.expect('Process details')
            s.send(b' ')
            s.expect('PAUSED @')
            detail = s.body()
            s.drain(.8)
            assert s.body() == detail
            s.send(b'q')
            s.expect('PAUSED @')
            s.send(b' ')
            s.expect('LIVE')
            s.quit()
            if output and not hardware:
                Path(output, f'{mode}-sample-times.txt').write_text(s.log.read_text())
        finally:
            if s.process.poll() is None:
                s.save(output, f'{mode}-failure')
            s.close()
    print('Live interval and display-only pause passed:', mode, 'hardware' if hardware else 'fixture')


def boundaries(binary, output):
    for interval in (59.9, .25):
        with tempfile.TemporaryDirectory(prefix='tui-interval-boundary-') as temp:
            s = Session(binary, True, interval, temp)
            try:
                s.expect('NPU-MEM', 10)
                if interval == 59.9:
                    s.send(b'++')
                    s.expect('wait 60.0s [max]')
                    count = len(s.samples())
                    s.drain(.5)
                    assert len(s.samples()) == count, 'longer wait was ignored'
                    # Shorten an active 60s wait; do not sleep for the old deadline.
                    s.send(b'-' * 598, .5)
                    s.expect('wait 0.2s [min]')
                    s.drain(.8)
                    assert len(s.samples()) >= count + 2, 'shortening did not wake the waiting sampler'
                    s.save(output, 'shortened-long-wait')
                else:
                    s.expect('wait 0.25s')
                    s.send(b'+')
                    s.expect('wait 0.35s')
                    s.send(b'-')
                    s.expect('wait 0.25s')
                s.quit()
            finally:
                s.close()
    print('Interval boundaries, custom value and wake-up passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('binary')
    parser.add_argument('--output')
    parser.add_argument('--hardware', action='store_true')
    args = parser.parse_args()
    run(args.binary, True, args.output, args.hardware)
    run(args.binary, False, args.output, args.hardware)
    if not args.hardware:
        boundaries(args.binary, args.output)
