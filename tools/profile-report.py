#!/usr/bin/env python3
"""Turns a profile written by the fork's ps2run (PS2_PROFILE=<file>) into tables of where the time went.

usage: profile-report.py <profile> <ps2run> [--top N] [--guest <generated dir>] [--symbols <SLUS tsv>] [--json]

The profiler samples the whole process once per millisecond of CPU time and counts hits per 16 bytes of
the executable (lines "<offset hex> <hits>"), hits in other libraries by symbol ("@ <hits> <name>"), and
for those the caller inside the executable ("^ <offset hex> <hits>"). This script names the executable's
buckets with `nm` and adds them up:

  * by kind: generated EE code (gen::unit_*), generated or interpreted VU code, the GS and its
    rasteriser, the IOP and its devices, the kernel and the rest of the runtime, other libraries;
  * by host function, the top N.

With --guest and a ps2run built with line tables (-gline-tables-only), time inside generated EE units is
also given per guest function: each bucket's source line (addr2line) is looked up in the generated
unit, the nearest label above it gives the guest address, and the nearest function entry at or below
that address (the entry switch's `enter(c, 0x...)` arms, or any label if the code was generated without
them) names the function. --symbols adds names from the symbol table (game/symbols/SLUS_210.50.tsv).

Everything printed is addresses, names from our own symbol table and percentages. The profile and the
executable are game-derived: keep them outside the repository.
"""
import bisect, collections, json, os, re, subprocess, sys

KINDS = [
    ('EE generated code', re.compile(r'^gen::unit_|^gen::')),
    ('VU', re.compile(r'vu1_|vu0_|ps2::vu|vu_micro|vu_jit|ps2::vif|\bvu::')),
    ('GS and rasteriser', re.compile(r'ps2::gs|gs::|raster|ps2::gif|gpu')),
    ('IOP and devices', re.compile(r'ps2::iop|iop::')),
    ('IPU', re.compile(r'ipu')),
    ('hooks', re.compile(r'^b3::|ee::abi|ps2::enter|ps2::offer|hook')),
    ('runtime', re.compile(r'^ps2::|^host|^main')),
]


def kind_of(name):
    for kind, pattern in KINDS:
        if pattern.search(name):
            return kind
    return 'other'


def read_profile(path):
    inside, callers, outside, samples = {}, {}, {}, 0
    with open(path) as f:
        for line in f:
            parts = line.split()
            if not parts:
                continue
            if parts[0] == '#':
                samples = int(parts[2])
            elif parts[0] == '@':
                outside[' '.join(parts[2:])] = outside.get(' '.join(parts[2:]), 0) + int(parts[1])
            elif parts[0] == '^':
                callers[int(parts[1], 16)] = int(parts[2])
            else:
                inside[int(parts[0], 16)] = int(parts[1])
    return inside, callers, outside, samples


def read_symbols(binary):
    """Sorted (address, name) of the executable's functions."""
    out = subprocess.run(['nm', '-C', '--defined-only', '-n', binary], capture_output=True, text=True, check=True).stdout
    symbols = []
    for line in out.splitlines():
        parts = line.split(None, 2)
        if len(parts) == 3 and parts[1] in 'tTwW':
            symbols.append((int(parts[0], 16), parts[2]))
    return symbols


def name_at(symbols, starts, offset):
    i = bisect.bisect_right(starts, offset) - 1
    return symbols[i][1] if i >= 0 else '?'


def short(name, width=78):
    name = re.sub(r'\(.*', '', name)
    return name if len(name) <= width else name[:width - 3] + '...'


def guest_table(binary, generated, inside, symbols, starts):
    """Hits per guest function for buckets inside generated EE units."""
    units = {}

    def unit(path):
        if path not in units:
            labels, entries = [], []   # (line number, guest address); sorted function entry addresses
            try:
                with open(path) as f:
                    for number, line in enumerate(f, 1):
                        m = re.match(r'L_([0-9A-Fa-f]{8}):', line)
                        if m:
                            labels.append((number, int(m.group(1), 16)))
                        m = re.search(r'enter\(c, 0x([0-9A-Fa-f]{8})u\)', line)
                        if m:
                            entries.append(int(m.group(1), 16))
            except OSError:
                pass
            units[path] = (labels, sorted(entries) or sorted(a for _, a in labels))
        return units[path]

    wanted = [o for o in inside if name_at(symbols, starts, o).startswith('gen::unit_')]
    if not wanted:
        return {}, 0
    query = '\n'.join('0x%x' % o for o in wanted)
    out = subprocess.run(['addr2line', '-e', binary], input=query, capture_output=True, text=True, check=True).stdout.splitlines()
    hits, unknown = collections.Counter(), 0
    for offset, where in zip(wanted, out):
        path, _, number = where.rpartition(':')
        number = int(re.match(r'\d+', number).group(0)) if re.match(r'\d+', number) else 0
        if not number or '?' in path:
            unknown += inside[offset]
            continue
        labels, entries = unit(os.path.join(generated, os.path.basename(path)))
        i = bisect.bisect_right([n for n, _ in labels], number) - 1
        if i < 0 or not entries:
            unknown += inside[offset]
            continue
        address = labels[i][1]
        j = bisect.bisect_right(entries, address) - 1
        hits[entries[j] if j >= 0 else address] += inside[offset]
    return hits, unknown


def read_names(path):
    names = {}
    if path:
        with open(path, encoding='utf-8') as f:
            header = f.readline().rstrip('\n').split('\t')
            for line in f:
                row = dict(zip(header, line.rstrip('\n').split('\t')))
                try:
                    names[int(row.get('address', '0'), 16)] = row.get('name', '')
                except ValueError:
                    pass
    return names


def main(argv):
    args, top, generated, table, as_json = [], 25, None, None, False
    i = 1
    while i < len(argv):
        a = argv[i]
        if a == '--top' and i + 1 < len(argv):
            top = int(argv[i + 1]); i += 2
        elif a == '--guest' and i + 1 < len(argv):
            generated = argv[i + 1]; i += 2
        elif a == '--symbols' and i + 1 < len(argv):
            table = argv[i + 1]; i += 2
        elif a == '--json':
            as_json = True; i += 1
        elif a.startswith('--'):
            sys.stderr.write(__doc__.split('\n\n')[1] + '\n'); return 2
        else:
            args.append(a); i += 1
    if len(args) != 2:
        sys.stderr.write('usage: profile-report.py <profile> <ps2run> [--top N] [--guest <generated dir>] [--symbols <tsv>] [--json]\n')
        return 2
    try:
        inside, callers, outside, samples = read_profile(args[0])
        symbols = read_symbols(args[1])
    except (OSError, subprocess.CalledProcessError) as error:
        sys.stderr.write('%s\n' % error)
        return 1
    starts = [a for a, _ in symbols]
    by_function, by_kind = collections.Counter(), collections.Counter()
    for offset, hits in inside.items():
        name = name_at(symbols, starts, offset)
        by_function[name] += hits
        by_kind[kind_of(name)] += hits
    for name, hits in outside.items():
        by_function['[lib] ' + name] += hits
        by_kind['other libraries'] += hits
    total = sum(by_function.values()) or 1
    report = {'samples': samples, 'counted': total,
              'kinds': {k: v for k, v in by_kind.most_common()},
              'functions': [(n, h) for n, h in by_function.most_common(top)]}
    guest, unknown = ({}, 0)
    if generated:
        guest, unknown = guest_table(args[1], generated, inside, symbols, starts)
        names = read_names(table)
        report['guest'] = [('0x%08X' % a, names.get(a, ''), h) for a, h in guest.most_common(top)]
        report['guest_unknown'] = unknown
    if as_json:
        print(json.dumps(report, indent=1))
        return 0
    print('%d samples (%d counted)' % (samples, total))
    print('\nby kind:')
    for kind, hits in by_kind.most_common():
        print('  %5.1f%%  %s' % (100.0 * hits / total, kind))
    print('\ntop %d host functions:' % top)
    for name, hits in by_function.most_common(top):
        print('  %5.1f%%  %s' % (100.0 * hits / total, short(name)))
    if generated:
        inside_units = sum(guest.values()) + unknown or 1
        print('\ntop %d guest functions (share of the time in generated EE code; %.1f%% not attributed):' % (top, 100.0 * unknown / inside_units))
        for address, name, hits in report['guest']:
            print('  %5.1f%%  %s  %s' % (100.0 * hits / inside_units, address, name))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
