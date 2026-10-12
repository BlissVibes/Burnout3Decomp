#!/usr/bin/env python3
"""The no-game-data gate: checks that no tracked file carries something copied from the game disc.

usage: nogamedata.py [repository root] [--max-bytes N]

Scans every file `git ls-files` lists in the repository (default: the current directory, which
must be the top of the repository) and reports:
  - ELF magic anywhere in a file (an executable, an IRX module or a piece of one);
  - a RenderWare source tag (the "$Id:" keyword followed by the //RenderWare/ depot path), which
    only exists in the game's executable and the SDK;
  - the reflected CRC-32 table (polynomial 0xEDB88320, recomputed here, never stored): eight or
    more consecutive table words from its start, as little- or big-endian binary words or
    written out as hex numbers in text. Code that needs the table must compute it;
  - a file over --max-bytes (default 4096) named .iso, .bin, .elf or .irx;
  - a PNG or JPEG image outside docs/ (a frame of the game does not belong in the tree;
    screenshots that document a run live under docs/).

Prints one line per finding as `path: reason`. Exit 0 when clean, 1 with findings, 2 for bad
arguments or when the folder is not a git repository. Tracked files missing from disk are skipped.
"""
import os
import re
import struct
import subprocess
import sys

MAX_IMAGE_BYTES = 4096
IMAGE_EXTENSIONS = ('.iso', '.bin', '.elf', '.irx')
MIN_TABLE_WORDS = 8

# The needles are assembled here so that this file does not contain them.
ELF_MAGIC = bytes([0x7F]) + b'ELF'
RW_TAG = b'$' + b'Id: //Render' + b'Ware/'
PICTURES = ((bytes([0x89]) + b'PNG\r\n' + bytes([0x1A]) + b'\n', 'PNG'), (bytes([0xFF, 0xD8, 0xFF]), 'JPEG'))
HEX_WORD = re.compile(rb'(?<![0-9A-Za-z])(?:0[xX])?([0-9A-Fa-f]{8})(?![0-9A-Za-z])')


def crc_table():
    table = []
    for n in range(256):
        c = n
        for _ in range(8):
            c = (c >> 1) ^ 0xEDB88320 if c & 1 else c >> 1
        table.append(c)
    return table


TABLE = crc_table()


def every(data, needle):
    at = data.find(needle)
    while at >= 0:
        yield at
        at = data.find(needle, at + 1)


def binary_tables(data):
    """Yields (offset, word count, byte order) for each run of table words from the table's start."""
    for order, name in (('<', 'little-endian'), ('>', 'big-endian')):
        packed = struct.pack(order + '256I', *TABLE)
        for at in every(data, packed[:MIN_TABLE_WORDS * 4]):
            count = MIN_TABLE_WORDS
            while count < 256 and data[at + count * 4:at + count * 4 + 4] == packed[count * 4:count * 4 + 4]:
                count += 1
            yield at, count, name


def text_tables(data):
    """Yields (offset, word count) for each run of hex numbers that spell the table from its start."""
    run_start, expected = None, 0
    for match in HEX_WORD.finditer(data):
        value = int(match.group(1), 16)
        if expected < 256 and value == TABLE[expected]:
            if expected == 0:
                run_start = match.start()
            expected += 1
            continue
        if expected >= MIN_TABLE_WORDS:
            yield run_start, expected
        run_start, expected = (match.start(), 1) if value == TABLE[0] else (None, 0)
    if expected >= MIN_TABLE_WORDS:
        yield run_start, expected


def check(path, data, max_image_bytes=MAX_IMAGE_BYTES):
    """Returns the findings for one file as `path: reason` strings. path uses forward slashes."""
    found = ['ELF magic at offset %d' % at for at in every(data, ELF_MAGIC)]
    found += ['RenderWare source tag at offset %d' % at for at in every(data, RW_TAG)]
    found += ['CRC-32 table (%d %s words) at offset %d' % (count, order, at) for at, count, order in binary_tables(data)]
    found += ['CRC-32 table written as text (%d words) at offset %d' % (count, at) for at, count in text_tables(data)]
    if path.lower().endswith(IMAGE_EXTENSIONS) and len(data) > max_image_bytes:
        found.append('%d bytes with a disc-image or executable extension' % len(data))
    if not path.startswith('docs/'):
        found += ['%s image outside docs/' % name for magic, name in PICTURES if data.startswith(magic)]
    return ['%s: %s' % (path, reason) for reason in found]


def tracked(root):
    env = dict(os.environ, GIT_CEILING_DIRECTORIES=os.path.dirname(os.path.abspath(root)))
    done = subprocess.run(['git', '-C', root, 'ls-files', '-z'], capture_output=True, env=env)
    if done.returncode:
        raise OSError(done.stderr.decode('utf-8', 'replace').strip() or 'git ls-files failed')
    return [name for name in done.stdout.decode('utf-8').split('\0') if name]


def main(argv):
    args = argv[1:]
    limit = MAX_IMAGE_BYTES
    try:
        if '--max-bytes' in args:
            at = args.index('--max-bytes')
            limit = int(args[at + 1])
            del args[at:at + 2]
        if len(args) > 1 or any(a.startswith('-') for a in args):
            raise IndexError
    except (IndexError, ValueError):
        print(__doc__.strip(), file=sys.stderr)
        return 2
    root = args[0] if args else '.'
    try:
        names = tracked(root)
    except OSError as e:
        print('nogamedata: %s' % e, file=sys.stderr)
        return 2
    findings, scanned = [], 0
    for name in names:
        try:
            with open(os.path.join(root, name), 'rb') as f:
                data = f.read()
        except OSError:
            continue
        scanned += 1
        findings += check(name, data, limit)
    for finding in findings:
        print(finding)
    if findings:
        print('%d finding%s in %d tracked files' % (len(findings), '' if len(findings) == 1 else 's', scanned))
        return 1
    print('clean: %d tracked files' % scanned)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
