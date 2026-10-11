#!/usr/bin/env python3
"""Stops a headless run that writes too much log or runs too long.

usage: watch-run.py --log FILE --match TEXT --marker FILE
                    [--name ps2run] [--cap-bytes N] [--timeout SECONDS] [--interval SECONDS]
Linux only (reads /proc). Runs until it is killed; tools/port.sh starts it in
the background and kills it when the port is done. Every --interval seconds it
looks for processes whose program is --name and whose command line contains
--match, and kills them when
  - FILE has grown past --cap-bytes ("log cap hit"), or
  - the process has been watched for more than --timeout seconds ("run timeout hit").
Each kill appends one line to the marker file and prints it to stderr. A value
of 0 turns that limit off. The time limit counts from when the watcher first
sees the process, so build time before the run does not count.
"""
import os
import signal
import sys
import time

DEFAULT_CAP_BYTES = 1 << 30
DEFAULT_TIMEOUT = 30 * 60


def matching_pids(name, match, proc='/proc'):
    found = []
    for entry in os.listdir(proc):
        if not entry.isdigit() or int(entry) == os.getpid():
            continue
        try:
            with open(os.path.join(proc, entry, 'cmdline'), 'rb') as f:
                argv = f.read().decode('utf-8', 'replace').split('\0')
        except OSError:
            continue
        if argv and os.path.basename(argv[0]) == name and match in ' '.join(argv):
            found.append(int(entry))
    return found


def verdict(log_bytes, watched_seconds, cap_bytes, timeout):
    """Why a run must stop, or None while it is within both limits."""
    if cap_bytes and log_bytes > cap_bytes:
        return 'log cap hit: %d bytes of log, cap %d' % (log_bytes, cap_bytes)
    if timeout and watched_seconds > timeout:
        return 'run timeout hit: %d seconds, limit %d' % (watched_seconds, timeout)
    return None


def log_size(path):
    try:
        return os.path.getsize(path)
    except OSError:
        return 0


def watch(log, name, match, marker, cap_bytes, timeout, interval):
    first_seen = {}
    while True:
        now = time.monotonic()
        pids = matching_pids(name, match)
        for pid in list(first_seen):
            if pid not in pids:
                del first_seen[pid]
        for pid in pids:
            started = first_seen.setdefault(pid, now)
            reason = verdict(log_size(log), now - started, cap_bytes, timeout)
            if reason is None:
                continue
            try:
                os.kill(pid, signal.SIGKILL)
            except OSError:
                continue
            line = '%s (killed %s, pid %d)' % (reason, name, pid)
            with open(marker, 'a') as f:
                f.write(line + '\n')
            print(line, file=sys.stderr, flush=True)
        time.sleep(interval)


def main(argv):
    options = {'--log': None, '--match': None, '--marker': None, '--name': 'ps2run',
               '--cap-bytes': str(DEFAULT_CAP_BYTES), '--timeout': str(DEFAULT_TIMEOUT), '--interval': '2'}
    args = argv[1:]
    try:
        if len(args) % 2:
            raise ValueError
        for flag, value in zip(args[0::2], args[1::2]):
            if flag not in options:
                raise ValueError
            options[flag] = value
        if None in options.values():
            raise ValueError
        cap_bytes, timeout, interval = int(options['--cap-bytes']), int(options['--timeout']), float(options['--interval'])
    except ValueError:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    watch(options['--log'], options['--name'], options['--match'], options['--marker'], cap_bytes, timeout, interval)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
