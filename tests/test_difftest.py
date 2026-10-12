import copy, importlib.util, io, json, os, struct, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, '..', 'tools', 'difftest.py')
spec = importlib.util.spec_from_file_location('difftest', TOOL)
difftest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(difftest)

ZERO = '0' * 32


def cpu(v0=0, f0=0):
    r = [ZERO] * 32
    r[2] = f'{v0:032x}'
    r[29] = f'{0x1FFF000:032x}'
    f = ['0x00000000'] * 32
    f[0] = f'0x{f0:08X}'
    return {'r': r, 'hi': ZERO, 'lo': ZERO, 'sa': 0, 'pc': '0x00001000', 'f': f, 'facc': '0x00000000', 'fcr31': '0x00000000',
            'vf': [ZERO] * 32, 'acc': ZERO, 'vi': [0] * 16, 'q': '0x0', 'p': '0x0', 'i': '0x0', 'vr': '0x0', 'mac': '0x0',
            'status': '0x0', 'clip': '0x0'}


def result(v0=0, status='ok', detail='', hook=False, cycles=43, ram=None, io=None, syscalls=None):
    return {'harness': 1, 'state_version': 4, 'frame': 300, 'status': status, 'detail': detail, 'hook_ran': hook,
            'sp': '0x01FFE000', 'scratch': '0x01FFE010', 'cycles': 0 if hook else cycles, 'before': cpu(), 'after': cpu(v0),
            'ram': ram or [], 'spr': [], 'stack': [], 'io': io or [], 'syscalls': syscalls or [], 'entries': []}


def mem_record(kind, address, data):
    return struct.pack('<BII', kind, address, len(data)) + data


class FakePs2run:
    """Stands in for run_ps2run: writes the result a real call would, from a table keyed by snapshot."""

    def __init__(self, recomp, decomp, mems=None, exit_codes=None):
        self.recomp, self.decomp, self.mems, self.exit_codes = recomp, decomp, mems or {}, exit_codes or {}
        self.commands = []

    def __call__(self, command, env, timeout):
        self.commands.append((list(command), dict(env), timeout))
        snapshot = os.path.basename(command[command.index('--snapshot') + 1])
        hooked = '--hook' in command
        doc = (self.decomp if hooked else self.recomp)(snapshot, command)
        if doc is None:
            return None, ''
        if isinstance(doc, int):
            return doc, '[state] state file from another version\n'
        out = command[command.index('--out') + 1]
        with open(out, 'w', encoding='utf-8') as f:
            json.dump(doc, f)
        with open(out + '.mem', 'wb') as f:
            f.write(self.mems.get((snapshot, hooked), b''))
        return (0 if doc['status'] == 'ok' else 3), ''


class CompareTest(unittest.TestCase):
    RETURNS = [('r', 2)]

    def test_equal_results_pass_with_the_recompiled_cycles(self):
        self.assertEqual(difftest.compare(result(0x180), result(0x180, hook=True), self.RETURNS), ('PASS', 'cycles 43'))

    def test_return_register_difference(self):
        verdict = difftest.compare(result(0x3F00), result(0x3E80, hook=True), self.RETURNS)
        self.assertEqual(verdict, ('FAIL', 'v0 0000000000003f00 != 0000000000003e80'))

    def test_upper_half_of_a_64_bit_result_counts(self):
        verdict = difftest.compare(result(0xFFFFFFFF80000000), result(0x80000000, hook=True), self.RETURNS)
        self.assertEqual(verdict[0], 'FAIL')

    def test_register_outside_the_return_set_is_ignored(self):
        a, b = result(1), result(1, hook=True)
        b['after']['r'][3] = f'{7:032x}'
        self.assertEqual(difftest.compare(a, b, self.RETURNS)[0], 'PASS')
        self.assertEqual(difftest.compare(a, b, [('r', 2), ('r', 3)])[0], 'FAIL')

    def test_float_return_register(self):
        a, b = result(), result(hook=True)
        b['after']['f'][0] = '0x3F800000'
        self.assertEqual(difftest.compare(a, b, [('f', 0)]), ('FAIL', 'f0 0x00000000 != 0x3F800000'))

    def test_recompiled_abort_is_a_skip(self):
        a = result(status='forbidden_syscall', detail='syscall 0x44 at 0x0010F208')
        self.assertEqual(difftest.compare(a, a, self.RETURNS), ('SKIP', 'recomp forbidden_syscall: syscall 0x44 at 0x0010F208'))

    def test_decompiled_abort_is_a_fail(self):
        b = result(status='hung', detail='cycle budget exhausted', hook=True)
        self.assertEqual(difftest.compare(result(), b, self.RETURNS), ('FAIL', 'status decomp hung: cycle budget exhausted'))

    def test_hook_that_did_not_run_is_a_fail(self):
        self.assertEqual(difftest.compare(result(1), result(1), self.RETURNS), ('FAIL', 'the hook did not run on the decomp side'))

    def test_callee_saved_register_changed_by_the_hook(self):
        b = result(hook=True)
        b['after']['r'][16] = f'{5:032x}'
        self.assertEqual(difftest.compare(result(), b, self.RETURNS),
                         ('FAIL', 'abi decomp changed s0: 0000000000000000 -> 0000000000000005'))

    def test_callee_saved_float_changed_by_the_recompiled_side(self):
        a = result()
        a['after']['f'][20] = '0x3F800000'
        self.assertEqual(difftest.compare(a, result(hook=True), self.RETURNS)[1], 'abi recomp changed f20: 0x00000000 -> 0x3F800000')

    def test_ram_bytes_differ(self):
        rng = {'at': '0x004E7280', 'len': 16, 'fnv': 'aa'}
        a, b = result(ram=[rng]), result(ram=[dict(rng, fnv='bb')], hook=True)
        am = {('ram', 0x4E7280): bytes([0, 0, 0, 0, 0, 0, 0x80, 0x3F] + [0] * 8)}
        bm = {('ram', 0x4E7280): bytes([0, 0, 0, 0, 0, 0, 0, 0x40] + [0] * 8)}
        verdict = difftest.compare(a, b, self.RETURNS, am, bm)
        self.assertEqual(verdict, ('FAIL', 'ram 0x004E7280+16 differs at +6: 80 3f 00 00 00 00 00 00 00 00 != 00 40 00 00 00 00 00 00 00 00'))

    def test_ram_written_by_one_side_only(self):
        rng = {'at': '0x004E7280', 'len': 16, 'fnv': 'aa'}
        self.assertEqual(difftest.compare(result(ram=[rng]), result(hook=True), self.RETURNS)[1], 'ram 0x004E7280+16 written by recomp only')
        self.assertEqual(difftest.compare(result(), result(ram=[rng], hook=True), self.RETURNS)[1], 'ram 0x004E7280+16 written by decomp only')

    def test_stack_ranges_are_ignored(self):
        a = result()
        a['stack'] = [{'at': '0x01FFDFF0', 'len': 16, 'fnv': 'aa'}]
        self.assertEqual(difftest.compare(a, result(hook=True), self.RETURNS)[0], 'PASS')

    def test_io_and_syscall_lists(self):
        write = {'at': '0x10003000', 'size': 4, 'value': '0x1'}
        self.assertEqual(difftest.compare(result(io=[write]), result(hook=True), self.RETURNS)[1], 'io differs at write 0: 1 recomp, 0 decomp')
        self.assertEqual(difftest.compare(result(syscalls=[100]), result(syscalls=[101], hook=True), self.RETURNS)[1], 'syscalls [100] != [101]')

    def test_fcr31_only_with_strict(self):
        a, b = result(), result(hook=True)
        b['after']['fcr31'] = '0x00800000'
        self.assertEqual(difftest.compare(a, b, self.RETURNS)[0], 'PASS')
        self.assertEqual(difftest.compare(a, b, self.RETURNS, strict=True), ('FAIL', 'fcr31 0x00000000 != 0x00800000'))

    def test_vu0_only_when_asked(self):
        a, b = result(), result(hook=True)
        b['after']['vf'][1] = f'{1:032x}'
        self.assertEqual(difftest.compare(a, b, self.RETURNS)[0], 'PASS')
        self.assertEqual(difftest.compare(a, b, self.RETURNS, compare_vu=True), ('FAIL', 'vu0 vf differs'))


class HelpersTest(unittest.TestCase):
    def test_parse_register(self):
        self.assertEqual(difftest.parse_register('v0'), ('r', 2))
        self.assertEqual(difftest.parse_register('ra'), ('r', 31))
        self.assertEqual(difftest.parse_register('f12'), ('f', 12))
        self.assertEqual(difftest.parse_register('r5'), ('r', 5))
        with self.assertRaises(difftest.SetupError):
            difftest.parse_register('f32')

    def test_read_mem(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, 'x.mem')
            with open(path, 'wb') as f:
                f.write(mem_record(1, 0x100, b'abcd') + mem_record(3, 0x200, b'xy'))
            self.assertEqual(difftest.read_mem(path), {('ram', 0x100): b'abcd', ('stack', 0x200): b'xy'})
            self.assertEqual(difftest.read_mem(os.path.join(d, 'missing.mem')), {})

    def test_case_names(self):
        self.assertEqual(difftest.case_name('/x/s003600.state', 0, 1), 's003600')
        self.assertEqual(difftest.case_name('/x/s003600.state', 7, 20), 's003600_c007')
        self.assertEqual(difftest.case_name('/x/e00389700_f000300_0001.entry.state', 0, 1), 'e00389700_f000300_0001')

    def test_entry_address(self):
        self.assertEqual(difftest.entry_address('e00389700_f000300_0001.entry.state'), 0x389700)
        self.assertIsNone(difftest.entry_address('s000300.state'))


class RunTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.snaps = os.path.join(self.tmp.name, 'snaps')
        self.out = os.path.join(self.tmp.name, 'out')
        os.mkdir(self.snaps)
        for name in ('s000300.state', 's000600.state', 'e00389700_f000300_0000.entry.state', 'e00216E40_f000300_0000.entry.state', 'CMD.txt'):
            open(os.path.join(self.snaps, name), 'wb').close()

    def run_tool(self, fake, *extra):
        text = io.StringIO()
        argv = ['difftest.py', '--ps2run', 'ps2run', '--elf', 'game.elf', '--function', '0x00389700', '--hook', 'pack_fixed16',
                '--snapshots', self.snaps, '--out', self.out, '--returns', 'v0'] + list(extra)
        return difftest.main(argv, runner=fake, out=text), text.getvalue().splitlines()

    def test_all_pass(self):
        fake = FakePs2run(lambda s, c: result(0x180), lambda s, c: result(0x180, hook=True))
        code, lines = self.run_tool(fake, '--args', '--f12 f:1.5 --a0 8 --a1 1')
        self.assertEqual(code, 0)
        self.assertEqual(lines, ['s000300 PASS cycles 43', 's000600 PASS cycles 43', 'pack_fixed16: 2 pass, 0 fail, 0 skip'])
        with open(os.path.join(self.out, 'summary.json'), encoding='utf-8') as f:
            summary = json.load(f)
        self.assertTrue(summary['accepted'])
        self.assertEqual(summary['cases'][0]['recomp'], os.path.join(self.out, 'recomp', 's000300.json'))
        self.assertEqual(summary['cases'][0]['args'], ['--f12', 'f:1.5', '--a0', '8', '--a1', '1'])

    def test_command_lines_and_environment(self):
        fake = FakePs2run(lambda s, c: result(1), lambda s, c: result(1, hook=True))
        self.run_tool(fake, '--args', '--a0 8', '--disc', 'disc.iso', '--env', 'PS2_VU_EXACT=1', '--timeout', '9')
        command, env, timeout = fake.commands[0]
        snapshot = os.path.join(self.snaps, 's000300.state')
        self.assertEqual(command, ['ps2run', 'game.elf', '--disc', 'disc.iso', '--snapshot', snapshot, '--call', '0x00389700', '--a0', '8',
                                   '--out', os.path.join(self.out, 'recomp', 's000300.json')])
        self.assertEqual(fake.commands[1][0][-4:], ['--hook', 'pack_fixed16', '--out', os.path.join(self.out, 'decomp', 's000300.json')])
        self.assertEqual((env['PS2_GPU_FALLBACK'], env['PS2_GS'], env['PS2_VU_EXACT'], timeout), ('1', 'off', '1', 9))

    def test_a_failing_case_gives_exit_1(self):
        fake = FakePs2run(lambda s, c: result(0x180), lambda s, c: result(0x180 if s == 's000300.state' else 0x181, hook=True))
        code, lines = self.run_tool(fake)
        self.assertEqual(code, 1)
        self.assertEqual(lines[1], 's000600 FAIL v0 0000000000000180 != 0000000000000181')
        self.assertEqual(lines[2], 'pack_fixed16: 1 pass, 1 fail, 0 skip')

    def test_recompiled_abort_skips_and_does_not_run_the_hook(self):
        fake = FakePs2run(lambda s, c: result(status='hung', detail='cycle budget exhausted'), lambda s, c: result(hook=True))
        code, lines = self.run_tool(fake)
        self.assertEqual(code, 0)
        self.assertEqual(lines[0], 's000300 SKIP recomp hung: cycle budget exhausted')
        self.assertEqual(len(fake.commands), 2)
        with open(os.path.join(self.out, 'summary.json'), encoding='utf-8') as f:
            self.assertFalse(json.load(f)['accepted'])

    def test_timeouts(self):
        code, lines = self.run_tool(FakePs2run(lambda s, c: None, lambda s, c: None), '--timeout', '5')
        self.assertEqual((code, lines[0]), (0, 's000300 SKIP recomp timeout: no result after 5 s'))
        code, lines = self.run_tool(FakePs2run(lambda s, c: result(), lambda s, c: None), '--timeout', '5')
        self.assertEqual((code, lines[0]), (1, 's000300 FAIL status decomp timeout: no result after 5 s'))

    def test_ps2run_setup_failure_is_exit_2(self):
        self.assertEqual(self.run_tool(FakePs2run(lambda s, c: 1, lambda s, c: 1))[0], 2)

    def test_missing_result_file_is_exit_2(self):
        def runner(command, env, timeout):
            return 0, ''
        self.assertEqual(self.run_tool(runner)[0], 2)

    def test_missing_snapshots_are_exit_2(self):
        argv = ['difftest.py', '--ps2run', 'p', '--elf', 'e', '--function', '0x389700', '--hook', 'h',
                '--snapshots', os.path.join(self.tmp.name, 'nowhere'), '--out', self.out]
        self.assertEqual(difftest.main(argv, runner=None, out=io.StringIO()), 2)

    def test_bad_arguments_are_exit_2(self):
        self.assertEqual(difftest.main(['difftest.py', '--function', '0x1'], runner=None, out=io.StringIO()), 2)
        self.assertEqual(self.run_tool(None, '--returns', 'q9')[0], 2)

    def test_args_file_makes_one_case_per_line(self):
        cases = os.path.join(self.tmp.name, 'cases.txt')
        with open(cases, 'w', encoding='utf-8') as f:
            f.write('# value bits signed\n--f12 f:1.5 --a0 8 --a1 1\n\n--f12 f:-1.5 --a0 8 --a1 1\n')
        fake = FakePs2run(lambda s, c: result(1), lambda s, c: result(1, hook=True))
        code, lines = self.run_tool(fake, '--args-file', cases, '-j', '4')
        self.assertEqual(code, 0)
        self.assertEqual([line.split()[0] for line in lines[:4]], ['s000300_c000', 's000300_c001', 's000600_c000', 's000600_c001'])
        self.assertEqual(lines[4], 'pack_fixed16: 4 pass, 0 fail, 0 skip')
        self.assertIn(['--f12', 'f:-1.5', '--a0', '8', '--a1', '1'], [c[0][6:12] for c in fake.commands])

    def test_args_snapshot_uses_only_this_functions_entry_snapshots(self):
        fake = FakePs2run(lambda s, c: result(1), lambda s, c: result(1, hook=True))
        code, lines = self.run_tool(fake, '--args', 'snapshot')
        self.assertEqual(code, 0)
        self.assertEqual(lines, ['e00389700_f000300_0000 PASS cycles 43', 'pack_fixed16: 1 pass, 0 fail, 0 skip'])
        self.assertNotIn('snapshot', fake.commands[0][0][6:])

    def test_differing_bytes_are_read_from_the_mem_files(self):
        rng = {'at': '0x00000100', 'len': 16, 'fnv': 'aa'}
        mems = {('s000300.state', False): mem_record(1, 0x100, bytes(16)), ('s000300.state', True): mem_record(1, 0x100, bytes(15) + b'\x01'),
                ('s000600.state', False): mem_record(1, 0x100, bytes(16)), ('s000600.state', True): mem_record(1, 0x100, bytes(15) + b'\x01')}
        fake = FakePs2run(lambda s, c: result(ram=[rng]), lambda s, c: result(ram=[dict(rng, fnv='bb')], hook=True), mems)
        code, lines = self.run_tool(fake)
        self.assertEqual(code, 1)
        self.assertEqual(lines[0], 's000300 FAIL ram 0x00000100+16 differs at +15: 00 != 01')


if __name__ == '__main__':
    unittest.main()
