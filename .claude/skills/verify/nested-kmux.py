#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Kmux contributors
# SPDX-License-Identifier: CC0-1.0
"""Run Kmux from the build tree in a nested, virtual KWin session.

Usage:
  nested-kmux.py WORK_DIR [--kmux PATH]
                 [--command SHELL_COMMAND | --report SECONDS:PAYLOAD ...]
                 [--shot NAME:SECONDS ...] [--duration SECONDS]

Kmux gets temporary XDG directories under WORK_DIR, so the user's settings,
workspace state, and running Kmux stay untouched. The script runs
--command in the first terminal through D-Bus, saves each --shot as
WORK_DIR/NAME.png that many seconds after the command, and writes the Kmux
debug log, including every derived program status, to WORK_DIR/kmux.log.

--report prints OSC 7501 reports instead, for example
`--report 0:state=working:app=claude-code --report 4:state=done:app=claude-code`,
and then waits in `cat`, because a new shell prompt would drop working and
blocked records. End a --command that reports such states with `cat` too.
"""

import argparse
import os
import shlex
import subprocess
import sys
import time

SCRIPT = os.path.abspath(__file__)
SOURCE_DIR = os.path.abspath(os.path.join(os.path.dirname(SCRIPT), '..', '..', '..'))


def arguments():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('work_dir')
    parser.add_argument('--kmux', default=os.path.join(SOURCE_DIR, 'build', 'bin', 'kmux'))
    parser.add_argument('--command', help='shell command to run in the first terminal')
    parser.add_argument('--report', action='append', default=[], metavar='SECONDS:PAYLOAD',
                        help='print "OSC 7501 ; PAYLOAD" in the first terminal at SECONDS; replaces --command')
    parser.add_argument('--shot', action='append', default=[], metavar='NAME:SECONDS')
    parser.add_argument('--duration', type=float, default=0, help='seconds to keep Kmux running after the command')
    parser.add_argument('--inside', action='store_true', help=argparse.SUPPRESS)
    return parser.parse_args()


def report_command(reports):
    """Returns a shell command that prints each OSC 7501 report at its time and then waits in `cat`."""
    parts = []
    elapsed = 0.0
    timed_reports = [(float(seconds), payload) for seconds, _, payload in (report.partition(':') for report in reports)]
    # Reports with the same time keep their command line order.
    for seconds, payload in sorted(timed_reports, key=lambda timed_report: timed_report[0]):
        if "'" in payload or '\\' in payload:
            sys.exit(f'unsupported character in OSC 7501 payload: {payload}')
        if seconds > elapsed:
            parts.append(f'sleep {seconds - elapsed:g}')
            elapsed = seconds
        parts.append(f"printf '\\033]7501;{payload}\\033\\\\'")
    return '; '.join(parts + ['cat'])


def run_outside(options):
    if options.report:
        if options.command:
            sys.exit('use either --command or --report')
        options.command = report_command(options.report)
    work_dir = os.path.abspath(options.work_dir)
    os.makedirs(work_dir, exist_ok=True)
    forwarded = ['--kmux', os.path.abspath(options.kmux), '--duration', str(options.duration)]
    if options.command:
        forwarded += ['--command', options.command]
    for shot in options.shot:
        forwarded += ['--shot', shot]
    session = os.path.join(work_dir, 'session.sh')
    with open(session, 'w') as script:
        command = [sys.executable, '-I', SCRIPT, work_dir, '--inside', *forwarded]
        script.write('#!/bin/sh\nexec ' + shlex.join(command) + f' > {shlex.quote(work_dir)}/session.log 2>&1\n')
    os.chmod(session, 0o755)

    # Without the outer display variables, helpers that D-Bus starts on the
    # test bus, such as ksecretd, cannot show dialogs on the user's desktop.
    environment = {key: value for key, value in os.environ.items() if key not in ('WAYLAND_DISPLAY', 'DISPLAY')}
    kwin = ['dbus-run-session', '--', 'kwin_wayland', '--virtual', '--no-lockscreen', '--width', '1400', '--height', '900',
            '--socket', f'kmux-verify-{os.getpid()}', '--exit-with-session', session]
    with open(os.path.join(work_dir, 'kwin.log'), 'w') as log:
        result = subprocess.run(kwin, env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=600)
    with open(os.path.join(work_dir, 'session.log')) as log:
        sys.stdout.write(log.read())
    return result.returncode


def run_inside(options):
    work_dir = os.path.abspath(options.work_dir)
    environment = dict(os.environ)
    environment.update({
        'QT_QPA_PLATFORM': 'wayland',
        'XDG_CONFIG_HOME': f'{work_dir}/config',
        'XDG_DATA_HOME': f'{work_dir}/data',
        'XDG_STATE_HOME': f'{work_dir}/state',
        'XDG_CACHE_HOME': f'{work_dir}/cache',
        'QT_LOGGING_RULES': 'io.github.vityas_off.kmux.debug=true',
        # Qt writes to the journal instead when stderr is not a terminal.
        'QT_FORCE_STDERR_LOGGING': '1',
        # The claude launcher would add hooks pointing into the temporary
        # data directory to the real ~/.claude/settings.json.
        'KMUX_CLAUDE_HOOKS_DISABLED': '1',
    })
    for name in ('config', 'data', 'state', 'cache'):
        os.makedirs(f'{work_dir}/{name}', exist_ok=True)
    kmuxrc = f'{work_dir}/config/kmuxrc'
    if not os.path.exists(kmuxrc):
        with open(kmuxrc, 'w') as config:
            config.write('[KonsoleWindow]\nEnableSecuritySensitiveDBusAPI=true\n')

    log = open(f'{work_dir}/kmux.log', 'w')
    kmux = subprocess.Popen([options.kmux], env=environment, stdout=log, stderr=subprocess.STDOUT)

    def qdbus(*qdbus_arguments):
        result = subprocess.run(['qdbus6', *qdbus_arguments], env=environment, capture_output=True, text=True)
        return (result.stdout + result.stderr).strip()

    service = ''
    for _ in range(60):
        # qdbus6 indents service names.
        services = [line.strip() for line in qdbus().splitlines()]
        service = next((name for name in services if name.startswith('io.github.vityas_off.kmux')), '')
        if service and '/Sessions/1' in qdbus(service):
            break
        time.sleep(0.5)
    else:
        print('Kmux did not register its first session on D-Bus; see kmux.log')
        kmux.terminate()
        return 1
    print('D-Bus service:', service)

    time.sleep(1)
    start = time.monotonic()
    if options.command:
        reply = qdbus(service, '/Sessions/1', 'io.github.vityas_off.kmux.Session.runCommand', options.command)
        print('runCommand:', reply or 'ok')

    shots = sorted(((float(seconds), name) for name, _, seconds in (shot.rpartition(':') for shot in options.shot)))
    for seconds, name in shots:
        time.sleep(max(0, start + seconds - time.monotonic()))
        path = f'{work_dir}/{name}.png'
        subprocess.run(['spectacle', '-b', '-n', '-f', '-o', path], env=environment, capture_output=True)
        print('screenshot:', path)
    time.sleep(max(0, start + options.duration - time.monotonic()))

    kmux.terminate()
    kmux.wait(timeout=10)
    return 0


if __name__ == '__main__':
    parsed = arguments()
    sys.exit(run_inside(parsed) if parsed.inside else run_outside(parsed))
