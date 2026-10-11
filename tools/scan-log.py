#!/usr/bin/env python3
"""Summarizes a ps2run / port.sh explore log and says whether the run was clean.

usage: scan-log.py <explore.log>
Exit 0 when no runtime gap was logged and the EE did not halt, 1 otherwise.
The known gap strings come from ps2-web-recomp's runtime; any other
"[tag] ..." line the scanner has not seen is listed under "unknown tags" so a
new error string cannot pass silently.
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
TAG = re.compile(r'^\[([a-z0-9]+)\]')
KNOWN_TAGS = {'sys', 'iop', 'ipu', 'vu1', 'io', 'ee', 'disc', 'web', 'gpu', 'gs', 'spu2', 'dma', 'sif', 'mc', 'vu0', 'vif', 'cdvd', 'kernel', 'thread'}
FRAME = re.compile(r'^\[ee\] frame (\d+)')


class Report:
    def __init__(self):
        self.gaps = {key: collections.Counter() for key in PATTERNS}
        self.halted = None
        self.unknown_tags = collections.Counter()
        self.frames = None

    def clean(self):
        return self.halted is None and not any(self.gaps.values())


def scan(lines):
    report = Report()
    for raw in lines:
        line = raw.rstrip('\r\n')
        halt = HALT.match(line)
        if halt:
            report.halted = halt.group(1)
            continue
        frame = FRAME.match(line)
        if frame:
            report.frames = int(frame.group(1))
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


def render(report):
    out = []
    total = sum(sum(c.values()) for c in report.gaps.values())
    out.append('frames reached: %s' % (report.frames if report.frames is not None else 'unknown'))
    out.append('halted: %s' % (report.halted or 'no'))
    out.append('gaps: %d distinct, %d total' % (sum(len(c) for c in report.gaps.values()), total))
    for key, counter in report.gaps.items():
        for site, count in counter.most_common():
            out.append('  %s  %s  x%d' % (key, site, count))
    if report.unknown_tags:
        out.append('unknown tags: ' + ', '.join('%s x%d' % kv for kv in report.unknown_tags.most_common()))
    out.append('result: ' + ('CLEAN' if report.clean() else 'GAPS'))
    return '\n'.join(out)


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    with open(argv[1], encoding='utf-8', errors='replace') as f:
        report = scan(f)
    print(render(report))
    return 0 if report.clean() else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
