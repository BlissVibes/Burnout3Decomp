import importlib.util, os, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('scan_log', os.path.join(HERE, '..', 'tools', 'scan-log.py'))
scan_log = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scan_log)

CLEAN = """[disc] disc: 1200 files, 1400304 sectors, layer 1 at 0
[web] presenting through WebGPU
[ee] frame 3600
"""

GAPPY = """[disc] disc: 1200 files, 1400304 sectors, layer 1 at 0
[sys] unhandled syscall 0x7A from 0x00123456
[sys] unhandled syscall 0x7A from 0x00123456
[iop] missing libsd #22 called from 0x000A1B2C
[ipu] command 0x12345678 is not implemented
[vu1] start at 0x40 through CMSAR1 (not implemented)
[io] open host0:/foo (flags 0x1) on unsupported device
[zzz] something new the scanner has never seen
[ee] halted: jump to unmapped 0x00000000
"""


class ScanLogTest(unittest.TestCase):
    def test_clean_run_is_clean(self):
        report = scan_log.scan(CLEAN.splitlines(True))
        self.assertEqual(report.halted, None)
        self.assertEqual(sum(sum(c.values()) for c in report.gaps.values()), 0)
        self.assertTrue(report.clean())

    def test_gaps_are_counted_by_distinct_site(self):
        report = scan_log.scan(GAPPY.splitlines(True))
        self.assertEqual(report.gaps['unhandled_syscall']['0x7A from 0x00123456'], 2)
        self.assertEqual(report.gaps['iop_missing']['libsd #22 called from 0x000A1B2C'], 1)
        self.assertEqual(report.gaps['ipu_unimplemented']['0x12345678'], 1)
        self.assertEqual(report.gaps['vu1_cmsar1']['0x40'], 1)
        self.assertEqual(report.gaps['io_unsupported']['host0:/foo'], 1)
        self.assertEqual(report.halted, 'jump to unmapped 0x00000000')
        self.assertFalse(report.clean())

    def test_unknown_bracket_tags_are_surfaced(self):
        report = scan_log.scan(GAPPY.splitlines(True))
        self.assertEqual(report.unknown_tags['zzz'], 1)
        self.assertNotIn('disc', report.unknown_tags)

    def test_render_mentions_every_gap_and_the_halt(self):
        text = scan_log.render(scan_log.scan(GAPPY.splitlines(True)))
        for needle in ('unhandled_syscall', '0x7A from 0x00123456', 'x2', 'libsd #22', 'halted: jump to unmapped', 'unknown tags: zzz'):
            self.assertIn(needle, text)


if __name__ == '__main__':
    unittest.main()
