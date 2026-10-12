import contextlib, importlib.util, io, os, shutil, struct, subprocess, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('nogamedata', os.path.join(HERE, '..', 'tools', 'nogamedata.py'))
nogamedata = importlib.util.module_from_spec(spec)
spec.loader.exec_module(nogamedata)

# Every needle is built at run time so that this file is not itself a finding.
ELF_MAGIC = bytes([0x7F]) + b'ELF'
RW_TAG = b'$' + b'Id: //Render' + b'Ware/RW36Active/rwsdk/src/example.c#1 $'
PNG = bytes([0x89]) + b'PNG\r\n' + bytes([0x1A]) + b'\n' + bytes(32)
JPEG = bytes([0xFF, 0xD8, 0xFF, 0xE0]) + bytes(32)


def crc_table():
    table = []
    for n in range(256):
        c = n
        for _ in range(8):
            c = (c >> 1) ^ 0xEDB88320 if c & 1 else c >> 1
        table.append(c)
    return table


def reasons(path, data):
    return [finding.split(': ', 1)[1] for finding in nogamedata.check(path, data)]


class Check(unittest.TestCase):
    def test_ordinary_files_are_clean(self):
        self.assertEqual(nogamedata.check('tools/thing.py', b'print("hello")\n'), [])
        self.assertEqual(nogamedata.check('docs/notes.md', b'The table at 0x0049B110 has 256 words.\n'), [])
        self.assertEqual(nogamedata.check('empty.txt', b''), [])

    def test_findings_name_the_file(self):
        self.assertEqual(nogamedata.check('a/b.dat', ELF_MAGIC + bytes(60)), ['a/b.dat: ELF magic at offset 0'])

    def test_elf_magic_is_found_anywhere_in_a_file(self):
        self.assertEqual(reasons('blob', bytes(100) + ELF_MAGIC + bytes(20)), ['ELF magic at offset 100'])

    def test_the_word_elf_alone_is_not_magic(self):
        self.assertEqual(nogamedata.check('docs/x.md', b'the ELF file, 0x7F ELF, \\x7fELF in a string literal\n'), [])

    def test_renderware_source_tags_are_refused(self):
        self.assertEqual(reasons('src/x.cpp', b'// ' + RW_TAG + b'\n'), ['RenderWare source tag at offset 3'])

    def test_naming_the_tag_in_prose_is_allowed(self):
        prose = b'refuse the string "$' + b'Id: //Render' + b'Ware" and its paths //RenderWare/RW36Active/rwsdk\n'
        self.assertEqual(nogamedata.check('docs/x.md', prose), [])

    def test_a_binary_crc_table_is_refused_in_either_byte_order(self):
        table = crc_table()
        little = bytes(10) + struct.pack('<256I', *table)
        self.assertEqual(reasons('data/t.dat', little), ['CRC-32 table (256 little-endian words) at offset 10'])
        big = struct.pack('>256I', *table)
        self.assertEqual(reasons('data/t.dat', big), ['CRC-32 table (256 big-endian words) at offset 0'])

    def test_the_start_of_a_table_is_enough(self):
        partial = struct.pack('<16I', *crc_table()[:16])
        self.assertEqual(reasons('data/t.dat', partial), ['CRC-32 table (16 little-endian words) at offset 0'])

    def test_a_few_table_words_are_not_a_table(self):
        self.assertEqual(nogamedata.check('data/t.dat', struct.pack('<4I', *crc_table()[:4])), [])
        self.assertEqual(nogamedata.check('data/t.dat', struct.pack('<8I', *range(8))), [])

    def test_a_crc_table_written_out_as_text_is_refused(self):
        table = crc_table()
        source = 'static const u32 kTable[256] = {\n' + ',\n'.join('    0x%08X' % w for w in table) + '\n};\n'
        self.assertEqual(reasons('src/crc.cpp', source.encode()), ['CRC-32 table written as text (256 words) at offset %d' % source.index('0x00000000')])
        lower = ' '.join('%08x' % w for w in table[:12])
        self.assertEqual(reasons('notes.txt', lower.encode()), ['CRC-32 table written as text (12 words) at offset 0'])

    def test_spot_values_and_the_polynomial_in_text_are_allowed(self):
        text = b'CHECK_EQ(table[1], 0x77073096u); CHECK_EQ(table[255], 0x2D02EF8Du); // polynomial 0xEDB88320\n'
        self.assertEqual(nogamedata.check('tests/unit/test_x.cpp', text), [])

    def test_big_disc_image_extensions_are_refused(self):
        big = bytes(nogamedata.MAX_IMAGE_BYTES + 1)
        for name in ('game.iso', 'DATA.BIN', 'main.elf', 'mod.IRX'):
            self.assertEqual(reasons('x/' + name, big), ['%d bytes with a disc-image or executable extension' % len(big)], name)

    def test_small_files_with_those_extensions_and_big_files_without_are_allowed(self):
        self.assertEqual(nogamedata.check('fixture.bin', bytes(nogamedata.MAX_IMAGE_BYTES)), [])
        self.assertEqual(nogamedata.check('docs/long.md', b'x' * (nogamedata.MAX_IMAGE_BYTES * 4)), [])

    def test_the_size_threshold_can_be_changed(self):
        self.assertEqual([f.split(': ', 1)[1] for f in nogamedata.check('a.bin', bytes(11), max_image_bytes=10)],
                         ['11 bytes with a disc-image or executable extension'])

    def test_pictures_are_allowed_under_docs_only(self):
        self.assertEqual(nogamedata.check('docs/re/shot.png', PNG), [])
        self.assertEqual(nogamedata.check('docs/photo.jpg', JPEG), [])
        self.assertEqual(reasons('web/shot.png', PNG), ['PNG image outside docs/'])
        self.assertEqual(reasons('frame.dat', JPEG), ['JPEG image outside docs/'])
        self.assertEqual(reasons('mydocs/shot.png', PNG), ['PNG image outside docs/'])

    def test_one_file_can_have_several_findings(self):
        data = ELF_MAGIC + bytes(12) + RW_TAG
        self.assertEqual(len(nogamedata.check('x.bin', data)), 2)


@unittest.skipUnless(shutil.which('git'), 'needs git')
class CommandLine(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.mkdtemp()
        self.git('init', '-q')

    def tearDown(self):
        shutil.rmtree(self.folder, ignore_errors=True)

    def git(self, *args):
        subprocess.run(['git', '-c', 'core.autocrlf=false'] + list(args), cwd=self.folder, check=True, capture_output=True)

    def put(self, name, data, track=True):
        path = os.path.join(self.folder, *name.split('/'))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(data)
        if track:
            self.git('add', name)

    def run_main(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = nogamedata.main(['nogamedata.py'] + list(args))
        return code, out.getvalue(), err.getvalue()

    def test_a_clean_repo_exits_0_and_says_how_many_files(self):
        self.put('a.txt', b'hello\n')
        self.put('docs/b.md', b'# notes\n')
        self.assertEqual(self.run_main(self.folder), (0, 'clean: 2 tracked files\n', ''))

    def test_findings_exit_1_and_are_listed(self):
        self.put('a.txt', b'hello\n')
        self.put('lib/x.o', ELF_MAGIC + bytes(40))
        self.put('web/shot.png', PNG)
        code, out, _ = self.run_main(self.folder)
        self.assertEqual(code, 1)
        self.assertEqual(out.splitlines(), ['lib/x.o: ELF magic at offset 0', 'web/shot.png: PNG image outside docs/',
                                            '2 findings in 3 tracked files'])

    def test_untracked_files_are_not_scanned(self):
        self.put('a.txt', b'hello\n')
        self.put('scratch/x.o', ELF_MAGIC + bytes(40), track=False)
        self.assertEqual(self.run_main(self.folder)[0], 0)

    def test_a_tracked_file_deleted_from_disk_is_skipped(self):
        self.put('a.txt', b'hello\n')
        self.put('gone.txt', b'bye\n')
        os.remove(os.path.join(self.folder, 'gone.txt'))
        self.assertEqual(self.run_main(self.folder), (0, 'clean: 1 tracked files\n', ''))

    def test_max_bytes_option(self):
        self.put('small.bin', bytes(100))
        self.assertEqual(self.run_main(self.folder)[0], 0)
        self.assertEqual(self.run_main(self.folder, '--max-bytes', '50')[0], 1)

    def test_a_folder_that_is_not_a_repository_exits_2(self):
        outside = tempfile.mkdtemp()
        try:
            code, _, err = self.run_main(outside)
        finally:
            shutil.rmtree(outside, ignore_errors=True)
        self.assertEqual(code, 2)
        self.assertIn('nogamedata:', err)

    def test_bad_usage_exits_2(self):
        self.assertEqual(self.run_main('a', 'b')[0], 2)
        self.assertEqual(self.run_main('--max-bytes')[0], 2)
        self.assertEqual(self.run_main('--max-bytes', 'lots')[0], 2)


if __name__ == '__main__':
    unittest.main()
