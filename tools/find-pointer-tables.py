#!/usr/bin/env python3
"""Lists tables of pointers into the loaded image stored in a PS2 ELF (static constructor lists, vtables, callback arrays).

usage: find-pointer-tables.py <program.elf> [--min N] [--containing ADDR] [--out FILE]
A table is N or more (default 4) consecutive 4-byte-aligned words that each hold a
4-byte-aligned address inside a loaded segment. --containing ADDR (hex) prints only
the table that stores ADDR as one of its pointers, with every entry. --out FILE writes
that table's distinct targets one per line in the entries.txt format of the fork's
port.sh (lower-case hex, no prefix).
"""
import struct
import sys


def load_segments(data):
    """Returns [(vaddr, file_offset, file_size)] for the PT_LOAD segments that have bytes in the file."""
    if data[:4] != b'\x7fELF':
        raise ValueError('not an ELF file')
    phoff, = struct.unpack_from('<I', data, 28)
    phentsize, phnum = struct.unpack_from('<HH', data, 42)
    segments = []
    for i in range(phnum):
        p_type, p_offset, p_vaddr, _, p_filesz = struct.unpack_from('<5I', data, phoff + i * phentsize)
        if p_type == 1 and p_filesz:
            segments.append((p_vaddr, p_offset, p_filesz))
    return segments


def find_tables(data, minimum=4):
    """Returns [(table_address, [pointer, ...])] for every run of at least `minimum` pointers."""
    segments = load_segments(data)

    def points_inside(word):
        return word % 4 == 0 and any(v <= word < v + size for v, _, size in segments)

    tables = []
    for vaddr, offset, size in segments:
        run_start, run = 0, []

        def flush():
            if len(run) >= minimum:
                tables.append((run_start, list(run)))
            run.clear()

        for at in range(0, size - size % 4, 4):
            word, = struct.unpack_from('<I', data, offset + at)
            if points_inside(word):
                if not run:
                    run_start = vaddr + at
                run.append(word)
            else:
                flush()
        flush()
    return tables


def main(argv):
    args, minimum, containing, out = [], 4, None, None
    it = iter(argv[1:])
    for arg in it:
        if arg == '--min':
            minimum = int(next(it))
        elif arg == '--containing':
            containing = int(next(it), 16)
        elif arg == '--out':
            out = next(it)
        else:
            args.append(arg)
    if len(args) != 1 or (out and containing is None):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    with open(args[0], 'rb') as f:
        data = f.read()
    try:
        tables = find_tables(data, minimum)
    except ValueError as e:
        print(e, file=sys.stderr)
        return 1
    if containing is None:
        for address, pointers in tables:
            print('%08X..%08X  %5d pointers  targets %08X..%08X' % (
                address, address + 4 * len(pointers) - 4, len(pointers), min(pointers), max(pointers)))
        print('%d tables' % len(tables))
        return 0
    for address, pointers in tables:
        if containing in pointers:
            print('table %08X..%08X, %d pointers, %d distinct' % (
                address, address + 4 * len(pointers) - 4, len(pointers), len(set(pointers))))
            for index, pointer in enumerate(pointers):
                print('%08X: %08X' % (address + 4 * index, pointer))
            if out:
                with open(out, 'w') as f:
                    for pointer in sorted(set(pointers)):
                        f.write('%x\n' % pointer)
            return 0
    print('no table of at least %d pointers stores %08X' % (minimum, containing))
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
