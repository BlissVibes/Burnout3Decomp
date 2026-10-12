import contextlib, importlib.util, io, os, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('progress', os.path.join(HERE, '..', 'tools', 'progress.py'))
progress = importlib.util.module_from_spec(spec)
spec.loader.exec_module(progress)

HEADER = 'address\tname\tkind\tsize\ttype\towner\tevidence\tstatus\tnote'


def row(address, name, kind='func', size='?', owner='game', evidence='X', status='recomp'):
    return '\t'.join(['0x%08X' % address, name, kind, size, '', owner, evidence, status, ''])


def table(*rows):
    return '\n'.join((HEADER,) + rows) + '\n'


OWNERS = '\n'.join(['start\tend\towner\tconf\twhat',
                    '0x00100000\t0x00100400\tgame\thigh\tfirst game range',
                    '0x00100400\t0x00100800\tsce\thigh\ta library',
                    '0x00100800\t0x00101000\tgame\tlow\tsecond game range']) + '\n'

SYMBOLS = table(
    row(0x00100000, 'done_a', size='0x40', evidence='CX', status='verified'),
    row(0x00100040, 'done_b', size='0x60', evidence='C', status='verified'),
    row(0x00100100, 'trying', size='0x80', status='draft'),
    row(0x00100200, 'trying_unsized', status='draft'),
    row(0x00100300, 'named', size='0x10'),
    row(0x00100380, 'a_label', kind='label'),
    row(0x00100400, 'library_done', size='0x100', owner='sce', evidence='C', status='verified'),
    row(0x00100800, 'fn_00100800'),
    row(0x00200000, 'some_data', kind='rodata', size='0x400'),
)


class Summarise(unittest.TestCase):
    def setUp(self):
        self.stats = progress.summarise(SYMBOLS, OWNERS)

    def test_game_bytes_are_the_game_ranges_only(self):
        self.assertEqual(self.stats['game_bytes'], 0x400 + 0x800)
        self.assertEqual(self.stats['game_ranges'], 2)

    def test_functions_are_counted_by_status_for_owner_game_only(self):
        self.assertEqual(self.stats['verified']['functions'], 2)
        self.assertEqual(self.stats['draft']['functions'], 2)
        self.assertEqual(self.stats['recomp']['functions'], 2)

    def test_bytes_are_the_sizes_of_the_functions_in_each_status(self):
        self.assertEqual(self.stats['verified']['bytes'], 0xA0)
        self.assertEqual(self.stats['draft']['bytes'], 0x80)

    def test_functions_without_a_size_are_counted_apart(self):
        self.assertEqual(self.stats['draft']['unsized'], 1)
        self.assertEqual(self.stats['verified']['unsized'], 0)

    def test_everything_not_draft_or_verified_is_still_recompiled(self):
        self.assertEqual(self.stats['recomp']['bytes'], 0xC00 - 0xA0 - 0x80)

    def test_labels_data_and_other_owners_do_not_count(self):
        total = sum(self.stats[s]['functions'] for s in ('recomp', 'draft', 'verified'))
        self.assertEqual(total, 6)

    def test_an_empty_table_is_all_recompiled(self):
        stats = progress.summarise(table(), OWNERS)
        self.assertEqual(stats['recomp']['bytes'], 0xC00)
        self.assertEqual(stats['verified']['functions'], 0)

    def test_no_game_range_gives_zero_percent_not_a_crash(self):
        stats = progress.summarise(SYMBOLS, 'start\tend\towner\tconf\twhat\n')
        self.assertEqual(stats['game_bytes'], 0)
        self.assertIn('0.00%', progress.one_line(stats))


class Render(unittest.TestCase):
    def setUp(self):
        self.stats = progress.summarise(SYMBOLS, OWNERS)

    def test_one_line_summary(self):
        self.assertEqual(progress.one_line(self.stats),
                         'game code 3,072 bytes in 2 ranges: verified 2 functions, 160 bytes (5.20%); '
                         'draft 2 functions, 128 bytes (4.16%), 1 without a size; '
                         'recompiled 2,784 bytes (90.62%), 2 of its functions in the table')

    def test_markdown_row_has_the_four_progress_columns(self):
        line = progress.markdown_row(self.stats, '2026-10-12')
        cells = [c.strip() for c in line.strip('|').split('|')]
        self.assertEqual(len(cells), 4)
        self.assertEqual(cells[0], '2026-10-12')
        self.assertEqual(cells[1], '4 Decompilation')
        self.assertEqual(cells[2], 'in progress')
        self.assertIn('verified 2 functions, 160 bytes (5.20%)', cells[3])
        self.assertTrue(line.startswith('| ') and line.endswith(' |'))
        self.assertNotIn('\n', line)

    def test_percentages_are_truncated_so_100_means_all_of_it(self):
        symbols = table(row(0x00100000, 'nearly', size='0xBFF', evidence='C', status='verified'))
        self.assertIn('(99.96%)', progress.one_line(progress.summarise(symbols, OWNERS)))


class CommandLine(unittest.TestCase):
    def run_main(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = progress.main(['progress.py'] + list(args))
        return code, out.getvalue(), err.getvalue()

    def write(self, folder, name, text):
        path = os.path.join(folder, name)
        with open(path, 'w', encoding='utf-8', newline='') as f:
            f.write(text)
        return path

    def test_prints_the_summary_then_the_row(self):
        with tempfile.TemporaryDirectory() as folder:
            code, out, _ = self.run_main(self.write(folder, 's.tsv', SYMBOLS), self.write(folder, 'o.tsv', OWNERS), '--date', '2026-10-12')
        lines = out.splitlines()
        self.assertEqual(code, 0)
        self.assertEqual(len(lines), 2)
        self.assertTrue(lines[0].startswith('game code 3,072 bytes'))
        self.assertTrue(lines[1].startswith('| 2026-10-12 | 4 Decompilation | in progress | '))

    def test_an_invalid_table_exits_1(self):
        with tempfile.TemporaryDirectory() as folder:
            bad = table(row(0x00100000, 'a', owner='ea'))
            code, out, err = self.run_main(self.write(folder, 's.tsv', bad), self.write(folder, 'o.tsv', OWNERS))
        self.assertEqual((code, out), (1, ''))
        self.assertIn('owner', err)

    def test_a_symbol_whose_owner_disagrees_with_its_range_exits_1(self):
        with tempfile.TemporaryDirectory() as folder:
            bad = table(row(0x00100400, 'a'))
            code, _, err = self.run_main(self.write(folder, 's.tsv', bad), self.write(folder, 'o.tsv', OWNERS))
        self.assertEqual(code, 1)
        self.assertIn('owned by sce', err)

    def test_bad_usage_and_missing_files_exit_2(self):
        self.assertEqual(self.run_main()[0], 2)
        self.assertEqual(self.run_main('only-one')[0], 2)
        self.assertEqual(self.run_main('a', 'b', '--date')[0], 2)
        self.assertEqual(self.run_main(os.path.join(HERE, 'no-such.tsv'), os.path.join(HERE, 'no-such-2.tsv'))[0], 2)


if __name__ == '__main__':
    unittest.main()
