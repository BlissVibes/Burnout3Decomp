import contextlib, importlib.util, io, json, os, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('symdb', os.path.join(HERE, '..', 'tools', 'symdb.py'))
symdb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(symdb)

HEADER = 'address\tname\tkind\tsize\ttype\towner\tevidence\tstatus\tnote'
OWNERS_HEADER = 'start\tend\towner\tconf\twhat'


def row(address, name, kind='func', size='?', type_='', owner='game', evidence='X', status='recomp', note=''):
    return '\t'.join(['0x%08X' % address, name, kind, size, type_, owner, evidence, status, note])


def table(*rows):
    return '\n'.join((HEADER,) + rows) + '\n'


GOOD = table(
    row(0x00100000, 'alpha', size='0x20', type_='void alpha(void)', evidence='SX'),
    row(0x00100010, 'alpha_loop', kind='label'),
    row(0x00100020, 'fn_00100020', owner='sce'),
    row(0x00100040, 'gamma', size='0x10', evidence='CX', status='verified'),
    row(0x00200000, 'str_hello', kind='string', size='0x6', evidence='S'),
    row(0x00200010, 'some_table', kind='rodata', size='0x10', type_='f32[4]', evidence='R'),
)


def errors(text, owners=None):
    return symdb.validate(text, symdb.parse_owners(owners)[0] if owners else None)


class Validate(unittest.TestCase):
    def test_accepts_a_well_formed_table(self):
        self.assertEqual(errors(GOOD), [])

    def test_rejects_a_wrong_header(self):
        found = errors(GOOD.replace('evidence\tstatus', 'status\tevidence', 1))
        self.assertTrue(any('header' in e for e in found), found)

    def test_rejects_a_row_with_the_wrong_number_of_columns(self):
        found = errors(table(row(0x00100000, 'alpha') + '\textra'))
        self.assertEqual(found, ['line 2: 10 columns, expected 9'])

    def test_rejects_addresses_out_of_order(self):
        found = errors(table(row(0x00100020, 'beta'), row(0x00100000, 'alpha')))
        self.assertEqual(found, ['line 3: address 0x00100000 is not above the previous row (0x00100020)'])

    def test_rejects_a_repeated_address(self):
        found = errors(table(row(0x00100000, 'alpha'), row(0x00100000, 'beta')))
        self.assertEqual(found, ['line 3: address 0x00100000 is not above the previous row (0x00100000)'])

    def test_rejects_a_repeated_name(self):
        found = errors(table(row(0x00100000, 'alpha'), row(0x00100020, 'alpha')))
        self.assertEqual(found, ['line 3: name alpha is already used on line 2'])

    def test_rejects_an_address_that_is_not_eight_upper_case_hex_digits(self):
        for bad in ('0x100000', '0x0010000g', '00100000', '0x0010abcd'):
            text = table(row(0x00100000, 'alpha').replace('0x00100000', bad))
            self.assertTrue(any('address' in e for e in errors(text)), bad)

    def test_rejects_a_name_that_is_not_a_c_identifier(self):
        for bad in ('9lives', 'has space', 'a-b', '', 'café'):
            self.assertTrue(any('identifier' in e for e in errors(table(row(0x00100000, bad)))), bad)

    def test_rejects_a_placeholder_name_for_another_address(self):
        found = errors(table(row(0x00100000, 'fn_00100004')))
        self.assertEqual(found, ['line 2: placeholder name fn_00100004 does not match address 0x00100000'])

    def test_rejects_unknown_kind_owner_and_status(self):
        self.assertTrue(any('kind' in e for e in errors(table(row(0x00100000, 'a', kind='function')))))
        self.assertTrue(any('owner' in e for e in errors(table(row(0x00100000, 'a', owner='ea')))))
        self.assertTrue(any('status' in e for e in errors(table(row(0x00100000, 'a', status='done')))))

    def test_rejects_bad_evidence(self):
        for bad in ('', 'Z', 'SS', 'S,X', 'sx'):
            self.assertTrue(any('evidence' in e for e in errors(table(row(0x00100000, 'a', evidence=bad)))), bad)

    def test_rejects_a_bad_size(self):
        for bad in ('', '32', '0x', '0x0', '0x1g', '0x1f'):
            self.assertTrue(any('size' in e for e in errors(table(row(0x00100000, 'a', size=bad)))), bad)

    def test_rejects_a_size_that_reaches_into_the_next_symbol(self):
        found = errors(table(row(0x00100000, 'alpha', size='0x24'), row(0x00100020, 'beta')))
        self.assertEqual(found, ['line 2: alpha (0x00100000 size 0x24) overlaps beta at 0x00100020'])

    def test_allows_a_label_inside_a_sized_function_but_not_the_symbol_after_it(self):
        inside = table(row(0x00100000, 'alpha', size='0x20'), row(0x00100010, 'mid', kind='label'), row(0x00100020, 'beta'))
        self.assertEqual(errors(inside), [])
        over = table(row(0x00100000, 'alpha', size='0x24'), row(0x00100010, 'mid', kind='label'), row(0x00100020, 'beta'))
        self.assertEqual(errors(over), ['line 2: alpha (0x00100000 size 0x24) overlaps beta at 0x00100020'])

    def test_verified_needs_oracle_evidence_and_a_function(self):
        self.assertEqual(errors(table(row(0x00100000, 'a', evidence='SX', status='verified'))),
                         ['line 2: status verified needs evidence C'])
        self.assertEqual(errors(table(row(0x00100000, 'a', kind='data', evidence='C', status='verified'))),
                         ['line 2: status verified is for kind func, not data'])

    def test_rejects_carriage_returns_and_a_missing_final_newline(self):
        self.assertTrue(any('CR' in e for e in errors(GOOD.replace('\n', '\r\n'))))
        self.assertTrue(any('newline' in e for e in errors(GOOD.rstrip('\n'))))

    def test_rejects_blank_lines(self):
        self.assertTrue(any('blank' in e for e in errors(GOOD + '\n')))

    def test_reports_every_problem_not_only_the_first(self):
        found = errors(table(row(0x00100000, 'a', owner='ea'), row(0x00100020, 'b', kind='thing')))
        self.assertEqual(len(found), 2)


OWNERS = '\n'.join([OWNERS_HEADER,
                    '0x00100000\t0x00100040\tgame\thigh\tfirst game range',
                    '0x00100040\t0x00100080\tsce\tmedium\ta library']) + '\n'


class Owners(unittest.TestCase):
    def test_parses_ranges(self):
        ranges, found = symdb.parse_owners(OWNERS)
        self.assertEqual(found, [])
        self.assertEqual([(r.start, r.end, r.owner) for r in ranges], [(0x00100000, 0x00100040, 'game'), (0x00100040, 0x00100080, 'sce')])

    def test_owner_at_finds_the_range_or_none(self):
        ranges, _ = symdb.parse_owners(OWNERS)
        self.assertEqual(symdb.owner_at(ranges, 0x00100000), 'game')
        self.assertEqual(symdb.owner_at(ranges, 0x0010003C), 'game')
        self.assertEqual(symdb.owner_at(ranges, 0x00100040), 'sce')
        self.assertIsNone(symdb.owner_at(ranges, 0x00100080))
        self.assertIsNone(symdb.owner_at(ranges, 0x000FFFFC))

    def test_rejects_overlapping_unsorted_or_empty_ranges_and_unknown_owners(self):
        def bad(*lines):
            return symdb.parse_owners('\n'.join((OWNERS_HEADER,) + lines) + '\n')[1]
        self.assertTrue(bad('0x00100000\t0x00100040\tgame\thigh\ta', '0x00100020\t0x00100080\tsce\thigh\tb'))
        self.assertTrue(bad('0x00100040\t0x00100080\tgame\thigh\ta', '0x00100000\t0x00100040\tsce\thigh\tb'))
        self.assertTrue(bad('0x00100040\t0x00100040\tgame\thigh\ta'))
        self.assertTrue(bad('0x00100000\t0x00100040\tea\thigh\ta'))
        self.assertTrue(bad('0x00100000\t0x00100040\tgame\tsure\ta'))

    def test_validate_flags_a_symbol_whose_owner_disagrees_with_its_range(self):
        text = table(row(0x00100000, 'alpha'), row(0x00100040, 'beta'), row(0x00200000, 'far', owner='rw'))
        self.assertEqual(errors(text, OWNERS), ['line 3: beta is owner game but 0x00100040 lies in a range owned by sce'])


class Exports(unittest.TestCase):
    def test_splat_lines_carry_type_and_size(self):
        self.assertEqual(symdb.export_splat(GOOD).splitlines(), [
            'alpha = 0x00100000; // type:func size:0x20',
            'alpha_loop = 0x00100010; // type:label',
            'fn_00100020 = 0x00100020; // type:func',
            'gamma = 0x00100040; // type:func size:0x10',
            'str_hello = 0x00200000; // type:asciz size:0x6',
            'some_table = 0x00200010; // size:0x10',
        ])

    def test_splat_line_without_attributes_has_no_comment(self):
        self.assertEqual(symdb.export_splat(table(row(0x00300000, 'thing', kind='bss'))), 'thing = 0x00300000;\n')

    def test_splat_takes_a_scalar_type_from_the_type_column(self):
        text = table(row(0x00300000, 'f32_00300000', kind='rodata', size='0x4', type_='f32'))
        self.assertEqual(symdb.export_splat(text), 'f32_00300000 = 0x00300000; // type:f32 size:0x4\n')

    def test_names_lists_code_symbols_only(self):
        self.assertEqual(symdb.export_names(GOOD).splitlines(), [
            '0x00100000 alpha', '0x00100010 alpha_loop', '0x00100020 fn_00100020', '0x00100040 gamma'])

    def test_hooks_lists_verified_functions_only(self):
        self.assertEqual(symdb.export_hooks(GOOD), '0x00100040 gamma\n')

    def test_hooks_of_a_table_with_nothing_verified_is_empty(self):
        self.assertEqual(symdb.export_hooks(table(row(0x00100000, 'alpha'))), '')


def anchors(*results):
    return json.dumps(list(results))


def hit(address, *functions):
    return {'address': address, 'refs': [{'pc': f + 8, 'via': 'addiu', 'function': f} for f in functions]}


class ImportAnchors(unittest.TestCase):
    def test_adds_a_string_row_named_after_its_text(self):
        found = anchors({'anchor': 'str:Physics/Drift', 'hits': [hit(0x00200040, 0x00100000)], 'references': 1, 'functions': 1})
        ranges, _ = symdb.parse_owners(OWNERS)
        text, added = symdb.import_anchors(GOOD, found, ranges)
        self.assertEqual(added, 1)
        self.assertEqual(errors(text), [])
        self.assertIn(row(0x00200040, 'str_physics_drift', kind='string', size='0xE', type_='char[14]', owner='game', evidence='S',
                          note='1 code reference, first from the function near 0x00100000'), text.splitlines())

    def test_keeps_the_table_sorted(self):
        found = anchors({'anchor': 'str:Zed', 'hits': [hit(0x00100030)], 'references': 0, 'functions': 0})
        text, _ = symdb.import_anchors(GOOD, found, None)
        addresses = [line.split('\t')[0] for line in text.splitlines()[1:]]
        self.assertEqual(addresses, sorted(addresses))
        self.assertIn('0x00100030', addresses)

    def test_owner_comes_from_the_referencing_function_and_defaults_to_game(self):
        ranges, _ = symdb.parse_owners(OWNERS)
        found = anchors({'anchor': 'str:In a library', 'hits': [hit(0x00200040, 0x00100050)], 'references': 1, 'functions': 1},
                        {'anchor': 'str:Nobody', 'hits': [hit(0x00200060)], 'references': 0, 'functions': 0})
        text, _ = symdb.import_anchors(GOOD, found, ranges)
        rows = {line.split('\t')[1]: line.split('\t') for line in text.splitlines()[1:]}
        self.assertEqual(rows['str_in_a_library'][5], 'sce')
        self.assertEqual(rows['str_nobody'][5], 'game')
        self.assertEqual(rows['str_nobody'][8], 'no code reference found')

    def test_skips_addresses_already_in_the_table_and_immediates(self):
        found = anchors({'anchor': 'str:hello', 'hits': [hit(0x00200000, 0x00100000)], 'references': 1, 'functions': 1},
                        {'anchor': 'u32:FD462907', 'hits': [{'address': 'imm', 'refs': [{'pc': 0x00100008, 'via': 'ori', 'function': 0x00100000}]}],
                         'references': 1, 'functions': 1})
        text, added = symdb.import_anchors(GOOD, found, None)
        self.assertEqual((text, added), (GOOD, 0))

    def test_words_floats_and_byte_runs_get_address_names(self):
        found = anchors({'anchor': 'f32:150.0', 'hits': [hit(0x00200040)], 'references': 0, 'functions': 0},
                        {'anchor': 'u32:DEADBEEF', 'hits': [hit(0x00200050)], 'references': 0, 'functions': 0},
                        {'anchor': 'bytes:0011223344', 'hits': [hit(0x00200060)], 'references': 0, 'functions': 0})
        text, added = symdb.import_anchors(GOOD, found, None)
        rows = {line.split('\t')[0]: line.split('\t') for line in text.splitlines()[1:]}
        self.assertEqual(added, 3)
        self.assertEqual(rows['0x00200040'][1:5], ['f32_00200040', 'rodata', '0x4', 'f32'])
        self.assertEqual(rows['0x00200050'][1:5], ['u32_00200050', 'rodata', '0x4', 'u32'])
        self.assertEqual(rows['0x00200060'][1:5], ['dat_00200060', 'rodata', '0x5', 'u8[5]'])
        self.assertEqual(errors(text), [])

    def test_two_strings_with_the_same_slug_get_distinct_names(self):
        found = anchors({'anchor': 'str:Boost Bar', 'hits': [hit(0x00200040), hit(0x00200060)], 'references': 0, 'functions': 0},
                        {'anchor': 'str:boost-bar', 'hits': [hit(0x00200080)], 'references': 0, 'functions': 0})
        text, _ = symdb.import_anchors(GOOD, found, None)
        names = [line.split('\t')[1] for line in text.splitlines()[1:]]
        self.assertIn('str_boost_bar', names)
        self.assertIn('str_boost_bar_00200060', names)
        self.assertIn('str_boost_bar_00200080', names)
        self.assertEqual(errors(text), [])

    def test_a_string_with_no_letters_is_named_by_address(self):
        found = anchors({'anchor': 'str:%d/%d', 'hits': [hit(0x00200040)], 'references': 0, 'functions': 0})
        text, _ = symdb.import_anchors(GOOD, found, None)
        self.assertIn('str_d_d', [line.split('\t')[1] for line in text.splitlines()[1:]])
        found = anchors({'anchor': 'str:...', 'hits': [hit(0x00200040)], 'references': 0, 'functions': 0})
        text, _ = symdb.import_anchors(GOOD, found, None)
        self.assertIn('str_00200040', [line.split('\t')[1] for line in text.splitlines()[1:]])


class CommandLine(unittest.TestCase):
    def run_main(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = symdb.main(['symdb.py'] + list(args))
        return code, out.getvalue(), err.getvalue()

    def write(self, folder, name, text):
        path = os.path.join(folder, name)
        with open(path, 'w', encoding='utf-8', newline='') as f:
            f.write(text)
        return path

    def test_validate_exits_0_and_says_how_many_symbols(self):
        with tempfile.TemporaryDirectory() as folder:
            code, out, _ = self.run_main('validate', self.write(folder, 'db.tsv', GOOD))
        self.assertEqual((code, out), (0, 'ok: 6 symbols\n'))

    def test_validate_exits_1_and_lists_the_errors(self):
        with tempfile.TemporaryDirectory() as folder:
            code, out, err = self.run_main('validate', self.write(folder, 'db.tsv', table(row(0x00100000, 'a', owner='ea'))))
        self.assertEqual(code, 1)
        self.assertIn('line 2: owner', err)

    def test_validate_sees_carriage_returns_in_the_file(self):
        with tempfile.TemporaryDirectory() as folder:
            code, _, err = self.run_main('validate', self.write(folder, 'db.tsv', GOOD.replace('\n', '\r\n')))
        self.assertEqual(code, 1)
        self.assertIn('CR', err)

    def test_validate_with_owners(self):
        with tempfile.TemporaryDirectory() as folder:
            db = self.write(folder, 'db.tsv', table(row(0x00100040, 'beta')))
            code, _, err = self.run_main('validate', db, '--owners', self.write(folder, 'owners.tsv', OWNERS))
        self.assertEqual(code, 1)
        self.assertIn('owned by sce', err)

    def test_export_refuses_an_invalid_table(self):
        with tempfile.TemporaryDirectory() as folder:
            code, out, err = self.run_main('export-hooks', self.write(folder, 'db.tsv', table(row(0x00100000, 'a', owner='ea'))))
        self.assertEqual((code, out), (1, ''))
        self.assertIn('owner', err)

    def test_export_writes_to_stdout_or_a_file_with_lf_endings(self):
        with tempfile.TemporaryDirectory() as folder:
            db = self.write(folder, 'db.tsv', GOOD)
            code, out, _ = self.run_main('export-hooks', db)
            self.assertEqual((code, out), (0, '0x00100040 gamma\n'))
            target = os.path.join(folder, 'names.txt')
            code, out, _ = self.run_main('export-names', db, '-o', target)
            self.assertEqual((code, out), (0, ''))
            with open(target, 'rb') as f:
                data = f.read()
        self.assertEqual(data.count(b'\n'), 4)
        self.assertNotIn(b'\r', data)

    def test_import_anchors_rewrites_the_table_in_place_with_lf_endings(self):
        found = anchors({'anchor': 'str:Zed', 'hits': [hit(0x00200040)], 'references': 0, 'functions': 0})
        with tempfile.TemporaryDirectory() as folder:
            db = self.write(folder, 'db.tsv', GOOD)
            code, out, _ = self.run_main('import-anchors', db, self.write(folder, 'anchors.json', found))
            with open(db, 'rb') as f:
                data = f.read()
        self.assertEqual((code, out), (0, 'added 1 symbol, 7 in all\n'))
        self.assertIn(b'str_zed', data)
        self.assertNotIn(b'\r', data)

    def test_bad_usage_exits_2(self):
        for args in ([], ['validate'], ['frobnicate', 'x'], ['validate', 'a', 'b'], ['export-splat', 'a', '-o']):
            self.assertEqual(self.run_main(*args)[0], 2, args)

    def test_a_missing_file_exits_2(self):
        code, _, err = self.run_main('validate', os.path.join(HERE, 'no-such-file.tsv'))
        self.assertEqual(code, 2)
        self.assertIn('symdb:', err)


if __name__ == '__main__':
    unittest.main()
