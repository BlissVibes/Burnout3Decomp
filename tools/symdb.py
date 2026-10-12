#!/usr/bin/env python3
"""Checks the symbol database and writes the flat files other tools read from it.

usage: symdb.py validate <symbols.tsv> [--owners <owners.tsv>]
       symdb.py export-splat <symbols.tsv> [-o FILE]     name = 0xADDR; // type:func size:0x..
       symdb.py export-names <symbols.tsv> [-o FILE]     0xADDR name, functions and labels
       symdb.py export-hooks <symbols.tsv> [-o FILE]     0xADDR name, functions with status verified
       symdb.py import-anchors <symbols.tsv> <anchors.json> [--owners <owners.tsv>] [-o FILE]

The symbol table is tab-separated with LF line endings, a header row and one symbol per line,
sorted by address:

  address  name  kind  size  type  owner  evidence  status  note

address is 0x followed by eight upper-case hex digits. name is a C identifier, unique in the
table; a name ending in _XXXXXXXX (fn_0021BFA0 for a function nobody has named yet) must carry
its own address. kind is one of func data rodata bss vtable jtbl ctor label string. size is
upper-case hex (0x70) or ? when unknown; a sized symbol must end at or before the next symbol,
except that labels may sit inside it. type is a C prototype or type and may be empty. owner is
one of game rw rwa sce crt dirtysock logitech vu. evidence is one or more of the letters C
(oracle-confirmed behaviour), S (string or constant anchor), X (call graph or cross-reference),
R (reB3 crosswalk inference), G (guess). status is recomp, draft or verified; verified is for
functions and needs evidence C.

The owners table lists address ranges (start inclusive, end exclusive, sorted, not overlapping)
with the owner of the code in them: start end owner conf what, conf being high, medium or low.
With --owners, validate also reports any symbol whose owner disagrees with the range it lies in.

import-anchors reads the --json output of elf-anchors.py and adds a row for every string, float,
word or byte run it found at an address the table does not have yet. Without -o it rewrites the
table in place. The exports write to standard output unless -o is given.

Exit 0 on success, 1 when the table is invalid, 2 for bad arguments or unreadable input.
"""
import bisect
import collections
import json
import re
import sys

COLUMNS = ['address', 'name', 'kind', 'size', 'type', 'owner', 'evidence', 'status', 'note']
KINDS = ['func', 'data', 'rodata', 'bss', 'vtable', 'jtbl', 'ctor', 'label', 'string']
OWNERS = ['game', 'rw', 'rwa', 'sce', 'crt', 'dirtysock', 'logitech', 'vu']
EVIDENCE = 'CSXRG'
STATUSES = ['recomp', 'draft', 'verified']
OWNER_COLUMNS = ['start', 'end', 'owner', 'conf', 'what']
CONFIDENCE = ['high', 'medium', 'low']

ADDRESS = re.compile(r'0x[0-9A-F]{8}\Z')
IDENTIFIER = re.compile(r'[A-Za-z_][A-Za-z0-9_]*\Z')
PLACEHOLDER = re.compile(r'_([0-9A-F]{8})\Z')
SIZE = re.compile(r'0x[0-9A-F]+\Z')
SPLAT_KINDS = {'func': 'func', 'label': 'label', 'jtbl': 'jtbl', 'string': 'asciz'}
SPLAT_SCALARS = ['s8', 'u8', 's16', 'u16', 's32', 'u32', 's64', 'u64', 'f32', 'f64']
CODE_KINDS = ['func', 'label']

Range = collections.namedtuple('Range', 'start end owner conf what')


def lines_of(text, errors):
    """Splits a table into its lines, reporting CR endings, a missing final newline and blank lines."""
    if '\r' in text:
        errors.append('CR line endings: the table must use LF')
        text = text.replace('\r', '')
    if not text.endswith('\n'):
        errors.append('no newline at the end of the last line')
    lines = text.split('\n')
    if lines and lines[-1] == '':
        lines.pop()
    for number, line in enumerate(lines, 1):
        if not line.strip():
            errors.append('line %d: blank line' % number)
    return lines


def parse_owners(text):
    """Returns (ranges, errors) for an owners table."""
    errors, ranges = [], []
    lines = lines_of(text, errors)
    if not lines or lines[0].split('\t') != OWNER_COLUMNS:
        errors.append('line 1: header must be ' + ' '.join(OWNER_COLUMNS))
    for number, line in enumerate(lines[1:], 2):
        if not line.strip():
            continue
        fields = line.split('\t')
        if len(fields) != len(OWNER_COLUMNS):
            errors.append('line %d: %d columns, expected %d' % (number, len(fields), len(OWNER_COLUMNS)))
            continue
        start, end, owner, conf, what = fields
        if not ADDRESS.match(start) or not ADDRESS.match(end):
            errors.append('line %d: start and end must be 0x and eight upper-case hex digits' % number)
            continue
        entry = Range(int(start, 16), int(end, 16), owner, conf, what)
        if entry.end <= entry.start:
            errors.append('line %d: range %s-%s is empty' % (number, start, end))
        if ranges and entry.start < ranges[-1].end:
            errors.append('line %d: range %s-%s starts before the previous one ends' % (number, start, end))
        if owner not in OWNERS:
            errors.append('line %d: owner %s is not one of %s' % (number, owner, ' '.join(OWNERS)))
        if conf not in CONFIDENCE:
            errors.append('line %d: conf %s is not one of %s' % (number, conf, ' '.join(CONFIDENCE)))
        ranges.append(entry)
    return ranges, errors


def owner_at(ranges, address):
    """The owner of the range holding address, or None."""
    at = bisect.bisect_right([r.start for r in ranges], address)
    return ranges[at - 1].owner if at and address < ranges[at - 1].end else None


def rows_of(text):
    """The data rows of a valid table as lists of nine fields."""
    return [line.split('\t') for line in text.split('\n')[1:] if line]


def validate(text, ranges=None):
    """Returns every problem in a symbol table as a list of messages; empty when it is valid."""
    errors = []
    lines = lines_of(text, errors)
    if not lines or lines[0].split('\t') != COLUMNS:
        errors.append('line 1: header must be ' + ' '.join(COLUMNS))
    seen, previous, sized = {}, None, []
    for number, line in enumerate(lines[1:], 2):
        if not line.strip():
            continue
        fields = line.split('\t')
        if len(fields) != len(COLUMNS):
            errors.append('line %d: %d columns, expected %d' % (number, len(fields), len(COLUMNS)))
            continue
        address, name, kind, size, _, owner, evidence, status, _ = fields

        def bad(message):
            errors.append('line %d: %s' % (number, message))

        value = int(address, 16) if ADDRESS.match(address) else None
        if value is None:
            bad('address %s is not 0x and eight upper-case hex digits' % address)
        else:
            if previous is not None and value <= previous:
                bad('address %s is not above the previous row (0x%08X)' % (address, previous))
            previous = value
        if not IDENTIFIER.match(name):
            bad('name %r is not a C identifier' % name)
        elif name in seen:
            bad('name %s is already used on line %d' % (name, seen[name]))
        else:
            seen[name] = number
            placeholder = PLACEHOLDER.search(name)
            if placeholder and value is not None and int(placeholder.group(1), 16) != value:
                bad('placeholder name %s does not match address %s' % (name, address))
        if kind not in KINDS:
            bad('kind %s is not one of %s' % (kind, ' '.join(KINDS)))
        length = None
        if size != '?':
            if SIZE.match(size) and int(size, 16):
                length = int(size, 16)
            else:
                bad('size %r is neither ? nor non-zero upper-case hex such as 0x70' % size)
        if owner not in OWNERS:
            bad('owner %s is not one of %s' % (owner, ' '.join(OWNERS)))
        elif ranges is not None and value is not None and owner_at(ranges, value) not in (None, owner):
            bad('%s is owner %s but %s lies in a range owned by %s' % (name, owner, address, owner_at(ranges, value)))
        if not evidence or len(set(evidence)) != len(evidence) or set(evidence) - set(EVIDENCE):
            bad('evidence %r is not one or more of the letters %s, each once' % (evidence, ' '.join(EVIDENCE)))
        if status not in STATUSES:
            bad('status %s is not one of %s' % (status, ' '.join(STATUSES)))
        elif status == 'verified' and kind != 'func':
            bad('status verified is for kind func, not %s' % kind)
        elif status == 'verified' and 'C' not in evidence:
            bad('status verified needs evidence C')
        if value is not None:
            sized.append((value, length, name, kind, number))
    for at, (value, length, name, _, number) in enumerate(sized):
        following = next((s for s in sized[at + 1:] if s[3] != 'label'), None)
        if length and following and value + length > following[0]:
            errors.append('line %d: %s (0x%08X size 0x%X) overlaps %s at 0x%08X' % (number, name, value, length, following[2], following[0]))
    return sorted(errors, key=lambda e: int(e[5:e.index(':')]) if e.startswith('line ') else 0)


def export_splat(text):
    """The table as splat symbol_addrs lines."""
    out = []
    for address, name, kind, size, type_, *_ in rows_of(text):
        attributes = []
        if kind in SPLAT_KINDS:
            attributes.append('type:' + SPLAT_KINDS[kind])
        elif type_ in SPLAT_SCALARS:
            attributes.append('type:' + type_)
        if size != '?':
            attributes.append('size:' + size)
        out.append('%s = %s;%s\n' % (name, address, ' // ' + ' '.join(attributes) if attributes else ''))
    return ''.join(out)


def export_names(text):
    """Address and name of every function and label, for the recompiler's --names."""
    return ''.join('%s %s\n' % (row[0], row[1]) for row in rows_of(text) if row[2] in CODE_KINDS)


def export_hooks(text):
    """Address and name of every function whose decompiled version is verified."""
    return ''.join('%s %s\n' % (row[0], row[1]) for row in rows_of(text) if row[2] == 'func' and row[7] == 'verified')


def import_anchors(text, anchors_json, ranges=None):
    """Adds a row per anchor hit at an address the table lacks. Returns (new table, rows added)."""
    rows = rows_of(text)
    addresses = {int(row[0], 16) for row in rows}
    names = {row[1] for row in rows}
    added = []
    for result in json.loads(anchors_json):
        kind, _, body = result['anchor'].partition(':')
        for hit in result['hits']:
            address = hit['address']
            if not isinstance(address, int) or address in addresses:
                continue
            tail = '%08X' % address
            if kind == 'str':
                slug = re.sub(r'[^a-z0-9]+', '_', body.lower()).strip('_')[:40].rstrip('_')
                name = 'str_' + (slug or tail)
                if name in names:
                    name += '_' + tail
                row_kind, length, type_ = 'string', len(body) + 1, 'char[%d]' % (len(body) + 1)
            elif kind in ('f32', 'u32'):
                name, row_kind, length, type_ = '%s_%s' % (kind, tail), 'rodata', 4, kind
            else:
                length = len(bytes.fromhex(body))
                name, row_kind, type_ = 'dat_' + tail, 'rodata', 'u8[%d]' % length
            functions = [ref['function'] for ref in hit['refs'] if ref.get('function') is not None]
            owner = (owner_at(ranges, functions[0]) if ranges and functions else None) or 'game'
            count = len(hit['refs'])
            if not count:
                note = 'no code reference found'
            else:
                note = '%d code reference%s' % (count, '' if count == 1 else 's')
                if functions:
                    note += ', first from the function near 0x%08X' % functions[0]
            added.append(['0x' + tail, name, row_kind, '0x%X' % length, type_, owner, 'S', 'recomp', note])
            addresses.add(address)
            names.add(name)
    if not added:
        return text, 0
    rows = sorted(rows + added, key=lambda row: int(row[0], 16))
    return '\n'.join(['\t'.join(COLUMNS)] + ['\t'.join(row) for row in rows]) + '\n', len(added)


def read(path):
    with open(path, 'rb') as f:
        return f.read().decode('utf-8')


def write(path, text):
    if path is None:
        raw = getattr(sys.stdout, 'buffer', None)      # bytes, so Windows does not turn LF into CRLF
        if raw:
            sys.stdout.flush()
            raw.write(text.encode('utf-8'))
        else:
            sys.stdout.write(text)
        return
    with open(path, 'wb') as f:
        f.write(text.encode('utf-8'))


def report(errors):
    for error in errors:
        print(error, file=sys.stderr)
    return 1


def main(argv):
    exports = {'export-splat': export_splat, 'export-names': export_names, 'export-hooks': export_hooks}
    args = argv[1:]
    options = {'--owners': None, '-o': None}
    try:
        for flag in options:
            if flag in args:
                at = args.index(flag)
                options[flag] = args[at + 1]
                del args[at:at + 2]
        command = args[0]
        allowed = {'validate': (1, ['--owners']), 'import-anchors': (2, ['--owners', '-o'])}.get(command, (1, ['-o']))
        if command not in exports and command not in ('validate', 'import-anchors'):
            raise IndexError
        if len(args) != 1 + allowed[0] or any(a.startswith('-') for a in args) or any(v and f not in allowed[1] for f, v in options.items()):
            raise IndexError
    except IndexError:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    try:
        text = read(args[1])
        ranges = None
        if options['--owners']:
            ranges, errors = parse_owners(read(options['--owners']))
            if errors:
                return report('%s: %s' % (options['--owners'], e) for e in errors)
        errors = validate(text, ranges if command == 'validate' else None)
        if errors:
            return report(errors)
        if command == 'validate':
            print('ok: %d symbols' % len(rows_of(text)))
        elif command in exports:
            write(options['-o'], exports[command](text))
        else:
            merged, count = import_anchors(text, read(args[2]), ranges)
            errors = validate(merged)
            if errors:
                return report(errors)
            write(options['-o'] or args[1], merged)
            print('added %d symbol%s, %d in all' % (count, '' if count == 1 else 's', len(rows_of(merged))))
    except (OSError, ValueError, KeyError, TypeError) as e:
        print('symdb: %s' % e, file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
