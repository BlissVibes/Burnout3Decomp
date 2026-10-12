import json, os, shutil, subprocess, sys, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
DEPLOY = os.path.join(HERE, '..', 'tools', 'deploy')


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


class VercelJsonTest(unittest.TestCase):
    def setUp(self):
        with open(os.path.join(DEPLOY, 'vercel.json'), encoding='utf-8') as f:
            self.config = json.load(f)

    def headers_for(self, source):
        return {h['key']: h['value'] for rule in self.config['headers'] if rule['source'] == source for h in rule['headers']}

    def test_every_file_is_cross_origin_isolated_and_revalidated(self):
        headers = self.headers_for('/(.*)')
        self.assertEqual(headers['Cross-Origin-Opener-Policy'], 'same-origin')
        self.assertEqual(headers['Cross-Origin-Embedder-Policy'], 'require-corp')
        self.assertEqual(headers['Cross-Origin-Resource-Policy'], 'same-origin')
        self.assertEqual(headers['Cache-Control'], 'no-cache')

    def test_wasm_has_its_content_type(self):
        self.assertEqual(self.headers_for('/(.*)\\.wasm')['Content-Type'], 'application/wasm')


@unittest.skipUnless(BASH, 'needs bash')
class DeployScriptTest(unittest.TestCase):
    def setUp(self):
        self.home = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.home, True)
        self.calls = os.path.join(self.home, 'vercel-calls.txt')
        bin_dir = os.path.join(self.home, 'bin')
        os.makedirs(bin_dir)
        # A stand-in for the vercel CLI that only records how it was called.
        with open(os.path.join(bin_dir, 'vercel'), 'w', newline='\n') as f:
            f.write('#!/usr/bin/env bash\necho "$*" >> "$VERCEL_CALLS"\n')
        os.chmod(os.path.join(bin_dir, 'vercel'), 0o755)
        self.env = dict(os.environ, HOME=self.home.replace('\\', '/'), VERCEL_CALLS=self.calls.replace('\\', '/'),
                        PATH=bin_dir + os.pathsep + os.environ['PATH'])

    def build(self):
        web = os.path.join(self.home, 'b3', 'out', 'web')
        os.makedirs(web)
        for name in ('ps2web.html', 'ps2web.wasm', 'ps2web.data', 'ps2web.js', 'disc.js', 'pad.js', 'ps2web.wasm.gz', 'boot.txt', 'build.ninja'):
            with open(os.path.join(web, name), 'w') as f:
                f.write(name)

    def run_script(self, *args):
        return subprocess.run([BASH, os.path.join(DEPLOY, 'deploy.sh').replace('\\', '/'), *args], env=self.env, capture_output=True, text=True)

    def vercel_calls(self):
        if not os.path.exists(self.calls):
            return []
        with open(self.calls) as f:
            return f.read().splitlines()

    def test_without_a_build_it_stops(self):
        result = self.run_script()
        self.assertEqual(result.returncode, 1)
        self.assertIn('run tools/build-web.sh', result.stderr)
        self.assertEqual(self.vercel_calls(), [])

    def test_dry_run_stages_the_page_and_does_not_call_vercel(self):
        self.build()
        result = self.run_script()
        self.assertEqual(result.returncode, 0, result.stderr)
        stage = os.path.join(self.home, 'b3', 'out', 'deploy')
        self.assertEqual(sorted(os.listdir(stage)), ['disc.js', 'index.html', 'pad.js', 'ps2web.data', 'ps2web.html', 'ps2web.js', 'ps2web.wasm', 'vercel.json'])
        with open(os.path.join(stage, 'index.html')) as f:
            self.assertEqual(f.read(), 'ps2web.html')
        self.assertEqual(self.vercel_calls(), [])
        self.assertIn('dry run', result.stdout)
        self.assertIn('vercel deploy --prod --yes --scope mark-wilsons-projects-4b36a71c', result.stdout)
        self.assertIn('--yes', result.stdout.splitlines()[-1])

    def test_yes_links_the_private_project_and_deploys_to_production(self):
        self.build()
        result = self.run_script('--yes')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.vercel_calls(), ['link --yes --project burnout3-web --scope mark-wilsons-projects-4b36a71c',
                                               'deploy --prod --yes --scope mark-wilsons-projects-4b36a71c'])

    def test_an_unknown_argument_is_refused(self):
        self.build()
        result = self.run_script('--prod')
        self.assertEqual(result.returncode, 2)
        self.assertEqual(self.vercel_calls(), [])


if __name__ == '__main__':
    unittest.main()
