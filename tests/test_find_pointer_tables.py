import importlib.util, os, struct, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('find_pointer_tables', os.path.join(HERE, '..', 'tools', 'find-pointer-tables.py'))
find_pointer_tables = importlib.util.module_from_spec(spec)
spec.loader.exec_module(find_pointer_tables)

BASE = 0x00100000
HEADER = 0x100


def make_elf(words: list[int]) -> bytes:
    """A 32-bit little-endian ELF with one PT_LOAD segment at BASE holding `words`."""
    body = b''.join(struct.pack('<I', w) for w in words)
    elf = bytearray(HEADER)
    elf[:4] = b'\x7fELF'
    struct.pack_into('<I', elf, 24, BASE)          # e_entry
    struct.pack_into('<I', elf, 28, 52)            # e_phoff
    struct.pack_into('<HH', elf, 42, 32, 1)        # e_phentsize, e_phnum
    struct.pack_into('<8I', elf, 52, 1, HEADER, BASE, BASE, len(body), len(body), 7, 16)
    return bytes(elf) + body


class FindPointerTablesTest(unittest.TestCase):
    def test_finds_a_run_bounded_by_zero_words(self):
        words = [0] * 4 + [BASE + 0x10, BASE + 0x14, BASE + 0x18, BASE + 0x1C, BASE] + [0] * 3
        self.assertEqual(find_pointer_tables.find_tables(make_elf(words)),
                         [(BASE + 0x10, [BASE + 0x10, BASE + 0x14, BASE + 0x18, BASE + 0x1C, BASE])])

    def test_short_runs_are_ignored(self):
        words = [0, BASE, BASE + 4, BASE + 8, 0, 0, 0, 0]
        self.assertEqual(find_pointer_tables.find_tables(make_elf(words)), [])
        self.assertEqual(len(find_pointer_tables.find_tables(make_elf(words), minimum=3)), 1)

    def test_unaligned_and_outside_words_break_a_run(self):
        outside = BASE + 0x1000
        words = [BASE, BASE + 4, BASE + 6, BASE + 8, BASE, outside, BASE, BASE + 4]
        self.assertEqual(find_pointer_tables.find_tables(make_elf(words), minimum=2),
                         [(BASE, [BASE, BASE + 4]), (BASE + 0xC, [BASE + 8, BASE]), (BASE + 0x18, [BASE, BASE + 4])])

    def test_run_reaching_the_end_of_the_segment_is_kept(self):
        words = [0, 0, BASE, BASE + 4, BASE, BASE + 4]
        self.assertEqual(find_pointer_tables.find_tables(make_elf(words)),
                         [(BASE + 8, [BASE, BASE + 4, BASE, BASE + 4])])

    def test_not_an_elf(self):
        with self.assertRaises(ValueError):
            find_pointer_tables.find_tables(b'MZ' + bytes(200))


if __name__ == '__main__':
    unittest.main()
