import importlib.util, os, shutil, subprocess, sys, tempfile, time, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, '..', 'tools', 'watch-run.py')
spec = importlib.util.spec_from_file_location('watch_run', TOOL)
watch_run = importlib.util.module_from_spec(spec)
spec.loader.exec_module(watch_run)


class VerdictTest(unittest.TestCase):
    def test_within_both_limits(self):
        self.assertIsNone(watch_run.verdict(1000, 10, 1000, 10))

    def test_log_cap(self):
        self.assertEqual(watch_run.verdict(1001, 0, 1000, 10), 'log cap hit: 1001 bytes of log, cap 1000')

    def test_timeout(self):
        self.assertEqual(watch_run.verdict(0, 11, 1000, 10), 'run timeout hit: 11 seconds, limit 10')

    def test_zero_turns_a_limit_off(self):
        self.assertIsNone(watch_run.verdict(10 ** 12, 10 ** 6, 0, 0))

    def test_defaults_are_one_gigabyte_and_thirty_minutes(self):
        self.assertEqual(watch_run.DEFAULT_CAP_BYTES, 1073741824)
        self.assertEqual(watch_run.DEFAULT_TIMEOUT, 1800)

    def test_bad_arguments_exit_2(self):
        self.assertEqual(watch_run.main(['watch-run.py', '--log', 'x']), 2)
        self.assertEqual(watch_run.main(['watch-run.py', '--log', 'x', '--match', 'y', '--marker', 'z', '--timeout', 'soon']), 2)


@unittest.skipUnless(sys.platform.startswith('linux'), 'needs /proc')
class WatchTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.log = os.path.join(self.dir, 'explore.log')
        self.marker = os.path.join(self.dir, 'marker')
        # A stand-in for ps2run: a copy of sleep under another name, run from the
        # temporary directory. The watcher matches on that directory's path, so
        # nothing else on the machine can match.
        self.program = os.path.join(self.dir, 'fakerun')
        shutil.copy(shutil.which('sleep'), self.program)
        self.children = []

    def tearDown(self):
        for child in self.children:
            if child.poll() is None:
                child.kill()
            child.wait()
        shutil.rmtree(self.dir)

    def start(self, argv):
        child = subprocess.Popen(argv, stderr=subprocess.DEVNULL)
        self.children.append(child)
        return child

    def run_stand_in(self, name='fakerun'):
        program = os.path.join(self.dir, name)
        if not os.path.exists(program):
            shutil.copy(self.program, program)
        return self.start([program, '60'])

    def watcher(self, *extra):
        return self.start([sys.executable, TOOL, '--log', self.log, '--match', self.dir, '--marker', self.marker,
                           '--name', 'fakerun', '--interval', '0.1'] + list(extra))

    def marker_text(self):
        with open(self.marker) as f:
            return f.read()

    def wait_dead(self, child, seconds=5):
        try:
            child.wait(timeout=seconds)
            return True
        except subprocess.TimeoutExpired:
            return False

    def test_kills_the_run_when_the_log_passes_the_cap(self):
        run = self.run_stand_in()
        self.watcher('--cap-bytes', '100', '--timeout', '0')
        time.sleep(0.5)
        self.assertIsNone(run.poll())                      # small log: left alone
        with open(self.log, 'w') as f:
            f.write('x' * 101)
        self.assertTrue(self.wait_dead(run))
        time.sleep(0.2)
        self.assertIn('log cap hit: 101 bytes of log, cap 100', self.marker_text())

    def test_kills_the_run_after_the_timeout(self):
        run = self.run_stand_in()
        self.watcher('--cap-bytes', '0', '--timeout', '1')
        self.assertTrue(self.wait_dead(run))
        time.sleep(0.2)
        self.assertIn('run timeout hit', self.marker_text())

    def test_leaves_other_programs_alone(self):
        other = self.run_stand_in('otherrun')              # same directory, different program name
        with open(self.log, 'w') as f:
            f.write('x' * 101)
        self.watcher('--cap-bytes', '100', '--timeout', '1')
        time.sleep(1.5)
        self.assertIsNone(other.poll())
        self.assertFalse(os.path.exists(self.marker))


if __name__ == '__main__':
    unittest.main()
