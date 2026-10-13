import importlib.util, io, os, tempfile, unittest
from contextlib import redirect_stderr, redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('profile_report', os.path.join(HERE, '..', 'tools', 'profile-report.py'))
profile_report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile_report)

# A made-up executable layout: three functions, as (address, demangled name).
SYMBOLS = [(0x1000, 'gen::unit_0012(ps2::Cpu&, unsigned int, unsigned int)'),
           (0x3000, 'ps2::gs::raster::draw(ps2::gs::Vertex const*)'),
           (0x4000, 'ps2::iop::execute(ps2::iop::Cpu&)')]

PROFILE = """# samples 100 lost 0
1000 40
1010 10
3000 25
4ff0 5
^ 3000 4
@ 15 memcpy
@ 5 /lib/x86_64-linux-gnu/libc.so.6
"""

# Two functions of a generated unit, written the way the recompiler writes them.
UNIT = """void unit_0012(Cpu &c, std::uint32_t entry, std::uint32_t depth) {
dispatch_entry:
    switch (entry) {
    case 1u: if (g_hooks) [[unlikely]] { if (enter(c, 0x00140000u)) return; } goto L_00140000;
    case 2u: goto L_00140008;
    case 3u: if (g_hooks) [[unlikely]] { if (enter(c, 0x00140020u)) return; } goto L_00140020;
    default: bad_entry(c, 0x00140000u); return;
    }
L_00140000:
    c.budget -= 2;
L_00140008:
    c.budget -= 1;
L_00140020:
    c.budget -= 3;
L_00140030:
    c.budget -= 1;
}
"""


class ProfileReportTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.profile = os.path.join(self.tmp.name, 'run.prof')
        with open(self.profile, 'w') as f:
            f.write(PROFILE)
        self.read_symbols = profile_report.read_symbols
        profile_report.read_symbols = lambda binary: SYMBOLS
        self.addCleanup(setattr, profile_report, 'read_symbols', self.read_symbols)

    def run_tool(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = profile_report.main(['profile-report.py', *args])
        return code, out.getvalue(), err.getvalue()

    def test_reads_every_kind_of_line(self):
        inside, callers, outside, samples = profile_report.read_profile(self.profile)
        self.assertEqual(samples, 100)
        self.assertEqual(inside, {0x1000: 40, 0x1010: 10, 0x3000: 25, 0x4FF0: 5})
        self.assertEqual(callers, {0x3000: 4})
        self.assertEqual(outside, {'memcpy': 15, '/lib/x86_64-linux-gnu/libc.so.6': 5})

    def test_a_bucket_belongs_to_the_function_that_starts_at_or_below_it(self):
        starts = [a for a, _ in SYMBOLS]
        self.assertTrue(profile_report.name_at(SYMBOLS, starts, 0x1010).startswith('gen::unit_0012'))
        self.assertTrue(profile_report.name_at(SYMBOLS, starts, 0x2FF0).startswith('gen::unit_0012'))
        self.assertTrue(profile_report.name_at(SYMBOLS, starts, 0x3000).startswith('ps2::gs::raster::draw'))
        self.assertEqual(profile_report.name_at(SYMBOLS, starts, 0x0FF0), '?')

    def test_kinds(self):
        self.assertEqual(profile_report.kind_of('gen::unit_0012(ps2::Cpu&, unsigned int, unsigned int)'), 'EE generated code')
        self.assertEqual(profile_report.kind_of('ps2::vu::run_fast(ps2::vu::Unit&)'), 'VU')
        self.assertEqual(profile_report.kind_of('ps2::gs::raster::draw(ps2::gs::Vertex const*)'), 'GS and rasteriser')
        self.assertEqual(profile_report.kind_of('ps2::iop::spu2::reset()'), 'IOP and devices')
        self.assertEqual(profile_report.kind_of('ps2::dispatch(ps2::Cpu&, unsigned int, unsigned int)'), 'runtime')
        self.assertEqual(profile_report.kind_of('floorf'), 'other')

    def test_report_adds_up_by_kind_and_by_function(self):
        code, out, _ = self.run_tool(self.profile, 'ps2run', '--top', '3')
        self.assertEqual(code, 0)
        self.assertIn('100 samples (100 counted)', out)
        self.assertIn(' 50.0%  EE generated code', out)
        self.assertIn(' 25.0%  GS and rasteriser', out)
        self.assertIn(' 20.0%  other libraries', out)
        self.assertIn('  5.0%  IOP and devices', out)
        lines = out.split('top 3 host functions:\n')[1].splitlines()
        self.assertEqual(lines[0].strip(), '50.0%  gen::unit_0012')
        self.assertEqual(lines[1].strip(), '25.0%  ps2::gs::raster::draw')
        self.assertEqual(lines[2].strip(), '15.0%  [lib] memcpy')

    def test_guest_functions_from_generated_lines(self):
        generated = os.path.join(self.tmp.name, 'gen')
        os.mkdir(generated)
        with open(os.path.join(generated, 'unit_0012.cpp'), 'w') as f:
            f.write(UNIT)
        lines = UNIT.split('\n')
        line_of = lambda text: lines.index(text) + 1
        # addr2line would say: bucket 0x1000 is the statement after L_00140008 (inside the first function),
        # bucket 0x1010 the statement after L_00140030 (inside the second).
        answers = {0x1000: '/x/gen/unit_0012.cpp:%d' % line_of('    c.budget -= 1;'),
                   0x1010: '/x/gen/unit_0012.cpp:%d (discriminator 2)' % (line_of('L_00140030:') + 1)}

        class Done:
            def __init__(self, stdout):
                self.stdout = stdout

        def fake_run(command, input=None, **kwargs):
            self.assertEqual(command[0], 'addr2line')
            return Done('\n'.join(answers[int(a, 16)] for a in input.split('\n')) + '\n')

        real_run = profile_report.subprocess.run
        profile_report.subprocess.run = fake_run
        self.addCleanup(setattr, profile_report.subprocess, 'run', real_run)
        inside, _, _, _ = profile_report.read_profile(self.profile)
        starts = [a for a, _ in SYMBOLS]
        hits, unknown = profile_report.guest_table('ps2run', generated, inside, SYMBOLS, starts)
        self.assertEqual(unknown, 0)
        self.assertEqual(dict(hits), {0x00140000: 40, 0x00140020: 10})

    def test_bad_arguments_and_missing_files(self):
        self.assertEqual(self.run_tool()[0], 2)
        self.assertEqual(self.run_tool(self.profile)[0], 2)
        self.assertEqual(self.run_tool(self.profile, 'ps2run', '--nonsense')[0], 2)
        self.assertEqual(self.run_tool(os.path.join(self.tmp.name, 'missing.prof'), 'ps2run')[0], 1)


if __name__ == '__main__':
    unittest.main()
