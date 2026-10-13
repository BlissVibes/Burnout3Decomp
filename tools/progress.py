#!/usr/bin/env python3
"""Reports how much of the game's own code is decompiled, from the symbol table and the owners table.

usage: progress.py <symbols.tsv> <owners.tsv> [--date YYYY-MM-DD]

Only code owned by the game counts: vendor libraries stay recompiled for good. The total is the
bytes of the owners-table ranges with owner game. Functions (kind func, owner game) are counted
by status; the bytes of a status are the sizes of its functions, and a function with size ? is
counted but adds no bytes (the summary says how many). Whatever is neither draft nor verified is
still the recompiled code, named in the table or not.

Prints two lines: a one-line summary, then a row for the table in docs/PROGRESS.md (date,
milestone, state, notes; --date defaults to today). Percentages are cut, not rounded, so 100.00%
means all of it.

Exit 0 on success, 1 when either table is invalid or they disagree, 2 for bad arguments or
unreadable input.
"""
import datetime
import importlib.util
import os
import sys

sys.dont_write_bytecode = True      # loading symdb.py must not leave a __pycache__ in the repo
spec = importlib.util.spec_from_file_location('symdb', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'symdb.py'))
symdb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(symdb)

MILESTONE = '4 Decomp into the scaffold'      # the milestone's name in docs/PROGRESS.md


def summarise(symbols, owners):
    """Returns the counts for two valid tables: game_bytes, game_ranges and, per status, functions, bytes, unsized."""
    ranges, _ = symdb.parse_owners(owners)
    game = [r for r in ranges if r.owner == 'game']
    stats = {'game_bytes': sum(r.end - r.start for r in game), 'game_ranges': len(game)}
    for status in symdb.STATUSES:
        stats[status] = {'functions': 0, 'bytes': 0, 'unsized': 0}
    for _, _, kind, size, _, owner, _, status, _ in symdb.rows_of(symbols):
        if kind != 'func' or owner != 'game':
            continue
        entry = stats[status]
        entry['functions'] += 1
        if size == '?':
            entry['unsized'] += 1
        else:
            entry['bytes'] += int(size, 16)
    stats['recomp']['bytes'] = max(stats['game_bytes'] - stats['draft']['bytes'] - stats['verified']['bytes'], 0)
    return stats


def percent(part, whole):
    return '%d.%02d%%' % divmod(part * 10000 // whole, 100) if whole else '0.00%'


def one_line(stats):
    def part(status):
        entry = stats[status]
        text = '%s %d function%s, %s bytes (%s)' % (status, entry['functions'], '' if entry['functions'] == 1 else 's',
                                                   format(entry['bytes'], ','), percent(entry['bytes'], stats['game_bytes']))
        return text + (', %d without a size' % entry['unsized'] if entry['unsized'] else '')

    recomp = stats['recomp']
    return 'game code %s bytes in %d range%s: %s; %s; recompiled %s bytes (%s), %d of its functions in the table' % (
        format(stats['game_bytes'], ','), stats['game_ranges'], '' if stats['game_ranges'] == 1 else 's', part('verified'), part('draft'),
        format(recomp['bytes'], ','), percent(recomp['bytes'], stats['game_bytes']), recomp['functions'])


def markdown_row(stats, date):
    return '| %s | %s | in progress | %s |' % (date, MILESTONE, one_line(stats))


def main(argv):
    args = argv[1:]
    date = datetime.date.today().isoformat()
    try:
        if '--date' in args:
            at = args.index('--date')
            date = args[at + 1]
            del args[at:at + 2]
        if len(args) != 2 or any(a.startswith('-') for a in args):
            raise IndexError
    except IndexError:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    try:
        symbols, owners = symdb.read(args[0]), symdb.read(args[1])
    except (OSError, ValueError) as e:
        print('progress: %s' % e, file=sys.stderr)
        return 2
    ranges, errors = symdb.parse_owners(owners)
    errors = ['%s: %s' % (args[1], e) for e in errors] or symdb.validate(symbols, ranges)
    if errors:
        return symdb.report(errors)
    stats = summarise(symbols, owners)
    print(one_line(stats))
    print(markdown_row(stats, date))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
