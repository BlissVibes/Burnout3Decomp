import importlib.util, os, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('scan_log', os.path.join(HERE, '..', 'tools', 'scan-log.py'))
scan_log = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scan_log)

# Shaped like a real ps2run log (headless run 002): the run ends at the frame
# limit, then the runtime prints its end-of-run statistics and the "stopped" line.
CLEAN = r"""[gpu] llvmpipe (LLVM 20.1.2, 256 bits)
[disc] disc: 834 files, 1400304 sectors, layer 1 at 0
[mc] card in slot 1: /memcard/Mcd001.ps2 (new, unformatted)
[sif] IOP reboot requested with "rom0:UDNL cdrom0:\IOP\DNAS280.IMG;1"
[vu0] cfc2 28
[frame] 25 prims 0
[ee] halted: frame limit reached
[thread] 1 status 1 prio 1 wait 0/11 pc 0x0010C2F1 ra 0x0010C2E4
[sema] 3 count 0 waiters 1
[gs] 1523 primitives drawn, register writes:
[dma] starts: ch0=0 ch1=0 ch2=0 ch3=0 ch4=0 ch5=0 ch6=0 ch7=0 ch8=0 ch9=0
[gif] path 1 tags: packed 0 reglist 0 image 0
[vif0]
[vif1] 01:1 02:1 03:1 04:1 05:1 06:1 20:1
[ee] stopped after 1800 frames, 10616841860 cycles: frame limit reached
"""

# Headless run 002 really looked like this: 1800 frames, nothing drawn.
NO_DRAW = CLEAN.replace('[gs] 1523 primitives drawn', '[gs] 0 primitives drawn')
NO_GS_LINE = ''.join(line for line in CLEAN.splitlines(True) if not line.startswith('[gs]'))

GAPPY = """[disc] disc: 834 files, 1400304 sectors, layer 1 at 0
[sys] unhandled syscall 0x7A from 0x00123456
[sys] unhandled syscall 0x7A from 0x00123456
[iop] missing libsd #22 called from 0x000A1B2C
[ipu] command 0x12345678 is not implemented
[vu1] start at 0x40 through CMSAR1 (not implemented)
[io] open host0:/foo (flags 0x1) on unsupported device
[zzz] something new the scanner has never seen
[ee]   r29 01FFFC90 00000000 00100844 00000000
[ee] halted: missing code at 0x004D5330 (ra 0x00100844)
[thread] 1 status 1 prio 1 wait 0/11 pc 0x0010C2F1 ra 0x0010C2E4
[ee] stopped after 2 frames, 13707699 cycles: missing code at 0x004D5330 (ra 0x00100844)
"""


class ScanLogTest(unittest.TestCase):
    def test_clean_run_is_clean(self):
        report = scan_log.scan(CLEAN.splitlines(True))
        self.assertEqual(report.halted, None)
        self.assertEqual(report.frames, 1800)
        self.assertEqual(sum(sum(c.values()) for c in report.gaps.values()), 0)
        self.assertEqual(sum(report.unknown_tags.values()), 0)
        self.assertTrue(report.clean())

    def test_gaps_are_counted_by_distinct_site(self):
        report = scan_log.scan(GAPPY.splitlines(True))
        self.assertEqual(report.gaps['unhandled_syscall']['0x7A from 0x00123456'], 2)
        self.assertEqual(report.gaps['iop_missing']['libsd #22 called from 0x000A1B2C'], 1)
        self.assertEqual(report.gaps['ipu_unimplemented']['0x12345678'], 1)
        self.assertEqual(report.gaps['vu1_cmsar1']['0x40'], 1)
        self.assertEqual(report.gaps['io_unsupported']['host0:/foo'], 1)
        self.assertEqual(report.halted, 'missing code at 0x004D5330 (ra 0x00100844)')
        self.assertEqual(report.frames, 2)
        self.assertFalse(report.clean())

    def test_unknown_bracket_tags_are_surfaced(self):
        report = scan_log.scan(GAPPY.splitlines(True))
        self.assertEqual(report.unknown_tags['zzz'], 1)
        self.assertNotIn('disc', report.unknown_tags)

    def test_render_mentions_every_gap_and_the_halt(self):
        text = scan_log.render(scan_log.scan(GAPPY.splitlines(True)))
        for needle in ('unhandled_syscall', '0x7A from 0x00123456', 'x2', 'libsd #22', 'frames reached: 2', 'halted: missing code at 0x004D5330', 'unknown tags: zzz'):
            self.assertIn(needle, text)

    def test_render_says_the_frame_limit_is_not_a_halt(self):
        text = scan_log.render(scan_log.scan(CLEAN.splitlines(True)), 1800)
        for needle in ('frames reached: 1800 of 1800', 'halted: no (frame limit reached)', 'gaps: 0 distinct', 'result: CLEAN'):
            self.assertIn(needle, text)

    def test_a_real_halt_alone_is_not_clean(self):
        log = "[ee] halted: missing code at 0x004D5330 (ra 0x00100844)\n[ee] stopped after 2 frames, 13707699 cycles: missing code at 0x004D5330 (ra 0x00100844)\n"
        report = scan_log.scan(log.splitlines(True))
        self.assertEqual(report.frames, 2)
        self.assertEqual(report.halted, 'missing code at 0x004D5330 (ra 0x00100844)')
        self.assertFalse(report.clean())

    def test_a_log_that_just_stops_is_not_clean_when_frames_are_expected(self):
        report = scan_log.scan(CLEAN.splitlines(True))          # stopped after 1800 frames
        self.assertTrue(report.clean(expect_frames=1800))
        self.assertFalse(report.clean(expect_frames=3600))
        truncated = scan_log.scan(CLEAN.splitlines(True)[:-1])   # no "stopped after" line
        self.assertEqual(truncated.frames, None)
        self.assertFalse(truncated.clean(expect_frames=1800))
        self.assertTrue(truncated.clean())                       # no expectation given: old behaviour

    def test_unknown_tags_make_the_run_not_clean(self):
        report = scan_log.scan("[zzz] new message\n[ee] stopped after 100 frames, 5 cycles: frame limit reached\n".splitlines(True))
        self.assertFalse(report.clean())
        self.assertIn('unknown tags: zzz', scan_log.render(report))

    def test_module_load_and_iop_output_lines_are_known(self):
        log = ("[loadfile] cdrom0:\IOP\SIO2MAN.IRX;1\n"
               "[iop-tty] USB Driver (Version 1.1.0)\n"
               "[iop-io] read 0x1F808280 = 0x0 (pc 0x00020184)\n")
        report = scan_log.scan((CLEAN + log).splitlines(True))
        self.assertEqual(sum(report.unknown_tags.values()), 0)
        self.assertTrue(report.clean(expect_frames=1800))

    def test_hyphenated_tags_are_read_whole(self):
        report = scan_log.scan("[iop-zzz] new message\n[zzz-tty] another\n".splitlines(True))
        self.assertEqual(report.unknown_tags['iop-zzz'], 1)
        self.assertEqual(report.unknown_tags['zzz-tty'], 1)
        self.assertNotIn('iop', report.unknown_tags)

    def test_a_missing_iop_import_is_still_a_gap_among_known_lines(self):
        log = "[loadfile] cdrom0:\IOP\LGAUD.IRX;1\n[iop] missing modload #18 called from 0x0006C1B4\n[iop-tty] lgAud Version 1.10.001\n"
        report = scan_log.scan((CLEAN + log).splitlines(True))
        self.assertEqual(report.gaps['iop_missing']['modload #18 called from 0x0006C1B4'], 1)
        self.assertEqual(sum(report.unknown_tags.values()), 0)
        self.assertFalse(report.clean(expect_frames=1800))

    def test_a_frame_limit_run_that_drew_nothing_is_not_clean(self):
        report = scan_log.scan(NO_DRAW.splitlines(True))
        self.assertEqual(report.primitives, 0)
        self.assertEqual(report.halted, None)
        self.assertFalse(report.clean())
        self.assertFalse(report.clean(expect_frames=1800))
        text = scan_log.render(report, 1800)
        for needle in ('primitives drawn: 0', 'result: GAPS'):
            self.assertIn(needle, text)

    def test_a_frame_limit_run_that_drew_is_clean(self):
        report = scan_log.scan(CLEAN.splitlines(True))
        self.assertEqual(report.primitives, 1523)
        self.assertTrue(report.clean(expect_frames=1800))
        self.assertFalse(report.clean(expect_frames=1800, min_primitives=2000))
        self.assertIn('primitives drawn: 1523', scan_log.render(report, 1800))

    def test_a_log_without_a_primitives_line_stays_clean(self):
        report = scan_log.scan(NO_GS_LINE.splitlines(True))
        self.assertEqual(report.primitives, None)
        self.assertTrue(report.clean(expect_frames=1800))
        self.assertIn('primitives drawn: unknown', scan_log.render(report, 1800))

    def test_main_min_primitives(self):
        import os, tempfile
        with tempfile.NamedTemporaryFile('w', suffix='.log', delete=False) as f:
            f.write(NO_DRAW)
        try:
            self.assertEqual(scan_log.main(['scan-log.py', f.name, '--expect-frames', '1800']), 1)
            self.assertEqual(scan_log.main(['scan-log.py', f.name, '--expect-frames', '1800', '--min-primitives', '0']), 0)
        finally:
            os.unlink(f.name)

    def test_main_exit_codes(self):
        import io, os, tempfile
        with tempfile.NamedTemporaryFile('w', suffix='.log', delete=False) as f:
            f.write(CLEAN)
        try:
            self.assertEqual(scan_log.main(['scan-log.py', f.name]), 0)
            self.assertEqual(scan_log.main(['scan-log.py', f.name, '--expect-frames', '1800']), 0)
            self.assertEqual(scan_log.main(['scan-log.py', f.name, '--expect-frames', '9999']), 1)
        finally:
            os.unlink(f.name)


if __name__ == '__main__':
    unittest.main()
