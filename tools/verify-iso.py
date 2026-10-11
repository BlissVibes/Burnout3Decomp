#!/usr/bin/env python3
"""Checks that a disc image is Burnout 3: Takedown, NTSC-U (boots SLUS_210.50).

usage: verify-iso.py <image.iso> [--sha1]
Exit 0 when the image boots SLUS_210.50, 1 otherwise. --sha1 also hashes the
whole image (about a minute) and compares it with the known-good release.
"""
import hashlib
import struct
import sys

SECTOR = 2048
EXPECTED_BOOT = 'SLUS_210.50'
EXPECTED_SHA1 = '11a7f335a37d2f3f5c13b967f3072c84f8eded02'
EXPECTED_SIZE = 2867822592


def read_sectors(f, first, count=1):
    f.seek(first * SECTOR)
    return f.read(count * SECTOR)


def root_dir(f):
    pvd = read_sectors(f, 16)
    if pvd[:6] != b'\x01CD001':
        raise ValueError('not an ISO 9660 image (no primary volume descriptor at sector 16)')
    return struct.unpack_from('<I', pvd, 158)[0], struct.unpack_from('<I', pvd, 166)[0]


def find_in_dir(f, lba, size, wanted):
    data = read_sectors(f, lba, (size + SECTOR - 1) // SECTOR)
    off = 0
    while off < size:
        length = data[off]
        if length == 0:
            off = (off // SECTOR + 1) * SECTOR
            continue
        rec = data[off:off + length]
        off += length
        name = rec[33:33 + rec[32]].decode('ascii', 'replace').split(';')[0].upper()
        if name == wanted:
            return struct.unpack_from('<I', rec, 2)[0], struct.unpack_from('<I', rec, 10)[0]
    return None


def boot_program(f):
    lba, size = root_dir(f)
    entry = find_in_dir(f, lba, size, 'SYSTEM.CNF')
    if not entry:
        return None
    text = read_sectors(f, entry[0], (entry[1] + SECTOR - 1) // SECTOR)[:entry[1]].decode('ascii', 'replace')
    for line in text.splitlines():
        key, _, value = line.partition('=')
        if key.strip() == 'BOOT2':
            return value.strip().split('\\')[-1].split(';')[0]
    return None


def sha1_of(path):
    h = hashlib.sha1()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 24), b''):
            h.update(chunk)
    return h.hexdigest()


def check(path, want_sha1):
    try:
        with open(path, 'rb') as f:
            try:
                boot = boot_program(f)
            except ValueError as e:
                return 1, str(e)
    except OSError as e:
        return 1, 'cannot open %s: %s' % (path, e.strerror)
    if boot is None:
        return 1, 'no SYSTEM.CNF with a BOOT2 line in the root directory'
    if boot != EXPECTED_BOOT:
        return 1, 'boots %s, expected %s' % (boot, EXPECTED_BOOT)
    if want_sha1:
        digest = sha1_of(path)
        if digest != EXPECTED_SHA1:
            return 1, 'boots %s but SHA-1 %s differs from the known release %s' % (boot, digest, EXPECTED_SHA1)
    return 0, 'OK %s' % boot


def main(argv):
    args = [a for a in argv[1:] if not a.startswith('--')]
    if len(args) != 1:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    code, message = check(args[0], '--sha1' in argv)
    print(message)
    return code


if __name__ == '__main__':
    sys.exit(main(sys.argv))
