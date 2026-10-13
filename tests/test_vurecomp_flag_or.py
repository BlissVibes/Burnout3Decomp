"""The fork's VU recompiler gives FMOR and FSOR their own meaning, and only FCOR the all-ones test (gap 012).

FMOR writes the MAC flags ORed with an integer register, FSOR the status flags ORed with a 12-bit immediate.
FCOR sets vi1 to 1 if the clip flags ORed with a 24-bit immediate are all ones, else to 0.

Runs the fork's `vurecomp` on a made-up five-instruction program and reads the generated C++
(the generated code keeps the integer registers a program uses in locals named i<n>).
Needs the tool: set B3_VURECOMP to its path (for example ~/b3/ps2-web-recomp/build/native-burnout3/vurecomp);
skipped without it. The interpreter and the WebAssembly JIT have the same three instructions and are not
covered here.
"""
import os, struct, subprocess, tempfile, unittest

VURECOMP = os.environ.get('B3_VURECOMP')

UPPER_NOP = 0x000002FF
UPPER_NOP_END = 0x400002FF       # E bit: the program ends after the next instruction
LOWER_NOP = 0x8000033C


def fmor(it, source):
    return (0x1B << 25) | (it << 16) | (source << 11)


def fsor(it, imm12):
    return (0x17 << 25) | ((imm12 >> 11) << 21) | (it << 16) | (imm12 & 0x7FF)


def fcor(imm24):
    return (0x13 << 25) | (imm24 & 0xFFFFFF)


# (lower, upper) pairs from microprogram address 0
PROGRAM = [
    (fmor(5, 6), UPPER_NOP),
    (fsor(7, 0x923), UPPER_NOP),
    (fcor(0xABCDEF), UPPER_NOP),
    (LOWER_NOP, UPPER_NOP_END),
    (LOWER_NOP, UPPER_NOP),
]


def program_key(entry):
    """The key vurecomp checks a recorded program against: FNV-1a over address, lower and upper word of each
    reachable instruction, started from the offset basis XOR the entry address."""
    h = 0xCBF29CE484222325 ^ entry
    for index, (lower, upper) in enumerate(PROGRAM):
        for value in (entry + index * 8, lower, upper):
            for byte in struct.pack('<I', value):
                h = ((h ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


@unittest.skipUnless(VURECOMP, 'B3_VURECOMP is not set')
class FlagOrTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        programs = os.path.join(cls.tmp.name, 'programs')
        os.mkdir(programs)
        code = b''.join(struct.pack('<II', lower, upper) for lower, upper in PROGRAM)
        with open(os.path.join(programs, '%016X_%04X.bin' % (program_key(0), 0)), 'wb') as f:
            f.write(code.ljust(16384, b'\0'))
        out = os.path.join(cls.tmp.name, 'gen')
        run = subprocess.run([VURECOMP, programs, out], capture_output=True, text=True)
        if run.returncode != 0:
            raise RuntimeError(run.stdout + run.stderr)
        cls.report = run.stdout + run.stderr
        cls.text = ''
        for name in sorted(os.listdir(out)):
            if name.startswith('vu1_0'):
                with open(os.path.join(out, name)) as f:
                    cls.text += f.read()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_the_made_up_program_is_accepted(self):
        self.assertNotIn('key mismatch', self.report)
        self.assertIn('1 VU1 programs', self.report)

    def test_fmor_writes_the_mac_flags_ored_with_the_register(self):
        self.assertIn('set_vi(i5, (fl.mac() | i6) & 0xFFFFu);', self.text)

    def test_fsor_writes_the_status_flags_ored_with_the_immediate(self):
        self.assertIn('set_vi(i7, (fl.status() | %du) & 0xFFFu);' % 0x923, self.text)

    def test_only_fcor_tests_for_all_ones(self):
        self.assertIn('set_vi(i1, ((fl.clip() | %du) & 0xFFFFFFu) == 0xFFFFFFu ? 1u : 0u);' % 0xABCDEF, self.text)
        self.assertNotIn('== 0xFFFFu', self.text)
        self.assertNotIn('== 0xFFFu', self.text)


if __name__ == '__main__':
    unittest.main()
