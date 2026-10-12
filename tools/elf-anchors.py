#!/usr/bin/env python3
"""Finds anchors (strings, float constants, 32-bit words, byte runs) in a MIPS ELF and the code that refers to them.

usage: elf-anchors.py <program.elf> <query file> [--gp 0xADDR] [--max-refs N] [--json]

The query file holds one anchor per line (blank lines and lines starting with # are skipped):
  str:<text>     an ASCII string, matched with its terminating NUL
  f32:<value>    a float constant, matched as its exact IEEE bit pattern
  u32:<hex>      a 32-bit word
  bytes:<hex>    a byte sequence at any offset
f32 and u32 are matched as little-endian words at 4-byte alignment.

For every occurrence in the loaded segments the tool lists the instructions that form its
address: `lui rX, hi` followed within 8 instructions by addiu/ori or a load/store through
`lo(rX)`, and, with --gp, addiu/loads/stores relative to gp. f32 and u32 values are also
looked for as immediates built in code (lui + ori/addiu, or lui + mtc1 for a float whose
low half is zero); those rows show "imm" in the hit column. Each reference is given with
the start of its function, taken as the nearest `addiu sp, sp, -N` at or before it, which
is approximate: a function with no stack frame is credited to the one before it.

Exit 0 whether or not anything was found, 2 for bad arguments or input. The ELF must be
ELF32 little-endian MIPS. The output describes the game's executable, so keep it out of
the repository.
"""
import bisect
import collections
import json
import struct
import sys

WINDOW = 8                      # instructions after a lui in which its low half may follow
LUI, ADDIU, ORI, COP1, GP = 0x0F, 0x09, 0x0D, 0x11, 28
MEMORY = {0x20: 'lb', 0x21: 'lh', 0x23: 'lw', 0x24: 'lbu', 0x25: 'lhu', 0x27: 'lwu', 0x28: 'sb', 0x29: 'sh', 0x2B: 'sw',
          0x31: 'lwc1', 0x39: 'swc1', 0x37: 'ld', 0x3F: 'sd', 0x1E: 'lq', 0x1F: 'sq'}
LOW = {**MEMORY, ADDIU: 'addiu', ORI: 'ori'}
IMMEDIATE = ('addiu', 'ori')

Segment = collections.namedtuple('Segment', 'vaddr data executable')
Anchor = collections.namedtuple('Anchor', 'text kind pattern value aligned')


def load(data):
    """Returns the PT_LOAD segments of an ELF32 little-endian MIPS file that have bytes in the file."""
    if data[:4] != b'\x7fELF':
        raise ValueError('not an ELF file')
    if len(data) < 52 or data[4] != 1 or data[5] != 1:
        raise ValueError('not a 32-bit little-endian ELF')
    machine, = struct.unpack_from('<H', data, 18)
    if machine != 8:
        raise ValueError('not a MIPS ELF (machine %d)' % machine)
    phoff, = struct.unpack_from('<I', data, 28)
    phentsize, phnum = struct.unpack_from('<HH', data, 42)
    segments = []
    for i in range(phnum):
        p_type, p_offset, p_vaddr, _, p_filesz, _, p_flags = struct.unpack_from('<7I', data, phoff + i * phentsize)
        if p_type == 1 and p_filesz:
            segments.append(Segment(p_vaddr, data[p_offset:p_offset + p_filesz], bool(p_flags & 1)))
    if not segments:
        raise ValueError('no loadable segments')
    return segments


def parse_query(lines):
    """Turns query lines into anchors; raises ValueError naming the first bad line."""
    anchors = []
    for raw in lines:
        line = raw.rstrip('\r\n')
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        kind, colon, body = line.partition(':')
        try:
            if not colon or not body:
                raise ValueError
            if kind == 'str':
                anchors.append(Anchor(line, kind, body.encode('ascii') + b'\0', None, False))
            elif kind == 'f32':
                pattern = struct.pack('<f', float(body))
                anchors.append(Anchor(line, kind, pattern, struct.unpack('<I', pattern)[0], True))
            elif kind == 'u32':
                value = int(body, 16)
                anchors.append(Anchor(line, kind, struct.pack('<I', value), value, True))
            elif kind == 'bytes':
                anchors.append(Anchor(line, kind, bytes.fromhex(body), None, False))
            else:
                raise ValueError
        except (ValueError, struct.error, OverflowError):
            raise ValueError('bad anchor line: %s' % line) from None
    return anchors


class Index:
    """Every address or immediate formed in code, the pcs of float immediates, and the function starts."""

    def __init__(self, segments, gp=None):
        self.refs = collections.defaultdict(list)           # value -> [(pc, via)]
        self.float_luis = collections.defaultdict(list)     # value with a zero low half -> [pc of the lui]
        self.prologues = []
        for segment in [s for s in segments if s.executable] or segments:
            self.scan(segment, gp)
        self.prologues.sort()

    def scan(self, segment, gp):
        count = len(segment.data) // 4
        words = struct.unpack_from('<%dI' % count, segment.data)
        for i, word in enumerate(words):
            op = word >> 26
            if op == LUI:
                self.pair(words, i, count, segment.vaddr)
            elif word & 0xFFFF8000 == 0x27BD8000:            # addiu sp, sp, -N
                self.prologues.append(segment.vaddr + i * 4)
            elif gp is not None and (word >> 21) & 31 == GP and op in LOW and op != ORI:
                target = (gp + (word & 0xFFFF) - ((word & 0x8000) << 1)) & 0xFFFFFFFF
                self.refs[target].append((segment.vaddr + i * 4, 'gp ' + LOW[op]))

    def pair(self, words, i, count, vaddr):
        register, high = (words[i] >> 16) & 31, (words[i] & 0xFFFF) << 16
        if register == 0:
            return
        for j in range(i + 1, min(i + 1 + WINDOW, count)):
            word = words[j]
            op, rs, rt = word >> 26, (word >> 21) & 31, (word >> 16) & 31
            if op == LUI and rt == register:
                return                                       # loaded again: the first lui is dead
            if op == COP1 and rs == 4 and rt == register:
                self.float_luis[high].append(vaddr + i * 4)  # mtc1 register, fN
            elif op in LOW and rs == register:
                low = word & 0xFFFF
                value = high | low if op == ORI else (high + low - ((low & 0x8000) << 1)) & 0xFFFFFFFF
                self.refs[value].append((vaddr + j * 4, LOW[op]))
                if op in (ADDIU, ORI) and rt == register:
                    return                                   # the register now holds the full value

    def function_start(self, pc):
        at = bisect.bisect_right(self.prologues, pc)
        return self.prologues[at - 1] if at else None


def occurrences(segments, anchor):
    found = []
    for segment in segments:
        at = segment.data.find(anchor.pattern)
        while at >= 0:
            if not anchor.aligned or (segment.vaddr + at) % 4 == 0:
                found.append(segment.vaddr + at)
            at = segment.data.find(anchor.pattern, at + 1)
    return sorted(found)


def search(segments, anchors, gp=None, max_refs=None):
    """Returns one result per anchor: its hits, each with its code references, and the totals."""
    index = Index(segments, gp)
    results = []
    for anchor in anchors:
        hits = [(address, sorted(index.refs.get(address, ()))) for address in occurrences(segments, anchor)]
        if anchor.value is not None:
            built = [(pc, via) for pc, via in index.refs.get(anchor.value, ()) if via in IMMEDIATE]
            if anchor.kind == 'f32' and anchor.value & 0xFFFF == 0:
                built += [(pc, 'lui+mtc1') for pc in index.float_luis.get(anchor.value, ())]
            if built:
                hits.append(('imm', sorted(built)))
        functions, total, left, listed = set(), 0, max_refs, []
        for address, found in hits:
            total += len(found)
            functions.update(index.function_start(pc) for pc, _ in found)
            shown = found if left is None else found[:left]
            if left is not None:
                left -= len(shown)
            listed.append({'address': address, 'refs': [{'pc': pc, 'via': via, 'function': index.function_start(pc)} for pc, via in shown]})
        results.append({'anchor': anchor.text, 'hits': listed, 'references': total, 'functions': len(functions)})
    return results


def render(results):
    def hexed(value):
        return '0x%08X' % value if isinstance(value, int) else (value or '-')

    def plural(count, noun):
        return '%d %s%s' % (count, noun, '' if count == 1 else 's')

    width = max([len(r['anchor']) for r in results] + [6])
    row = '%-' + str(width) + 's  %-10s  %-10s  %-8s  %s'
    out = [row % ('anchor', 'hit', 'ref pc', 'via', 'function (approx)')]
    for result in results:
        for hit in result['hits']:
            for ref in hit['refs'] or [{'pc': None, 'via': '-', 'function': None}]:
                out.append((row % (result['anchor'], hexed(hit['address']), hexed(ref['pc']), ref['via'], hexed(ref['function']))).rstrip())
    out.append('')
    for result in results:
        data_hits = sum(1 for hit in result['hits'] if hit['address'] != 'imm')
        out.append('%s: %s, %s, %s' % (result['anchor'], plural(data_hits, 'hit'), plural(result['references'], 'reference'),
                                       plural(result['functions'], 'function')))
    return '\n'.join(out)


def main(argv):
    args = argv[1:]
    as_json = '--json' in args
    if as_json:
        args.remove('--json')
    options = {'--gp': None, '--max-refs': None}
    try:
        for flag in options:
            if flag in args:
                at = args.index(flag)
                options[flag] = int(args[at + 1], 16 if flag == '--gp' else 10)
                del args[at:at + 2]
        if len(args) != 2 or args[0].startswith('--'):
            raise IndexError
    except (IndexError, ValueError):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    try:
        with open(args[0], 'rb') as f:
            segments = load(f.read())
        with open(args[1], encoding='utf-8') as f:
            anchors = parse_query(f)
    except (OSError, ValueError, struct.error) as e:
        print('elf-anchors: %s' % e, file=sys.stderr)
        return 2
    results = search(segments, anchors, options['--gp'], options['--max-refs'])
    print(json.dumps(results, indent=1) if as_json else render(results))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
