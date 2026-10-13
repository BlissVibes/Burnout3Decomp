"""tools/port.sh brings the fork's recompilers up to date before the fork's port script translates with them.

The fork's scripts/port.sh builds eerecomp and vurecomp only when they are missing. Run from a made-up home
directory with stand-ins for cmake, python3 and the fork's script that only record how they were called.
"""
import os, shutil, subprocess, sys, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.join(HERE, '..', 'tools', 'port.sh')


def find_bash():
    """Git's bash on Windows (System32's bash.exe is WSL and cannot see these paths)."""
    if sys.platform != 'win32':
        return shutil.which('bash')
    git = shutil.which('git')
    if not git:
        return None
    root = os.path.dirname(os.path.abspath(git))       # Git/cmd or Git/mingw64/bin
    for _ in range(3):
        candidate = os.path.join(root, 'bin', 'bash.exe')
        if os.path.isfile(candidate):
            return candidate
        root = os.path.dirname(root)
    return None


BASH = find_bash()


@unittest.skipUnless(BASH, 'needs bash')
class PortScriptTest(unittest.TestCase):
    def setUp(self):
        self.home = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.home, True)
        self.calls = os.path.join(self.home, 'calls.txt')
        self.fork = os.path.join(self.home, 'b3', 'ps2-web-recomp')
        os.makedirs(os.path.join(self.home, 'b3', 'iso'))
        open(os.path.join(self.home, 'b3', 'iso', 'burnout3.iso'), 'w').close()
        os.makedirs(os.path.join(self.fork, 'games', 'burnout3', 'shots'))
        bin_dir = os.path.join(self.home, 'bin')
        self.stand_in(os.path.join(bin_dir, 'cmake'), 'echo "cmake $*" >> "$B3_CALLS"\nexit "${CMAKE_STATUS:-0}"\n')
        self.stand_in(os.path.join(bin_dir, 'python3'), 'exit 0\n')       # the ISO check, the watcher, the log scan
        self.stand_in(os.path.join(self.fork, 'scripts', 'port.sh'), 'echo "port.sh $*" >> "$B3_CALLS"\n')
        self.env = dict(os.environ, HOME=self.home.replace('\\', '/'), B3_CALLS=self.calls.replace('\\', '/'),
                        PATH=bin_dir + os.pathsep + os.environ['PATH'])

    def stand_in(self, path, body):
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'w', newline='\n') as f:
            f.write('#!/usr/bin/env bash\n' + body)
        os.chmod(path, 0o755)

    def configure_tools(self):
        """What the fork's setup.sh leaves behind: a configured build/tools."""
        os.makedirs(os.path.join(self.fork, 'build', 'tools'))
        open(os.path.join(self.fork, 'build', 'tools', 'CMakeCache.txt'), 'w').close()

    def run_script(self, **env):
        return subprocess.run([BASH, PORT.replace('\\', '/')], env=dict(self.env, **env), capture_output=True, text=True)

    def recorded(self):
        if not os.path.exists(self.calls):
            return []
        with open(self.calls) as f:
            return f.read().splitlines()

    def test_the_recompilers_are_built_before_the_fork_script_runs(self):
        self.configure_tools()
        run = self.run_script()
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        calls = self.recorded()
        self.assertEqual(len(calls), 2, calls)
        self.assertRegex(calls[0], r'^cmake --build \S*/b3/ps2-web-recomp/build/tools --target eerecomp vurecomp$')
        self.assertRegex(calls[1], r'^port\.sh burnout3 \S*/b3/iso/burnout3\.iso --frames 3600$')

    def test_a_failed_build_stops_before_anything_is_translated(self):
        self.configure_tools()
        run = self.run_script(CMAKE_STATUS='1')
        self.assertNotEqual(run.returncode, 0)
        self.assertEqual([call.split()[0] for call in self.recorded()], ['cmake'])

    def test_a_fork_that_was_never_set_up_is_left_to_its_own_script(self):
        # No build/tools: the fork's port.sh finds the tools missing and runs its setup.sh, which builds them.
        run = self.run_script()
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertEqual([call.split()[0] for call in self.recorded()], ['port.sh'])


if __name__ == '__main__':
    unittest.main()
