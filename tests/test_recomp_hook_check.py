"""The fork's recompiler puts the hook check where a function is entered, and nowhere else.

Runs the fork's `eerecomp` on a made-up three-function program and reads the generated C++, then on a
second program whose functions are only ever called through a pointer.
Needs the tool: B3_EERECOMP names it; without the variable it is the one tools/port.sh translates the game
with, ~/b3/ps2-web-recomp/build/tools/eerecomp (tests/fork_tools.py). Skipped when neither exists.
"""
import importlib.util, os, re, struct, subprocess, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('fork_tools', os.path.join(HERE, 'fork_tools.py'))
fork_tools = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fork_tools)

EERECOMP = fork_tools.find('B3_EERECOMP', 'eerecomp')
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


def elf(words=CODE):
    code = b''.join(struct.pack('<I', w) for w in words)
    offset = 0x100
    header = b'\x7fELF\x01\x01\x01' + bytes(9) + struct.pack('<HHIIIIIHHHHHH', 2, 8, 1, MAIN, 52, 0, 0, 52, 32, 1, 40, 0, 0)
    segment = struct.pack('<IIIIIIII', 1, offset, BASE, BASE, len(code), len(code), 5, 16)
    return (header + segment).ljust(offset, b'\0') + code


def la_a0(address):
    """lui a0, high; addiu a0, a0, low: the address as a constant in the code."""
    low = address & 0xFFFF
    return [0x3C040000 | (((address - (low - 0x10000 if low & 0x8000 else low)) >> 16) & 0xFFFF), 0x24840000 | low]


# A function the program only hands on as a pointer (its address is a constant in the code), one whose
# address is only a word in the data, and an address in the data that is not the start of a function.
# The recompiler finds all three as entries. main and called are found by the call.
CALLBACK, VIA_DATA, CALLED = BASE + 0x18, BASE + 0x28, BASE + 0x34
INNER = CALLED + 4
POINTED = la_a0(CALLBACK) + [                             # 0x00 main: a0 = callback
    jal(CALLED), NOP,                                     # 0x08
    JR_RA, NOP,                                           # 0x10
    0x24020001, JR_RA, NOP,                               # 0x18 callback: addiu v0, zero, 1
    NOP,                                                  # 0x24 padding, never run
    0x24020002, JR_RA, NOP,                               # 0x28 via_data: addiu v0, zero, 2
    0x2484FFFF,                                           # 0x34 called: addiu a0, a0, -1
    0x2484FFFF,                                           # 0x38 inner: the same again, in the middle of called
    JR_RA, NOP,                                           # 0x3C
    VIA_DATA,                                             # 0x44 data: a function pointer
    INNER,                                                # 0x48 data: an address inside a function
]


class Generated(unittest.TestCase):
    """The C++ the recompiler writes for one made-up program."""
    WORDS = None

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        path = os.path.join(cls.tmp.name, 'made_up.elf')
        with open(path, 'wb') as f:
            f.write(elf(cls.WORDS))
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

    def assertOffered(self, address):
        self.assertRegex(self.arm(address), r'if \(g_hooks\) \[\[unlikely\]\] \{ if \(enter\(c, 0x%08Xu?\)\) return; \} goto' % address)


@unittest.skipUnless(EERECOMP, fork_tools.missing('B3_EERECOMP', 'eerecomp'))
class HookCheckTest(Generated):
    WORDS = CODE

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


@unittest.skipUnless(EERECOMP, fork_tools.missing('B3_EERECOMP', 'eerecomp'))
class PointedFunctionTest(Generated):
    """Functions the recompiler knows only because something points at them."""
    WORDS = POINTED

    def test_a_function_known_only_from_an_address_in_the_code_is_offered_to_the_hooks(self):
        self.assertOffered(CALLBACK)

    def test_a_function_known_only_from_a_pointer_in_the_data_is_offered_to_the_hooks(self):
        self.assertOffered(VIA_DATA)          # it starts after a return and a word of padding

    def test_an_address_inside_a_function_is_an_entry_without_the_check(self):
        self.assertNotIn('enter(', self.arm(INNER))

    def test_the_functions_found_by_a_call_keep_the_check_and_nothing_else_gets_one(self):
        self.assertOffered(BASE)
        self.assertOffered(CALLED)
        self.assertEqual(self.text.count('enter('), 4)


if __name__ == '__main__':
    unittest.main()
