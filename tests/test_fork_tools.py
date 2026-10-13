"""Where the guard tests find the fork's recompilers (tests/fork_tools.py)."""
import importlib.util, os, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('fork_tools', os.path.join(HERE, 'fork_tools.py'))
fork_tools = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fork_tools)


class FindTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.home = tmp.name
        self.built = os.path.join(self.home, 'b3', 'ps2-web-recomp', 'build', 'tools', 'eerecomp')

    def build(self):
        os.makedirs(os.path.dirname(self.built))
        open(self.built, 'w').close()

    def test_the_variable_names_the_tool(self):
        self.build()
        self.assertEqual(fork_tools.find('B3_EERECOMP', 'eerecomp', {'B3_EERECOMP': '/somewhere/eerecomp'}, self.home), '/somewhere/eerecomp')

    def test_without_the_variable_it_is_the_one_port_sh_runs(self):
        self.build()
        self.assertEqual(fork_tools.find('B3_EERECOMP', 'eerecomp', {}, self.home), self.built)

    def test_an_empty_variable_counts_as_unset(self):
        self.build()
        self.assertEqual(fork_tools.find('B3_EERECOMP', 'eerecomp', {'B3_EERECOMP': ''}, self.home), self.built)

    def test_nothing_built_and_no_variable_is_none(self):
        self.assertIsNone(fork_tools.find('B3_EERECOMP', 'eerecomp', {}, self.home))

    def test_another_tool_being_built_does_not_count(self):
        self.build()
        self.assertIsNone(fork_tools.find('B3_VURECOMP', 'vurecomp', {}, self.home))

    def test_the_skip_reason_names_both_ways(self):
        reason = fork_tools.missing('B3_VURECOMP', 'vurecomp')
        self.assertIn('B3_VURECOMP', reason)
        self.assertIn('build/tools/vurecomp', reason)


if __name__ == '__main__':
    unittest.main()
