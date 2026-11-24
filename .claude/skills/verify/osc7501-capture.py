#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Kmux contributors
# SPDX-License-Identifier: CC0-1.0
"""Run a coding agent in a pty that supports OSC 7501 and log what it reports.

Usage:
  osc7501-capture.py LOG PROMPT [--agent PATH] [--project DIR]
                     [--watch SECONDS] [--then PROMPT]... [-- AGENT_ARGUMENTS...]

The pty answers the OSC 7501 support query, so Claude Code reports its state
as it would in Kmux. After startup the script types PROMPT, logs every OSC
7501 report with its time since launch (title and msg decoded) for --watch
seconds, types each --then prompt followed by another --watch period, appends
the end of the screen, and stops the agent with SIGTERM,
which also ends its background tasks.

The agent runs with a minimal environment: no Kmux, Konsole, or parent
Claude Code variables, and no Kmux agent launchers on PATH, so it does not
install hooks. It answers Claude Code's folder trust dialog with "Yes".

Each run is a real model turn on the user's account: keep prompts short.
Allow the tools the prompt needs with agent arguments, for example
`-- --allowedTools 'Bash(sleep:*)'`.
"""

import argparse
import base64
import fcntl
import os
import pty
import re
import select
import shutil
import signal
import struct
import tempfile
import termios
import time

OSC_PATTERN = re.compile(rb'\x1b\](\d+);(.*?)(?:\x07|\x1b\\)', re.S)
CONTROL_PATTERN = re.compile(rb'\x1b\[[0-9;?>=]*[ -/]*[@-~]|\x1b\][^\x07\x1b]*(?:\x07|\x1b\\)|\x1b[()][0-9A-B]')
QUERY_REPLY = b'\x1b]7501;?:states=idle,working,done,blocked,error:kinds=permission,question,auth\x1b\\'
ROWS, COLUMNS = 40, 120


def arguments():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('log')
    parser.add_argument('prompt')
    parser.add_argument('--agent', help='agent executable; default: the first claude on PATH outside the Kmux launchers')
    parser.add_argument('--project', help='working directory; default: a new empty temporary directory')
    parser.add_argument('--watch', type=float, default=45, help='seconds to log after sending each prompt')
    parser.add_argument('--then', action='append', default=[], metavar='PROMPT', help='a later prompt, typed after the previous --watch period; repeatable')
    parser.add_argument('agent_arguments', nargs='*')
    return parser.parse_args()


def clean_path():
    directories = os.environ.get('PATH', '/usr/bin:/bin').split(':')
    return ':'.join(directory for directory in directories if 'agent-shims' not in directory)


def agent_environment():
    environment = {
        'HOME': os.environ['HOME'],
        'PATH': clean_path(),
        'TERM': 'xterm-256color',
        'LANG': os.environ.get('LANG', 'C.UTF-8'),
        'USER': os.environ.get('USER', ''),
        'COLUMNS': str(COLUMNS),
        'LINES': str(ROWS),
    }
    if 'XDG_RUNTIME_DIR' in os.environ:
        environment['XDG_RUNTIME_DIR'] = os.environ['XDG_RUNTIME_DIR']
    return environment


def decoded(payload):
    fields = []
    for pair in payload.split(':'):
        key, _, value = pair.partition('=')
        if key in ('title', 'msg') and value:
            try:
                value = repr(base64.b64decode(value).decode())
            except ValueError:
                pass
        fields.append(f'{key}={value}' if _ else pair)
    return ':'.join(fields)


class Capture:
    def __init__(self, descriptor, log):
        self.descriptor = descriptor
        self.log = log
        self.start = time.monotonic()
        self.pending = b''
        self.screen = b''

    def note(self, text):
        self.log.write(f'{time.monotonic() - self.start:7.2f}s  {text}\n')
        self.log.flush()

    def visible(self):
        return CONTROL_PATTERN.sub(b'', self.screen)

    def write(self, data):
        os.write(self.descriptor, data)

    def pump(self, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            ready, _, _ = select.select([self.descriptor], [], [], 0.1)
            if not ready:
                continue
            try:
                data = os.read(self.descriptor, 65536)
            except OSError:
                return False
            if not data:
                return False
            self.screen = (self.screen + data)[-20000:]
            self.answer_queries(data)
            self.log_reports(data)
        return True

    def answer_queries(self, data):
        if b'\x1b]7501;?' in data:
            self.write(QUERY_REPLY)
            self.note('answered the OSC 7501 query')
        if b'\x1b[6n' in data:
            self.write(b'\x1b[1;1R')
        # The 7501 reply must arrive before the DA1 reply that ends the probe.
        if b'\x1b[c' in data or b'\x1b[0c' in data:
            self.write(b'\x1b[?62;22c')

    def log_reports(self, data):
        self.pending += data
        for match in OSC_PATTERN.finditer(self.pending):
            if match.group(1) == b'7501' and not match.group(2).startswith(b'?'):
                self.note('OSC 7501 ' + decoded(match.group(2).decode(errors='replace')))
        unfinished = self.pending.rfind(b'\x1b]')
        tail = self.pending[unfinished:] if unfinished >= 0 else b''
        self.pending = tail if tail and not OSC_PATTERN.match(tail) else b''

    def wait_for(self, text, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if text in self.visible():
                return True
            if not self.pump(0.5):
                return False
        return False


def main():
    options = arguments()
    agent = options.agent or shutil.which('claude', path=clean_path())
    if agent is None:
        raise SystemExit('no claude on PATH; pass --agent')
    project = options.project or tempfile.mkdtemp(prefix='osc7501-project-')

    process_id, descriptor = pty.fork()
    if process_id == 0:
        os.chdir(project)
        os.execve(agent, [agent, *options.agent_arguments], agent_environment())
    fcntl.ioctl(descriptor, termios.TIOCSWINSZ, struct.pack('HHHH', ROWS, COLUMNS, 0, 0))

    with open(options.log, 'w') as log:
        capture = Capture(descriptor, log)
        capture.note(f'started {agent} in {project}')
        capture.pump(5)
        if capture.wait_for(b'trustthisfolder', 3) or capture.wait_for(b'trust this folder', 1):
            capture.note('answered the folder trust dialog with Yes')
            capture.write(b'\x1b[B')
            capture.pump(0.5)
            capture.write(b'\r')
            capture.pump(5)
        for prompt in [options.prompt, *options.then]:
            capture.note(f'typing the prompt: {prompt}')
            for character in prompt:
                capture.write(character.encode())
                time.sleep(0.01)
            capture.pump(1)
            capture.write(b'\r')
            capture.pump(options.watch)
        capture.note('end of the screen:')
        log.write(capture.visible()[-1500:].decode(errors='replace') + '\n')
        os.kill(process_id, signal.SIGTERM)
        capture.pump(2)
    print(open(options.log).read())


if __name__ == '__main__':
    main()
