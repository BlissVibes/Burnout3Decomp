"""The fork's recompiler puts the hook check where a function is entered, and nowhere else.

Runs the fork's `eerecomp` on a made-up three-function program and reads the generated C++.
Needs the tool: set B3_EERECOMP to its path (for example ~/b3/ps2-web-recomp/build/native-burnout3/eerecomp);
skipped without it.
"""
import os, re, struct, subprocess, tempfile, unittest

EERECOMP = os.environ.get('B3_EERECOMP')
BASE = 0x00100000

NOP = 0x00000000
JR_RA = 0x03E00008


def jal(target):
    return 0x0C000000 | ((target & 0x0FFFFFFF) >> 2)


def bne_a0_zero(pc, target):
    return 0x14800000 | (((target - (pc + 4)) >> 2) & 0xFFFF)


# main: calls counter and leaf, returns.  counter: a loop whose head is the function's first instruction.
MAIN, COUNTER, LEAF = BASE, BASE + 0x18, BASE + 0x2C
CODE = [
    jal(COUNTER), NOP,                                    # 0x00
    jal(LEAF), NOP,                                       # 0x08
    JR_RA, NOP,                                           # 0x10
    0x2484FFFF,                                           # 0x18 counter: addiu a0, a0, -1
    bne_a0_zero(COUNTER + 4, COUNTER), NOP,               # 0x1C bne a0, zero, counter
    JR_RA, NOP,                                           # 0x24
    JR_RA, NOP,                                           # 0x2C leaf
]


def elf():
    code = b''.join(struct.pack('<I', w) for w in CODE)
    offset = 0x100
    header = b'\x7fELF\x01\x01\x01' + bytes(9) + struct.pack('<HHIIIIIHHHHHH', 2, 8, 1, MAIN, 52, 0, 0, 52, 32, 1, 40, 0, 0)
    segment = struct.pack('<IIIIIIII', 1, offset, BASE, BASE, len(code), len(code), 5, 16)
    return (header + segment).ljust(offset, b'\0') + code


@unittest.skipUnless(EERECOMP, 'B3_EERECOMP is not set')
class HookCheckTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        path = os.path.join(cls.tmp.name, 'three.elf')
        with open(path, 'wb') as f:
            f.write(elf())
        entries = os.path.join(cls.tmp.name, 'entries.txt')
        open(entries, 'w').close()
        out = os.path.join(cls.tmp.name, 'gen')
        run = subprocess.run([EERECOMP, path, '--entries', entries, '--out', out], capture_output=True, text=True)
        if run.returncode != 0:
            raise RuntimeError(run.stdout + run.stderr)
        cls.text = ''
        for name in sorted(os.listdir(out)):
            if name.startswith('unit_'):
                with open(os.path.join(out, name)) as f:
                    cls.text += f.read()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def arm(self, address):
        """The switch arm that enters the unit at this address."""
        found = re.findall(r'^\s*case \d+u:.*goto \w*%08X\w*;.*$' % address, self.text, re.M | re.I)
        self.assertEqual(len(found), 1, 'one switch arm for 0x%08X, got %r' % (address, found))
        return found[0]

    def test_a_function_entered_through_the_unit_is_offered_to_the_hooks(self):
        for address in (MAIN, COUNTER, LEAF):
            self.assertRegex(self.arm(address), r'if \(g_hooks\) \[\[unlikely\]\] \{ if \(enter\(c, 0x%08Xu?\)\) return; \} goto' % address)

    def test_a_return_address_is_not(self):
        for address in (BASE + 0x08, BASE + 0x10):
            self.assertNotIn('enter(', self.arm(address))

    def test_the_check_is_only_in_the_switch(self):
        # A loop back to the first instruction of a function is a goto to its label: it must not meet the check.
        self.assertEqual(self.text.count('enter('), 3)
        loop = re.search(r'goto (\w*%08X\w*);' % COUNTER, self.text.split('default:')[1], re.I)
        self.assertIsNotNone(loop, 'the loop branch is a goto to the label')
        label = re.search(r'^%s:\n(.*)$' % re.escape(loop.group(1)), self.text, re.M)
        self.assertIsNotNone(label)
        self.assertNotIn('enter(', label.group(1))


if __name__ == '__main__':
    unittest.main()
