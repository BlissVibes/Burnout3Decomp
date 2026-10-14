"""The recompiler writes each unit's entry table as a list of entry offsets, not as a dense table.

A unit's entry table maps every word of the unit to the id of the switch arm that enters it there (0: not
an entry). Nearly all of it is zero, and written out in full it cost the page 1.85 MB of initialised data,
18,402 data segments and a 542 KB start-up function. The recompiler now leaves the table zero, lists the
word offsets of the unit's entries in id order, and register_generated_code() fills the table from the
list (ps2::fill_entries) before it registers the units.

Runs the fork's `eerecomp` on the made-up programs of test_recomp_hook_check.py and reads the C++.
Needs the tool: B3_EERECOMP names it (tests/fork_tools.py). Skipped when it does not exist.
"""
import importlib.util, os, re, subprocess, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))


def load(name):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


fork_tools = load('fork_tools')
hook_check = load('test_recomp_hook_check')
EERECOMP = fork_tools.find('B3_EERECOMP', 'eerecomp')
UNIT_WORDS = 4096          # a 16 KiB unit, the recompiler's default


class EntryTable:
    """The recompiler's output for one made-up program: unit files by name, and table.cpp."""
    WORDS = None

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        path = os.path.join(cls.tmp.name, 'made_up.elf')
        with open(path, 'wb') as f:
            f.write(hook_check.elf(cls.WORDS))
        entries = os.path.join(cls.tmp.name, 'entries.txt')
        open(entries, 'w').close()
        out = os.path.join(cls.tmp.name, 'gen')
        run = subprocess.run([EERECOMP, path, '--entries', entries, '--out', out], capture_output=True, text=True)
        if run.returncode != 0:
            raise RuntimeError(run.stdout + run.stderr)
        cls.units = {}
        for name in sorted(os.listdir(out)):
            with open(os.path.join(out, name)) as f:
                text = f.read()
            if name.startswith('unit_'):
                cls.units[name[:-4]] = text
            elif name == 'table.cpp':
                cls.table = text
            elif name == 'units.hpp':
                cls.header = text

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def arms(self, text):
        """The unit's switch arms: id -> guest address."""
        return {int(i): int(a, 16) for i, a in re.findall(r'^\s*case (\d+)u:.*goto L_([0-9A-F]{8});$', text, re.M)}

    def test_the_table_is_left_zero(self):
        for unit, text in self.units.items():
            number = unit[len('unit_'):]
            declared = re.search(r'^std::uint16_t entries_%s\[%d\];$' % (number, UNIT_WORDS), text, re.M)
            self.assertIsNotNone(declared, '%s declares its table without an initialiser' % unit)
            self.assertNotIn('entries_%s[%d] = {' % (number, UNIT_WORDS), text, 'no dense initialiser')
            self.assertRegex(self.header, r'extern std::uint16_t entries_%s\[\];' % number)

    def test_the_offsets_in_id_order_give_the_table_the_switch_reads(self):
        for unit, text in self.units.items():
            number = unit[len('unit_'):]
            base = int(re.search(r"bad_entry\(c, 0x([0-9A-F]{8})u\)", text).group(1), 16)
            listed = re.search(r'^const std::uint16_t entry_offsets_%s\[\] = \{([^}]*)\};$' % number, text, re.M)
            self.assertIsNotNone(listed, unit)
            offsets = [int(v) for v in listed.group(1).replace(',', ' ').split()]
            arms = self.arms(text)
            self.assertEqual(sorted(arms), list(range(1, len(offsets) + 1)), 'one offset per arm, ids from 1')
            for id, address in arms.items():
                self.assertEqual(offsets[id - 1], (address - base) >> 2, 'arm %d enters at 0x%08X' % (id, address))

    def test_registration_fills_every_table_before_the_units_are_registered(self):
        body = re.search(r'void register_generated_code\(\) \{(.*)\}', self.table).group(1)
        fill, register = body.find('ps2::fill_entries(gen::kEntryLists, '), body.find('ps2::register_units(')
        self.assertGreaterEqual(fill, 0, body)
        self.assertLess(fill, register, 'the tables are filled before anything can dispatch')
        rows = re.findall(r'^\s*\{entries_([0-9A-F]{4}), entry_offsets_\1, (\d+)u\},$', self.table, re.M)
        self.assertEqual(sorted(n for n, _ in rows), sorted(u[len('unit_'):] for u in self.units))
        for number, count in rows:
            self.assertEqual(int(count), len(self.arms(self.units['unit_' + number])))
        self.assertIn('ps2::fill_entries(gen::kEntryLists, %du);' % len(rows), body)


@unittest.skipUnless(EERECOMP, fork_tools.missing('B3_EERECOMP', 'eerecomp'))
class CalledFunctionsTest(EntryTable, unittest.TestCase):
    WORDS = hook_check.CODE


@unittest.skipUnless(EERECOMP, fork_tools.missing('B3_EERECOMP', 'eerecomp'))
class PointedFunctionsTest(EntryTable, unittest.TestCase):
    WORDS = hook_check.POINTED


if __name__ == '__main__':
    unittest.main()
