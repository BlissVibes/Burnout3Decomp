import importlib.util, os, struct, sys, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('verify_iso', os.path.join(HERE, '..', 'tools', 'verify-iso.py'))
verify_iso = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify_iso)

SECTOR = 2048


def dir_record(name: bytes, lba: int, size: int, flags: int = 0) -> bytes:
    body = bytearray(b'\x00')
    body += struct.pack('<I', lba) + struct.pack('>I', lba)
    body += struct.pack('<I', size) + struct.pack('>I', size)
    body += bytes(7)
    body += bytes([flags, 0, 0]) + struct.pack('<H', 1) + struct.pack('>H', 1)
    body += bytes([len(name)]) + name
    if len(name) % 2 == 0:
        body += b'\x00'
    return bytes([len(body) + 1]) + bytes(body)


def make_iso(path: str, system_cnf: bytes | None) -> None:
    """A minimal ISO 9660 image: PVD at sector 16, root directory at 18, SYSTEM.CNF at 19."""
    with open(path, 'wb') as f:
        f.write(bytes(16 * SECTOR))
        pvd = bytearray(SECTOR)
        pvd[0] = 1
        pvd[1:6] = b'CD001'
        pvd[6] = 1
        struct.pack_into('<I', pvd, 80, 20)
        struct.pack_into('>I', pvd, 84, 20)
        pvd[156:156 + 34] = dir_record(b'\x00', 18, SECTOR, 2)
        f.write(pvd)
        f.write(bytes(SECTOR))  # sector 17: terminator left blank
        root = bytearray(SECTOR)
        off = 0
        for rec in (dir_record(b'\x00', 18, SECTOR, 2), dir_record(b'\x01', 18, SECTOR, 2)):
            root[off:off + len(rec)] = rec
            off += len(rec)
        if system_cnf is not None:
            rec = dir_record(b'SYSTEM.CNF;1', 19, len(system_cnf))
            root[off:off + len(rec)] = rec
        f.write(root)
        f.write((system_cnf or b'').ljust(SECTOR, b'\x00'))


class VerifyIsoTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.tmp.name, 'disc.iso')

    def tearDown(self):
        self.tmp.cleanup()

    def test_boot_program_reads_boot2(self):
        make_iso(self.path, b'BOOT2 = cdrom0:\\SLUS_210.50;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n')
        with open(self.path, 'rb') as f:
            self.assertEqual(verify_iso.boot_program(f), 'SLUS_210.50')

    def test_other_disc_is_reported(self):
        make_iso(self.path, b'BOOT2 = cdrom0:\\SLES_525.84;1\r\nVER = 1.00\r\nVMODE = PAL\r\n')
        code, message = verify_iso.check(self.path, want_sha1=False)
        self.assertEqual(code, 1)
        self.assertIn('boots SLES_525.84', message)

    def test_missing_system_cnf(self):
        make_iso(self.path, None)
        code, message = verify_iso.check(self.path, want_sha1=False)
        self.assertEqual(code, 1)
        self.assertIn('no SYSTEM.CNF', message)

    def test_not_an_iso(self):
        with open(self.path, 'wb') as f:
            f.write(b'MZ' + bytes(40000))
        code, message = verify_iso.check(self.path, want_sha1=False)
        self.assertEqual(code, 1)
        self.assertIn('not an ISO 9660 image', message)

    def test_right_disc_ok(self):
        make_iso(self.path, b'BOOT2 = cdrom0:\\SLUS_210.50;1\r\n')
        code, message = verify_iso.check(self.path, want_sha1=False)
        self.assertEqual((code, message), (0, 'OK SLUS_210.50'))


if __name__ == '__main__':
    unittest.main()
