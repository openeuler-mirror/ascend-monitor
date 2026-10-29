#!/usr/bin/env python3
"""Check the interactive wait starts after a completed collection."""
import argparse
import fcntl
import os
import pty
import select
import signal
import struct
import subprocess
import tempfile
import termios
import time
from pathlib import Path


def run(binary):
    with tempfile.TemporaryDirectory() as directory:
        log = Path(directory, 'samples.txt')
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 55, 120, 0, 0))
        process = subprocess.Popen(
            [binary], stdin=slave, stdout=slave, stderr=slave, start_new_session=True,
            env={**os.environ, 'TERM': 'xterm-256color', 'ASCEND_TEST_SAMPLE_LOG': str(log)},
        )
        try:
            deadline = time.monotonic() + 1.4
            while time.monotonic() < deadline and process.poll() is None:
                if select.select([master], [], [], .1)[0]:
                    os.read(master, 65536)
            os.write(master, b'q')
            deadline = time.monotonic() + 5
            while process.poll() is None and time.monotonic() < deadline:
                if select.select([master], [], [], .1)[0]:
                    os.read(master, 65536)
            process.wait(timeout=1)
            assert process.returncode == 0, process.returncode
            samples = [float(line) for line in log.read_text().splitlines()]
            assert len(samples) >= 4, samples
            gaps = [right - left for left, right in zip(samples, samples[1:])]
            # The fixture takes 0.05s to collect and waits 0.2s afterward.
            assert all(.22 <= gap <= .6 for gap in gaps), gaps
            print('Interactive post-collection interval passed:', len(samples), 'samples')
        finally:
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            os.close(master)
            os.close(slave)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('binary')
    run(parser.parse_args().binary)
