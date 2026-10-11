#!/usr/bin/env python3
"""Summarizes a ps2run / port.sh explore log and says whether the run was clean.

usage: scan-log.py <explore.log> [--expect-frames N] [--min-primitives N]
Exit 0 when no runtime gap was logged and the EE did not halt, 1 otherwise.
The runtime ends every run with "[ee] halted: <reason>" and, after its
statistics, "[ee] stopped after N frames, ...". The reason "frame limit
reached" is the normal end and does not count as a halt. With --expect-frames N
the run must also have stopped after at least N frames, so a log that just
stops is not clean. A run that reaches the frame limit having drawn fewer than
--min-primitives primitives (default 1, from "[gs] N primitives drawn") is not
clean either: the game hung without drawing.
The known gap strings come from ps2-web-recomp's runtime; any other
"[tag] ..." line the scanner has not seen is listed under "unknown tags" and
fails the run, so a new error string cannot pass silently. Tags may contain
hyphens ("[iop-tty]", the IOP modules' own text output, and "[iop-io]" are
known, as is "[loadfile]", one line per IOP module the game loads).
"""
import collections
import re
import sys

PATTERNS = {
    'unhandled_syscall': re.compile(r'^\[sys\] unhandled syscall (0x[0-9A-Fa-f]+ from 0x[0-9A-Fa-f]+)'),
    'iop_missing': re.compile(r'^\[iop\] missing (\S+ #\d+ called from 0x[0-9A-Fa-f]+)'),
    'ipu_unimplemented': re.compile(r'^\[ipu\] command (0x[0-9A-Fa-f]+) is not implemented'),
    'vu1_cmsar1': re.compile(r'^\[vu1\] start at (0x[0-9A-Fa-f]+) through CMSAR1'),
    'io_unsupported': re.compile(r'^\[io\] open (\S+) .* on unsupported device'),
}
HALT = re.compile(r'^\[ee\] halted: (.*)$')
FRAME_LIMIT = 'frame limit reached'
TAG = re.compile(r'^\[([a-z0-9]+(?:-[a-z0-9]+)*)\]')
KNOWN_TAGS = {'sys', 'iop', 'ipu', 'vu1', 'io', 'ee', 'disc', 'web', 'gpu', 'gs', 'spu2', 'dma', 'sif', 'mc', 'vu0', 'vif', 'cdvd', 'kernel', 'thread',
              'frame', 'dmac', 'gif', 'sema', 'vif0', 'vif1', 'loadfile', 'iop-tty', 'iop-io'}
STOPPED = re.compile(r'^\[ee\] stopped after (\d+) frames')
PRIMITIVES = re.compile(r'^\[gs\] (\d+) primitives drawn')


class Report:
    def __init__(self):
        self.gaps = {key: collections.Counter() for key in PATTERNS}
        self.halted = None
        self.frame_limit = False
        self.unknown_tags = collections.Counter()
        self.frames = None
        self.primitives = None

    def clean(self, expect_frames=None, min_primitives=1):
        if self.halted is not None or any(self.gaps.values()) or self.unknown_tags:
            return False
        if expect_frames is not None and (self.frames is None or self.frames < expect_frames):
            return False
        # A run can count frames to the limit while the game hangs and draws nothing.
        if self.frame_limit and self.primitives is not None and self.primitives < min_primitives:
            return False
        return True


def scan(lines):
    report = Report()
    for raw in lines:
        line = raw.rstrip('\r\n')
        halt = HALT.match(line)
        if halt:
            if halt.group(1) == FRAME_LIMIT:
                report.frame_limit = True
            else:
                report.halted = halt.group(1)
            continue
        stopped = STOPPED.match(line)
        if stopped:
            report.frames = int(stopped.group(1))
            continue
        primitives = PRIMITIVES.match(line)
        if primitives:
            report.primitives = int(primitives.group(1))
            continue
        matched = False
        for key, pattern in PATTERNS.items():
            m = pattern.match(line)
            if m:
                report.gaps[key][m.group(1)] += 1
                matched = True
                break
        if matched:
            continue
        tag = TAG.match(line)
        if tag and tag.group(1) not in KNOWN_TAGS:
            report.unknown_tags[tag.group(1)] += 1
    return report


def render(report, expect_frames=None, min_primitives=1):
    out = []
    total = sum(sum(c.values()) for c in report.gaps.values())
    out.append('frames reached: %s%s' % (report.frames if report.frames is not None else 'none', ' of %d' % expect_frames if expect_frames is not None else ''))
    out.append('halted: %s' % (report.halted or ('no (%s)' % FRAME_LIMIT if report.frame_limit else 'no')))
    out.append('primitives drawn: %s' % (report.primitives if report.primitives is not None else 'unknown'))
    out.append('gaps: %d distinct, %d total' % (sum(len(c) for c in report.gaps.values()), total))
    for key, counter in report.gaps.items():
        for site, count in counter.most_common():
            out.append('  %s  %s  x%d' % (key, site, count))
    if report.unknown_tags:
        out.append('unknown tags: ' + ', '.join('%s x%d' % kv for kv in report.unknown_tags.most_common()))
    out.append('result: ' + ('CLEAN' if report.clean(expect_frames, min_primitives) else 'GAPS'))
    return '\n'.join(out)


def main(argv):
    args = argv[1:]
    options = {'--expect-frames': None, '--min-primitives': 1}
    for flag in options:
        if flag in args:
            at = args.index(flag)
            try:
                options[flag] = int(args[at + 1])
            except (IndexError, ValueError):
                print(__doc__.strip(), file=sys.stderr)
                return 2
            del args[at:at + 2]
    if len(args) != 1:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    expect_frames, min_primitives = options['--expect-frames'], options['--min-primitives']
    with open(args[0], encoding='utf-8', errors='replace') as f:
        report = scan(f)
    print(render(report, expect_frames, min_primitives))
    return 0 if report.clean(expect_frames, min_primitives) else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
